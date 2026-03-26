#pragma once
#include <string>

// Execution status codes (from spec)
enum class ExecStatus : int {
    New      = 0,
    Rejected = 1,
    Fill     = 2,
    PFill    = 3
};

struct ExecutionReport {
    std::string orderId;        // System generated (UUID)
    std::string clientOrderId;  
    std::string instrument;
    int         side;           // 1=Buy, 2=Sell
    ExecStatus  status;
    int         quantity;       
    double      price;          // Matched price
    std::string reason;         // Populated only on Rejected orders
    std::string transactTime;   // YYYYMMDD-HHMMSS.sss

    // Default constructor — needed for queue/vector operations
    ExecutionReport()
        : side(0), status(ExecStatus::New), quantity(0), price(0.0) {}

    ExecutionReport(std::string  ordId,
                    std::string  clOrdId,
                    std::string  instr,
                    int          sd,
                    ExecStatus   st,
                    int          qty,
                    double       px,
                    std::string  rsn,
                    std::string  txTime)
        : orderId(std::move(ordId))
        , clientOrderId(std::move(clOrdId))
        , instrument(std::move(instr))
        , side(sd)
        , status(st)
        , quantity(qty)
        , price(px)
        , reason(std::move(rsn))
        , transactTime(std::move(txTime))
    {}

    // Move Semantics because no string heap copies needed.
    ExecutionReport(ExecutionReport&&) noexcept = default;
    ExecutionReport& operator=(ExecutionReport&&) noexcept = default;

    ExecutionReport(const ExecutionReport&) = default;
    ExecutionReport& operator=(const ExecutionReport&) = default;
};