// AssetPolicies.hpp

#pragma once
#include <cstdint>
#include <string>

// --- THE UNIVERSAL ASSET BLUEPRINT ---
// All fixed-point math: Decimals are removed by multiplying by 'Multiplier'
// Example: $0.01 tick size = Multiplier of 100. Price $2500.50 -> 250050

struct ETH_Policy {
    static constexpr const char* name = "ETHUSDT";
    static constexpr uint64_t tickMultiplier = 100;     // 2 decimal places
    static constexpr uint64_t maxPriceTicks = 10000000; // Fits up to $100,000.00
    static constexpr uint64_t maxOrdersInRAM = 2000000000; // Cap memory usage per reality
};

struct BTC_Policy {
    static constexpr const char* name = "BTCUSDT";
    static constexpr uint64_t tickMultiplier = 100;     // 2 decimal places
    static constexpr uint64_t maxPriceTicks = 20000000; // Fits up to $200,000.00
    static constexpr uint64_t maxOrdersInRAM = 5000000; 
};

struct SP500_Policy {
    static constexpr const char* name = "SP500_Emini";
    static constexpr uint64_t tickMultiplier = 4;       // Moves in 0.25 increments
    static constexpr uint64_t maxPriceTicks = 40000;    // Fits up to 10,000 points
    static constexpr uint64_t maxOrdersInRAM = 1000000; 
};

//More policies can be created