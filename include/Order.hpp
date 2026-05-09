// Order.hpp

#pragma once
#include "Types.hpp"

struct Order {
    OrderId id;             
    Price price;           
    Quantity quantity;      
    OrderSide side;

    Order(OrderId orderId, Price orderPrice, Quantity orderQuantity, OrderSide orderSide)
        : id(orderId),
          price(orderPrice),
          quantity(orderQuantity),
          side(orderSide) {}
};