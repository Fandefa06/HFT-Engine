🚀 High-Frequency Trading (HFT) Matching Engine

A low-latency, institutional-grade Limit Order Book (LOB) and Market Simulator built in C++. Designed for high-throughput financial simulations, massive historical data replay, and quantitative AI research.

⚡ Performance Metrics

Engine Throughput (Monte Carlo): ~12,500,000 ops/sec (Pure RAM matching without I/O).

Pipeline Throughput (Async Logging): ~8,800,000 ops/sec (Matching + Smart Binary Disk Logging).

Historical Replay Throughput: ~1,470,000 ops/sec (Reading multi-gigabyte pure binary real-world market data directly from SSD into the matching engine).

Capacity: Stress-tested with continuous 1 Billion+ order injections and 20GB+ historical datasets without memory leaks.

Latency: Sub-microsecond matching for top-of-book orders via L1 Cache optimization.

🛠️ Key Features

3-Core Async Pipeline: Segregated execution threads (Producer -> Engine -> Logger) to prevent I/O blocking.

Hardware-Level Optimization: CPU thread-pinning (pthread_setaffinity_np) to eliminate context-switching overhead on Linux/WSL.

Lock-Free Communication: Single-Producer Single-Consumer (SPSC) Ring Buffers utilizing hardware-aligned atomics to prevent False Sharing.

Multi-Model Mathematical Engine: A Monte Carlo simulator generating alternate realities using Geometric Brownian Motion (GBM), Mean Reversion, Jump Diffusion, Cauchy distributions, and Trending algorithms.

DNA Extraction & Historical Shadowing: Automatically extracts the mathematical drift and volatility from historical tick data. The engine dynamically matches the exact order count of the real history to generate perfectly length-matched parallel universes.

Ultra-Fast Binary Ingestion: Bypasses standard CSV string-parsing by pre-compiling historical data into 21-byte raw structs, allowing the engine to ingest months of trades in seconds.

Dynamic Quantitative Dashboard: Python visualizer using memory-mapping (np.memmap) to render hundreds of millions of trades instantly. Features normalized Order Flow Imbalance (OFI) comparison and probability cloud rendering.

Big Data ETL Pipeline: Fully automated Python downloader that fetches years of Binance tick data, extracts it, triggers the C++ pre-compiler, and cleans up heavy files to protect local storage limits.

Zero-Latency CLI: Non-blocking terminal progress reporting.

🏗️ Architectural Decisions (ADR)

1. Price Indexing: Cache-Local Deques

Decision: Used a pre-allocated std::vector<std::deque<Order>> where the index represents the price tick.

Why: Achieves absolute O(1) access time, bypassing O(log N) search overhead and O(N) memory shifts.

Trade-off: Optimized for CPU Cache locality over RAM footprint. Orders physically sit next to each other in the silicon.

2. Order Cancellation: Lazy Deletion (O(1))

Decision: Implemented a "Soft Delete" ledger using a pre-allocated std::vector<OrderId>.

Why: Instead of searching for an order to remove it (O(N)), we mark it as cancelled using its ID as the direct index. The matching engine ignores these ghost orders using [[unlikely]] branch prediction only when they reach the top of the book.

Result: Constant time cancellation regardless of the book size.

3. Template-Based Asset Policies

Decision: Used C++ templates (OrderBook<ETH_Policy>) to define asset rules at compile-time.

Why: Avoids runtime branching for tick sizes and price bounds. Assets like BTC, ETH, and the S&P 500 E-mini have custom bounds and multipliers defined safely in AssetPolicies.hpp.

4. Smart Output Management & "AI Nutrients"

Decision: Data is compressed into 64-byte StateVector structs (OHLCV + Order Flow Imbalance).

Why: Asking the OS to write millions of human-readable CSV rows causes catastrophic backpressure. The engine batches 1,000-trade buckets in RAM and streams them into /dev/shm (RAM-disk) as pure binary for instantaneous Python consumption.

5. Concurrency: SPSC Lock-Free Ring Buffers

Decision: Replaced traditional std::mutex with atomic ring buffers (alignas(64)).

Why: Mutex locks force the CPU to sleep. Lock-free atomics aligned to 64-byte cache lines allow the Engine and Logger cores to communicate safely without ever waiting for each other.

6. Big Data Pipeline: The "Pre-Compiler" Pattern

Decision: Never read CSV files in the hot path. Created a standalone PreCompiler.cpp.

Why: Text parsing (std::stod, std::stoull) is a massive CPU bottleneck. The Pre-Compiler does the dirty work once, converting heavy 20GB+ CSVs into ultra-compact .bin files.

⚙️ Build & Installation

Requirements

Linux or WSL2 (Windows Subsystem for Linux)

GCC/G++ (Compiler supporting C++20)

CMake

Python 3 (with numpy and matplotlib)

Compilation Commands

# 1. Configure CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build the engine across all CPU cores
cmake --build build -j $(nproc)

# 3. Pre-compile historical CSVs into Binary (Optional)
g++ -O3 src/PreCompiler.cpp -o precompiler
./precompiler "NAME.csv" "NAME.bin"

# 4. Run the Engine
./build/MotorHFT


💻 Usage

You can customize the simulation mode directly via the Master Toggles in src/main.cpp. The engine supports running Historical, Monte Carlo, or a Hybrid Mode that combines both.

// =========================================================================
// --- MASTER TOGGLE: HISTORICAL (BINARY) VS MONTE CARLO ---
// =========================================================================
// Set both to 'true' to run a Hybrid Simulation (Reality vs Probability Cloud)
bool RUN_HISTORICAL = true; 
bool RUN_MONTE_CARLO = true; 
// =========================================================================

// Asset selection via Compile-Time Policies
OrderBook<ETH_Policy> myBook; // Options: BTC_Policy, ETH_Policy, SP500_Policy

// Mathematical Model Selection
MarketModel currentModel = MarketModel::JUMP_DIFFUSION; // GBM, CAUCHY, TRENDING, etc.


📊 Scalability Note

The engine is capable of generating massive datasets (e.g., multi-gigabyte .dat files). Because the output is highly-optimized pure binary, standard spreadsheet software like Excel cannot read it. It is highly recommended to use the included Python visualizer (scripts/visualizer.py), which uses memory-mapping (np.fromfile) to instantly deserialize the data into arrays for quantitative analysis without exhausting system RAM.

🗺️ Roadmap & Future Work (Active Development)

This engine is being incrementally upgraded to bridge the gap between academic simulation and institutional-grade infrastructure.

Phase 1: Networking & Real-World Data Ingestion

Build a Market Data Handler in Python/C++ to ingest live order flow via WebSockets (e.g., Polymarket API).

Implement a lightweight FIX Protocol parser for standardized exchange messaging.

Phase 2: Quantitative Research & Algorithmic Trading (In Progress)

Develop a stochastic Monte Carlo simulator (using Poisson processes and Normal distributions) for realistic stress-testing. (Completed)

Integrate C++ execution bots (SimpleBot) that analyze Order Flow Imbalance (OFI) directly from the StateVector to track Mark-to-Market PnL.

Design a Market Making Bot leveraging Reinforcement Learning (RL) to dynamically adjust spread width and inventory skewness based on self-play PnL metrics.

AND MORE TO COME