// OrderBook.cpp

#include "OrderBook.hpp"
#include "RingBuffer.hpp" // Required for the pipeline
#include <iostream>
#include <fstream>      
#include <filesystem>   

namespace fs = std::filesystem;

OrderBook::OrderBook() : bestBid(0), bestAsk(MAX_PRICE) {
    startTime = std::chrono::steady_clock::now(); 

    bids.resize(MAX_PRICE);
    asks.resize(MAX_PRICE);
    
    // RAM FIX: We no longer reserve 10M spots in memory. 
    // Trades will be immediately sent to the Async Logger thread.
    
    cancelledOrders.resize(100000500, false); 
}

void OrderBook::addOrder(Order order) {
    
    if (cancelledOrders[order.id]) return;

    if (order.side == OrderSide::Bid) {
        
        while (order.quantity > 0 && bestAsk <= order.price) {
            std::deque<Order>& askQueue = asks[bestAsk];

            while (!askQueue.empty() && cancelledOrders[askQueue.front().id]) {
                askQueue.pop_front();
            }

            while (!askQueue.empty() && order.quantity > 0) {
                Order& bestAskOrder = askQueue.front();
                
                uint32_t matchQty = std::min(order.quantity, bestAskOrder.quantity);
                
                // STREAMING PIPELINE: Send trade to Core 4 and forget it
                if (externalTradeBuffer) {
                    auto* tb = static_cast<RingBuffer<Trade, 4194304>*>(externalTradeBuffer);
                    while(!tb->push(Trade(order.id, bestAskOrder.id, bestAsk, matchQty))) {
                        __builtin_ia32_pause(); 
                    }
                }

                if (order.quantity >= bestAskOrder.quantity) {
                    order.quantity -= bestAskOrder.quantity;
                    askQueue.pop_front(); 
                } else {
                    bestAskOrder.quantity -= order.quantity;
                    order.quantity = 0;
                }
                
                while (!askQueue.empty() && cancelledOrders[askQueue.front().id]) {
                    askQueue.pop_front();
                }
            }

            if (askQueue.empty()) {
                while (bestAsk < MAX_PRICE && asks[bestAsk].empty()) {
                    bestAsk++;
                }
                if (bestAsk == MAX_PRICE) break; 
            }
        }
        
        if (order.quantity > 0) {
            bids[order.price].push_back(order);
            if (order.price > bestBid) bestBid = order.price;
        }
    } 
    else {
        while (order.quantity > 0 && bestBid >= order.price && bestBid > 0) {
            std::deque<Order>& bidQueue = bids[bestBid];

            while (!bidQueue.empty() && cancelledOrders[bidQueue.front().id]) {
                bidQueue.pop_front();
            }

            while (!bidQueue.empty() && order.quantity > 0) {
                Order& bestBidOrder = bidQueue.front();
                
                uint32_t matchQty = std::min(order.quantity, bestBidOrder.quantity);
                
                // STREAMING PIPELINE: Send trade to Core 4 and forget it
                if (externalTradeBuffer) {
                    auto* tb = static_cast<RingBuffer<Trade, 4194304>*>(externalTradeBuffer);
                    while(!tb->push(Trade(bestBidOrder.id, order.id, bestBid, matchQty))) {
                        __builtin_ia32_pause(); 
                    }
                }

                if (order.quantity >= bestBidOrder.quantity) {
                    order.quantity -= bestBidOrder.quantity;
                    bidQueue.pop_front();
                } else {
                    bestBidOrder.quantity -= order.quantity;
                    order.quantity = 0;
                }
                
                while (!bidQueue.empty() && cancelledOrders[bidQueue.front().id]) {
                    bidQueue.pop_front();
                }
            }

            if (bidQueue.empty()) {
                while (bestBid > 0 && bids[bestBid].empty()) {
                    bestBid--;
                }
                if (bestBid == 0) break; 
            }
        }

        if (order.quantity > 0) {
            asks[order.price].push_back(order);
            if (order.price < bestAsk) bestAsk = order.price;
        }
    }
}

void OrderBook::cancelOrder(OrderId orderId) {
    cancelledOrders[orderId] = true;
}

void OrderBook::recordSpread() {
    // Disabled for maximum HFT throughput stress testing.
}

void OrderBook::exportTradesToCSV(const std::string& filename) const {
    // Disabled. Handled asynchronously by the 3rd Thread in main.cpp
}

void OrderBook::exportSpreadToCSV(const std::string& filename) const {
    // Disabled. Handled asynchronously by the 3rd Thread in main.cpp
}