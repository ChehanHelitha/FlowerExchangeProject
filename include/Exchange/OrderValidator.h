// SOLID SRP — validation logic isolated from everything else.
#pragma once
#include "Order.h"
#include "ExecutionReport.h"
#include <string>

class OrderValidator {
public:
    // Returns empty string if valid, or a rejection reason if not.
    // We use static methods because the validator holds no state—it's
    // essentially a collection of pure functions. This makes the design
    // explicit and eliminates the need to instantiate the class.
    static std::string validate(const Order& order);

    // Convenience: build a Rejected ExecutionReport in one call
    static ExecutionReport makeRejected(const Order& order,
                                        const std::string& reason);
};