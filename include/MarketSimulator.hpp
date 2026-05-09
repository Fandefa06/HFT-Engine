// MarketSimulator.hpp
// MarketSimulator.hpp
#pragma once
#include "RingBuffer.hpp"
#include "Types.hpp"
#include <random>
#include <iostream>

class MarketSimulator {
public:
    template<size_t S>
    static void generateRandomOrders(RingBuffer<MarketEvent, S>& buffer, 
                                     uint32_t numOrders,
                                     uint32_t cancelPercent = 10,
                                     uint32_t minPrice = 90,
                                     uint32_t maxPrice = 115,
                                     uint32_t minQty = 1,
                                     uint32_t maxQty = 15,
                                     bool useFixedSeed = true) { 
        
        std::random_device rd; 
        std::mt19937 gen(useFixedSeed ? 12345 : rd()); 
        
        std::uniform_int_distribution<uint32_t> priceDist(minPrice, maxPrice);
        std::uniform_int_distribution<uint32_t> qtyDist(minQty, maxQty);
        std::uniform_int_distribution<int> sideDist(0, 1);
        std::uniform_int_distribution<uint32_t> eventDist(1, 100);

        uint32_t orderCounter = 0;
        uint32_t cancelCounter = 0;

        for (uint32_t i = 1; i <= numOrders; ++i) {
            if (orderCounter > 0 && eventDist(gen) <= cancelPercent) {
                std::uniform_int_distribution<uint32_t> idDist(1, orderCounter);
                while(!buffer.push(MarketEvent::cancel(idDist(gen)))) {
                    __builtin_ia32_pause();
                }
                cancelCounter++;
            } else {
                orderCounter++;
                OrderSide side = (sideDist(gen) == 0) ? OrderSide::Bid : OrderSide::Ask;
                Order newOrd(orderCounter, priceDist(gen), qtyDist(gen), side);
                while(!buffer.push(MarketEvent::newOrder(newOrd))) {
                    __builtin_ia32_pause();
                }
            }
        }
        
        // Signal termination
        MarketEvent term;
        term.type = EventType::TERMINATE;
        while(!buffer.push(term)) { __builtin_ia32_pause(); }
    }
};