// main.cpp

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
#include "RingBuffer.hpp"

void pinThread(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

int main() {
    OrderBook myBook;
    
    // The Input Pipeline
    RingBuffer<MarketEvent, 4194304> eventBuffer; 
    
    // The Output Pipeline
    RingBuffer<Trade, 4194304> tradeBuffer; 
    myBook.setTradeBuffer(&tradeBuffer); 

    const uint32_t numOrders = 100000000; 
    const uint32_t cancelPercent = 5;   

    std::cout << "--- STARTING 3-THREAD ASYNC HFT PIPELINE ---\n" << std::endl;
    
    std::atomic<bool> producerDone{false};
    std::atomic<bool> consumerDone{false};
    std::atomic<uint64_t> totalTrades{0};

    auto start = std::chrono::high_resolution_clock::now();

    // THREAD 1: PRODUCER
    std::thread producer([&]() {
        pinThread(0); 
        MarketSimulator::generateRandomOrders(eventBuffer, numOrders, cancelPercent);
        producerDone.store(true, std::memory_order_release);
    });

    // THREAD 2: CONSUMER (The Engine)
    std::thread consumer([&]() {
        pinThread(2); 
        MarketEvent ev;
        bool running = true;
        
        while (running) {
            if (eventBuffer.pop(ev)) {
                if (ev.type == EventType::NEW_ORDER) myBook.addOrder(ev.order);
                else if (ev.type == EventType::CANCEL_ORDER) myBook.cancelOrder(ev.order.id);
                else if (ev.type == EventType::TERMINATE) running = false;
            } else {
                __builtin_ia32_pause(); 
            }
        }
        consumerDone.store(true, std::memory_order_release);
    });

    // THREAD 3: TRUE BINARY ASYNC LOGGER
    std::thread logger([&]() {
        pinThread(4); 
        
        // 1. NTFS BYPASS: We are writing to the Linux RAM/Disk (/tmp/)
        
        std::ofstream file("/mnt/c/Proyectos personales/Limit Order Book Personal/output/trades_binary.dat", std::ios::binary);
        
        std::vector<char> ioBuffer(1024 * 1024); 
        file.rdbuf()->pubsetbuf(ioBuffer.data(), ioBuffer.size());

        Trade t;
        uint64_t tradeCount = 0;
        
        while (!consumerDone.load(std::memory_order_acquire) || tradeCount > 0) {
            if (tradeBuffer.pop(t)) {
                // 2. BINARY LOGGER: No strings, no commas. Pure memory copy.
                file.write(reinterpret_cast<const char*>(&t), sizeof(Trade));
                tradeCount++;
            } else {
                if (consumerDone.load(std::memory_order_acquire)) break; 
                __builtin_ia32_pause();
            }
        }
        totalTrades.store(tradeCount, std::memory_order_release);
        file.close();
    });

    producer.join();
    consumer.join();
    logger.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "\n--- PERFORMANCE METRICS ---" << std::endl;
    std::cout << "Total trades logged to BINARY: " << totalTrades.load() << std::endl;
    std::cout << "Elapsed time: " << elapsed.count() << " seconds" << std::endl;
    std::cout << "Throughput: " << (numOrders / elapsed.count()) << " ops/sec" << std::endl;
    std::cout << "File saved at: /tmp/hft_output/trades_binary.dat" << std::endl;
    std::cout << "---------------------------" << std::endl;

    return 0;
}
// =========================================================================
// QUICK TERMINAL COMMANDS (WSL / LINUX)
// =========================================================================

// --- MODE 1: DEBUGGING ---
// 1. Switch to Debug config:  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
// 2. Build and Debug:         cmake --build build -j $(nproc) && gdb ./build/MotorHFT
// (Inside GDB: type 'r' to run, 'bt' if it crashes to see the line, 'q' to exit)

// --- MODE 2: ПОТУЖНО ---
// 1. Switch to Release config: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
// 2. Build and Run:            cmake --build build -j $(nproc) && ./build/MotorHFT

// --- PRO TIP: THE "ALL-IN-ONE" SWITCH (Copy-paste this to change & run) ---
// Switch to Release and Run:
// cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j $(nproc) && ./build/MotorHFT

// Switch to Debug and Run in GDB:
// cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j $(nproc) && gdb ./build/MotorHFT

// Note: If you get "Permission denied" or CMake errors, run 'rm -rf build' 
// while VSCode's debugger is NOT running.
// =========================================================================