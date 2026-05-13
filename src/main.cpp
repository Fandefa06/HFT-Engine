// main.cpp

// ====================================================================
// --- VERY IMPORTANT, UPDATE README ---
// ====================================================================

#include <iostream>
#include <chrono>
#include <thread>
#include <pthread.h>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include "OrderBook.hpp"
#include "MarketSimulator.hpp"
#include "MonteCarloSimulator.hpp"
#include "RingBuffer.hpp"
#include "HistoricalParser.hpp"
#include "BinaryParser.hpp"
#include "AssetPolicies.hpp"

// Structure to hold the statistical "DNA" of a historical period
struct MarketDNA {
    double drift = 0.0;
    double volatility = 2.0;
};

std::atomic<Price> globalLastPrice{0};
std::atomic<Price> globalFirstPrice{0}; 

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
    bool RUN_HISTORICAL = true;
    bool RUN_MONTE_CARLO = true; 
    // =========================================================================


    // =========================================================================
    // --- BINARY FILE TO OBTAIN THE DATA ---
    // =========================================================================
    std::string binFilename = "data/ETH_FULL_2024.bin";
    // =========================================================================


    // ========================================================================
    // --- CHANGE THE POLICY OF THE DATA TO ANALYZE HERE ---
    OrderBook<ETH_Policy> myBook;
    // ========================================================================



    // =========================================================================
    // --- MONTE CARLO PARAMETERS ---
    // =========================================================================
    // DYNAMIC MATCHING: We start at 0 and auto-detect the size of the history!
    uint64_t dynamicNumOrders = 0; 
    const uint32_t NUM_SIMULATIONS = 30;    
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
    std::ofstream initFile("/dev/shm/features_binary.dat", std::ios::binary | std::ios::trunc);
    initFile.close();

    auto startGlobal = std::chrono::high_resolution_clock::now();
    
    MarketDNA dna;
    std::vector<double> realPricesForDNA;

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
        std::atomic<uint64_t> totalRawTrades{0}; 

        auto startHist = std::chrono::high_resolution_clock::now();

        // THREAD 1: PRODUCER
        std::thread producer([&]() {
            pinThread(0); 
            BinaryParser::feedFromBinary(eventBuffer, binFilename);
            producerDone.store(true, std::memory_order_release);
        });

        // THREAD 2: CONSUMER (AUTO-COUNTS ORDERS FOR DYNAMIC MATCHING)
        std::thread consumer([&]() {
            pinThread(2); 
            MarketEvent ev;
            bool running = true;
            uint64_t orderCounter = 0; // Tracks every order injected
            
            while (running) {
                if (eventBuffer.pop(ev)) {
                    if (ev.type == EventType::NEW_ORDER) {
                        myBook.addOrder(ev.order);
                        orderCounter++;
                    }
                    else if (ev.type == EventType::CANCEL_ORDER) myBook.cancelOrder(ev.order.id);
                    else if (ev.type == EventType::TERMINATE) running = false;
                } else { __builtin_ia32_pause(); }
            }
            
            dynamicNumOrders = orderCounter; // SAVE THE EXACT SIZE OF HISTORY
            consumerDone.store(true, std::memory_order_release);
        });

        // THREAD 3: THE FEATURE ENGINE
        std::thread logger([&]() {
            pinThread(4); 
            std::ofstream file("/dev/shm/features_binary.dat", std::ios::binary | std::ios::app);
            
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
                    
                    if (tradeCount == 1) {
                        globalFirstPrice.store(t.price, std::memory_order_relaxed);
                    }
                    globalLastPrice.store(t.price, std::memory_order_relaxed);
                    
                    if (tradeCount % TRADES_PER_BUCKET == 1) {
                        currentBucket = StateVector(); 
                        currentBucket.simId = t.simId;
                        currentBucket.bucketId = ++bucketCounter;
                        currentBucket.openPrice = t.price;
                        currentBucket.lowPrice = t.price;
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
                        realPricesForDNA.push_back(static_cast<double>(currentBucket.closePrice));

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
                realPricesForDNA.push_back(static_cast<double>(currentBucket.closePrice));
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

        // --- SAFE DNA EXTRACTION WITH AMPLIFIER ---
        if (realPricesForDNA.size() >= 2) {
            std::vector<double> returns;
            for (size_t i = 1; i < realPricesForDNA.size(); ++i) {
                if (realPricesForDNA[i-1] > 0 && realPricesForDNA[i] > 0) {
                    returns.push_back(std::log(realPricesForDNA[i] / realPricesForDNA[i-1]));
                }
            }
            if (!returns.empty()) {
                double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
                double mean = sum / returns.size();
                double sq_sum = std::inner_product(returns.begin(), returns.end(), returns.begin(), 0.0);
                double variance = std::max(0.0, sq_sum / returns.size() - mean * mean);
                
                dna.drift = mean * 100.0;
                dna.volatility = std::sqrt(variance) * 100.0 * 50.0; 
            }
        }
        if (std::isnan(dna.drift) || std::isnan(dna.volatility)) {
            dna.drift = 0.0;
            dna.volatility = 2.0;
        }

        auto endHist = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = endHist - startHist;

        uint64_t finalBuckets = totalTradesLogged.load();
        uint64_t finalRawTrades = totalRawTrades.load();

        std::cout << "\n--- BINARY REPLAY COMPLETE ---" << std::endl;
        std::cout << "Total Realities:           1 (The Real World)" << std::endl;
        std::cout << "Total Orders Injected:     " << dynamicNumOrders << std::endl; // SHOW THE EXACT COUNT
        std::cout << "Total Feature Buckets:     " << finalBuckets << std::endl;
        std::cout << "Elapsed time:              " << elapsed.count() << " seconds" << std::endl;
        std::cout << "Average Throughput:        " << (finalRawTrades / elapsed.count()) << " trades/sec" << std::endl;
        std::cout << "Extracted Drift:           " << dna.drift << std::endl;
        std::cout << "Extracted Volatility:      " << dna.volatility << std::endl;
        std::cout << "----------------------------------" << std::endl;

    } 
    
    if (RUN_MONTE_CARLO) {
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
        
        // If we didn't run historical, set a default fallback length
        uint64_t ordersToGenerate = (dynamicNumOrders > 0) ? static_cast<uint64_t>(dynamicNumOrders * 2.0) : 10000000;

        std::cout << "=======================================" << std::endl;
        std::cout << "--- STARTING MONTE CARLO SIMULATION ---" << std::endl;
        std::cout << "Model: " << modelName << " | Paths: " << NUM_SIMULATIONS << std::endl;
        std::cout << "Orders per Path: " << ordersToGenerate << " (Automatically Matched)" << std::endl;
        std::cout << "=======================================\n" << std::endl;

        std::atomic<uint64_t> totalTradesGlobal{0};
        std::atomic<uint64_t> totalRawTradesGlobal{0};

        auto startMC = std::chrono::high_resolution_clock::now();

        std::ofstream file("/dev/shm/features_binary.dat", std::ios::binary | std::ios::app);
        
        for (uint32_t sim = 1; sim <= NUM_SIMULATIONS; ++sim) {
            std::cout << "Running Simulation " << sim << " / " << NUM_SIMULATIONS << "...\r" << std::flush;
            
            myBook.setSimId(sim);
            myBook.reset();

            std::atomic<bool> producerDone{false};
            std::atomic<bool> consumerDone{false};
            std::atomic<uint64_t> totalTradesSim{0};
            std::atomic<uint64_t> totalRawTradesSim{0};

            // Start exactly where the historical data started!
            Price startPrice = globalFirstPrice.load() > 0 ? globalFirstPrice.load() : 
                               ETH_Policy::minPriceTicks + ((ETH_Policy::maxPriceTicks - ETH_Policy::minPriceTicks) / 2);

            std::thread producer([&]() {
                pinThread(0); 
                // Uses the automatically matched order count!
                MonteCarloSimulator::generateFlow(eventBuffer, ordersToGenerate, 30, currentModel, startPrice, dna.drift, dna.volatility);
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
                            currentBucket.lowPrice = t.price;
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

        auto endMC = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsedMC = endMC - startMC;

        std::cout << "\n\n--- MONTE CARLO SIMULATION COMPLETE ---" << std::endl;
        std::cout << "Model Simulated:           " << modelName << std::endl;
        std::cout << "Total Realities Simulated: " << NUM_SIMULATIONS << std::endl;
        std::cout << "Total Feature Buckets:     " << totalTradesGlobal.load() << std::endl;
        std::cout << "Elapsed time:              " << elapsedMC.count() << " seconds" << std::endl;
        std::cout << "Average Throughput:        " << (totalRawTradesGlobal.load() / elapsedMC.count()) << " trades/sec" << std::endl;
        std::cout << "Metadata saved:            " << modelName << " -> output/metadata.txt" << std::endl;
        std::cout << "---------------------------------------" << std::endl;
    }

    std::ofstream meta("output/metadata.txt");
    if (RUN_HISTORICAL && RUN_MONTE_CARLO) meta << "HYBRID_INFLUENCED\n" << binFilename;
    else if (RUN_HISTORICAL) meta << "HISTORICAL\n" << binFilename;
    else meta << "MONTE_CARLO\n" << "PURE_MATH";
    meta.close();

    // =========================================================================
    // --- AUTOMATIC VISUALIZATION ---
    // =========================================================================
    std::cout << "Launching Visualizer..." << std::endl;
    int result = std::system("python3 scripts/visualizer.py");
    (void)result; 
    
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