//MarketSimulator.hpp

#pragma once
#include "OrderBook.hpp"
#include <random>
#include <iostream>

class MarketSimulator {
public:
    // We added default parameters so you can customize the market behavior from main.cpp
    static void generateRandomOrders(OrderBook& book, 
                                     uint32_t numOrders,
                                     uint32_t cancelPercent = 10, // Added cancellation probability
                                     uint32_t minPrice = 90,
                                     uint32_t maxPrice = 115,
                                     uint32_t minQty = 1,
                                     uint32_t maxQty = 15,
                                     bool useFixedSeed = true) { // Set to true for debugging. Set to false to test
        
        // 1. The Seed Engine
        std::random_device rd; // True hardware-based random number (Entropy)
        
        // If useFixedSeed is true, we use 12345. Otherwise, we ask the hardware for a true random seed.
        std::mt19937 gen(useFixedSeed ? 12345 : rd()); 
        
        // 2. The Market Parameters
        std::uniform_int_distribution<uint32_t> priceDist(minPrice, maxPrice);
        std::uniform_int_distribution<uint32_t> qtyDist(minQty, maxQty);
        std::uniform_int_distribution<int> sideDist(0, 1);
        std::uniform_int_distribution<uint32_t> eventDist(1, 100); // 1-100 for integer probability

        // Progress bar parameters
        uint32_t updateInterval = numOrders / 20; 
        if (updateInterval == 0) updateInterval = 1; 
        const int barWidth = 50; 

        // Track active IDs to cancel and stats
        uint32_t orderCounter = 0;
        uint32_t cancelCounter = 0;

        // 3. Generate the flow
        for (uint32_t i = 1; i <= numOrders; ++i) {
            
            // Decide between Order or Cancel based on eventDist
            if (orderCounter > 0 && eventDist(gen) <= cancelPercent) {
                std::uniform_int_distribution<uint32_t> idDist(1, orderCounter);
                book.cancelOrder(idDist(gen));
                cancelCounter++;
            } else {
                orderCounter++;
                OrderSide side = (sideDist(gen) == 0) ? OrderSide::Bid : OrderSide::Ask;
                book.addOrder(Order(orderCounter, priceDist(gen), qtyDist(gen), side));
            }

            // Efficient terminal update (every 5%)
            if (i % updateInterval == 0 || i == numOrders) {
                // Integer math optimization to avoid floats
                uint32_t progress = (static_cast<uint64_t>(i) * 100) / numOrders;
                int pos = (barWidth * progress) / 100;

                std::cout << "\r[";
                for (int p = 0; p < barWidth; ++p) {
                    if (p < pos) std::cout << "=";
                    else if (p == pos) std::cout << ">";
                    else std::cout << " ";
                }
                std::cout << "] " << progress << "% [Orders: " << orderCounter << " | Cancels: " << cancelCounter << "]" << std::flush;
            }
        }
        std::cout << std::endl;
    }

    
    static void injectMarketShock(OrderBook& book, uint32_t quantity, OrderSide side, uint32_t aggressivePrice) {
        std::cout << "\n!!! INJECTING MARKET SHOCK: " << quantity << " units !!!" << std::endl;
        
        //Let's crash the market
        book.addOrder(Order(99999999, aggressivePrice, quantity, side));
}
};