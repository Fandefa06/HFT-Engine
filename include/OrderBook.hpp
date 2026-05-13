// OrderBook.hpp
#pragma once
#include <vector>
#include <deque>
#include <string>
#include <chrono>
#include "Types.hpp"
#include "Order.hpp"
#include "Trade.hpp"
#include "RingBuffer.hpp"
#include "AssetPolicies.hpp" // Inject our policies

// The Template Definition - 'Asset' represents our Policy (ETH, BTC, etc.)
template <typename Asset>
class OrderBook {
private:
    void* externalTradeBuffer = nullptr; 
    uint32_t currentSimId = 0; 
    
    // Dynamically sized based on the specific Asset Policy!
    std::vector<std::deque<Order>> bids; 
    std::vector<std::deque<Order>> asks;
    std::vector<bool> cancelledOrders;
    
    Price bestBid;
    Price bestAsk;

public:
    OrderBook() : bestBid(0), bestAsk(Asset::maxPriceTicks) {
        // We allocate EXACTLY what the asset needs, not a flat 2 Million
        bids.resize(Asset::maxPriceTicks);
        asks.resize(Asset::maxPriceTicks);
        
        // RAM FIX: Cap the order array to prevent 1TB allocation in Monte Carlo
        cancelledOrders.resize(Asset::maxOrdersInRAM, false); 
    }

    void setTradeBuffer(void* bufferPtr) { externalTradeBuffer = bufferPtr; }
    void setSimId(uint32_t id) { currentSimId = id; }

    void reset() {
        for (auto& q : bids) q.clear();
        for (auto& q : asks) q.clear();
        std::fill(cancelledOrders.begin(), cancelledOrders.end(), false);
        bestBid = 0;
        bestAsk = Asset::maxPriceTicks;
    }

    void cancelOrder(OrderId orderId) {
        // Safety bounds check to prevent segfaults with weird data
        if (orderId < Asset::maxOrdersInRAM) {
            cancelledOrders[orderId] = true;
        }
    }

    void addOrder(Order order) {
        // Ignore corrupted out-of-bounds orders safely
        if (order.id >= Asset::maxOrdersInRAM || order.price >= Asset::maxPriceTicks) return;
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
                    
                    if (externalTradeBuffer) {
                        auto* tb = static_cast<RingBuffer<Trade, 4194304>*>(externalTradeBuffer);
                        while(!tb->push(Trade(currentSimId, order.id, bestAskOrder.id, bestAsk, matchQty))) {
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
                }

                if (askQueue.empty()) {
                    while (bestAsk < Asset::maxPriceTicks && asks[bestAsk].empty()) {
                        bestAsk++;
                    }
                    if (bestAsk == Asset::maxPriceTicks) break; 
                }
            }
            
            if (order.quantity > 0) {
                bids[order.price].push_back(order);
                if (order.price > bestBid) bestBid = order.price;
            }
        } 
        else { // Logic for ASK order (Seller)
            while (order.quantity > 0 && bestBid >= order.price && bestBid > 0) {
                std::deque<Order>& bidQueue = bids[bestBid];

                while (!bidQueue.empty() && cancelledOrders[bidQueue.front().id]) {
                    bidQueue.pop_front();
                }

                while (!bidQueue.empty() && order.quantity > 0) {
                    Order& bestBidOrder = bidQueue.front();
                    uint32_t matchQty = std::min(order.quantity, bestBidOrder.quantity);
                    
                    if (externalTradeBuffer) {
                        auto* tb = static_cast<RingBuffer<Trade, 4194304>*>(externalTradeBuffer);
                        while(!tb->push(Trade(currentSimId, bestBidOrder.id, order.id, bestBid, matchQty))) {
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
};