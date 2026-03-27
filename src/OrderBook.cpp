#include "OrderBook.h"
#include "Utils.h"

OrderBook::OrderBook(std::string instrument)
    : instrument_(std::move(instrument))   // move the string, not copy
{}

// ----------------------------------------------------------------
// CORE MATCHING LOGIC
//
// Matching Condition:
//   Buy  incoming: executes against sell orders where sellPrice <= buyPrice
//   Sell incoming: executes against buy  orders where buyPrice  >= sellPrice
//
// Price-Time Priority:
//   - std::map iteration gives best price first.
//   - Within a price level, std::queue gives oldest order first.
//
// Execution Price Rule:
//   "The execution price of an aggressive order is decided by the
//    passive order sitting in the book."
//   i.e., we always use the PASSIVE (book) order's price for fills.
// ----------------------------------------------------------------

void OrderBook::processOrder(Order                        order,
                              std::vector<ExecutionReport>& reports) {
    if (order.side == 1) {       // Incoming BUY
        matchBuy(order, reports);
    } else {                     // Incoming SELL
        matchSell(order, reports);
    }
}

void OrderBook::matchBuy(Order& incoming, std::vector<ExecutionReport>& reports) {
    // Iterate sell book from lowest ask upward (default map order)
    // Stop when no more matchable sell orders remain.
    bool anyExecutionHappened = false;
    while (incoming.quantity > 0 && !sellBook_.empty()) {
        auto bestSellIt = sellBook_.begin();   // Lowest ask price first
        double bestSellPrice = bestSellIt->first;

        // Matching condition: buyer's limit >= best ask
        if (incoming.price < bestSellPrice) {
            break;  // No match possible at remaining sell prices
        }

        // We have a match — work through the queue at this price level
        auto& priceQueue = bestSellIt->second;

        while (incoming.quantity > 0 && !priceQueue.empty()) {
            Order& passive = priceQueue.front();

            // Fill quantity = min of the two remaining quantities
            int fillQty = std::min(incoming.quantity, passive.quantity);

            // Execution price is always the PASSIVE (book) order's price
            double fillPrice = bestSellPrice;

            // Determine status of the PASSIVE (sell) order
            ExecStatus passiveStatus = (fillQty == passive.quantity)
                                         ? ExecStatus::Fill
                                         : ExecStatus::PFill;

            // Determine status of the AGGRESSIVE (buy) order
            ExecStatus aggressiveStatus = ((incoming.quantity - fillQty) == 0)
                                            ? ExecStatus::Fill
                                            : ExecStatus::PFill;

            // Emit report for the AGGRESSIVE (incoming) order first
            reports.push_back(makeFill(incoming, aggressiveStatus,
                                        fillQty, fillPrice));

            // Emit report for the PASSIVE (book) order
            reports.push_back(makeFill(passive, passiveStatus,
                                        fillQty, fillPrice));

            // Update quantities
            incoming.quantity -= fillQty;
            passive.quantity  -= fillQty;

            anyExecutionHappened = true;
            
            // Remove fully filled passive order from book
            if (passive.quantity == 0) {
                priceQueue.pop();
            }
        }

        // Remove the price level entirely if no orders remain
        if (priceQueue.empty()) {
            sellBook_.erase(bestSellIt);
        }
    }

    // If the incoming order still has remaining quantity → add to book
    if (incoming.quantity > 0) {
        // Emit "New" status for the passive resting portion
        if (!anyExecutionHappened) {
            reports.push_back(makeFill(incoming, ExecStatus::New,
                                        incoming.quantity, incoming.price));
        }
        addToBook(std::move(incoming));  // Move the order into the book instead of copying it
    }
}

void OrderBook::matchSell(Order& incoming, std::vector<ExecutionReport>& reports) {
    // Iterate buy book from highest bid downward (std::greater gives descending)
    bool anyExecutionHappened = false;
    while (incoming.quantity > 0 && !buyBook_.empty()) {
        auto bestBuyIt = buyBook_.begin();   // Highest bid first
        double bestBuyPrice = bestBuyIt->first;

        // Matching condition: seller's limit <= best bid
        if (incoming.price > bestBuyPrice) {
            break;  // No match possible
        }

        auto& priceQueue = bestBuyIt->second;

        while (incoming.quantity > 0 && !priceQueue.empty()) {
            Order& passive = priceQueue.front();

            int    fillQty   = std::min(incoming.quantity, passive.quantity);
            double fillPrice = bestBuyPrice;   // Passive (buy) order's price

            ExecStatus passiveStatus = (fillQty == passive.quantity)
                                         ? ExecStatus::Fill
                                         : ExecStatus::PFill;

            ExecStatus aggressiveStatus = ((incoming.quantity - fillQty) == 0)
                                            ? ExecStatus::Fill
                                            : ExecStatus::PFill;

            // Aggressive (sell) report first
            reports.push_back(makeFill(incoming, aggressiveStatus,
                                        fillQty, fillPrice));
            // Passive (buy) report second
            reports.push_back(makeFill(passive, passiveStatus,
                                        fillQty, fillPrice));

            incoming.quantity -= fillQty;
            passive.quantity  -= fillQty;

            anyExecutionHappened = true;

            if (passive.quantity == 0) {
                priceQueue.pop();
            }
        }

        if (priceQueue.empty()) {
            buyBook_.erase(bestBuyIt);
        }
    }

    if (incoming.quantity > 0) {
        if (!anyExecutionHappened) {
            reports.push_back(makeFill(incoming, ExecStatus::New,
                                        incoming.quantity, incoming.price));
        }
        addToBook(std::move(incoming));
    }
}

void OrderBook::addToBook(Order order) {
    // Move the order into the queue to avoid copying its data.
    // This is more efficient since the order is about to go out of scope anyway.
    if (order.side == 1) {
        buyBook_[order.price].push(std::move(order));
    } else {
        sellBook_[order.price].push(std::move(order));
    }
}

ExecutionReport OrderBook::makeFill(const Order& order,
                                     ExecStatus    status,
                                     int           fillQty,
                                     double        fillPrice) {
    return ExecutionReport(
        order.orderId,
        order.clientOrderId,
        order.instrument,
        order.side,
        status,
        fillQty,
        fillPrice,
        "",    // No rejection reason for fills
        Utils::getCurrentTimestamp()
    );
}