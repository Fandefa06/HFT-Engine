import urllib.request
import zipfile
import os
from datetime import datetime

# =========================================================================
# --- QUANT DATA CONFIGURATION ---
# Change these dates to whatever range you please! 
# Format MUST be "YYYY-MM"
# =========================================================================
SYMBOL = "ETHUSDT"
START_MONTH = "2024-03" 
END_MONTH = "2024-09"
# =========================================================================

def get_month_range(start_str, end_str):
    """Generates a list of 'YYYY-MM' strings between start and end dates."""
    start_dt = datetime.strptime(start_str, "%Y-%m")
    end_dt = datetime.strptime(end_str, "%Y-%m")
    
    months = []
    current = start_dt
    while current <= end_dt:
        months.append(current.strftime("%Y-%m"))
        # Move to the next month
        if current.month == 12:
            current = current.replace(year=current.year + 1, month=1)
        else:
            current = current.replace(month=current.month + 1)
    return months

# --- PATH SETUP ---
script_dir = os.path.dirname(os.path.abspath(__file__))
data_dir = os.path.join(script_dir, "..", "data")
os.makedirs(data_dir, exist_ok=True)

master_csv_name = f"{SYMBOL}-trades-{START_MONTH}_to_{END_MONTH}.csv"
master_csv_path = os.path.join(data_dir, master_csv_name)

target_months = get_month_range(START_MONTH, END_MONTH)

print(f"=======================================================")
print(f"--- STARTING MULTI-MONTH DOWNLOAD: {SYMBOL} ---")
print(f"Range: {START_MONTH} to {END_MONTH} ({len(target_months)} months)")
print(f"Target Output: {master_csv_name}")
print(f"=======================================================\n")

# Open the master file in write mode
with open(master_csv_path, 'w', encoding='utf-8') as master_file:
    header_written = False

    for month in target_months:
        url = f"https://data.binance.vision/data/spot/monthly/trades/{SYMBOL}/{SYMBOL}-trades-{month}.zip"
        zip_path = os.path.join(data_dir, f"temp_{month}.zip")
        extracted_csv_name = f"{SYMBOL}-trades-{month}.csv"
        extracted_csv_path = os.path.join(data_dir, extracted_csv_name)

        print(f"[{month}] Downloading data (This may take a moment)...")
        try:
            urllib.request.urlretrieve(url, zip_path)
            
            print(f"[{month}] Extracting zip file...")
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                zip_ref.extractall(data_dir)
            
            print(f"[{month}] Merging into master dataset...")
            with open(extracted_csv_path, 'r', encoding='utf-8') as month_file:
                first_line = month_file.readline()
                
                # Header management: Only keep the header for the very first file
                if not header_written:
                    master_file.write(first_line)
                    header_written = True
                else:
                    # If this line is a text header, skip it. If it's pure data, write it.
                    try:
                        float(first_line.split(',')[1]) # Test if the price column is a number
                        master_file.write(first_line)   # It's data, write it
                    except ValueError:
                        pass # It's a text header, ignore it
                
                # Write the rest of the current month into the master file
                for chunk in month_file:
                    master_file.write(chunk)

            # --- CLEANUP (Critical to save disk space) ---
            os.remove(zip_path)
            os.remove(extracted_csv_path)
            print(f"[{month}] Merged successfully and temporary files cleaned.\n")

        except Exception as e:
            print(f"[{month}] Error: {e}")
            print(f"[{month}] Skipping this month. Ensure the date is valid on Binance Vision.")

print("=======================================================")
print(f"SUCCESS! Master dataset ready for the HFT Engine.")
print(f"Location: {master_csv_path}")
print(f"Total Size: {os.path.getsize(master_csv_path) / (1024**3):.2f} GB")
print("=======================================================")