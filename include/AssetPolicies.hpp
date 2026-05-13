// AssetPolicies.hpp
#pragma once
#include <cstdint>
#include <string>

// --- THE UNIVERSAL ASSET BLUEPRINT ---
// All fixed-point math: Decimals are removed by multiplying by 'Multiplier'
// Bounded Windows: We only track prices between minPriceTicks and maxPriceTicks to save RAM.

// AssetPolicies.hpp
#pragma once
#include <cstdint>
#include <string>

struct ETH_Policy {
    static constexpr const char* name = "ETHUSDT";
    static constexpr uint64_t tickMultiplier = 100;
    static constexpr uint64_t minPriceTicks  = 100000;  // Floor: $1,000.00
    static constexpr uint64_t maxPriceTicks  = 1000000; // Ceiling: $10,000.00
    // Reduced to 50M to prevent the 16GB allocation crash
    static constexpr uint64_t maxOrdersInRAM = 50000000; 
};

struct BTC_Policy {
    static constexpr const char* name = "BTCUSDT";
    static constexpr uint64_t tickMultiplier = 100;
    static constexpr uint64_t minPriceTicks  = 3000000;  // Floor: $30,000.00
    static constexpr uint64_t maxPriceTicks  = 15000000; // Ceiling: $150,000.00
    static constexpr uint64_t maxOrdersInRAM = 50000000; 
};

struct SP500_Policy {
    static constexpr const char* name = "SP500_Emini";
    
    // Moves in 0.25 increments
    static constexpr uint64_t tickMultiplier = 4;       
    
    // Floor: 3,000 points
    static constexpr uint64_t minPriceTicks  = 12000;   
    
    // Ceiling: 10,000 points
    static constexpr uint64_t maxPriceTicks  = 40000;   
    
    static constexpr uint64_t maxOrdersInRAM = 10000000; 
};
//More policies can be created