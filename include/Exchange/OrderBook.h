// Price-Time Priority matching engine for ONE instrument.
// One OrderBook per instrument — Single Responsibility Principle + clean composition.
// ExchangeApplication maintains 5 OrderBook instances, each running in its own thread.
// This eliminates lock contention between instruments and keeps state isolated.

#pragma once
#include "Order.h"
#include "ExecutionReport.h"
#include "ThreadSafeQueue.h"
#include <map>
#include <queue>
#include <vector>
#include <functional>
#include <string>

class OrderBook {
public:
    // WHY explicit: Prevents accidental implicit construction from a bare string.
    explicit OrderBook(std::string instrument);

    // Non-copyable as the order book owns live market state and copying it would duplicate positions — a trading disaster.
    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // Process one incoming order and generate execution reports.
    // Appends all fills and acknowledgments to `reports` (passed by reference
    // for efficiency, avoiding repeated heap allocations).
    void processOrder(Order order,
                      std::vector<ExecutionReport>& reports);

private:
    // WHY std::map WITH CUSTOM COMPARATOR FOR BUY SIDE:
    //
    // Buy side: highest price has priority (most attractive bid).
    //   std::map<double, queue, std::greater<double>> sorts
    //   descending — begin() always points to the best (highest)
    //   bid price, so we iterate from begin() when matching.
    //
    // Sell side: lowest price has priority (cheapest ask).
    //   Default std::map<double, queue> sorts ascending —
    //   begin() always points to the best (lowest) ask price.
    //
    // WHY std::queue<Order> AS THE VALUE:
    //   Orders at the same price are served FIFO — oldest first.
    //   This implements TIME PRIORITY within a price level.
    //   std::queue enforces FIFO semantics explicitly; using
    //   vector or list would allow accidental middle insertion.
    using BuyBook  = std::map<double, std::queue<Order>, std::greater<double>>;
    using SellBook = std::map<double, std::queue<Order>>;

    BuyBook  buyBook_;
    SellBook sellBook_;

    std::string instrument_;

    //  Matching Logic

    // Try to match an aggressive buy order against the sell book.
    // Returns generated reports (fills + passive fills).
    void matchBuy (Order& incoming, std::vector<ExecutionReport>& reports);
    void matchSell(Order& incoming, std::vector<ExecutionReport>& reports);

    // Insert a passive order into the appropriate side of the book.
    void addToBook(Order order);

    // Build an ExecutionReport for a fill event.
    static ExecutionReport makeFill(const Order& order,
                                    ExecStatus    status,
                                    int           fillQty,
                                    double        fillPrice);
};