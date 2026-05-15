// BotBase.hpp
#pragma once
#include <iostream>
#include "OrderBook.hpp" 

class BotBase {
public:
    virtual ~BotBase() = default;
    
    //This function will call your bot each time that a buckeet is created
    virtual void evaluateMarket(const StateVector& bucket) = 0;
    
    //This function will be called at the end to retrieve results
    virtual void printReport() const = 0;
};