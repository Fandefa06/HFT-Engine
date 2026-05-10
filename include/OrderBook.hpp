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

constexpr Price MAX_PRICE = 200000; 

struct SpreadSnap {
    // ... (Keep this as it is) ...
};

class OrderBook {
private:
    void* externalTradeBuffer = nullptr; // NEW: Pointer to our conveyor belt

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
    
    void exportTradesToCSV(const std::string& filename) const; 
    void exportSpreadToCSV(const std::string& filename) const;
};