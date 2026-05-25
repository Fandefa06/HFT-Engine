# High-Frequency Trading (HFT) Matching Engine

A high-performance Limit Order Book (LOB) matching engine and quantitative market simulator built from scratch in C++20. Designed for ultra-low latency, deterministic execution, and seamless integration with Machine Learning inference pipelines.

![Architecture Status](https://img.shields.io/badge/Architecture-C%2B%2B20%20%7C%20Lock--Free%20%7C%20Zero--Copy-blue)
![Build Status](https://img.shields.io/badge/Build-CMake%20%7C%20Release-brightgreen)

## 🚀 Performance Metrics
- **Engine Throughput (Monte Carlo):** ~12,500,000 ops/sec (Pure RAM matching).
- **Pipeline Throughput (Async Logging):** ~8,830,000 ops/sec (Matching + Smart Binary Disk Logging).
- **Historical Replay Throughput:** ~7,980,000 ops/sec (Reading multi-gigabyte pure binary data).
- **Capacity:** Stress-tested with continuous **1 Billion+** order injections and 20GB+ historical datasets.
- **Hybrid Cloud Generation:** Dynamically matches historical order counts (e.g., 143M+ orders) across parallel universes.
- **Latency:** Sub-microsecond matching for top-of-book orders via L1 Cache optimization.

## 🧠 AI Integration & Bot Infrastructure
* **AI Nutrients (`StateVector`):** The engine compresses high-frequency market depth updates into a 64-byte `StateVector` struct (OHLCV + Order Flow Imbalance). This allows for zero-copy ingestion by predictive models without runtime preprocessing lag.
* **BotBase Architecture:** An abstract execution layer allowing custom algorithmic strategies to be injected directly into the live market loop.

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
> **Core Philosophy:** In the markets, latency is the only real enemy. Every architectural decision prioritizes CPU cache locality and zero dynamic dispatch.

### 1. Concurrency: SPSC Lock-Free Ring Buffers & Thread Pinning
* **Decision:** Replaced traditional `std::mutex` with lock-free atomic ring buffers, forcing memory alignment (`alignas(64)`).
* **Why:** Mutexes force the OS scheduler to intervene. Lock-free atomics aligned to 64-byte boundaries allow the Engine and Logger cores to communicate synchronously while preventing CPU False Sharing. Combined with `pthread_setaffinity_np` (thread pinning to physical cores), this ensures deterministic latency in the hot path.

### 2. Price Indexing: Cache-Local Deques (O(1))
* **Decision:** Pre-allocated `std::vector<std::deque<Order>>` where the index maps directly to the price tick.
* **Why:** Achieves absolute **O(1) access time**, bypassing the O(log N) overhead of standard maps. Orders physically sit contiguous in silicon (L1/L2 Cache).

### 3. Order Cancellation: Lazy Deletion (O(1))
* **Decision:** Implemented a "Soft Delete" ledger using a pre-allocated `std::vector<bool>`.
* **Why:** Instead of searching for an order to delete it (O(N)), we flag its ID as cancelled. The matching engine ignores these ghost orders using `[[unlikely]]` branch prediction only when they reach the top of the book.

### 4. Zero Dynamic Dispatch
* **Decision:** Heavy use of C++ Templates (`OrderBook<ETH_Policy>`).
* **Why:** Market rules (tick sizes, price bounds) are resolved strictly at compile-time, completely eliminating `vtable` overhead during live execution.

### 5. The "Pre-Compiler" Data Pipeline
* **Decision:** Never read CSV files in the hot path.
* **Why:** Text parsing (`std::stod`) destroys performance. A standalone `PreCompiler.cpp` converts massive historical CSVs into 21-byte raw binary structs. The main engine copies these bytes directly from SSD to CPU memory.

## 📊 Scalability Note
The engine is capable of generating massive datasets (e.g., **multi-gigabyte `.dat` files**). Because the output is now highly-optimized pure binary. It is highly recommended to use the included **Python visualizer (`scripts/visualizer.py`)** which uses memory-mapping to instantly deserialize the data into arrays for quantitative analysis without filling up your RAM.

## 📖 Glossary & Technical Definitions

- **Limit Order Book (LOB):** The central ledger where resting Buy (Bid) and Sell (Ask) orders are matched.
- **Order Flow Imbalance (OFI):** A metric tracking net aggressive buying vs selling pressure, normalized from -1 to 1.
- **DNA Extraction:** Reverse-engineering historical returns to inject real Drift and Volatility into simulations.
- **False Sharing:** A CPU bottleneck prevented here using hardware alignment (alignas(64)).
- **SPSC Ring Buffer:** A lock-free structure allowing the engine and logger to communicate without pausing.

## 💻 Usage
You can customize the simulation mode directly via the Master Toggle in `src/main.cpp`:

```cpp
    // =========================================================================
    // --- MASTER TOGGLE: HISTORICAL (BINARY) VS MONTE CARLO ---
    // =========================================================================
    bool RUN_HISTORICAL = true;
    bool RUN_MONTE_CARLO = true; 
    bool SAVE_FEATURES = true;
    // =========================================================================


    // =========================================================================
    // --- BINARY FILE TO OBTAIN THE DATA ---
    // =========================================================================
    std::string binFilename = "data/ETHUSDT-trades-2022-11.bin";
    // =========================================================================


    // ========================================================================
    // --- CHANGE THE POLICY OF THE DATA TO ANALYZE HERE ---
    using ActivePolicy = ETH_Policy; 
    OrderBook<ActivePolicy> myBook;
    // ========================================================================



    // =========================================================================
    // --- MONTE CARLO PARAMETERS ---
    // =========================================================================
    // DYNAMIC MATCHING: We start at 0 and auto-detect the size of the history!
    uint64_t dynamicNumOrders = 0; 
    uint64_t targetBuckets = 10000; // Target used for atomic kill switch
    const uint32_t NUM_SIMULATIONS = 5;    
    // =========================================================================
    

    // ==========================================================================
    // --- CHOOSE THE DISTRIBUTION FOR THE MONTE CARLO SIMULATION
    // ==========================================================================
    MarketModel currentModel = MarketModel::JUMP_DIFFUSION;
    // ==========================================================================
    std::string modelName;
    switch(currentModel) {
        case MarketModel::GBM:            modelName = "GBM"; break;
        case MarketModel::MEAN_REVERSION: modelName = "MEAN_REVERSION"; break;
        case MarketModel::JUMP_DIFFUSION: modelName = "JUMP_DIFFUSION"; break;
        case MarketModel::CAUCHY:         modelName = "CAUCHY"; break;
        case MarketModel::TRENDING:       modelName = "TRENDING"; break;
    }

    // ==========================================================================
    // Execution command (WSL && Linux)
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j $(nproc) && ./build/MotorHFT
    //==========================================================================

```
## 🗺️ Roadmap & Future Work (Active Development)

This engine is being incrementally upgraded to bridge the gap between academic simulation and institutional-grade infrastructure.

- [ ]**Phase 1: Networking & Real-World Data Ingestion**
    *   Build a Market Data Handler in Python/C++ to ingest live order flow via WebSockets (e.g., Polymarket API).
    *   Implement a lightweight FIX Protocol parser for standardized exchange messaging.
- [*]**Phase 2: Quantitative Research & AI Integration (In Progress)**
    *   Develop stochastic Monte Carlo simulator (Completed)
    *  Integrate C++ execution bots (SimpleBot) to track PnL (Next Step)

## AND MORE TO COME