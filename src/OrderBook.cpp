#include "OrderBook.hpp"
#include <iostream>
#include <fstream>      
#include <filesystem>   

namespace fs = std::filesystem;

OrderBook::OrderBook() : bestBid(0), bestAsk(MAX_PRICE) {
    startTime = std::chrono::steady_clock::now(); 

    bids.resize(MAX_PRICE);
    asks.resize(MAX_PRICE);
    trades.reserve(1000000);
    
    // Asumimos el coste de RAM para asegurar latencia cero
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
                trades.emplace_back(order.id, bestAskOrder.id, bestAsk, matchQty);

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
                trades.emplace_back(bestBidOrder.id, order.id, bestBid, matchQty);

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
    recordSpread();
}

void OrderBook::cancelOrder(OrderId orderId) {
    cancelledOrders[orderId] = true;
    recordSpread();
}

void OrderBook::recordSpread() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - startTime);
    
    if (bestBid > 0 && bestAsk < MAX_PRICE) {
        spreadHistory.emplace_back(duration.count(), bestBid, bestAsk);
    }
}

void OrderBook::exportTradesToCSV(const std::string& filename) const {
    fs::path current = fs::current_path();
    if (current.filename() == "build") {
        current = current.parent_path();
    }

    fs::path outputDir = current / "output";
    if (!fs::exists(outputDir)) {
        fs::create_directories(outputDir);
    }

    fs::path fullPath = outputDir / filename;
    std::ofstream file(fullPath);
    
    if (!file.is_open()) {
        std::cerr << "Error: Could not create file at: " << fullPath << std::endl;
        return;
    }

    file << "Microseconds,BuyerID,SellerID,Price,Quantity\n";
    
    for (const auto& t : trades) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(t.timestamp - startTime);
        
        file << duration.count() << "," 
             << t.buyerId << "," 
             << t.sellerId << "," 
             << t.price << "," 
             << t.quantity << "\n";
    }

    file.close();
    std::cout << "File successfully saved to: " << fullPath << std::endl;
}

void OrderBook::exportSpreadToCSV(const std::string& filename) const {
    fs::path current = fs::current_path();
    if (current.filename() == "build") current = current.parent_path();
    fs::path outputDir = current / "output";
    if (!fs::exists(outputDir)) fs::create_directories(outputDir);

    fs::path fullPath = outputDir / filename;
    std::ofstream file(fullPath);

    if (!file.is_open()) return;

    file << "Microseconds,BestBid,BestAsk\n";
    for (const auto& s : spreadHistory) {
        file << s.timestamp << "," << s.bid << "," << s.ask << "\n";
    }
    file.close();
    std::cout << "Spread data saved to: " << fullPath << std::endl;
}