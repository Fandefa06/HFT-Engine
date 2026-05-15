// MonteCarloSimulator.hpp
#pragma once
#include <random>
#include <cmath>
#include <atomic>
#include <algorithm>
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
    template <typename Policy, size_t S> 
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

        // --- DECODIFICACIÓN GEOMÉTRICA EXACTA ---
        double trueDriftPerBucket = avgDrift / 100.0; 
        double trueVolPerBucket = stdDev / 5000.0;
        
        // El ratio empírico exacto de tu motor: 1042 órdenes = 1 Bucket
        double driftPerOrder = trueDriftPerBucket / 1042.0;
        double volPerOrder = trueVolPerBucket / std::sqrt(1042.0);

        while (isRunning.load(std::memory_order_relaxed)) {
            
            double pctChange = 0.0;

            if (model == MarketModel::GBM) {
                pctChange = driftPerOrder + (volPerOrder * noise(rng));
            } else if (model == MarketModel::MEAN_REVERSION) {
                double meanPrice = static_cast<double>(startPrice);
                pctChange = 0.0001 * ((meanPrice - currentPrice) / currentPrice) + (volPerOrder * noise(rng));
            } else if (model == MarketModel::TRENDING) {
                pctChange = (driftPerOrder * 1.5) + (volPerOrder * noise(rng)); 
            } else if (model == MarketModel::JUMP_DIFFUSION) {
                pctChange = driftPerOrder + (volPerOrder * noise(rng));
                
                // BUG FIX: Saltos Simétricos Orgánicos. 
                // Al usar directamente noise(rng), el salto puede ser positivo (Rally) o negativo (Crash).
                // Probabilidad ajustada para ~60 eventos cisne negro por cada 124M de órdenes.
                if (jumpChance(rng) < 0.0000005) { 
                    double jumpPct = noise(rng) * trueVolPerBucket * 5.0; 
                    pctChange += jumpPct; 
                }
            } else if (model == MarketModel::CAUCHY) {
                pctChange = driftPerOrder + (volPerOrder * cauchyNoise(rng) * 0.1); 
            }

            currentPrice += currentPrice * pctChange;

            double safeMin = static_cast<double>(Policy::minPriceTicks) * 1.01; 
            double safeMax = static_cast<double>(Policy::maxPriceTicks) * 0.99; 
            currentPrice = std::clamp(currentPrice, safeMin, safeMax);
            
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