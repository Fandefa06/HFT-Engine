// MonteCarloSimulator.hpp
#pragma once
#include <random>
#include <cmath>
#include "Types.hpp"
#include "RingBuffer.hpp"

enum class MarketModel {
    GBM,
    MEAN_REVERSION,
    JUMP_DIFFUSION,
    CAUCHY,
    TRENDING
};

class MonteCarloSimulator {
public:
    template <size_t S>
    static void generateFlow(RingBuffer<MarketEvent, S>& buffer, uint32_t numOrders, uint32_t cancelPercent, MarketModel model, Price startPrice) {
        
        // Use a thread-local random engine for extreme speed
        thread_local std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<uint32_t> cancelDist(1, 100);
        std::uniform_int_distribution<uint32_t> sideDist(0, 1);
        std::uniform_real_distribution<double> jumpChance(0.0, 1.0); // For Jump Diffusion
        
        // Physics modifiers
        std::normal_distribution<double> noise(0.0, 1.0);
        std::cauchy_distribution<double> cauchyNoise(0.0, 1.0); // For Cauchy (Fat Tails)
        
        // ANCHOR THE STARTING PRICE
        double currentPrice = static_cast<double>(startPrice);
        uint64_t orderCounter = 1;

        for (uint32_t i = 0; i < numOrders; ++i) {
            
            // 1. Calculate the next price tick based on the model
            double drift = 0.0;
            double volatility = 2.0; // Ticks of movement

            if (model == MarketModel::GBM) {
                currentPrice += drift + (volatility * noise(rng));
            } else if (model == MarketModel::MEAN_REVERSION) {
                double meanPrice = static_cast<double>(startPrice);
                currentPrice += 0.05 * (meanPrice - currentPrice) + (volatility * noise(rng));
            } else if (model == MarketModel::TRENDING) {
                currentPrice += 0.5 + (volatility * noise(rng)); // Constant upward pressure
            } else if (model == MarketModel::JUMP_DIFFUSION) {
                // Normal random walk...
                double step = volatility * noise(rng);
                
                // ...but a 0.1% chance of a MASSIVE flash crash or spike!
                if (jumpChance(rng) < 0.001) { 
                    step += noise(rng) * 200.0; // 200 ticks of instant panic
                }
                currentPrice += step;
            } else if (model == MarketModel::CAUCHY) {
                // Cauchy math creates wild, unpredictable "Black Swan" events natively
                currentPrice += (volatility * cauchyNoise(rng) * 0.1); 
            }

            Price tickPrice = static_cast<Price>(std::round(currentPrice));

            // 2. Decide if it's a new order or a cancellation
            if (cancelDist(rng) <= cancelPercent && orderCounter > 100) {
                // Cancel a recent order
                uint64_t cancelId = orderCounter - (cancelDist(rng) % 50); 
                while (!buffer.push(MarketEvent::cancel(cancelId))) {
                    __builtin_ia32_pause();
                }
            } else {
                // Generate a new order
                OrderSide side = (sideDist(rng) == 0) ? OrderSide::Bid : OrderSide::Ask;
                Quantity qty = 10 + (cancelDist(rng) % 50); // Random quantity between 10 and 60
                
                Order newOrd(orderCounter++, tickPrice, qty, side);
                while (!buffer.push(MarketEvent::newOrder(newOrd))) {
                    __builtin_ia32_pause();
                }
            }
        }
        
        // Tell the consumer we are done
        while (!buffer.push(MarketEvent())) {
            __builtin_ia32_pause();
        }
    }
};