//OrderBook.hpp

#pragma once
#include <vector>
#include <deque>
#include <algorithm>
#include <string>
#include <chrono>
#include "Types.hpp"
#include "Order.hpp"
#include "Trade.hpp"

constexpr Price MAX_PRICE = 2000000; 

struct SpreadSnap {
    Price bid;
    Price ask;
    std::chrono::steady_clock::time_point timestamp;

    SpreadSnap(Price b, Price a) 
        : bid(b), ask(a), timestamp(std::chrono::steady_clock::now()) {}
};

class OrderBook {
private:
    void* externalTradeBuffer = nullptr; // NEW: Pointer to our conveyor belt

    uint32_t currentSimId = 0; // Tracks the current timeline
    std::vector<std::deque<Order>> bids; 
    std::vector<std::deque<Order>> asks;
    std::vector<Trade> trades;
    std::vector<SpreadSnap> spreadHistory;
    Price bestBid;
    Price bestAsk;
    std::vector<bool> cancelledOrders;
    std::chrono::steady_clock::time_point startTime;

    void recordSpread(); 

public:
    OrderBook();
    
    void setTradeBuffer(void* bufferPtr) { externalTradeBuffer = bufferPtr; } // NEW: Connect the pipe

    void addOrder(Order order);
    void cancelOrder(OrderId orderId);
    
    void printTrades() const;
    const std::vector<Trade>& getTrades() const { return trades; }

    void setSimId(uint32_t id) { currentSimId = id; }
    void reset(); // The memory wipe function
    
    void exportTradesToCSV(const std::string& filename) const; 
    void exportSpreadToCSV(const std::string& filename) const;
};