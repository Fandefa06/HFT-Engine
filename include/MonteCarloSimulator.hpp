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
        
        // 50/50 perfecto para mantener el flujo de liquidez del OrderBook intacto
        std::uniform_int_distribution<uint32_t> sideDist(0, 1); 
        
        std::uniform_real_distribution<double> jumpChance(0.0, 1.0); 
        std::normal_distribution<double> noise(0.0, 1.0);
        std::cauchy_distribution<double> cauchyNoise(0.0, 1.0); 
        
        double currentPrice = static_cast<double>(startPrice);
        uint64_t orderCounter = 1;

        // --- DECODIFICACIÓN GEOMÉTRICA EXACTA ---
        // Recuperamos los porcentajes puros
        double trueDriftPerBucket = avgDrift / 100.0; 
        double trueVolPerBucket = stdDev / 5000.0;
        
        // Escalado por orden (aprox 2000 órdenes reales = 1 bucket de 1000 trades)
        double driftPerOrder = trueDriftPerBucket / 2000.0;
        double volPerOrder = trueVolPerBucket / std::sqrt(2000.0);

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
                
                // EL BUG ESTABA AQUÍ: 0.0001 generaba demasiados saltos y el precio se hundía.
                // Ajustado a 0.000002 (~60 saltos por simulación, perfecto para el crash de Enero 2022).
                if (jumpChance(rng) < 0.000002) { 
                    double jumpPct = std::abs(noise(rng)) * trueVolPerBucket * 3.0; 
                    pctChange += (driftPerOrder < 0) ? -jumpPct : jumpPct; 
                }
            } else if (model == MarketModel::CAUCHY) {
                pctChange = driftPerOrder + (volPerOrder * cauchyNoise(rng) * 0.1); 
            }

            // Aplicar el cambio geométrico
            currentPrice += currentPrice * pctChange;

            // EL SEGUNDO BUG (EL CUELGUE): Límite de seguridad estricto.
            // Garantiza que el precio nunca baje de tu ETH_Policy::minPriceTicks (100.000)
            // ni supere el maxPriceTicks (1.000.000), salvando al OrderBook de congelarse.
            currentPrice = std::clamp(currentPrice, 105000.0, 950000.0);
            
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