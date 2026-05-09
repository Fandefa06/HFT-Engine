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
    uint64_t timestamp;
    Price bid;
    Price ask;

    SpreadSnap(uint64_t ts, Price b, Price a) 
        : timestamp(ts), bid(b), ask(a) {}
};

class OrderBook {
private:
    std::vector<std::deque<Order>> bids; 
    std::vector<std::deque<Order>> asks;
    std::vector<Trade> trades;
    
    std::vector<SpreadSnap> spreadHistory;

    Price bestBid;
    Price bestAsk;

    // EL REGRESO DEL REY: Flat Array Ledger. 
    // Quemamos RAM para obtener acceso directo sin coste de CPU.
    std::vector<bool> cancelledOrders;
    
    std::chrono::steady_clock::time_point startTime;

    void recordSpread(); 

public:
    OrderBook();
    
    void addOrder(Order order);
    void cancelOrder(OrderId orderId);
    
    void printTrades() const;
    const std::vector<Trade>& getTrades() const { return trades; }
    
    void exportTradesToCSV(const std::string& filename) const; 
    void exportSpreadToCSV(const std::string& filename) const;
};