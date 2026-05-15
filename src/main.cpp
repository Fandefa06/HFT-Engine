// main.cpp

// ===============================================================================================================
// --- VERY IMPORTANT: FIX LAST IDEA ON CONNECTING THE BOT TO THE ENGINE, IT IS EXPLAINED IN LAST PROMPT IN GEMINI
// ===============================================================================================================

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
#include <iomanip>  // <--- ADDED FOR PRECISION FORMATTING
#include <sstream>  // <--- ADDED FOR STRING STREAMING
#include "OrderBook.hpp"
#include "MarketSimulator.hpp"
#include "MonteCarloSimulator.hpp"
#include "RingBuffer.hpp"
#include "HistoricalParser.hpp"
#include "BinaryParser.hpp"
#include "AssetPolicies.hpp"
#include "BotBase.hpp"

#if __has_include("MyStrategy.hpp")
    #include "MyStrategy.hpp"
    using ActiveBot = MyStrategy;
#else
    #include "TemplateBot.hpp"
    using ActiveBot = TemplateBot;
#endif

// --- FORMATTING UTILITY ---
// Converts raw seconds into a precise Days, Hours, Minutes, and Seconds format
std::string formatElapsedTime(double total_seconds) {
    uint64_t t = static_cast<uint64_t>(total_seconds);
    double fractional = total_seconds - t;

    uint64_t days = t / 86400;
    t %= 86400;
    uint64_t hours = t / 3600;
    t %= 3600;
    uint64_t minutes = t / 60;
    t %= 60; // <--- THE BUG FIX: Extract remaining seconds properly
    double seconds = t + fractional;

    std::ostringstream oss;
    if (days > 0) oss << days << " days, ";
    if (days > 0 || hours > 0) oss << hours << " hours, ";
    if (days > 0 || hours > 0 || minutes > 0) oss << minutes << " minutes, ";
    oss << std::fixed << std::setprecision(4) << seconds << " seconds";
    
    return oss.str();
}
// ------------------------------

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
    bool SAVE_FEATURES = true;
    // =========================================================================


    // =========================================================================
    // --- BINARY FILE TO OBTAIN THE DATA ---
    // =========================================================================
    std::string binFilename = "data/ETHUSDT-trades-2022-01.bin";
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
    const uint32_t NUM_SIMULATIONS = 3;    
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
            
            // <--- NEW: Initialize the trading bot for the Historical reality
            ActiveBot myHistoricalBot; 

            Trade t;
            uint64_t tradeCount = 0;
            uint64_t bucketCounter = 0;
            const uint64_t TRADES_PER_BUCKET = 1000; 
            
            StateVector currentBucket;
            std::vector<StateVector> memoryBuffer;
            memoryBuffer.reserve(10000); 
            

            while (true) {
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

                        // <--- NEW: Feed the freshly closed bucket to our bot
                        myHistoricalBot.evaluateMarket(currentBucket);

                        if (memoryBuffer.size() >= 1000) {
                            if (SAVE_FEATURES) { // <--- CHECK THE SWITCH BEFORE WRITING
                                file.write(reinterpret_cast<const char*>(memoryBuffer.data()), memoryBuffer.size() * sizeof(StateVector));
                            }
                            memoryBuffer.clear(); // Always clear memory to avoid RAM explosion
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
                
                // <--- NEW: Feed the remaining incomplete bucket to our bot just in case
                myHistoricalBot.evaluateMarket(currentBucket);
            }
            if (SAVE_FEATURES && !memoryBuffer.empty()) { // <--- ADD CHECK HERE
                file.write(reinterpret_cast<const char*>(memoryBuffer.data()), memoryBuffer.size() * sizeof(StateVector));
            }
            
            // <--- NEW: Print the results of the bot operating on Real Historical Data
            std::cout << "\n[=== REAL HISTORICAL DATA BOT PERFORMANCE ===]" << std::endl;
            myHistoricalBot.printReport();

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

        targetBuckets = finalBuckets;

        std::cout << "\n--- BINARY REPLAY COMPLETE ---" << std::endl;
        std::cout << "Total Realities:           1 (The Real World)" << std::endl;
        std::cout << "Total Orders Injected:     " << dynamicNumOrders << std::endl; // SHOW THE EXACT COUNT
        std::cout << "Total Feature Buckets:     " << finalBuckets << std::endl;
        
        // --- USING THE NEW PRECISE TIME FORMATTER ---
        std::cout << "Elapsed time:              " << formatElapsedTime(elapsed.count()) << std::endl;

        std::cout << "Average Throughput:        " << (finalRawTrades / elapsed.count()) << " trades/sec" << std::endl;
        std::cout << "Extracted Drift:           " << dna.drift << std::endl;
        std::cout << "Extracted Volatility:      " << dna.volatility << std::endl;
        std::cout << "----------------------------------" << std::endl;

    } 
    
    if (RUN_MONTE_CARLO) {
        // =========================================================================
        // 2. MONTE CARLO MODE (Mathematical Multi-Path Engine)
        // =========================================================================

        std::cout << "=======================================" << std::endl;
        std::cout << "--- STARTING MONTE CARLO SIMULATION ---" << std::endl;
        std::cout << "Model: " << modelName << " | Paths: " << NUM_SIMULATIONS << std::endl;
        std::cout << "Target Length: " << targetBuckets << " Buckets" << std::endl;
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
            std::atomic<bool> isRunningMC{true};

            // Start exactly where the historical data started!
            Price startPrice = globalFirstPrice.load() > 0 ? globalFirstPrice.load() : 
                               ActivePolicy::minPriceTicks + ((ActivePolicy::maxPriceTicks - ActivePolicy::minPriceTicks) / 2);

            std::thread producer([&]() {
                pinThread(0); 
                // Passed the ActivePolicy template here!
                MonteCarloSimulator::generateFlow<ActivePolicy>(eventBuffer, isRunningMC, 30, currentModel, startPrice, dna.drift, dna.volatility);
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
                
                // <--- NEW: Initialize a FRESH bot for this specific parallel universe
                ActiveBot myMonteCarloBot;

                Trade t;
                uint64_t tradeCount = 0;
                uint64_t bucketCounter = 0;
                const uint64_t TRADES_PER_BUCKET = 1000; 
                
                StateVector currentBucket;
                std::vector<StateVector> memoryBuffer;
                memoryBuffer.reserve(10000); 
                

                while (true) {
                    if (tradeBuffer.pop(t)) {
                        tradeCount++;
                        
                        if (tradeCount % TRADES_PER_BUCKET == 1) {
                            currentBucket = StateVector(); 
                            currentBucket.simId = t.simId;
                            currentBucket.bucketId = ++bucketCounter;
                            currentBucket.openPrice = t.price;
                            currentBucket.lowPrice = t.price;

                            if (bucketCounter >= targetBuckets) {
                                isRunningMC.store(false, std::memory_order_relaxed);
                            }
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

                            // <--- NEW: Feed the freshly closed bucket to the Monte Carlo bot
                            myMonteCarloBot.evaluateMarket(currentBucket);

                            if (memoryBuffer.size() >= 1000) {
                                if (SAVE_FEATURES) { // <--- CHECK THE SWITCH BEFORE WRITING
                                    file.write(reinterpret_cast<const char*>(memoryBuffer.data()), memoryBuffer.size() * sizeof(StateVector));
                                }
                                memoryBuffer.clear(); // Always clear memory to avoid RAM explosion
                            }
                        }
                    } else {
                        if (consumerDone.load(std::memory_order_acquire)) break;
                        __builtin_ia32_pause();
                    }
                }
                
                if (tradeCount % TRADES_PER_BUCKET != 0 && tradeCount > 0) {
                    memoryBuffer.push_back(currentBucket);

                    // <--- NEW: Feed remaining bucket
                    myMonteCarloBot.evaluateMarket(currentBucket);
                }
                if (SAVE_FEATURES && !memoryBuffer.empty()) { // <--- ADD CHECK HERE
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
        
        // --- USING THE NEW PRECISE TIME FORMATTER & ADDING AVG TIME ---
        std::cout << "Elapsed time:              " << formatElapsedTime(elapsedMC.count()) << std::endl;
        std::cout << "Avg time per simulation:   " << formatElapsedTime(elapsedMC.count() / NUM_SIMULATIONS) << std::endl;

        std::cout << "Average Throughput:        " << (totalRawTradesGlobal.load() / elapsedMC.count()) << " trades/sec" << std::endl;
        std::cout << "Metadata saved:            " << modelName << " -> output/metadata.txt" << std::endl;
        std::cout << "---------------------------------------" << std::endl;
    }

    std::ofstream meta("output/metadata.txt");
    if (RUN_HISTORICAL && RUN_MONTE_CARLO) meta << modelName << "\n" << binFilename;
    else if (RUN_HISTORICAL) meta << "HISTORICAL\n" << binFilename;
    else meta << modelName << "\n" << "PURE_MATH";
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