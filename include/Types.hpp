// Types.hpp

#pragma once
#include <cstdint>

using Price = int64_t;
using Quantity = uint32_t;
using OrderId = uint64_t;

enum class OrderSide : uint8_t { Bid, Ask };

// Types of actions that can enter the pipeline
enum class EventType : uint8_t { 
    NEW_ORDER, 
    CANCEL_ORDER,
    TERMINATE // To tell the consumer thread to stop
};

#include "Order.hpp" // We need Order definition for the struct below

struct MarketEvent {
    EventType type;
    Order order;

    // Fast constructors
    MarketEvent() : type(EventType::TERMINATE), order(0,0,0,OrderSide::Bid) {}
    
    static MarketEvent newOrder(Order o) {
        MarketEvent ev;
        ev.type = EventType::NEW_ORDER;
        ev.order = o;
        return ev;
    }

    static MarketEvent cancel(OrderId id) {
        MarketEvent ev;
        ev.type = EventType::CANCEL_ORDER;
        ev.order.id = id; // We reuse the order.id field for cancellation
        return ev;
    }
};