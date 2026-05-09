//Types.cpp

#pragma once
#include <cstdint>

// Alias de tipos fundamentales para el motor
using Price = int64_t;      // int64_t permite multiplicadores de hasta 6 decimales sin desbordamiento
using Quantity = uint32_t;
using OrderId = uint64_t;

// Enum centralizado
enum class OrderSide : uint8_t {
    Bid,
    Ask
};