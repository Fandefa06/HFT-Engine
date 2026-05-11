import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import os
import time

# --- PRO FIX: AUTOMATIC MODEL DETECTION ---
script_dir = os.path.dirname(os.path.abspath(__file__))
meta_path = os.path.normpath(os.path.join(script_dir, "..", "output", "metadata.txt"))

if os.path.exists(meta_path):
    with open(meta_path, 'r') as f:
        CURRENT_MODEL_NAME = f.read().strip()
    print(f"Auto-detected Market Model from C++: {CURRENT_MODEL_NAME}")
else:
    CURRENT_MODEL_NAME = "UNKNOWN"
    print("Warning: metadata.txt not found. Did you run the C++ engine?")

is_historical = (CURRENT_MODEL_NAME == "HISTORICAL")

# Define C++ Memory Layout (48 bytes per trade with padding)
trade_dtype = np.dtype([
    ('simId', np.uint32),
    ('padding1', np.uint32),
    ('buyerId', np.uint64),
    ('sellerId', np.uint64),
    ('price', np.int64),
    ('quantity', np.uint32),
    ('padding2', np.uint32),
    ('timestamp', np.int64) # This is CPU Machine Time, NOT Binance Epoch Time!
])

file_path = os.path.normpath(os.path.join(script_dir, "..", "output", "trades_binary.dat"))

if not os.path.exists(file_path):
    print(f"Error: {file_path} not found.")
    exit()

# 1. BIG DATA OPTIMIZATION: Memory Mapping
print(f"Memory-mapping {os.path.getsize(file_path) / (1024**3):.2f} GB of data...")
start_time = time.time()
mmap_data = np.memmap(file_path, dtype=trade_dtype, mode='r')

# 2. DATA DISCOVERY
unique_sims = np.unique(mmap_data['simId'])
n_simulations = len(unique_sims)
total_trades = len(mmap_data)
print(f"Successfully linked {total_trades:,} trades across {n_simulations} universes.")

# 3. ADVANCED DASHBOARD LAYOUT (2x2 Grid)
fig = plt.figure(figsize=(22, 14))
gs = fig.add_gridspec(2, 2, width_ratios=[4, 1], height_ratios=[2.5, 1.2])
fig.suptitle(f'MotorHFT: Advanced Risk & Dispersion Analytics | Model: {CURRENT_MODEL_NAME}', 
             fontsize=24, fontweight='bold')

ax_paths = fig.add_subplot(gs[0, 0])
ax_hist = fig.add_subplot(gs[0, 1], sharey=ax_paths)
ax_disp = fig.add_subplot(gs[1, 0], sharex=ax_paths)
ax_metrics = fig.add_subplot(gs[1, 1])
ax_metrics.axis('off')

# 4. EFFICIENT PROCESSING & SAMPLING
final_prices = []
sampled_paths = []
max_paths_to_plot = min(n_simulations, 100) 
sample_step = 500 # Skip points for rendering performance to bypass the Pixel Problem

print(f"Processing and sampling {max_paths_to_plot} paths...")

# Dynamic styling
if n_simulations == 1:
    path_color = 'midnightblue' 
    path_alpha = 1.0            
    path_lw = 1.5               
else:
    path_color = 'darkblue'     
    path_alpha = 0.15           
    path_lw = 1.0               

for i in range(max_paths_to_plot):
    sim_id = unique_sims[i]
    mask = (mmap_data['simId'] == sim_id)
    sim_prices = mmap_data['price'][mask]
    
    final_prices.append(sim_prices[-1])
    
    # Plot using standard index (Trade Sequence) to avoid CPU Timestamp hallucination
    ax_paths.plot(sim_prices[::sample_step], color=path_color, alpha=path_alpha, linewidth=path_lw)
    sampled_paths.append(sim_prices[::sample_step])

# Align paths for dispersion matrix
min_length = min([len(p) for p in sampled_paths])
price_matrix = np.array([p[:min_length] for p in sampled_paths])

# --- PANEL 1 & 2: PATHS & PROBABILITY DENSITY ---
if is_historical:
    ax_paths.set_title('Historical Price Action (Real Market Data)', fontsize=16)
else:
    ax_paths.set_title(f'Simulated Realities ({CURRENT_MODEL_NAME})', fontsize=16)

ax_paths.set_ylabel('Execution Price (Ticks)', fontsize=14)
ax_paths.grid(True, linestyle='--', alpha=0.5)

ax_hist.hist(final_prices, bins=40 if n_simulations > 1 else 1, orientation='horizontal', color='darkorange', edgecolor='black', alpha=0.7)
mean_p = np.mean(final_prices)
median_p = np.median(final_prices)
ax_hist.axhline(y=mean_p, color='red', linestyle='--', label=f'Mean: {mean_p:.2f}')
ax_hist.axhline(y=median_p, color='green', linestyle='-', label=f'Median: {median_p:.2f}')
ax_hist.set_title('Terminal Distribution', fontsize=16)
ax_hist.set_xlabel(f'Frequency (N={n_simulations})', fontsize=12)
ax_hist.legend()
ax_hist.grid(True, linestyle='--', alpha=0.5)

# --- PANEL 3: CROSS-SECTIONAL DISPERSION ---
p50 = np.percentile(price_matrix, 50, axis=0)
p25 = np.percentile(price_matrix, 25, axis=0)
p75 = np.percentile(price_matrix, 75, axis=0)
p05 = np.percentile(price_matrix, 5, axis=0)
p95 = np.percentile(price_matrix, 95, axis=0)

x_axis_disp = range(min_length)

if is_historical:
    ax_disp.plot(x_axis_disp, p50, color='black', linewidth=2, label='Real Market Path')
    ax_disp.set_xlabel('Trade Sequence (Sampled)', fontsize=14)
else:
    ax_disp.plot(x_axis_disp, p50, color='black', linewidth=2, label='Median Path')
    ax_disp.fill_between(x_axis_disp, p25, p75, color='blue', alpha=0.3, label='Interquartile Range (25th-75th)')
    ax_disp.fill_between(x_axis_disp, p05, p95, color='lightblue', alpha=0.15, label='Extreme Dispersion (5th-95th)')
    ax_disp.set_xlabel('Normalized Sampled Sequence', fontsize=14)

ax_disp.set_title('Market Dispersion Over Time', fontsize=16)
ax_disp.legend(loc='upper left')
ax_disp.grid(True, linestyle='--', alpha=0.5)

# --- PANEL 4: DEEP STATISTICAL MOMENTS (DYNAMIC) ---
fp_series = pd.Series(final_prices)
start_price = price_matrix[0][0]

if is_historical:
    final_close = fp_series.iloc[0]
    total_return_pct = ((final_close - start_price) / start_price) * 100
    
    metrics_text = (
        f"--- HISTORICAL TIMELINE ---\n\n"
        f"Data Source:       BINANCE REAL\n"
        f"X-Axis format:     Trade Sequence\n\n"
        f"--- PRICE ACTION (Ticks) ---\n\n"
        f"Initial Start:     {start_price:,.2f}\n"
        f"Final Close:       {final_close:,.2f}\n"
        f"Absolute Max:      {mmap_data['price'].max():,.2f}\n"
        f"Absolute Min:      {mmap_data['price'].min():,.2f}\n\n"
        f"--- OUTCOMES ---\n\n"
        f"Total Return:      {total_return_pct:+.2f}%\n"
        f"Total Trades:      {total_trades:,}\n"
        f"Script Latency:    {time.time() - start_time:.2f}s"
    )
else:
    win_rate = (fp_series > start_price).mean() * 100
    std_val = fp_series.std() if n_simulations > 1 else 0.0
    skew_val = fp_series.skew() if n_simulations > 2 else 0.0
    kurt_val = fp_series.kurtosis() if n_simulations > 3 else 0.0
    
    metrics_text = (
        f"--- DEEP STATISTICAL MOMENTS ---\n\n"
        f"Market Model:      {CURRENT_MODEL_NAME}\n"
        f"Initial Start:     {start_price:,.2f}\n"
        f"Mean Final:        {mean_p:,.2f}\n"
        f"Median Final:      {median_p:,.2f}\n"
        f"Absolute Max:      {fp_series.max():,.2f}\n"
        f"Absolute Min:      {fp_series.min():,.2f}\n\n"
        f"--- RISK & VOLATILITY ---\n\n"
        f"Standard Dev (σ):  {std_val:,.2f}\n"
        f"Skewness:          {skew_val:.4f}\n"
        f"Kurtosis:          {kurt_val:.4f}\n\n"
        f"--- OUTCOMES ---\n\n"
        f"Prob. Appreciation: {win_rate:.1f}%\n"
        f"Total Data Points:  {total_trades:,}\n"
        f"Script Latency:     {time.time() - start_time:.2f}s"
    )

ax_metrics.text(0.1, 0.9, metrics_text, fontsize=14, fontfamily='monospace', 
                verticalalignment='top', bbox=dict(boxstyle='round,pad=1', facecolor='#f8f9fa', edgecolor='black'))

plt.tight_layout(rect=[0, 0.03, 1, 0.95])
output_img = os.path.normpath(os.path.join(script_dir, "..", "output", f"report_{CURRENT_MODEL_NAME}.png"))
plt.savefig(output_img, dpi=200, bbox_inches='tight')

print(f"Analysis Complete. Multi-panel report saved: {output_img}")