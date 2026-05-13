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
    ('padding', np.uint32), 
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
print(f"Successfully loaded {total_buckets:,} State Vectors from RAM ({file_size_mb:.2f} MB).")

# 3. DASHBOARD INITIALIZATION
fig = plt.figure(figsize=(22, 14))
gs = fig.add_gridspec(2, 2, width_ratios=[4, 1], height_ratios=[2.5, 1.2])
fig.suptitle(f'MotorHFT Analytics | Model: {CURRENT_MODEL_NAME} | Asset: {FILE_TAG}', fontsize=24, fontweight='bold')

ax_price = fig.add_subplot(gs[0, 0])
ax_hist = fig.add_subplot(gs[0, 1], sharey=ax_price)
ax_lower = fig.add_subplot(gs[1, 0], sharex=ax_price)
ax_metrics = fig.add_subplot(gs[1, 1])
ax_metrics.axis('off')

# =========================================================================
# PLOTTING LOGIC: HISTORICAL VS MONTE CARLO
# =========================================================================

if is_historical:
    prices = features['closePrice']
    ax_price.plot(prices, color='midnightblue', linewidth=1.5, alpha=0.9)
    ax_price.set_title('Price Action (Single Reality)', fontsize=16)
    
    cumulative_ofi = np.cumsum(features['orderFlowImbalance'])
    ax_lower.plot(cumulative_ofi, color='crimson', linewidth=1.5)
    ax_lower.set_title('AI Signal: Cumulative Order Flow Imbalance')
    
    final_price = prices[-1]
    min_price = prices.min()
    max_price = prices.max()

else:
    # MONTE CARLO MODE
    ax_price.set_title(f'Monte Carlo Simulation: 1000 Alternate Realities ({CURRENT_MODEL_NAME})', fontsize=16)
    
    unique_sims = np.unique(features['simId'])
    # To prevent matplotlib from freezing, we only plot the first 100 paths
    plot_limit = min(100, len(unique_sims)) 
    
    final_prices = []
    
    print(f"Plotting {plot_limit} simulation paths...")
    for i in range(plot_limit):
        sim_data = features[features['simId'] == unique_sims[i]]
        sim_prices = sim_data['closePrice']
        final_prices.append(sim_prices[-1])
        
        # Draw each reality faintly
        ax_price.plot(sim_prices, linewidth=1, alpha=0.15)
        
        # Plot Cumulative OFI for each path in the lower chart
        cumulative_ofi = np.cumsum(sim_data['orderFlowImbalance'])
        ax_lower.plot(cumulative_ofi, linewidth=1, alpha=0.15)
        
    ax_lower.set_title('Simulation OFI Drift')
    
    prices = features['closePrice'] # For the overall histogram
    final_price = np.mean(final_prices) # Average ending price
    min_price = prices.min()
    max_price = prices.max()

# =========================================================================

ax_price.set_ylabel('Price (Ticks)')
ax_price.grid(True, alpha=0.3)

# Histogram of all prices across all realities
ax_hist.hist(prices, bins=100, orientation='horizontal', color='darkorange', alpha=0.7)
ax_hist.set_title('Global Price Distribution')

ax_lower.grid(True, alpha=0.3)

# 6. SUMMARY METRICS
metrics_text = (
    f"--- PIPELINE REPORT ---\n\n"
    f"Asset Source:      {FILE_TAG}\n"
    f"RAM Payload:       {file_size_mb:.2f} MB\n"
    f"Total Buckets:     {total_buckets:,}\n\n"
    f"--- MARKET STATS (Ticks) ---\n\n"
    f"{'Ending Price:' if is_historical else 'Mean Ending Price:'} {final_price:,.2f}\n"
    f"Max Observed:      {max_price:,.2f}\n"
    f"Min Observed:      {min_price:,.2f}\n\n"
    f"Total Latency:     {time.time() - start_time:.2f}s"
)

ax_metrics.text(0.1, 0.9, metrics_text, fontsize=14, fontfamily='monospace', 
                verticalalignment='top', bbox=dict(boxstyle='round,pad=1', facecolor='#f8f9fa', edgecolor='gray'))

plt.tight_layout(rect=[0, 0.03, 1, 0.95])
output_img = os.path.join(script_dir, "..", "output", f"report_{FILE_TAG}_{CURRENT_MODEL_NAME}.png")
plt.savefig(output_img, dpi=150)

print(f"Analysis Complete! Multi-panel report saved: {output_img}")
print("Opening report...")
os.system(f"explorer.exe $(wslpath -w '{output_img}') &")