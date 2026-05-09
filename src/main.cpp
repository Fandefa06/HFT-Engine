//main.cpp

#include <iostream>
#include <chrono> // Added for high-resolution timing
#include "OrderBook.hpp"
#include "MarketSimulator.hpp"

int main() {
    OrderBook myBook;
    const uint32_t numOrders = 10000000; // Define order count here for cleaner calculations
    const uint32_t cancelPercent = 5;   //Probability to cancel an order

    // ==========================================
    // MODE 1: Manual Debugging (For bug hunting)
    // ==========================================
    /*
    myBook.addOrder(Order(1, 100, 10, OrderSide::Bid));
    myBook.addOrder(Order(2, 105, 5, OrderSide::Bid));
    myBook.addOrder(Order(3, 110, 2, OrderSide::Ask));
    */

    // ==========================================
    // MODE 2: Stress Simulation
    // ==========================================
    std::cout << "--- STARTING MARKET SIMULATION ---\n" << std::endl;
    
    //Start timer
    auto start = std::chrono::high_resolution_clock::now();

    //Execute workload
    MarketSimulator::generateRandomOrders(myBook, numOrders, cancelPercent);
    //MarketSimulator::injectMarketShock(myBook, 500000, OrderSide::Bid, 200); 

    //Stop timer
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    //View results
    //myBook.print();
    //myBook.printTrades();

    // Print performance metrics
    std::cout << "--- PERFORMANCE METRICS ---" << std::endl;
    std::cout << "Total trades executed: " << myBook.getTrades().size() << std::endl;
    std::cout << "Elapsed time: " << elapsed.count() << " seconds" << std::endl;
    std::cout << "Throughput: " << (numOrders / elapsed.count()) << " ops/sec" << std::endl;
    std::cout << "---------------------------" << std::endl;
    std::cout << "Exporting result to CSV: " << std::endl;
    myBook.exportTradesToCSV("market_simulation_results.csv");
    std::cout << "Exporting spread to CSV: " << std::endl;
    myBook.exportSpreadToCSV("market_spread.csv");
    std::cout << "Done. Finishing program" << std::endl;

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