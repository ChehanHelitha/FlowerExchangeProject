// Pure virtual interface for any output writer.
// INTERFACE for SOLID — Dependency Inversion Principle.
// ExchangeApplication depends on this abstraction, not directly on
// CSVWriter. This makes writers swappable (e.g., CSV to database)
// without touching the engine core. The virtual destructor ensures
// proper cleanup when deleting through a base pointer.

#pragma once
#include "ExecutionReport.h"

class IWriter {
public:
    // WHY PURE VIRTUAL (= 0): Forces every concrete subclass to
    // implement this method. IWriter cannot be instantiated directly—
    // it's a contract that derived classes must fulfill.
    virtual void writeReport(ExecutionReport report) = 0;

    // Virtual destructor ensures proper cleanup when deleting
    // through a base pointer. Without it, derived class destructors
    // (like CSVWriter's file handle cleanup) won't be called.
    virtual ~IWriter() = default;
};