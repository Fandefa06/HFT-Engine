//Trade.hpp

#pragma once
#include <chrono>
#include "Types.hpp"

struct Trade {
    uint32_t simId;  // <--- NEW: Which timeline did this happen in?
    OrderId buyerId;                                        
    OrderId sellerId;                                   
    Price price;                                       
    Quantity quantity;                                  
    std::chrono::steady_clock::time_point timestamp;

    Trade() : simId(0), buyerId(0), sellerId(0), price(0), quantity(0) {}

    // Updated constructor
    Trade(uint32_t sId, OrderId bId, OrderId sId2, Price p, Quantity q)
        : simId(sId), buyerId(bId), sellerId(sId2), price(p), quantity(q),
          timestamp(std::chrono::steady_clock::now()) {}
};