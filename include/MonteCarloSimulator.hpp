//MonteCarloSimulator.hpp

#pragma once
#include "RingBuffer.hpp"
#include "Types.hpp"
#include <random>
#include <cmath>
#include <algorithm>

// Define the mathematical universes we can simulate
enum class MarketModel {
    GBM,             // Classic Random Walk
    MEAN_REVERSION,  // Rubber-band effect (Pairs Trading/Stablecoins)
    JUMP_DIFFUSION,  // Random Walk + Sudden Flash Crashes/Spikes
    CAUCHY,          // Extreme Fat Tails (Wild Volatility)
    TRENDING         // Shifted Normal (Constant upward/downward bias)
};

class MonteCarloSimulator {
public:
    template<size_t S>
    static void generateFlow(RingBuffer<MarketEvent, S>& buffer, 
                             uint32_t numEvents,
                             uint32_t cancelPercent = 90,
                             MarketModel model = MarketModel::GBM) // Default to GBM
    {
        // TRUE RANDOMNESS
        std::random_device rd;
        std::mt19937 gen(rd()); 
        std::normal_distribution<double> normDist(0.0, 1.0);
        std::uniform_real_distribution<double> roll(0.0, 1.0);

        // Standard Parameters
        double currentMidPrice = 1000.0; 
        uint32_t orderCounter = 0;
        double cancelProbability = cancelPercent / 100.0; 
        double volatility = 0.0005;

        for (uint32_t i = 0; i < numEvents; ++i) {
            
            // --- NEW PHYSICS ENGINE SWITCHER ---
            switch(model) {
                case MarketModel::GBM: {
                    double shock = (-0.5 * volatility * volatility) + (volatility * normDist(gen));
                    currentMidPrice *= std::exp(shock);
                    break;
                }
                case MarketModel::MEAN_REVERSION: {
                    double theta = 0.001; // Speed of reversion (The "Rubber Band" strength)
                    double mu = 1000.0;   // The "Fair Value" target
                    currentMidPrice += theta * (mu - currentMidPrice) + (volatility * currentMidPrice * normDist(gen));
                    break;
                }
                case MarketModel::JUMP_DIFFUSION: {
                    // Standard movement
                    double shock = (-0.5 * volatility * volatility) + (volatility * normDist(gen));
                    currentMidPrice *= std::exp(shock);
                    // Rare Jump logic (1 in 100k chance)
                    if (roll(gen) < 0.00001) { 
                        double jump = (roll(gen) > 0.5 ? 0.05 : -0.05); // 5% Gap
                        currentMidPrice *= (1.0 + jump);
                    }
                    break;
                }
                case MarketModel::CAUCHY: {
                    // Cauchy generates massive outliers without needing explicit jump logic
                    std::cauchy_distribution<double> cauchy(0.0, 0.00005);
                    currentMidPrice += cauchy(gen) * currentMidPrice;
                    break;
                }
                case MarketModel::TRENDING: {
                    double drift = 0.00001; // Constant upward drift (The "Bull Market" model)
                    currentMidPrice += drift + (volatility * currentMidPrice * normDist(gen));
                    break;
                }
            }

            // Safety Ceiling & Floor to prevent memory crashes
            currentMidPrice = std::clamp(currentMidPrice, 10.0, 199990.0);

            // --- ORDER GENERATION LOGIC ---
            if (roll(gen) < cancelProbability && orderCounter > 100) {
                std::uniform_int_distribution<uint32_t> idDist(1, orderCounter);
                while(!buffer.push(MarketEvent::cancel(idDist(gen)))) {
                    __builtin_ia32_pause();
                }
            } else {
                orderCounter++;
                OrderSide side = (roll(gen) > 0.5) ? OrderSide::Bid : OrderSide::Ask;
                
                uint32_t spreadOffset = static_cast<uint32_t>(roll(gen) * 5) + 1;
                Price price;
                if (side == OrderSide::Bid) {
                    price = static_cast<Price>(currentMidPrice - spreadOffset);
                } else {
                    price = static_cast<Price>(currentMidPrice + spreadOffset);
                }

                price = std::max(static_cast<Price>(1), price);

                Order newOrd(orderCounter, price, 10, side);
                while(!buffer.push(MarketEvent::newOrder(newOrd))) {
                    __builtin_ia32_pause();
                }
            }
        }

        MarketEvent term;
        term.type = EventType::TERMINATE;
        while(!buffer.push(term)) { __builtin_ia32_pause(); }
    }
};