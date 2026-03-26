#pragma once
#include <string>

struct Order {
    std::string clientOrderId;  // Max 7 chars alphanumeric
    std::string instrument;     // Rose, Lavender, Lotus, Tulip, Orchid
    int         side;           // 1 = Buy, 2 = Sell
    double      price;          // Price > 0.0
    int         quantity;       // 10-1000, multiple of 10
    std::string orderId;        // System-generated
    int         seqNum;         // Arrival sequence for time priority

    // Used overload constructors for a clean construction without requiring a separate assignment step.
    Order() : side(0), price(0.0), quantity(0), seqNum(0) {}

    Order(std::string clOrdId,
          std::string instr,
          int         sd,
          double      px,
          int         qty,
          std::string ordId,
          int         seq)
        : clientOrderId(std::move(clOrdId))  // use std::move to transfer ownership of these strings to avoid unnecessary copying
        , instrument(std::move(instr))        
        , side(sd)                          
        , price(px)                           
        , quantity(qty)                      
        , orderId(std::move(ordId))
        , seqNum(seq)
    {}

    Order(Order&& other) noexcept = default;
    Order& operator=(Order&& other) noexcept = default;

    // Copy is still needed when an order must be kept in the book AND referenced in an execution report simultaneously.
    Order(const Order&) = default;
    Order& operator=(const Order&) = default;
};