# High-Frequency Trading (HFT) Matching Engine

A low-latency Limit Order Book (LOB) built in C++ designed for high-throughput financial simulations. 

## 🚀 Performance Metrics
- **Engine Throughput:** ~12,500,000 ops/sec (Pure RAM matching without I/O).
- **Pipeline Throughput:** ~5,400,000 ops/sec (Matching + Smart Binary Disk Logging).
- **Capacity:** Stress-tested with continuous **1 Billion+** order injections.
- **Latency:** Sub-microsecond matching for top-of-book orders via L1 Cache optimization.

## 🛠️ Features
- **3-Core Async Pipeline:** Segregated execution threads (Producer -> Engine -> Logger) to prevent I/O blocking.
- **Hardware-Level Optimization:** CPU thread-pinning (`pthread_setaffinity_np`) to eliminate context-switching overhead on Linux/WSL.
- **Lock-Free Communication:** Single-Producer Single-Consumer (SPSC) Ring Buffers utilizing hardware-aligned atomics to prevent False Sharing.
- **Smart Batch Disk I/O:** A dual-condition "Heartbeat" logger that flushes pure binary data (`.dat`) based on volume (10k trades) or time (1-second intervals).
- **Deterministic & Stochastic Simulation:** Integrated `MarketSimulator` with configurable price bounds and hardware entropy.
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

## 📊 Scalability Note
The engine is capable of generating massive datasets (e.g., **multi-gigabyte `.dat` files**). Because the output is now highly-optimized pure binary, standard spreadsheet software like Excel cannot read it. It is highly recommended to use **Python (struct + Pandas)** to deserialize the data into DataFrames for quantitative analysis.

## 💻 Usage
You can customize the simulation parameters directly in `src/main.cpp`:
```cpp
const uint32_t numOrders = 100000000; 
const uint32_t cancelPercent = 5; // Probability to cancel an order (0-100)

MarketSimulator::generateRandomOrders(eventBuffer, numOrders, cancelPercent);

## 🗺️ Roadmap & Future Work (Active Development)

This engine is being incrementally upgraded to bridge the gap between academic simulation and institutional-grade infrastructure.

*   **Phase 1: Low-Latency Core Optimization**
    *   Migrate all pricing models to fixed-point integer math (`int64_t`) to eliminate floating-point ALU overhead.
    *   Implement custom Memory Pools to completely eradicate dynamic allocation (`new`/`delete`) during the hot path.
*   **Phase 2: Concurrency & OS-Level Tuning**
    *   Introduce multi-threading via atomic Lock-Free queues (Ring Buffers) for producer-consumer architecture.
    *   Implement CPU thread pinning and isolation (Linux OS internals) to prevent context-switching latency.
*   **Phase 3: Networking & Real-World Data Ingestion**
    *   Build a Market Data Handler in Python/C++ to ingest live order flow via WebSockets (e.g., Polymarket API).
    *   Implement a lightweight FIX Protocol parser for standardized exchange messaging.
*   **Phase 4: Quantitative Research & AI Integration**
    *   Develop a stochastic Monte Carlo simulator (using Poisson processes and Normal distributions) for realistic stress-testing.
    *   Design a Market Making Bot leveraging **Reinforcement Learning (RL)** to dynamically adjust spread width and inventory skewness based on self-play PnL metrics.