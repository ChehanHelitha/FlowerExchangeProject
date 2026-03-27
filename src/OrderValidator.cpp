#include "OrderValidator.h"
#include "Utils.h"
#include <set>

static const std::set<std::string> VALID_INSTRUMENTS = {
    "Rose", "Lavender", "Lotus", "Tulip", "Orchid"
};

std::string OrderValidator::validate(const Order& order) {
    // Rule 1 — Instrument must be one of the 5 allowed types
    if (VALID_INSTRUMENTS.find(order.instrument) == VALID_INSTRUMENTS.end()) {
        return "Invalid instrument";
    }

    // Rule 2 — Side must be 1 (Buy) or 2 (Sell)
    if (order.side != 1 && order.side != 2) {
        return "Invalid side";
    }

    // Rule 3 — Price must be strictly positive
    if (order.price <= 0.0) {
        return "Invalid price";
    }

    // Rule 4 — Quantity: multiple of 10, within [10, 1000]
    if (order.quantity < 10 || order.quantity > 1000) {
        return "Invalid size";
    }
    if (order.quantity % 10 != 0) {
        return "Invalid size";
    }

    return "";  // Empty string = valid
}

ExecutionReport OrderValidator::makeRejected(const Order&       order,
                                              const std::string& reason) {
    // Move the reason string into the report to avoid copying it.
    // This is more efficient when passing temporary strings.
    return ExecutionReport(
        order.orderId,          // orderId already assigned before validation
        order.clientOrderId,
        order.instrument,
        order.side,
        ExecStatus::Rejected,
        order.quantity,
        order.price,
        reason,
        Utils::getCurrentTimestamp()
    );
}