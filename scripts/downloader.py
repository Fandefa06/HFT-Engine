import os
import requests
import zipfile
import subprocess
import time
import warnings

warnings.filterwarnings('ignore', message='Unverified HTTPS request')

# ==========================================
# --- CONFIGURATION ZONE ---
# ==========================================
SYMBOL = "ETHUSDT"

# [TEST MODE]: Uncomment these two lines to test just 2 months right now
YEARS = [2025]
MONTHS = [1, 2]

# [OVERNIGHT MODE]: Uncomment these to download EVERYTHING (From 2017 to 2026)
# YEARS = range(2017, 2027) 
# MONTHS = range(1, 13)
# ==========================================

DATA_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "data"))
PRECOMPILER_PATH = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "precompiler"))

# Ensure data directory exists
os.makedirs(DATA_DIR, exist_ok=True)

def download_and_compile(symbol, year, month):
    month_str = f"{month:02d}"
    
    # Files naming convention matching the month exactly
    base_name = f"{symbol}-trades-{year}-{month_str}"
    zip_name = f"{base_name}.zip"
    csv_name = f"{base_name}.csv"
    bin_name = f"{base_name}.bin" 
    
    zip_path = os.path.join(DATA_DIR, zip_name)
    csv_path = os.path.join(DATA_DIR, csv_name)
    bin_path = os.path.join(DATA_DIR, bin_name)

    # RESUME LOGIC: If the binary already exists, skip everything!
    if os.path.exists(bin_path):
        print(f"[*] {bin_name} already exists. Skipping download.")
        return

    # 1. DOWNLOAD
    url = f"https://data.binance.vision/data/spot/monthly/trades/{symbol}/{zip_name}"
    print(f"\n[{year}-{month_str}] 1. Downloading {zip_name}...")
    
    response = requests.get(url, stream=True)
    if response.status_code == 404:
        print(f"[-] Data for {year}-{month_str} not available. Skipping.")
        return
        
    with open(zip_path, 'wb') as f:
        for chunk in response.iter_content(chunk_size=8192):
            f.write(chunk)
            
    # 2. EXTRACT
    print(f"[{year}-{month_str}] 2. Unzipping {csv_name}...")
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(DATA_DIR)
        
    # 3. PRE-COMPILE
    print(f"[{year}-{month_str}] 3. Compiling to pure binary ({bin_name})...")
    try:
        subprocess.run([PRECOMPILER_PATH, csv_path, bin_path], check=True)
    except Exception as e:
        print(f"[-] Pre-Compiler failed: {e}")
        return

    # 4. CLEANUP (Protecting your 200GB SSD limit)
    print(f"[{year}-{month_str}] 4. Deleting heavy files (.zip, .csv)...")
    if os.path.exists(zip_path): os.remove(zip_path)
    if os.path.exists(csv_path): os.remove(csv_path)
    
    print(f"[SUCCESS] Saved as: {bin_name}")

# --- EXECUTION SCRIPT ---
print("=========================================")
print("  MOTOR HFT: GIGA-DOWNLOADER PIPELINE")
print("=========================================")

if not os.path.exists(PRECOMPILER_PATH):
    print(f"Error: Executable not found at {PRECOMPILER_PATH}")
    print("Please compile it first: g++ -O3 src/PreCompiler.cpp -o precompiler")
    exit()

for y in YEARS:
    for m in MONTHS:
        download_and_compile(SYMBOL, y, m)
        time.sleep(1) # Be polite to Binance API servers