// OrderBook.hpp
#pragma once
#include <vector>
#include <deque>
#include <string>
#include <chrono>
#include <algorithm> 
#include "Types.hpp"
#include "Trade.hpp"
#include "RingBuffer.hpp"
#include "AssetPolicies.hpp" 

template <typename Asset>
class OrderBook {
private:
    void* externalTradeBuffer = nullptr; 
    uint32_t currentSimId = 0; 
    
    std::vector<std::deque<Order>> bids; 
    std::vector<std::deque<Order>> asks;
    
    // TAGGING ARRAY: Stores OrderId instead of bool to prevent collisions
    std::vector<OrderId> cancelledOrders;
    
    Price bestBidIdx;
    Price bestAskIdx;
    Price vectorSize;

    Price minPriceIdxSeen;
    Price maxPriceIdxSeen;
    OrderId maxOrderIdSeen;

public:
    OrderBook() : maxOrderIdSeen(0) {
        vectorSize = Asset::maxPriceTicks - Asset::minPriceTicks;
        bestBidIdx = 0;
        bestAskIdx = vectorSize;
        minPriceIdxSeen = vectorSize;
        maxPriceIdxSeen = 0;

        bids.resize(vectorSize);
        asks.resize(vectorSize);
        cancelledOrders.resize(Asset::maxOrdersInRAM, 0); // Initialize with ID 0
    }

    void setTradeBuffer(void* bufferPtr) { externalTradeBuffer = bufferPtr; }
    void setSimId(uint32_t id) { currentSimId = id; }

    void reset() {
        if (maxPriceIdxSeen >= minPriceIdxSeen) {
            for (Price p = minPriceIdxSeen; p <= maxPriceIdxSeen; ++p) {
                // SWAP TRICK: Forces OS to reclaim RAM immediately
                std::deque<Order>().swap(bids[p]);
                std::deque<Order>().swap(asks[p]);
            }
        }
        
        if (maxOrderIdSeen > 0) {
            uint64_t clearLimit = std::min(maxOrderIdSeen + 1, Asset::maxOrdersInRAM);
            std::fill(cancelledOrders.begin(), cancelledOrders.begin() + clearLimit, 0);
        }

        bestBidIdx = 0;
        bestAskIdx = vectorSize;
        minPriceIdxSeen = vectorSize;
        maxPriceIdxSeen = 0;
        maxOrderIdSeen = 0;
    }

    void cancelOrder(OrderId orderId) {
        uint64_t safeId = orderId % Asset::maxOrdersInRAM;
        cancelledOrders[safeId] = orderId; // Store actual ID tag
        if (safeId > maxOrderIdSeen) maxOrderIdSeen = safeId; 
    }

    void addOrder(Order order) {
        if (order.price < Asset::minPriceTicks || order.price >= Asset::maxPriceTicks) return;
        
        // MODULO TAG CHECK: Verifies exact ID match to prevent ghost cancellations
        if (cancelledOrders[order.id % Asset::maxOrdersInRAM] == order.id) return;

        Price pIdx = order.price - Asset::minPriceTicks;

        if (pIdx < minPriceIdxSeen) minPriceIdxSeen = pIdx;
        if (pIdx > maxPriceIdxSeen) maxPriceIdxSeen = pIdx;
        if (order.id > maxOrderIdSeen) maxOrderIdSeen = order.id;

        if (order.side == OrderSide::Bid) {
            while (order.quantity > 0 && bestAskIdx <= pIdx) {
                std::deque<Order>& askQueue = asks[bestAskIdx];

                while (!askQueue.empty() && cancelledOrders[askQueue.front().id % Asset::maxOrdersInRAM] == askQueue.front().id) {
                    askQueue.pop_front();
                }

                while (!askQueue.empty() && order.quantity > 0) {
                    Order& bestAskOrder = askQueue.front();
                    uint32_t matchQty = std::min(order.quantity, bestAskOrder.quantity);
                    
                    if (externalTradeBuffer) {
                        auto* tb = static_cast<RingBuffer<Trade, 4194304>*>(externalTradeBuffer);
                        Price realTradePrice = bestAskIdx + Asset::minPriceTicks;
                        while(!tb->push(Trade(currentSimId, order.id, bestAskOrder.id, realTradePrice, matchQty))) {
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
                    // BOUNDED SCAN: Only search through prices actually touched
                    while (bestAskIdx <= maxPriceIdxSeen && asks[bestAskIdx].empty()) {
                        bestAskIdx++;
                    }
                    if (bestAskIdx > maxPriceIdxSeen) bestAskIdx = vectorSize;
                    if (bestAskIdx == vectorSize) break; 
                }
            }
            
            if (order.quantity > 0) {
                bids[pIdx].push_back(order);
                if (pIdx > bestBidIdx) bestBidIdx = pIdx;
            }
        } 
        else {
            while (order.quantity > 0 && bestBidIdx >= pIdx && bestBidIdx > 0) {
                std::deque<Order>& bidQueue = bids[bestBidIdx];

                while (!bidQueue.empty() && cancelledOrders[bidQueue.front().id % Asset::maxOrdersInRAM] == bidQueue.front().id) {
                    bidQueue.pop_front();
                }

                while (!bidQueue.empty() && order.quantity > 0) {
                    Order& bestBidOrder = bidQueue.front();
                    uint32_t matchQty = std::min(order.quantity, bestBidOrder.quantity);
                    
                    if (externalTradeBuffer) {
                        auto* tb = static_cast<RingBuffer<Trade, 4194304>*>(externalTradeBuffer);
                        Price realTradePrice = bestBidIdx + Asset::minPriceTicks;
                        while(!tb->push(Trade(currentSimId, bestBidOrder.id, order.id, realTradePrice, matchQty))) {
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
                    // BOUNDED SCAN: Only search through prices actually touched
                    while (bestBidIdx >= minPriceIdxSeen && bids[bestBidIdx].empty()) {
                        bestBidIdx--;
                    }
                    if (bestBidIdx < minPriceIdxSeen) bestBidIdx = 0;
                    if (bestBidIdx == 0) break; 
                }
            }

            if (order.quantity > 0) {
                asks[pIdx].push_back(order);
                if (pIdx < bestAskIdx) bestAskIdx = pIdx;
            }
        }
    }
};