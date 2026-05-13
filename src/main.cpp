// main.cpp

// ================================================================
// --- VERY IMPORTANT, MONTE CARLO IS BROKEN, SHOULD BE FIXED SOON
// ================================================================

#include <iostream>
#include <chrono>
#include <thread>
#include <pthread.h>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <vector>
#include "OrderBook.hpp"
#include "MarketSimulator.hpp"
#include "MonteCarloSimulator.hpp"
#include "RingBuffer.hpp"
#include "HistoricalParser.hpp"
#include "BinaryParser.hpp"
#include "AssetPolicies.hpp"

void pinThread(int core_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

int main() {

    // =========================================================================
    // --- MASTER TOGGLE: HISTORICAL (BINARY) VS MONTE CARLO ---
    // =========================================================================
    bool RUN_HISTORICAL = false;
    // =========================================================================


    // =========================================================================
    // --- BINARY FILE TO OBTAIN THE DATA ---
    // =========================================================================
    std::string binFilename = "data/ETHUSDT-trades-2025-05.bin";
    // =========================================================================


    // ========================================================================
    // --- CHANGE THE POLICY OF THE DATA TO ANALYZE HERE ---
    OrderBook<ETH_Policy> myBook;
    // ========================================================================



    // =========================================================================
    // --- MONTE CARLO PARAMETERS ---
    // =========================================================================
    const uint32_t numOrdersPerSim = 1000000; // Total orders per simulation path
    const uint32_t NUM_SIMULATIONS = 100;    // Number of parallel realities
    // =========================================================================
    

    // ==========================================================================
    // --- CHOOSE THE DISTRIBUTION FOR THE MONTE CARLO SIMULATION
    // ==========================================================================
   
    // MarketModel currentModel = MarketModel::GBM;
    // MarketModel currentModel = MarketModel::MEAN_REVERSION;
    MarketModel currentModel = MarketModel::JUMP_DIFFUSION;
    // MarketModel currentModel = MarketModel::CAUCHY;
    // MarketModel currentModel = MarketModel::TRENDING;
    // ==========================================================================




    RingBuffer<MarketEvent, 4194304> eventBuffer; 
    RingBuffer<Trade, 4194304> tradeBuffer; 
    myBook.setTradeBuffer(&tradeBuffer); 

    

    // Ensure output directory exists and initialize RAM-disk file
    std::filesystem::create_directories("output");
    // Initialize the RAM-disk file once
    std::ofstream initFile("/dev/shm/features_binary.dat", std::ios::binary | std::ios::trunc);
    initFile.close();

    auto startGlobal = std::chrono::high_resolution_clock::now();

    if (RUN_HISTORICAL) {
        // =========================================================================
        // 1. HISTORICAL REPLAY MODE (Single Timeline - PURE BINARY)
        // =========================================================================
        std::cout << "=======================================" << std::endl;
        std::cout << "--- STARTING BINARY DATA ENGINE ---" << std::endl;
        std::cout << "Target: " << binFilename << std::endl;
        std::cout << "=======================================\n" << std::endl;

        myBook.setSimId(0); 
        myBook.reset();

        std::atomic<bool> producerDone{false};
        std::atomic<bool> consumerDone{false};
        std::atomic<uint64_t> totalTradesLogged{0};
        std::atomic<uint64_t> totalRawTrades{0}; // Tracks raw speed

        // THREAD 1: PRODUCER
        std::thread producer([&]() {
            pinThread(0); 
            BinaryParser::feedFromBinary(eventBuffer, binFilename);
            producerDone.store(true, std::memory_order_release);
        });

        // THREAD 2: CONSUMER
        std::thread consumer([&]() {
            pinThread(2); 
            MarketEvent ev;
            bool running = true;
            while (running) {
                if (eventBuffer.pop(ev)) {
                    if (ev.type == EventType::NEW_ORDER) myBook.addOrder(ev.order);
                    else if (ev.type == EventType::CANCEL_ORDER) myBook.cancelOrder(ev.order.id);
                    else if (ev.type == EventType::TERMINATE) running = false;
                } else { __builtin_ia32_pause(); }
            }
            consumerDone.store(true, std::memory_order_release);
        });

        // THREAD 3: THE FEATURE ENGINE (RAM-Only Aggregator)
        std::thread logger([&]() {
            pinThread(4); 
            std::ofstream file("/dev/shm/features_binary.dat", std::ios::binary | std::ios::trunc);
            
            Trade t;
            uint64_t tradeCount = 0;
            uint64_t bucketCounter = 0;
            const uint64_t TRADES_PER_BUCKET = 1000; 
            
            StateVector currentBucket;
            std::vector<StateVector> memoryBuffer;
            memoryBuffer.reserve(10000); 
            
            while (!consumerDone.load(std::memory_order_acquire) || tradeCount > 0) {
                if (tradeBuffer.pop(t)) {
                    tradeCount++;
                    
                    if (tradeCount % TRADES_PER_BUCKET == 1) {
                        currentBucket = StateVector(); 
                        currentBucket.simId = t.simId;
                        currentBucket.bucketId = ++bucketCounter;
                        currentBucket.openPrice = t.price;
                    }
                    
                    if (t.price > currentBucket.highPrice) currentBucket.highPrice = t.price;
                    if (t.price < currentBucket.lowPrice)  currentBucket.lowPrice = t.price;
                    currentBucket.closePrice = t.price;
                    currentBucket.totalVolume += t.quantity;
                    
                    if (t.buyerId > t.sellerId) { 
                        currentBucket.orderFlowImbalance += t.quantity; 
                    } else if (t.sellerId > t.buyerId) {
                        currentBucket.orderFlowImbalance -= t.quantity; 
                    }

                    if (tradeCount % TRADES_PER_BUCKET == 0) {
                        memoryBuffer.push_back(currentBucket);
                        if (memoryBuffer.size() >= 1000) {
                            file.write(reinterpret_cast<const char*>(memoryBuffer.data()), memoryBuffer.size() * sizeof(StateVector));
                            memoryBuffer.clear();
                        }
                    }
                } else {
                    if (consumerDone.load(std::memory_order_acquire)) break;
                    __builtin_ia32_pause();
                }
            }
            
            if (tradeCount % TRADES_PER_BUCKET != 0 && tradeCount > 0) {
                memoryBuffer.push_back(currentBucket);
            }
            if (!memoryBuffer.empty()) {
                file.write(reinterpret_cast<const char*>(memoryBuffer.data()), memoryBuffer.size() * sizeof(StateVector));
            }
            
            totalTradesLogged.store(bucketCounter, std::memory_order_release);
            totalRawTrades.store(tradeCount, std::memory_order_release);
            file.close();
        });

        producer.join(); 
        consumer.join(); 
        logger.join();

        auto endGlobal = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = endGlobal - startGlobal;

        // Save metadata as HISTORICAL
        std::ofstream meta("output/metadata.txt");
        meta << "HISTORICAL\n" << binFilename;
        meta.close();

        // Load the values for printing
        uint64_t finalBuckets = totalTradesLogged.load();
        uint64_t finalRawTrades = totalRawTrades.load();

        std::cout << "\n--- BINARY REPLAY COMPLETE ---" << std::endl;
        std::cout << "Total Realities:           1 (The Real World)" << std::endl;
        std::cout << "Total Feature Buckets:     " << finalBuckets << std::endl;
        std::cout << "Elapsed time:              " << elapsed.count() << " seconds" << std::endl;
        std::cout << "Average Throughput:        " << (finalRawTrades / elapsed.count()) << " trades/sec" << std::endl;
        std::cout << "Metadata saved:            HISTORICAL -> output/metadata.txt" << std::endl;
        std::cout << "----------------------------------" << std::endl;

    } else {
        // =========================================================================
        // 2. MONTE CARLO MODE (Mathematical Multi-Path Engine)
        // =========================================================================
        std::string modelName;
        switch(currentModel) {
            case MarketModel::GBM:            modelName = "GBM"; break;
            case MarketModel::MEAN_REVERSION: modelName = "MEAN_REVERSION"; break;
            case MarketModel::JUMP_DIFFUSION: modelName = "JUMP_DIFFUSION"; break;
            case MarketModel::CAUCHY:         modelName = "CAUCHY"; break;
            case MarketModel::TRENDING:       modelName = "TRENDING"; break;
        }

        std::cout << "=======================================" << std::endl;
        std::cout << "--- STARTING MONTE CARLO SIMULATION ---" << std::endl;
        std::cout << "Model: " << modelName << " | Paths: " << NUM_SIMULATIONS << std::endl;
        std::cout << "=======================================\n" << std::endl;

        std::atomic<uint64_t> totalTradesGlobal{0};
        std::atomic<uint64_t> totalRawTradesGlobal{0};

        // For Monte Carlo, we append to the same RAM file across realities
        std::ofstream file("/dev/shm/features_binary.dat", std::ios::binary | std::ios::app);
        
        for (uint32_t sim = 0; sim < NUM_SIMULATIONS; ++sim) {
            std::cout << "Running Simulation " << sim + 1 << " / " << NUM_SIMULATIONS << "...\r" << std::flush;
            
            myBook.setSimId(sim);
            myBook.reset();

            std::atomic<bool> producerDone{false};
            std::atomic<bool> consumerDone{false};
            std::atomic<uint64_t> totalTradesSim{0};
            std::atomic<uint64_t> totalRawTradesSim{0};

            // --- THE ANCHOR PRICE ---
            // Calculate the exact center of the BTC_Policy RAM window!
            Price startPrice = BTC_Policy::minPriceTicks + ((BTC_Policy::maxPriceTicks - BTC_Policy::minPriceTicks) / 2);

            std::thread producer([&]() {
                pinThread(0); 
                // Pass the startPrice to the generator
                MonteCarloSimulator::generateFlow(eventBuffer, numOrdersPerSim, 90, currentModel, startPrice);
                producerDone.store(true, std::memory_order_release);
            });

            std::thread consumer([&]() {
                pinThread(2); 
                MarketEvent ev;
                bool running = true;
                while (running) {
                    if (eventBuffer.pop(ev)) {
                        if (ev.type == EventType::NEW_ORDER) myBook.addOrder(ev.order);
                        else if (ev.type == EventType::CANCEL_ORDER) myBook.cancelOrder(ev.order.id);
                        else if (ev.type == EventType::TERMINATE) running = false;
                    } else { __builtin_ia32_pause(); }
                }
                consumerDone.store(true, std::memory_order_release);
            });

            // THREAD 3: THE FEATURE ENGINE (RAM-Only Aggregator for MC)
            std::thread logger([&]() {
                pinThread(4); 
                
                Trade t;
                uint64_t tradeCount = 0;
                uint64_t bucketCounter = 0;
                const uint64_t TRADES_PER_BUCKET = 1000; 
                
                StateVector currentBucket;
                std::vector<StateVector> memoryBuffer;
                memoryBuffer.reserve(10000); 
                
                while (!consumerDone.load(std::memory_order_acquire) || tradeCount > 0) {
                    if (tradeBuffer.pop(t)) {
                        tradeCount++;
                        
                        if (tradeCount % TRADES_PER_BUCKET == 1) {
                            currentBucket = StateVector(); 
                            currentBucket.simId = t.simId;
                            currentBucket.bucketId = ++bucketCounter;
                            currentBucket.openPrice = t.price;
                        }
                        
                        if (t.price > currentBucket.highPrice) currentBucket.highPrice = t.price;
                        if (t.price < currentBucket.lowPrice)  currentBucket.lowPrice = t.price;
                        currentBucket.closePrice = t.price;
                        currentBucket.totalVolume += t.quantity;
                        
                        if (t.buyerId > t.sellerId) { 
                            currentBucket.orderFlowImbalance += t.quantity; 
                        } else if (t.sellerId > t.buyerId) {
                            currentBucket.orderFlowImbalance -= t.quantity; 
                        }

                        if (tradeCount % TRADES_PER_BUCKET == 0) {
                            memoryBuffer.push_back(currentBucket);
                            if (memoryBuffer.size() >= 1000) {
                                file.write(reinterpret_cast<const char*>(memoryBuffer.data()), memoryBuffer.size() * sizeof(StateVector));
                                memoryBuffer.clear();
                            }
                        }
                    } else {
                        if (consumerDone.load(std::memory_order_acquire)) break;
                        __builtin_ia32_pause();
                    }
                }
                
                if (tradeCount % TRADES_PER_BUCKET != 0 && tradeCount > 0) {
                    memoryBuffer.push_back(currentBucket);
                }
                if (!memoryBuffer.empty()) {
                    file.write(reinterpret_cast<const char*>(memoryBuffer.data()), memoryBuffer.size() * sizeof(StateVector));
                }
                
                totalTradesSim.store(bucketCounter, std::memory_order_release);
                totalRawTradesSim.store(tradeCount, std::memory_order_release);
            });

            producer.join(); 
            consumer.join(); 
            logger.join();
            
            totalTradesGlobal += totalTradesSim.load();
            totalRawTradesGlobal += totalRawTradesSim.load();
        }
        file.close();

        std::ofstream meta("output/metadata.txt");
        meta << modelName << "\n" << "MONTE_CARLO_DATA";
        meta.close();

        auto endGlobal = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = endGlobal - startGlobal;

        std::cout << "\n\n--- MONTE CARLO SIMULATION COMPLETE ---" << std::endl;
        std::cout << "Model Simulated:           " << modelName << std::endl;
        std::cout << "Total Realities Simulated: " << NUM_SIMULATIONS << std::endl;
        std::cout << "Total Feature Buckets:     " << totalTradesGlobal.load() << std::endl;
        std::cout << "Elapsed time:              " << elapsed.count() << " seconds" << std::endl;
        std::cout << "Average Throughput:        " << (totalRawTradesGlobal.load() / elapsed.count()) << " trades/sec" << std::endl;
        std::cout << "Metadata saved:            " << modelName << " -> output/metadata.txt" << std::endl;
        std::cout << "---------------------------------------" << std::endl;
    }

    // =========================================================================
    // --- AUTOMATIC VISUALIZATION ---
    // =========================================================================
    std::cout << "Launching Visualizer..." << std::endl;
    int result = std::system("python3 scripts/visualizer.py");
    
    if (result == 0) {
        std::cout << "Visualizer executed successfully." << std::endl;
    } else {
        std::cerr << "Visualizer failed to execute." << std::endl;
    }

    return 0;
}

// =========================================================================
// QUICK TERMINAL COMMANDS (WSL / LINUX)
// =========================================================================
// 1. Build:                            cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
// 2. Run:                              cmake --build build -j $(nproc) && ./build/MotorHFT
// 3. Download:                         python3 scripts/downloader.py
// 4. Download without terminal open:   nohup python3 scripts/downloader.py &
// 5. Tracking nohup:                   tail -f nohup.out
// =========================================================================

// =========================================================================
// HOW TO PRECOMPILE THE FILES (WSL / LINUX)
// =========================================================================
// 1. g++ -O3 src/PreCompiler.cpp -o precompiler
// 2. ./precompiler "NAME.csv" "NAME.bin"
// =========================================================================