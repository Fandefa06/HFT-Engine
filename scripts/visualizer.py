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

# Define C++ Memory Layout (48 bytes per trade with padding)
trade_dtype = np.dtype([
    ('simId', np.uint32),
    ('padding1', np.uint32),
    ('buyerId', np.uint64),
    ('sellerId', np.uint64),
    ('price', np.int64),
    ('quantity', np.uint32),
    ('padding2', np.uint32),
    ('timestamp', np.int64) 
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
sample_step = 500 # Skip points for rendering performance

print(f"Processing and sampling {max_paths_to_plot} paths...")

for i in range(max_paths_to_plot):
    sim_id = unique_sims[i]
    # Efficient slice using numpy boolean indexing
    sim_prices = mmap_data['price'][mmap_data['simId'] == sim_id]
    
    final_prices.append(sim_prices[-1])
    
    # Path plotting (sampled)
    ax_paths.plot(sim_prices[::sample_step], color='royalblue', alpha=0.10, linewidth=1)
    sampled_paths.append(sim_prices[::sample_step])

# Align paths for dispersion matrix (Euler-Maruyama step alignment)
min_length = min([len(p) for p in sampled_paths])
price_matrix = np.array([p[:min_length] for p in sampled_paths])

# --- PANEL 1 & 2: PATHS & PROBABILITY DENSITY ---
ax_paths.set_title(f'Simulated Realities ({CURRENT_MODEL_NAME})', fontsize=16)
ax_paths.set_ylabel('Execution Price (Ticks)', fontsize=14)
ax_paths.grid(True, linestyle='--', alpha=0.5)

ax_hist.hist(final_prices, bins=40, orientation='horizontal', color='darkorange', edgecolor='black', alpha=0.7)
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

ax_disp.plot(p50, color='black', linewidth=2, label='Median Path')
ax_disp.fill_between(range(min_length), p25, p75, color='blue', alpha=0.3, label='Interquartile Range (25th-75th)')
ax_disp.fill_between(range(min_length), p05, p95, color='lightblue', alpha=0.15, label='Extreme Dispersion (5th-95th)')
ax_disp.set_title('Market Dispersion Over Time', fontsize=16)
ax_disp.set_xlabel('Normalized Sampled Sequence', fontsize=14)
ax_disp.legend(loc='upper left')
ax_disp.grid(True, linestyle='--', alpha=0.5)

# --- PANEL 4: DEEP STATISTICAL MOMENTS ---
fp_series = pd.Series(final_prices)
start_price = price_matrix[0][0]
win_rate = (fp_series > start_price).mean() * 100

metrics_text = (
    f"--- DEEP STATISTICAL MOMENTS ---\n\n"
    f"Market Model:      {CURRENT_MODEL_NAME}\n"
    f"Initial Start:     {start_price:,.2f}\n"
    f"Mean Final:        {mean_p:,.2f}\n"
    f"Median Final:      {median_p:,.2f}\n"
    f"Absolute Max:      {fp_series.max():,.2f}\n"
    f"Absolute Min:      {fp_series.min():,.2f}\n\n"
    f"--- RISK & VOLATILITY ---\n\n"
    f"Standard Dev (σ):  {fp_series.std():,.2f}\n"
    f"Skewness:          {fp_series.skew():.4f}\n"
    f"Kurtosis:          {fp_series.kurtosis():.4f}\n\n"
    f"--- OUTCOMES ---\n\n"
    f"Prob. Appreciation: {win_rate:.1f}%\n"
    f"Total Data Points:  {total_trades:,}\n"
    f"Script Latency:     {time.time() - start_time:.2f}s"
)

ax_metrics.text(0.1, 0.9, metrics_text, fontsize=14, fontfamily='monospace', 
                verticalalignment='top', bbox=dict(boxstyle='round,pad=1', facecolor='#f8f9fa', edgecolor='black'))

plt.tight_layout(rect=[0, 0.03, 1, 0.95])
# The Fix: output image now uses the dynamic model name correctly
output_img = os.path.normpath(os.path.join(script_dir, "..", "output", f"report_{CURRENT_MODEL_NAME}.png"))
plt.savefig(output_img, dpi=200, bbox_inches='tight')

print(f"Analysis Complete. Multi-panel report saved: {output_img}")