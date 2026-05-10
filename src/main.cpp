// main.cpp
#include <iostream>
#include <chrono>
#include <thread>
#include <pthread.h>
#include <atomic>
#include <fstream>
#include <filesystem>
#include "OrderBook.hpp"
#include "MarketSimulator.hpp"
#include "MonteCarloSimulator.hpp"
#include "RingBuffer.hpp"

void pinThread(int core_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

int main() {
    OrderBook myBook;
    RingBuffer<MarketEvent, 4194304> eventBuffer; 
    RingBuffer<Trade, 4194304> tradeBuffer; 
    myBook.setTradeBuffer(&tradeBuffer); 

    // --- QUANT COMMAND CENTER ---
    const uint32_t numOrdersPerSim = 100000; // Total orders per simulation path
    const uint32_t NUM_SIMULATIONS = 100;     // Number of parallel realities
    
    // Choose your Reality (Uncomment ONLY ONE):
    // MarketModel currentModel = MarketModel::GBM;
    // MarketModel currentModel = MarketModel::MEAN_REVERSION;
    // MarketModel currentModel = MarketModel::JUMP_DIFFUSION;
    // MarketModel currentModel = MarketModel::CAUCHY;
    MarketModel currentModel = MarketModel::TRENDING;

    std::cout << "--- STARTING MULTI-PATH HFT MONTE CARLO ---\n" << std::endl;
    std::atomic<uint64_t> totalTradesGlobal{0};

    // Ensure output directory exists and initialize data file
    std::filesystem::create_directories("output");
    std::ofstream initFile("output/trades_binary.dat", std::ios::binary | std::ios::trunc);
    initFile.close();

    auto startGlobal = std::chrono::high_resolution_clock::now();

    for (uint32_t sim = 0; sim < NUM_SIMULATIONS; ++sim) {
        std::cout << "Running Simulation " << sim + 1 << " / " << NUM_SIMULATIONS << "...\r" << std::flush;
        
        myBook.setSimId(sim);
        myBook.reset();

        std::atomic<bool> producerDone{false};
        std::atomic<bool> consumerDone{false};
        std::atomic<uint64_t> totalTradesSim{0};

        // THREAD 1: PRODUCER (Stochastic Engine)
        std::thread producer([&]() {
            pinThread(0); 
            MonteCarloSimulator::generateFlow(eventBuffer, numOrdersPerSim, 90, currentModel);
            producerDone.store(true, std::memory_order_release);
        });

        // THREAD 2: CONSUMER (Matching Engine)
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

        // THREAD 3: ASYNC LOGGER (Binary Batch Writer)
        std::thread logger([&]() {
            pinThread(4); 
            std::ofstream file("output/trades_binary.dat", std::ios::binary | std::ios::app);
            Trade t;
            uint64_t tradeCount = 0;
            std::vector<Trade> batchBuffer;
            batchBuffer.reserve(10000);
            
            while (!consumerDone.load(std::memory_order_acquire) || tradeCount > 0) {
                if (tradeBuffer.pop(t)) {
                    batchBuffer.push_back(t);
                    tradeCount++;
                    if (batchBuffer.size() >= 10000) {
                        file.write(reinterpret_cast<const char*>(batchBuffer.data()), batchBuffer.size() * sizeof(Trade));
                        batchBuffer.clear();
                    }
                } else {
                    if (consumerDone.load(std::memory_order_acquire)) break;
                    __builtin_ia32_pause();
                }
            }
            if (!batchBuffer.empty()) {
                file.write(reinterpret_cast<const char*>(batchBuffer.data()), batchBuffer.size() * sizeof(Trade));
            }
            totalTradesSim.store(tradeCount, std::memory_order_release);
            file.close();
        });

        producer.join(); 
        consumer.join(); 
        logger.join();
        
        totalTradesGlobal += totalTradesSim.load();
    }

    // --- PRO FIX: SAVE METADATA FOR PYTHON ---
    std::string modelName;
    switch(currentModel) {
        case MarketModel::GBM:            modelName = "GBM"; break;
        case MarketModel::MEAN_REVERSION: modelName = "MEAN_REVERSION"; break;
        case MarketModel::JUMP_DIFFUSION: modelName = "JUMP_DIFFUSION"; break;
        case MarketModel::CAUCHY:         modelName = "CAUCHY"; break;
        case MarketModel::TRENDING:       modelName = "TRENDING"; break;
    }
    
    std::ofstream meta("output/metadata.txt");
    meta << modelName;
    meta.close();

    auto endGlobal = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endGlobal - startGlobal;

    // --- RESTORED SPEEDOMETER OUTPUT ---
    uint64_t totalOrders = static_cast<uint64_t>(numOrdersPerSim) * NUM_SIMULATIONS;

    std::cout << "\n\n--- MULTI-PATH COMPLETE | Model: " << modelName << " ---" << std::endl;
    std::cout << "Total Realities Simulated: " << NUM_SIMULATIONS << std::endl;
    std::cout << "Total Orders Processed:    " << totalOrders << std::endl;
    std::cout << "Total Trades Logged:       " << totalTradesGlobal.load() << std::endl;
    std::cout << "Elapsed time:              " << elapsed.count() << " seconds" << std::endl;
    std::cout << "Average Throughput:        " << (totalOrders / elapsed.count()) << " ops/sec" << std::endl;
    std::cout << "Metadata saved:            " << modelName << " -> output/metadata.txt" << std::endl;
    std::cout << "---------------------------------------" << std::endl;


    // --- AUTOMATIC VISUALIZATION ---
    std::cout << "Launching Visualizer..." << std::endl;
    
    // El comando depende de si estás en Windows o Linux/WSL. 
    // Para WSL/Linux usamos python3:
    int result = std::system("python3 scripts/visualizer.py");
    
    if (result == 0) {
        std::cout << "Visualizer executed successfully." << std::endl;
    } else {
        std::cerr << "Visualizer failed to execute. Check if python3 and dependencies are installed." << std::endl;
    }



    return 0;
}

// =========================================================================
// QUICK TERMINAL COMMANDS (WSL / LINUX)
// =========================================================================
// 1. Build: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
// 2. Run:   cmake --build build -j $(nproc) && ./build/MotorHFT
// =========================================================================