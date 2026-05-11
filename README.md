# High-Frequency Trading (HFT) Matching Engine

A low-latency Limit Order Book (LOB) built in C++ designed for high-throughput financial simulations and massive historical data replay. 

## 🚀 Performance Metrics
- **Engine Throughput (Monte Carlo):** ~12,500,000 ops/sec (Pure RAM matching without I/O).
- **Pipeline Throughput (Async Logging):** ~5,400,000 ops/sec (Matching + Smart Binary Disk Logging).
- **Historical Replay Throughput:** ~1,470,000 ops/sec (Reading multi-gigabyte pure binary real-world market data directly from SSD into the matching engine).
- **Capacity:** Stress-tested with continuous **1 Billion+** order injections and 20GB+ historical datasets.
- **Latency:** Sub-microsecond matching for top-of-book orders via L1 Cache optimization.

## 🛠️ Features
- **3-Core Async Pipeline:** Segregated execution threads (Producer -> Engine -> Logger) to prevent I/O blocking.
- **Hardware-Level Optimization:** CPU thread-pinning (`pthread_setaffinity_np`) to eliminate context-switching overhead on Linux/WSL.
- **Lock-Free Communication:** Single-Producer Single-Consumer (SPSC) Ring Buffers utilizing hardware-aligned atomics to prevent False Sharing.
- **Multi-Model Mathematical Engine:** A Monte Carlo simulator generating alternate realities using Geometric Brownian Motion (GBM), Mean Reversion, Jump Diffusion, Cauchy distributions, and Trending algorithms.
- **Big Data ETL Pipeline:** Fully automated Python downloader that fetches years of Binance tick data, extracts it, triggers the C++ pre-compiler, and cleans up the heavy files to protect local storage limits.
- **Ultra-Fast Binary Ingestion:** Bypasses standard CSV string-parsing by pre-compiling historical data into 21-byte raw structs, allowing the engine to ingest months of trades in seconds.
- **Dynamic Quantitative Dashboard:** Python visualizer using memory-mapping (`np.memmap`) to render hundreds of millions of trades without crashing. It auto-detects the engine's execution mode (Historical vs. Probabilistic) to display context-aware risk and dispersion analytics.
- **Zero-Latency CLI:** Non-blocking terminal progress reporting.

## 🏗️ Architectural Decisions (ADR)

### 1. Price Indexing: Cache-Local Deques
- **Decision:** Used a pre-allocated `std::vector<std::deque<Order>>` where the index represents the price tick.
- **Why:** Achieves absolute **O(1) access time**, bypassing O(log N) search overhead and O(N) memory shifts.
- **Trade-off:** Optimized for CPU Cache locality over RAM footprint. Orders physically sit next to each other in the silicon.

### 2. Order Cancellation: Lazy Deletion (O(1))
- **Decision:** Implemented a "Soft Delete" ledger using a pre-allocated `std::vector<bool>`.
- **Why:** Instead of searching for an order to remove it (O(N)), we mark it as cancelled using its ID as the direct index. The matching engine ignores these ghost orders using `[[unlikely]]` branch prediction only when they reach the top of the book. 
- **Result:** Constant time cancellation regardless of the book size.

### 3. Smart Output Management & Binary Batching
- **Decision:** Automated path resolution using C++20 `std::filesystem` and pure binary `.dat` files.
- **Why:** Asking the OS to write millions of human-readable CSV rows causes catastrophic backpressure. The engine detects its execution context, batches 10,000 trades in RAM, and dumps them into a dedicated `./output` directory in one microscopic block.

### 4. Concurrency: SPSC Lock-Free Ring Buffers
- **Decision:** Replaced traditional `std::mutex` with atomic ring buffers (`alignas(64)`).
- **Why:** Mutex locks force the CPU to sleep. Lock-free atomics aligned to 64-byte cache lines allow the Engine and Logger cores to communicate safely without ever waiting for each other.

### 5. Big Data Pipeline: The "Pre-Compiler" Pattern
- **Decision:** Never read CSV files in the hot path. Created a standalone `PreCompiler.cpp`.
- **Why:** Text parsing (`std::stod`, `std::stoull`) is a massive CPU bottleneck. The Pre-Compiler does the dirty work once, converting heavy 20GB+ CSVs into ultra-compact, 21-byte C++ structs (`.bin`). The main engine directly copies these bytes from the SSD to the CPU, maximizing hardware bandwidth.

## 📊 Scalability Note
The engine is capable of generating massive datasets (e.g., **multi-gigabyte `.dat` files**). Because the output is now highly-optimized pure binary, standard spreadsheet software like Excel cannot read it. It is highly recommended to use the included **Python visualizer (`scripts/visualizer.py`)** which uses memory-mapping to instantly deserialize the data into arrays for quantitative analysis without filling up your RAM.

## 💻 Usage
You can customize the simulation mode directly via the Master Toggle in `src/main.cpp`:

```cpp
// =========================================================================
// --- MASTER TOGGLE: HISTORICAL (BINARY) VS MONTE CARLO ---
// =========================================================================
// Set to 'true' to replay real Binance binary data (1 timeline).
// Set to 'false' to run mathematical Monte Carlo universes (1000 timelines).
bool RUN_HISTORICAL = true; 
// =========================================================================

// If Monte Carlo is selected:
const uint32_t numOrdersPerSim = 1000000; 
const uint32_t NUM_SIMULATIONS = 1000;    
MarketModel currentModel = MarketModel::GBM;

## 🗺️ Roadmap & Future Work (Active Development)

This engine is being incrementally upgraded to bridge the gap between academic simulation and institutional-grade infrastructure.

*   **Phase 1: Networking & Real-World Data Ingestion**
    *   Build a Market Data Handler in Python/C++ to ingest live order flow via WebSockets (e.g., Polymarket API).
    *   Implement a lightweight FIX Protocol parser for standardized exchange messaging.
*   **Phase 2: Quantitative Research & AI Integration**
    *   Develop a stochastic Monte Carlo simulator (using Poisson processes and Normal distributions) for realistic stress-testing.
    *   Design a Market Making Bot leveraging **Reinforcement Learning (RL)** to dynamically adjust spread width and inventory skewness based on self-play PnL metrics.

## AND MORE TO COME