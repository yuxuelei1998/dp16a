import os
import subprocess
import sys
import platform

class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

if platform.system() == "Windows":
    os.system('color')

def print_step(msg):
    print(f"\n{Colors.BLUE}{Colors.BOLD}[*] {msg}{Colors.ENDC}")

def print_success(msg):
    print(f"{Colors.GREEN}[+] {msg}{Colors.ENDC}")

def print_error(msg):
    print(f"{Colors.FAIL}[!] {msg}{Colors.ENDC}")

def get_gpu_architecture(gpu_name):
    name = gpu_name.upper()
    if "H100" in name or "HOPPER" in name: return "sm_90"
    if "L40" in name or "4090" in name or "4080" in name or "ADA" in name: return "sm_89"
    if "A100" in name: return "sm_80"
    if "3090" in name or "3080" in name or "A10" in name or "A30" in name: return "sm_86"
    if "T4" in name or "2080" in name or "TITAN RTX" in name: return "sm_75"
    if "V100" in name or "TITAN V" in name: return "sm_70"
    if "P100" in name: return "sm_60"
    return None

def detect_gpus():
    try:
        result = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"], 
            encoding='utf-8'
        )
        gpus = [line.strip() for line in result.strip().split('\n') if line.strip()]
        return gpus
    except FileNotFoundError:
        print_error("nvidia-smi not found. Please ensure CUDA drivers are installed.")
        return []
    except Exception as e:
        print_error(f"Error checking GPUs: {e}")
        return []

def run_project():
    print(f"{Colors.HEADER}{'='*60}")
    print(f"      DP16A AUTOMATION PROBE SYSTEM")
    print(f"{'='*60}{Colors.ENDC}")
    print_step("Detecting Hardware...")
    gpus = detect_gpus()
    
    selected_gpu = ""
    arch_flag = ""

    if not gpus:
        print_error("No NVIDIA GPUs detected.")
        manual_arch = input(f"{Colors.WARNING}Please enter architecture flag manually (e.g., sm_90): {Colors.ENDC}")
        arch_flag = manual_arch.strip()
        selected_gpu = "Manual Selection"
    else:
        if len(gpus) == 1:
            selected_gpu = gpus[0]
            print(f"Detected: {Colors.BOLD}{selected_gpu}{Colors.ENDC}")
        else:
            print("Multiple GPUs detected:")
            for i, gpu in enumerate(gpus):
                print(f"  [{i+1}] {gpu}")
            choice = input("Select GPU index (default 1): ")
            idx = int(choice) - 1 if choice.isdigit() else 0
            if 0 <= idx < len(gpus):
                selected_gpu = gpus[idx]
            else:
                selected_gpu = gpus[0]
        
        arch_flag = get_gpu_architecture(selected_gpu)
        if not arch_flag:
            print(f"{Colors.WARNING}Could not auto-detect architecture for {selected_gpu}.{Colors.ENDC}")
            arch_flag = input("Please enter -arch flag (e.g., sm_90): ").strip()

    print_success(f"Target Hardware: {selected_gpu} | Arch: {arch_flag}")

    print_step("Select Precision")
    print("  [1] fp16")
    print("  [2] bf16")
    p_choice = input("Enter choice (1 or 2): ").strip()
    
    precision = "fp16"
    if p_choice == "2" or p_choice.lower() == "bf16":
        precision = "bf16"
    
    print_success(f"Selected Precision: {Colors.BOLD}{precision}{Colors.ENDC}")

    root_dir = os.getcwd()
    base_dir = os.path.join(root_dir, precision)
    src_dir = os.path.join(base_dir, "src")
    lib_dir = os.path.join(base_dir, "lib")
    
    if not os.path.exists(lib_dir):
        os.makedirs(lib_dir)
        print_success(f"Created directory: {lib_dir}")
    is_windows = platform.system() == "Windows"
    exe_ext = ".exe" if is_windows else ""
    
    cuda_src_file = f"{precision}_dp16a_wmma.cu"
    cuda_exe_name = f"{precision}_dp16a_wmma{exe_ext}"
    
    cpp_src_file = "ProbeDesign.cpp"
    cpp_exe_name = f"probe_analysis{exe_ext}"

    print_step(f"Compiling CUDA Kernel ({cuda_src_file})...")
    
    cuda_input = os.path.join(src_dir, cuda_src_file)
    cuda_output = os.path.join(lib_dir, cuda_exe_name)
    
    nvcc_cmd = [
        "nvcc", 
        f"-arch={arch_flag}", 
        "-std=c++17", 
        "-o", cuda_output, 
        cuda_input
    ]
    
    print(f"Executing: {' '.join(nvcc_cmd)}")
    try:
        subprocess.run(nvcc_cmd, check=True)
        print_success("CUDA compilation successful.")
    except subprocess.CalledProcessError:
        print_error("CUDA compilation failed.")
        sys.exit(1)
    except FileNotFoundError:
        print_error("nvcc not found. Check your CUDA path.")
        sys.exit(1)

    print_step(f"Compiling Probe Analysis ({cpp_src_file})...")
    
    cpp_input = os.path.join(src_dir, cpp_src_file)
    cpp_output = os.path.join(lib_dir, cpp_exe_name)
    
    gpp_cmd = [
        "g++", 
        "-o", cpp_output, 
        cpp_input, 
        "-std=c++17"
    ]
    
    print(f"Executing: {' '.join(gpp_cmd)}")
    try:
        subprocess.run(gpp_cmd, check=True)
        print_success("C++ compilation successful.")
    except subprocess.CalledProcessError:
        print_error("C++ compilation failed.")
        sys.exit(1)
    except FileNotFoundError:
        print_error("g++ not found. Check your MinGW/GCC path.")
        sys.exit(1)

    print_step("Running Analysis...")
    
    os.chdir(lib_dir)
    print(f"Changed working directory to: {os.getcwd()}")

    print(f"\n{Colors.BOLD}--- Step A: Generating Fingerprint (CUDA) ---{Colors.ENDC}")
    run_cuda_cmd = [f".{os.sep}{cuda_exe_name}"]
    try:
        subprocess.run(run_cuda_cmd, check=True)
    except subprocess.CalledProcessError:
        print_error("Execution of CUDA binary failed.")
        sys.exit(1)

    print(f"\n{Colors.BOLD}--- Step B: Analyzing Report (C++) ---{Colors.ENDC}")
    run_cpp_cmd = [f".{os.sep}{cpp_exe_name}"]
    try:
        subprocess.run(run_cpp_cmd, check=True)
    except subprocess.CalledProcessError:
        print_error("Execution of Probe Analysis failed.")
        sys.exit(1)
    
    print_step("Automation Complete.")

if __name__ == "__main__":
    run_project()