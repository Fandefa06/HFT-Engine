// MonteCarloSimulator.hpp
#pragma once
#include <random>
#include <cmath>
#include <atomic>
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
    static void generateFlow(RingBuffer<MarketEvent, S>& buffer, 
                             std::atomic<bool>& isRunning, 
                             uint32_t cancelPercent, 
                             MarketModel model, 
                             Price startPrice,
                             double avgDrift = 0.0,     
                             double stdDev = 2.0) {      
        
        thread_local std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<uint32_t> cancelDist(1, 100);
        std::uniform_int_distribution<uint32_t> sideDist(0, 1);
        std::uniform_real_distribution<double> jumpChance(0.0, 1.0); 
        
        std::normal_distribution<double> noise(0.0, 1.0);
        std::cauchy_distribution<double> cauchyNoise(0.0, 1.0); 
        
        double currentPrice = static_cast<double>(startPrice);
        uint64_t orderCounter = 1;

        while (isRunning.load(std::memory_order_relaxed)) {
            
            // Usamos el ADN inyectado en lugar de valores fijos
            double drift = avgDrift;
            double volatility = stdDev; 

            if (model == MarketModel::GBM) {
                currentPrice += drift + (volatility * noise(rng));
            } else if (model == MarketModel::MEAN_REVERSION) {
                double meanPrice = static_cast<double>(startPrice);
                currentPrice += 0.05 * (meanPrice - currentPrice) + (volatility * noise(rng));
            } else if (model == MarketModel::TRENDING) {
                currentPrice += 0.5 + drift + (volatility * noise(rng)); 
            } else if (model == MarketModel::JUMP_DIFFUSION) {
                double step = drift + (volatility * noise(rng));
                if (jumpChance(rng) < 0.001) { 
                    step += noise(rng) * 200.0; 
                }
                currentPrice += step;
            } else if (model == MarketModel::CAUCHY) {
                currentPrice += drift + (volatility * cauchyNoise(rng) * 0.1); 
            }

            Price tickPrice = static_cast<Price>(std::round(currentPrice));

            if (cancelDist(rng) <= cancelPercent && orderCounter > 100) {
                uint64_t cancelId = orderCounter - (cancelDist(rng) % 50); 
                while (isRunning.load(std::memory_order_relaxed) && !buffer.push(MarketEvent::cancel(cancelId))) {
                    __builtin_ia32_pause();
                }
            } else {
                OrderSide side = (sideDist(rng) == 0) ? OrderSide::Bid : OrderSide::Ask;
                Quantity qty = 10 + (cancelDist(rng) % 50); 
                
                Order newOrd(orderCounter++, tickPrice, qty, side);
                while (isRunning.load(std::memory_order_relaxed) && !buffer.push(MarketEvent::newOrder(newOrd))) {
                    __builtin_ia32_pause();
                }
            }
        }
        
        while (!buffer.push(MarketEvent())) {
            __builtin_ia32_pause();
        }
    }
};