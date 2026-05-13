import numpy as np
import matplotlib.pyplot as plt
import os
import time

# --- CONFIGURATION & DETECTION ---
script_dir = os.path.dirname(os.path.abspath(__file__))
meta_path = os.path.normpath(os.path.join(script_dir, "..", "output", "metadata.txt"))

if os.path.exists(meta_path):
    with open(meta_path, 'r') as f:
        lines = f.readlines()
        CURRENT_MODEL_NAME = lines[0].strip() if len(lines) > 0 else "UNKNOWN"
        FILE_TAG = os.path.basename(lines[1].strip()).replace('.bin', '') if len(lines) > 1 else "DATA"
    print(f"Auto-detected Market Model: {CURRENT_MODEL_NAME} | Source: {FILE_TAG}")
else:
    CURRENT_MODEL_NAME = "UNKNOWN"
    FILE_TAG = "UNKNOWN"

is_historical = (CURRENT_MODEL_NAME == "HISTORICAL")

# --- THE AI STATE VECTOR LAYOUT (64 Bytes) ---
feature_dtype = np.dtype([
    ('simId', np.uint32),
    ('padding', np.uint32), # REQUIRED TO ALIGN WITH C++
    ('bucketId', np.uint64),
    ('openPrice', np.int64),
    ('highPrice', np.int64),
    ('lowPrice', np.int64),
    ('closePrice', np.int64),
    ('totalVolume', np.uint64),
    ('orderFlowImbalance', np.int64)
])

file_path = "/dev/shm/features_binary.dat"
if not os.path.exists(file_path):
    print(f"Error: RAM-disk file not found.")
    exit()

file_size_mb = os.path.getsize(file_path) / (1024**2)
start_time = time.time()

# Load everything from RAM instantly
features = np.fromfile(file_path, dtype=feature_dtype)
total_buckets = len(features)
unique_sims = np.unique(features['simId'])
print(f"Successfully loaded {total_buckets:,} State Vectors from RAM ({file_size_mb:.2f} MB).")
print(f"Detected {len(unique_sims)} Unique Timelines.")

# 3. DASHBOARD INITIALIZATION
fig = plt.figure(figsize=(24, 14)) # Slightly wider for the larger text box
gs = fig.add_gridspec(2, 2, width_ratios=[4, 1.2], height_ratios=[2.5, 1.2])
fig.suptitle(f'MotorHFT Analytics | Model: {CURRENT_MODEL_NAME} | Asset: {FILE_TAG}', fontsize=24, fontweight='bold')

ax_price = fig.add_subplot(gs[0, 0])
ax_hist = fig.add_subplot(gs[0, 1], sharey=ax_price)
ax_lower = fig.add_subplot(gs[1, 0], sharex=ax_price)
ax_metrics = fig.add_subplot(gs[1, 1])
ax_metrics.axis('off')

# =========================================================================
# DATA PRE-PROCESSING & METRICS EXTRACTION
# =========================================================================

hist_mask = features['simId'] == 0
hist_data = features[hist_mask]
hist_len = len(hist_data)

hist_end_price = hist_data['closePrice'][-1] if hist_len > 0 else 0
hist_max = hist_data['highPrice'].max() if hist_len > 0 else 0
hist_min = hist_data['lowPrice'].min() if hist_len > 0 else 0

mc_mask = features['simId'] > 0
mc_data = features[mc_mask]
mc_count = len(np.unique(mc_data['simId'])) if len(mc_data) > 0 else 0
mc_max = mc_data['highPrice'].max() if mc_count > 0 else 0
mc_min = mc_data['lowPrice'].min() if mc_count > 0 else 0

# =========================================================================
# PLOTTING LOGIC: HYBRID (REALITY VS CLOUD)
# =========================================================================

mc_final_prices = []
print(f"Plotting {min(101, len(unique_sims))} simulation paths...")

for sim_id in unique_sims:
    sim_data = features[features['simId'] == sim_id]
    if len(sim_data) == 0: continue
        
    sim_prices = sim_data['closePrice']
    raw_ofi = np.cumsum(sim_data['orderFlowImbalance'])
    
    # NORMALIZATION: This scales OFI between -1 and 1 so shapes can be compared 
    # regardless of whether the volume was 10 or 10,000,000
    max_ofi = np.max(np.abs(raw_ofi))
    norm_ofi = raw_ofi / max_ofi if max_ofi != 0 else raw_ofi

    if sim_id == 0:
        # THE REAL WORLD (Bold Blue)
        ax_price.plot(sim_prices, color='midnightblue', linewidth=3, zorder=10, label='Reality (Historical)')
        ax_lower.plot(norm_ofi, color='midnightblue', linewidth=2, zorder=10)
    else:
        # SIMULATIONS (Faded Orange Cloud)
        if sim_id <= 100:
            # Truncate MC data to match history length exactly
            plot_prices = sim_prices[:hist_len] if hist_len > 0 else sim_prices
            plot_ofi = norm_ofi[:hist_len] if hist_len > 0 else norm_ofi
            
            if len(plot_prices) > 0:
                mc_final_prices.append(plot_prices[-1])
                
            ax_price.plot(plot_prices, color='darkorange', linewidth=0.8, alpha=0.15)
            ax_lower.plot(plot_ofi, color='darkorange', linewidth=0.8, alpha=0.15)

ax_price.set_title(f'Price Action: Reality vs {CURRENT_MODEL_NAME} Probability Cloud', fontsize=16)
ax_lower.set_title('AI Signal: Normalized Cumulative OFI [-1 to 1] (Shape Comparison)')
ax_lower.set_ylabel('Normalized Score')
ax_price.legend(loc='upper left')

mc_mean_end = np.mean(mc_final_prices) if len(mc_final_prices) > 0 else 0

# =========================================================================

ax_price.set_ylabel('Price (Ticks)')
ax_price.grid(True, alpha=0.3)

# Histogram of all prices across all realities
prices = features['closePrice']
ax_hist.hist(prices, bins=100, orientation='horizontal', color='darkorange', alpha=0.7)
if hist_len > 0:
    ax_hist.axhline(y=hist_end_price, color='midnightblue', linestyle='--', linewidth=2, label='Real Final Price')
ax_hist.set_title('Global Price Distribution')
ax_hist.legend()

ax_lower.grid(True, alpha=0.3)

# =========================================================================
# 6. ENHANCED SUMMARY METRICS
# =========================================================================
metrics_text = (
    f"--- PIPELINE REPORT ---\n\n"
    f"Asset Source:      {FILE_TAG}\n"
    f"RAM Payload:       {file_size_mb:.2f} MB\n"
    f"Total Buckets:     {total_buckets:,}\n"
    f"Timelines Plotted: {mc_count} MC + 1 Real\n\n"
    f"--- REALITY STATS (Ticks) ---\n\n"
    f"Historical End:    {hist_end_price:,.2f}\n"
    f"Historical Max:    {hist_max:,.2f}\n"
    f"Historical Min:    {hist_min:,.2f}\n\n"
    f"--- SIMULATION STATS (Ticks) ---\n\n"
    f"MC Mean End:       {mc_mean_end:,.2f}\n"
    f"MC Max Observed:   {mc_max:,.2f}\n"
    f"MC Min Observed:   {mc_min:,.2f}\n\n"
    f"Total Latency:     {time.time() - start_time:.2f}s"
)

ax_metrics.text(0.05, 0.95, metrics_text, fontsize=13, fontfamily='monospace', 
                verticalalignment='top', bbox=dict(boxstyle='round,pad=1', facecolor='#f8f9fa', edgecolor='gray'))

plt.tight_layout(rect=[0, 0.03, 1, 0.95])
output_img = os.path.join(script_dir, "..", "output", f"report_{FILE_TAG}_{CURRENT_MODEL_NAME}.png")
plt.savefig(output_img, dpi=150)

print(f"Analysis Complete! Multi-panel report saved: {output_img}")
os.system(f"explorer.exe $(wslpath -w '{output_img}') &")