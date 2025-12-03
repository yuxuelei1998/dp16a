#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

const int WIDTH_TYPE = 30;
const int WIDTH_RESULT = 95;

uint32_t hexStringToUint(const std::string& hexStr) {
    try {
        return std::stoul(hexStr, nullptr, 16);
    } catch (...) {
        return 0;
    }
}

float uintToFloat(uint32_t i) {
    union {
        uint32_t u;
        float f;
    } temp;
    temp.u = i;
    return temp.f;
}

std::vector<uint32_t> readFingerprint(const std::string& filepath) {
    std::vector<uint32_t> data;
    std::ifstream file(filepath);
    std::string line;
    if (!file.is_open()) return data;
    
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (!line.empty()) {
            data.push_back(hexStringToUint(line));
        }
    }
    return data;
}

void printRow(const std::string& type, const std::string& result) {
    std::cout << "| " << std::left << std::setw(WIDTH_TYPE) << type 
              << "| " << std::left << std::setw(WIDTH_RESULT) << result << " |" << std::endl;
}

void printSeparator() {
    std::cout << "+" << std::string(WIDTH_TYPE + 1, '-') 
              << "+" << std::string(WIDTH_RESULT + 2, '-') << "+" << std::endl;
}

int main() {
    std::string targetDir = "../numeric_fingerprints";
    std::string targetFile = "bf16_dp16a_16x16_wmma_output.txt";
    fs::path targetPath = fs::path(targetDir) / targetFile;
    
    std::vector<uint32_t> data = readFingerprint(targetPath.string());

    if (data.size() < 88) {
        std::cerr << "Error: Target file not found or data insufficient." << std::endl;
        std::cerr << "Path: " << fs::absolute(targetPath) << std::endl;
        return 1;
    }

    std::string signedZero = (data[0] == 0x80000000) ? "-0" : ((data[0] == 0x00000000) ? "+0" : "Unknown");
    std::string nanInf;
    bool allSameNaN = true;
    for (int i = 1; i <= 19; ++i) if (data[i] != data[1]) allSameNaN = false;
    if (allSameNaN) {
        std::stringstream ss; ss << "Fixed NaN: 0x" << std::hex << data[1]; nanInf = ss.str();
    } else nanInf = "Propagates NaN Payload";

    bool subnormalSupported = false;
    for (int i = 20; i <= 53; ++i) if (data[i] != 0) subnormalSupported = true;
    std::string subnormal = subnormalSupported ? "Supported" : "Not Supported (Flushed to Zero)";

    std::string roundingMode = "Unknown";
    uint32_t r1 = data[54], r2 = data[55];
    if (r1 == 0x3f800001 && r2 == 0xbf800001) roundingMode = "Truncation (RZ)";
    else if (r1 == 0x3f800001 && r2 == 0xbf800002) roundingMode = "Round to Negative Infinity (RM)";
    else if (r1 == 0x3f800002 && r2 == 0xbf800001) roundingMode = "Round to Positive Infinity (RP)";
    else if (r1 == 0x3f800002 && r2 == 0xbf800002) roundingMode = "Round to Nearest Even (RN)";

    bool hasOrder = false;
    for (int i = 56; i <= 71; ++i) if (data[i] != data[56]) hasOrder = true;
    std::string accumOrder = hasOrder ? "Has Accumulation Order" : "No Accumulation Order";

    int groups = 1;
    std::vector<uint32_t> gVals;
    gVals.push_back(data[56]);
    uint32_t cur = data[56];
    for (int i = 57; i <= 71; ++i) {
        if (data[i] != cur) { groups++; cur = data[i]; gVals.push_back(cur); }
    }
    
    int dpWidth = 16 / groups;
    bool isSeq = true;
    for (size_t i = 0; i < gVals.size() - 1; ++i) if (gVals[i] <= gVals[i+1]) isSeq = false;
    bool isButter = (groups > 1 && groups % 2 == 0);
    
    std::string normType = "Complex/Unknown";
    if (!hasOrder) normType = "Single Group";
    else if (isSeq) normType = "Sequential Grouping (" + std::to_string(groups) + " groups)";
    else if (isButter) normType = "Butterfly Grouping (" + std::to_string(groups) + " groups)";

    std::stringstream normSS; 
    normSS << (2 * groups - 1) << " Stages, " << normType;
    std::string normalization = normSS.str();

    int precBits = 0;
    int startIdx = (roundingMode.find("Nearest") != std::string::npos) ? 72 : 76;
    for (int i = startIdx; i < startIdx + 4; ++i) if (data[i] == 0x4e800002) precBits++;
    
    std::string monotonic = "Satisfies Monotonicity";
    for (int i = 80; i < 88; i += 2) {
        if (uintToFloat(data[i]) > uintToFloat(data[i+1])) { monotonic = "Non-Monotonic"; break; }
    }

    std::stringstream structSS;
    structSS << "RM: " << roundingMode.substr(0, std::min((size_t)15, roundingMode.length()))
             << ((roundingMode.length() > 15) ? "..." : "")
             << " | Acc: " << (hasOrder ? "Ordered" : "No Order")
             << " | DP Width: " << dpWidth
             << " | Extra Bits: " << precBits;
    std::string internalStructure = structSS.str();

    std::string matchResult = "No exact match found.";
    if (fs::exists(targetDir)) {
        for (const auto& entry : fs::directory_iterator(targetDir)) {
            if (entry.path().filename() == targetFile) continue;
            std::vector<uint32_t> other = readFingerprint(entry.path().string());
            if (other == data) {
                matchResult = "Matches Hardware: " + entry.path().stem().string();
                break;
            }
        }
    }

    int totalWidth = WIDTH_TYPE + WIDTH_RESULT + 5;
    std::string title = " NUMERIC PROBE ANALYSIS REPORT ";
    int padding = (totalWidth - title.length()) / 2;

    std::cout << std::endl;
    std::cout << std::string(totalWidth, '=') << std::endl;
    std::cout << std::string(padding, ' ') << title << std::endl;
    std::cout << std::string(totalWidth, '=') << std::endl;
    
    printSeparator();
    printRow("PROBE TYPE", "RESULT FEEDBACK");
    printSeparator();
    
    printRow("Signed Zero", signedZero);
    printRow("NaN & INF", nanInf);
    printRow("Subnormal Support", subnormal);
    printRow("Rounding Mode", roundingMode);
    printRow("Accumulation Order", accumOrder);
    printRow("Dot Product Unit Width", std::to_string(dpWidth));
    printRow("Extra Precision Bits", std::to_string(precBits));
    printRow("Normalization", normalization);
    printRow("Monotonicity", monotonic);
    printRow("Internal Data Path", internalStructure);
    
    printSeparator();
    printRow("HARDWARE IDENTIFICATION", matchResult);
    printSeparator();
    std::cout << std::endl;

    return 0;
}
