import numpy as np
import os
import subprocess
import plotly.graph_objects as go
from plotly.subplots import make_subplots

script_dir = os.path.dirname(os.path.abspath(__file__))
meta_path = os.path.normpath(os.path.join(script_dir, "..", "output", "metadata.txt"))

if os.path.exists(meta_path):
    with open(meta_path, 'r') as f:
        lines = f.readlines()
        CURRENT_MODEL_NAME = lines[0].strip() if len(lines) > 0 else "UNKNOWN"
        FILE_TAG = os.path.basename(lines[1].strip()).replace('.bin', '') if len(lines) > 1 else "DATA"
else:
    CURRENT_MODEL_NAME = "UNKNOWN"
    FILE_TAG = "UNKNOWN"

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
    print("Error: RAM-disk file not found.")
    exit()

bot_trades_path = os.path.join(script_dir, "..", "output", "bot_trades.csv")
bot_actions = []
if os.path.exists(bot_trades_path):
    with open(bot_trades_path, 'r') as f:
        for line in f:
            parts = line.strip().split(',')
            if len(parts) == 4:
                bot_actions.append({
                    'simId': int(parts[0]),
                    'bucketId': int(parts[1]),
                    'action': parts[2],
                    'price': float(parts[3]) / 100.0
                })

features = np.fromfile(file_path, dtype=feature_dtype)
unique_sims = np.unique(features['simId'])

hist_mask = features['simId'] == 0
hist_len = len(features[hist_mask]) if len(features[hist_mask]) > 0 else 0

fig = make_subplots(
    rows=3, cols=1, 
    shared_xaxes=True,
    vertical_spacing=0.03,
    row_heights=[0.55, 0.25, 0.20],
    subplot_titles=('Market Price & Executions', 'Strategy Portfolio Value (USD)', 'Normalized Order Flow Imbalance')
)

total_sims_plotted = min(len(unique_sims), 15)
traces_per_sim = 5 
all_profits = []
all_drawdowns = []
buttons = []

for i, sim_id in enumerate(unique_sims):
    if i >= total_sims_plotted: break

    sim_data = features[features['simId'] == sim_id]
    
    sim_prices = sim_data['closePrice'][:hist_len] / 100.0 if hist_len > 0 else sim_data['closePrice'] / 100.0
    x_vals = np.arange(len(sim_prices))
    
    raw_ofi = np.cumsum(sim_data['orderFlowImbalance'])
    max_ofi_val = np.max(np.abs(raw_ofi))
    norm_ofi = raw_ofi / max_ofi_val if max_ofi_val != 0 else raw_ofi
    plot_ofi = norm_ofi[:hist_len] if hist_len > 0 else norm_ofi

    sim_trades = [t for t in bot_actions if t['simId'] == sim_id]
    buy_x, buy_y, sell_x, sell_y = [], [], [], []
    usd_balance = 10000.0
    asset_position = 0.0
    pnl_x, pnl_y = [0], [10000.0]

    for t in sim_trades:
        idx = t['bucketId'] - 1
        if idx < hist_len:
            if t['action'] == 'BUY':
                buy_x.append(idx)
                buy_y.append(t['price'])
                usd_balance -= t['price']
                asset_position += 1.0
            else:
                sell_x.append(idx)
                sell_y.append(t['price'])
                usd_balance += t['price']
                asset_position -= 1.0
            
            pnl_x.append(idx)
            pnl_y.append(usd_balance + (asset_position * t['price']))
            
    if len(sim_prices) > 0:
        pnl_x.append(len(sim_prices) - 1)
        pnl_y.append(usd_balance + (asset_position * sim_prices.iloc[-1] if hasattr(sim_prices, 'iloc') else sim_prices[-1]))

    pnl_array = np.array(pnl_y)
    net_profit_pct = ((pnl_array[-1] - 10000.0) / 10000.0) * 100.0
    roll_max = np.maximum.accumulate(pnl_array)
    drawdowns = (pnl_array - roll_max) / roll_max
    max_drawdown = np.min(drawdowns) * 100.0 if len(drawdowns) > 0 else 0.0
    total_trades = len(sim_trades)

    all_profits.append(net_profit_pct)
    all_drawdowns.append(max_drawdown)

    is_real = (sim_id == 0)
    
    # Establish distinct visual hierarchy between Reality and Monte Carlo clouds
    price_color = '#ffffff' if is_real else '#ff8c00'
    price_width = 2 if is_real else 1
    price_opacity = 1.0 if is_real else 0.3
    
    pnl_color = '#00bfff' if is_real else '#555555'
    pnl_width = 2 if is_real else 1
    pnl_opacity = 1.0 if is_real else 0.4
    fill_type = 'tozeroy' if is_real else 'none'

    # Add traces initialized for the OVERVIEW mode (Price and PnL visible, others hidden)
    fig.add_trace(go.Scatter(x=x_vals, y=sim_prices, mode='lines', line=dict(color=price_color, width=price_width), opacity=price_opacity, name='Real Price' if is_real else f'MC Price {sim_id}', visible=True), row=1, col=1)
    fig.add_trace(go.Scatter(x=buy_x, y=buy_y, mode='markers', marker=dict(symbol='triangle-up', color='#00ff00', size=12, line=dict(width=1, color='black')), name='BUY', visible=False), row=1, col=1)
    fig.add_trace(go.Scatter(x=sell_x, y=sell_y, mode='markers', marker=dict(symbol='triangle-down', color='#ff3333', size=12, line=dict(width=1, color='black')), name='SELL', visible=False), row=1, col=1)
    fig.add_trace(go.Scatter(x=pnl_x, y=pnl_y, mode='lines', line=dict(color=pnl_color, width=pnl_width), opacity=pnl_opacity, fill=fill_type, fillcolor='rgba(0, 191, 255, 0.1)', name='Real PnL' if is_real else f'MC PnL {sim_id}', visible=True), row=2, col=1)
    fig.add_trace(go.Scatter(x=x_vals, y=plot_ofi, mode='lines', line=dict(color='#9400d3', width=2), name='OFI', visible=False), row=3, col=1)

    vis_array = [False] * (total_sims_plotted * traces_per_sim)
    for j in range(traces_per_sim):
        vis_array[i * traces_per_sim + j] = True

    sim_name = "Reality" if is_real else f"Path {sim_id}"
    header_text = (
        f"<b>MotorHFT Quant Terminal</b> | Asset: {FILE_TAG} | Model: {CURRENT_MODEL_NAME}<br>"
        f"<span style='font-size: 14px; color: #00ff00;'>Net Profit: {net_profit_pct:+.2f}%</span> | "
        f"<span style='font-size: 14px; color: #ff3333;'>Max Drawdown: {max_drawdown:.2f}%</span> | "
        f"<span style='font-size: 14px; color: #00bfff;'>Executions: {total_trades}</span>"
    )

    buttons.append(dict(
        label=sim_name,
        method='update',
        args=[{'visible': vis_array}, {'title.text': header_text}]
    ))

avg_profit = np.mean(all_profits)
worst_dd = np.min(all_drawdowns)
win_rate = (sum(1 for p in all_profits if p > 0) / len(all_profits)) * 100.0

global_vis_array = [False] * (total_sims_plotted * traces_per_sim)
for i in range(total_sims_plotted):
    global_vis_array[i * traces_per_sim] = True      # Price
    global_vis_array[i * traces_per_sim + 3] = True  # PnL

global_header = (
    f"<b>MotorHFT GLOBAL SUMMARY</b> | Evaluated Paths: {total_sims_plotted}<br>"
    f"<span style='font-size: 14px; color: #00ff00;'>Avg Profit: {avg_profit:+.2f}%</span> | "
    f"<span style='font-size: 14px; color: #ff3333;'>Worst Case DD: {worst_dd:.2f}%</span> | "
    f"<span style='font-size: 14px; color: #00bfff;'>Win Rate: {win_rate:.0f}%</span>"
)

# Insert the Overview button at the top
buttons.insert(0, dict(
    label="OVERVIEW (All Paths)",
    method='update',
    args=[{'visible': global_vis_array}, {'title.text': global_header}]
))

fig.update_layout(
    height=900, 
    title_text=global_header, 
    template="plotly_dark",
    paper_bgcolor='#0a0a0a', 
    plot_bgcolor='#0a0a0a',   
    font=dict(color='#ffffff'),
    hovermode="x unified",
    margin=dict(l=40, r=20, t=110, b=20),
    xaxis=dict(showgrid=False),
    xaxis2=dict(showgrid=False),
    xaxis3=dict(showgrid=False),
    yaxis=dict(showgrid=True, gridcolor='#222222'),
    yaxis2=dict(showgrid=True, gridcolor='#222222'),
    yaxis3=dict(showgrid=True, gridcolor='#222222'),
    updatemenus=[dict(
        active=0,
        buttons=buttons,
        direction="down",
        pad={"r": 10, "t": 10},
        showactive=True,
        x=1.0, xanchor="right",
        y=1.12, yanchor="top",
        bgcolor='#111111', 
        bordercolor='#555555',
        font=dict(color='#ffffff', size=14)
    )]
)

output_file = os.path.join(script_dir, "..", "output", f"report_{FILE_TAG}_{CURRENT_MODEL_NAME}.html")

# Generate the raw HTML string from the Plotly figure
html_content = fig.to_html(config={'scrollZoom': True}, full_html=True)

# --- CSS INJECTION: FIX PLOTLY DROPDOWN HOVER BUG ---
# This forces the SVG elements of the menu to maintain a dark theme and highlights text in blue
custom_css = """
<style>
    body { background-color: #0a0a0a !important; margin: 0px !important; }
    
    /* Override Plotly's default hover behaviors for the update menu */
    .updatemenu-item rect { fill: #111111 !important; }
    .updatemenu-item:hover rect { fill: #2a2a2a !important; }
    .updatemenu-item text { fill: #ffffff !important; }
    .updatemenu-item:hover text { fill: #00bfff !important; font-weight: bold !important; }
</style>
"""
html_content = html_content.replace('</head>', f'{custom_css}</head>')

# Save the heavily customized HTML to disk
with open(output_file, 'w', encoding='utf-8') as f:
    f.write(html_content)

# Launch in Windows using WSL bridge
try:
    win_path = subprocess.check_output(['wslpath', '-w', output_file]).decode().strip()
    subprocess.run(['explorer.exe', win_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
except Exception as e:
    pass