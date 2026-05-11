// include/HistoricalParser.hpp
#pragma once
#include "RingBuffer.hpp"
#include "Types.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <unordered_map>
#include <cmath>

class HistoricalParser {
public:
    template<size_t S>
    static void feedFromCSV(RingBuffer<MarketEvent, S>& buffer, const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open " << filename << std::endl;
            // Send termination signal so the engine doesn't hang
            MarketEvent term; term.type = EventType::TERMINATE;
            while (!buffer.push(term)) { __builtin_ia32_pause(); }
            return;
        }

        std::cout << "Starting CSV Ingestion: " << filename << std::endl;

        std::string line;
        
        // --- GATEWAY PATTERN (Translation Dictionary) ---
        // Maps: Real Exchange ID -> Internal Sequential ID
        std::unordered_map<uint64_t, OrderId> idTranslator;
        OrderId nextInternalId = 1;
        uint32_t count = 0;

        while (std::getline(file, line)) {
            // Skip empty lines for safety
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string item;
            
            uint64_t externalId;
            double price, qty;
            std::string isBuyerMaker;
            
            try {
                // Parse format: ID, Price, Qty, QuoteQty, Time, isBuyerMaker, isBestMatch
                std::getline(ss, item, ','); externalId = std::stoull(item);
                std::getline(ss, item, ','); price = std::stod(item);
                std::getline(ss, item, ','); qty = std::stod(item);
                std::getline(ss, item, ','); // skip quoteQty
                std::getline(ss, item, ','); // skip time
                std::getline(ss, item, ','); isBuyerMaker = item;

                // 1. ID TRANSLATION (Gateway)
                OrderId internalId;
                auto it = idTranslator.find(externalId);
                if (it != idTranslator.end()) {
                    internalId = it->second; // ID already exists
                } else {
                    internalId = nextInternalId++;
                    idTranslator[externalId] = internalId; // Register new ID
                }

                // 2. PARSE SIDE ("True" or "true" in Binance means the aggressor was a Seller -> Ask)
                OrderSide side = (isBuyerMaker == "true" || isBuyerMaker == "True") ? OrderSide::Ask : OrderSide::Bid;
                
                // 3. FLOAT TO INTEGER (Tick Sizing)
                // Price: * 100 (e.g., 3014.04 -> 301404 cents)
                Price tickPrice = static_cast<Price>(std::round(price * 100.0)); 
                
                // Quantity: * 10000 (e.g., 0.0139 -> 139 micro-units)
                Quantity tickQty = static_cast<Quantity>(std::round(qty * 10000.0)); 
                
                if(tickQty == 0) tickQty = 1; // Minimum safety boundary

                // Create and inject the order
                Order ord(internalId, tickPrice, tickQty, side);
                
                while (!buffer.push(MarketEvent::newOrder(ord))) {
                    __builtin_ia32_pause();
                }
                count++;
                
            } catch (...) {
                // If the first line is a "header" (strings), stod will throw. We catch and ignore it.
                continue;
            }
        }

        // Shut down the engine
        MarketEvent term;
        term.type = EventType::TERMINATE;
        while (!buffer.push(term)) { __builtin_ia32_pause(); }
        
        std::cout << "CSV Ingestion Complete. Injected " << count << " translated historical orders." << std::endl;
    }
};