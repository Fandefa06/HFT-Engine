// Trade.hpp

#pragma once
#include <chrono>
#include "Types.hpp"

struct Trade {
    OrderId buyerId;                                        
    OrderId sellerId;                                   
    Price price;                                       
    Quantity quantity;                                  
    std::chrono::steady_clock::time_point timestamp;

    Trade(OrderId bId, OrderId sId, Price p, Quantity q)
        : buyerId(bId), sellerId(sId), price(p), quantity(q),
          timestamp(std::chrono::steady_clock::now()) {}
};