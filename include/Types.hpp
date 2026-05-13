// Types.hpp

#pragma once
#include <cstdint>

// 1. Define the fundamental names first
using Price = int64_t;
using Quantity = uint32_t;
using OrderId = uint64_t;

enum class OrderSide : uint8_t { Bid, Ask };
enum class EventType : uint8_t { NEW_ORDER, CANCEL_ORDER, TERMINATE };

// 2. Define Order NOW, because it knows what Price and Quantity are
struct Order {
    OrderId id;             
    Price price;           
    Quantity quantity;      
    OrderSide side;

    Order(OrderId orderId, Price orderPrice, Quantity orderQuantity, OrderSide orderSide)
        : id(orderId), price(orderPrice), quantity(orderQuantity), side(orderSide) {}
        
    // Default constructor needed for MarketEvent
    Order() : id(0), price(0), quantity(0), side(OrderSide::Bid) {} 
};

// 3. Define MarketEvent NOW, because it knows what an Order is
struct MarketEvent {
    EventType type;
    Order order;

    MarketEvent() : type(EventType::TERMINATE), order() {}
    
    static MarketEvent newOrder(Order o) {
        MarketEvent ev;
        ev.type = EventType::NEW_ORDER;
        ev.order = o;
        return ev;
    }

    static MarketEvent cancel(OrderId id) {
        MarketEvent ev;
        ev.type = EventType::CANCEL_ORDER;
        ev.order.id = id; 
        return ev;
    }
};

// =========================================================================
// --- COMPRESSED AI NUTRIENTS (The Feature Engine Output) ---
// =========================================================================
struct StateVector {
    uint32_t simId;
    uint32_t padding;         // <--- CRITICAL FIX: Memory alignment for Python
    uint64_t bucketId;        
    int64_t openPrice;
    int64_t highPrice;
    int64_t lowPrice;
    int64_t closePrice;
    uint64_t totalVolume;
    int64_t orderFlowImbalance; 
    
    // INT64_MAX ensures the first trade sets the real lowPrice
    StateVector() : simId(0), padding(0), bucketId(0), openPrice(0), highPrice(0), 
                    lowPrice(INT64_MAX), closePrice(0), totalVolume(0), orderFlowImbalance(0) {}
};