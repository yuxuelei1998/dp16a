#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <map>

// 使用C++17 filesystem
namespace fs = std::filesystem;

// 辅助函数：将十六进制字符串转为uint32_t
uint32_t hexStringToUint(const std::string& hexStr) {
    return std::stoul(hexStr, nullptr, 16);
}

// 辅助函数：将uint32_t位模式转换为float (用于单调性比较)
float uintToFloat(uint32_t i) {
    union {
        uint32_t u;
        float f;
    } temp;
    temp.u = i;
    return temp.f;
}

// 辅助函数：读取指纹文件
std::vector<uint32_t> readFingerprint(const std::string& filepath) {
    std::vector<uint32_t> data;
    std::ifstream file(filepath);
    std::string line;
    if (!file.is_open()) {
        return data; // 返回空向量表示读取失败
    }
    while (std::getline(file, line)) {
        // 去除可能存在的空白字符
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (!line.empty()) {
            try {
                data.push_back(hexStringToUint(line));
            } catch (...) {
                // 忽略非十六进制行
            }
        }
    }
    return data;
}

// 格式化输出函数
void printRow(const std::string& type, const std::string& result) {
    std::cout << "| " << std::left << std::setw(30) << type 
              << "| " << std::left << std::setw(60) << result << " |" << std::endl;
}

void printSeparator() {
    std::cout << "+" << std::string(31, '-') << "+" << std::string(62, '-') << "+" << std::endl;
}

int main() {
    std::string targetPath = "../numeric_fingerprints/fp16_dp16a_16x16_wmma_output.txt";
    std::vector<uint32_t> data = readFingerprint(targetPath);

    if (data.size() < 88) {
        std::cerr << "Error: Target file not found or insufficient data (lines < 88)." << std::endl;
        std::cerr << "Path checked: " << fs::absolute(targetPath) << std::endl;
        return 1;
    }

    // --- 1. 分析逻辑 ---

    // 1.1 符号零 (Line 1 -> Index 0)
    std::string signedZero;
    if (data[0] == 0x80000000) signedZero = "-0";
    else if (data[0] == 0x00000000) signedZero = "+0";
    else signedZero = "Unknown";

    // 1.2 NaN & INF (Lines 2-20 -> Index 1-19)
    std::string nanInf;
    bool allSame = true;
    uint32_t firstNan = data[1];
    for (int i = 1; i <= 19; ++i) {
        if (data[i] != firstNan) {
            allSame = false;
            break;
        }
    }
    if (allSame) {
        std::stringstream ss;
        ss << "Fixed NaN: 0x" << std::hex << firstNan;
        nanInf = ss.str();
    } else {
        nanInf = "Propagates NaN Payload";
    }

    // 1.3 次正规数支持 (Lines 21-54 -> Index 20-53)
    std::string subnormal;
    bool allZero = true;
    for (int i = 20; i <= 53; ++i) {
        if (data[i] != 0x00000000) {
            allZero = false;
            break;
        }
    }
    subnormal = allZero ? "Not Supported (Flushed to Zero)" : "Supported";

    // 1.4 舍入模式 (Lines 55-56 -> Index 54-55)
    std::string roundingMode;
    uint32_t r1 = data[54];
    uint32_t r2 = data[55];
    
    if (r1 == 0x3f800001 && r2 == 0xbf800001) roundingMode = "Truncation (RZ)";
    else if (r1 == 0x3f800001 && r2 == 0xbf800002) roundingMode = "Round to Negative Infinity (RM)";
    else if (r1 == 0x3f800002 && r2 == 0xbf800001) roundingMode = "Round to Positive Infinity (RP)";
    else if (r1 == 0x3f800002 && r2 == 0xbf800002) roundingMode = "Round to Nearest Even (RN)";
    else roundingMode = "Unknown Rounding Mode";

    // 1.5 结构：累加顺序、点积宽度、归一化 (Lines 57-72 -> Index 56-71)
    std::string accumOrder;
    std::string dotProductWidth;
    std::string normalization;
    
    // 检查累加顺序
    bool lines57_72_Same = true;
    uint32_t firstStruct = data[56];
    for (int i = 56; i <= 71; ++i) {
        if (data[i] != firstStruct) {
            lines57_72_Same = false;
            break;
        }
    }
    accumOrder = lines57_72_Same ? "No Accumulation Order" : "Has Accumulation Order";

    // 计算组数 n
    int groups = 0;
    std::vector<uint32_t> groupValues;
    if (lines57_72_Same) {
        groups = 1;
        groupValues.push_back(firstStruct);
    } else {
        // 统计连续相同的组
        uint32_t currentVal = data[56];
        groupValues.push_back(currentVal);
        groups = 1;
        for (int i = 57; i <= 71; ++i) {
            if (data[i] != currentVal) {
                groups++;
                currentVal = data[i];
                groupValues.push_back(currentVal);
            }
        }
    }

    // 点积宽度 = 16 / n
    int widthVal = 16 / groups;
    dotProductWidth = std::to_string(widthVal);

    // 归一化分析
    int stages = 2 * groups - 1;
    bool isSequential = true;
    for (size_t i = 0; i < groupValues.size() - 1; ++i) {
        // 简单的数值递减检查 (按无符号整数比较，假设测试指纹特性如此)
        if (groupValues[i] <= groupValues[i+1]) {
            isSequential = false;
            break;
        }
    }

    bool isButterfly = false;
    if (groups > 1 && groups % 2 == 0) {
        bool halvesMatch = true;
        int half = groups / 2;
        for (int i = 0; i < half; ++i) {
            if (groupValues[i] != groupValues[i + half]) {
                halvesMatch = false;
                break;
            }
        }
        if (halvesMatch) isButterfly = true;
    }

    std::stringstream normSS;
    normSS << stages << " Stages, ";
    if (lines57_72_Same) {
        normSS << "Single Group (No grouping)";
    } else if (isSequential) {
        normSS << "Sequential Grouping (" << groups << " groups)";
    } else if (isButterfly) {
        normSS << "Butterfly Grouping (" << groups << " groups)";
    } else {
        normSS << "Complex/Unknown Grouping (" << groups << " groups)";
    }
    normalization = normSS.str();

    // 1.6 额外精度位数 (根据舍入模式选择行)
    std::string extraPrecision;
    int precisionBits = 0;
    
    // 检测是否包含 "Round to Nearest"
    bool isRN = (roundingMode.find("Nearest") != std::string::npos);
    bool isRZ = (roundingMode.find("Truncation") != std::string::npos);

    if (isRN) {
        // Index 72-75
        for (int i = 72; i <= 75; ++i) {
            if (data[i] == 0x4e800002) precisionBits++;
        }
    } else if (isRZ) {
        // Index 76-79
        for (int i = 76; i <= 79; ++i) {
            if (data[i] == 0x4e800002) precisionBits++;
        }
    } else {
        // 如果不是这两种模式，按逻辑可能没有额外精度，或者尝试在 RN 区间检测
        // 为了鲁棒性，这里默认检测 RN 区间，如果为 0 则为 0
        int bitsRN = 0; 
        for (int i = 72; i <= 75; ++i) if (data[i] == 0x4e800002) bitsRN++;
        precisionBits = bitsRN;
    }
    extraPrecision = std::to_string(precisionBits);

    // 1.7 单调性 (Lines 81-88 -> Index 80-87)
    // 81,82 (i=80,81); 83,84 (i=82,83); etc.
    std::string monotonicity = "Satisfies Monotonicity";
    for (int i = 80; i < 88; i += 2) {
        float v1 = uintToFloat(data[i]);
        float v2 = uintToFloat(data[i+1]);
        if (v1 > v2) {
            monotonicity = "Non-Monotonic";
            break;
        }
    }

    // 1.8 内部数据路径结构 (推断)
    std::stringstream structSS;
    structSS << "RM: " << (roundingMode.size() > 15 ? roundingMode.substr(0, 15) + "..." : roundingMode) 
             << " | Acc: " << (accumOrder == "Has Accumulation Order" ? "Ordered" : "No Order")
             << " | DP Width: " << widthVal
             << " | Extra Bits: " << precisionBits;
    std::string internalStructure = structSS.str();

    // --- 2. 指纹比对 ---
    std::string matchResult = "No exact match found.";
    std::string fingerprintsDir = "../numeric_fingerprints";
    
    if (fs::exists(fingerprintsDir)) {
        for (const auto& entry : fs::directory_iterator(fingerprintsDir)) {
            if (entry.path().filename() == "fp16_dp16a_16x16_wmma_output.txt") continue; // 跳过自己
            
            std::vector<uint32_t> otherData = readFingerprint(entry.path().string());
            if (otherData.size() == data.size()) {
                bool match = true;
                for (size_t k = 0; k < data.size(); ++k) {
                    if (data[k] != otherData[k]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    matchResult = "Matches Hardware: " + entry.path().stem().string();
                    break;
                }
            }
        }
    } else {
        matchResult = "Directory ../numeric_fingerprints not found.";
    }

    // --- 3. 输出图表 ---

    std::cout << std::endl;
    std::cout << "==============================================================================================" << std::endl;
    std::cout << "                              NUMERIC PROBE ANALYSIS REPORT                                   " << std::endl;
    std::cout << "==============================================================================================" << std::endl;
    printSeparator();
    std::cout << "| " << std::left << std::setw(30) << "PROBE TYPE" 
              << "| " << std::left << std::setw(60) << "RESULT FEEDBACK" << " |" << std::endl;
    printSeparator();
    
    printRow("Signed Zero", signedZero);
    printRow("NaN & INF", nanInf);
    printRow("Subnormal Support", subnormal);
    printRow("Rounding Mode", roundingMode);
    printRow("Accumulation Order", accumOrder);
    printRow("Dot Product Unit Width", dotProductWidth);
    printRow("Extra Precision Bits", extraPrecision);
    printRow("Normalization", normalization);
    printRow("Monotonicity", monotonicity);
    printRow("Internal Data Path", internalStructure);
    
    printSeparator();
    std::cout << "| " << std::left << std::setw(30) << "HARDWARE IDENTIFICATION" 
              << "| " << std::left << std::setw(60) << matchResult << " |" << std::endl;
    printSeparator();
    std::cout << std::endl;

    return 0;
}