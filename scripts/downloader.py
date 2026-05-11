import os
import requests
import zipfile
import subprocess
import time

# --- CONFIGURATION ---
SYMBOL = "ETHUSDT"
YEARS = [2022, 2023] # Years you want to download
MONTHS = range(1, 13) # Months 1 through 12

DATA_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "data"))
PRECOMPILER_PATH = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "precompiler"))

# Ensure data directory exists
os.makedirs(DATA_DIR, exist_ok=True)

def download_and_compile(symbol, year, month):
    month_str = f"{month:02d}" # Formats 1 to "01"
    file_name = f"{symbol}-trades-{year}-{month_str}"
    zip_name = f"{file_name}.zip"
    csv_name = f"{file_name}.csv"
    bin_name = f"{symbol}_{year}_{month_str}.bin"
    
    zip_path = os.path.join(DATA_DIR, zip_name)
    csv_path = os.path.join(DATA_DIR, csv_name)
    bin_path = os.path.join(DATA_DIR, bin_name)

    # 1. DOWNLOAD
    url = f"https://data.binance.vision/data/spot/monthly/trades/{symbol}/{zip_name}"
    print(f"\n[{year}-{month_str}] 1. Downloading from Binance...")
    
    response = requests.get(url, stream=True)
    if response.status_code == 404:
        print(f"[-] Data for {year}-{month_str} not found on Binance. Skipping.")
        return
        
    with open(zip_path, 'wb') as f:
        for chunk in response.iter_content(chunk_size=8192):
            f.write(chunk)
            
    # 2. EXTRACT
    print(f"[{year}-{month_str}] 2. Unzipping CSV...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(DATA_DIR)
        
    # 3. PRE-COMPILE (C++ Execution)
    print(f"[{year}-{month_str}] 3. Running C++ Pre-Compiler...")
    try:
        # Calls: ./precompiler data/ETHUSDT...csv data/ETH_2022_01.bin
        subprocess.run([PRECOMPILER_PATH, csv_path, bin_path], check=True)
    except Exception as e:
        print(f"[-] Pre-Compiler failed: {e}")
        return

    # 4. CLEANUP (Destroy the heavy files)
    print(f"[{year}-{month_str}] 4. Cleaning up heavy files...")
    if os.path.exists(zip_path): os.remove(zip_path)
    if os.path.exists(csv_path): os.remove(csv_path)
    
    print(f"[SUCCESS] {bin_name} is ready for MotorHFT!")

# --- EXECUTION ---
print("=========================================")
print("  MOTOR HFT: AUTOMATED DATA PIPELINE")
print("=========================================")

# Ensure C++ precompiler exists
if not os.path.exists(PRECOMPILER_PATH):
    print(f"Error: Could not find '{PRECOMPILER_PATH}'.")
    print("Please compile it first: g++ -O3 src/PreCompiler.cpp -o precompiler")
    exit()

for y in YEARS:
    for m in MONTHS:
        download_and_compile(SYMBOL, y, m)
        time.sleep(1) # Be polite to Binance API servers