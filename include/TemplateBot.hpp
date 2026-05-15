//TemplateBot.hpp
#pragma once
#include "BotBase.hpp"

class TemplateBot : public BotBase {
public:
    TemplateBot() {
        std::cout << "[SYSTEM] Using Public Template Bot (No Alpha)" << std::endl;
    }

    void evaluateMarket(const StateVector& bucket) override {
        //Dummy bot
    }

    void printReport() const override {
        std::cout << "\n--- TEMPLATE BOT REPORT ---" << std::endl;
        std::cout << "This is an empty template. Build your own strategy!" << std::endl;
    }
};