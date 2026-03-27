// COMPOSITION:
//   - 5 OrderBook instances (one per instrument)
//   - 5 instrument-specific ThreadSafeQueues (producer → workers)
//   - 1 output ThreadSafeQueue (workers → writer thread)
//   - 5 worker std::threads + 1 writer std::thread
//   - 1 IWriter (CSVWriter behind interface pointer)
//
// WHY unique_ptr FOR ORDERBOOKS AND WRITER:
//   unique_ptr communicates EXCLUSIVE OWNERSHIP. When
//   ExchangeApplication is destroyed, all books and the writer
//   are automatically deallocated — no manual delete required.
//   This is the "ownership = deletion" principle.
//
// WHY unique_ptr FOR THREADS:
//   std::thread is move-only. Storing them in unique_ptrs in a
//   map gives us named access (by instrument string) and
//   automatic join/cleanup semantics via the destructor.
#pragma once
#include "OrderBook.h"
#include "IWriter.h"
#include "ThreadSafeQueue.h"
#include "ExecutionReport.h"
#include <memory>
#include <thread>
#include <unordered_map>
#include <string>
#include <vector>

class ExchangeApplication {
public:
    // Takes ownership of the writer via unique_ptr — only this application object may delete it.
    explicit ExchangeApplication(std::unique_ptr<IWriter> writer);

    // Non-copyable: owns threads and mutable market state.
    ExchangeApplication(const ExchangeApplication&)            = delete;
    ExchangeApplication& operator=(const ExchangeApplication&) = delete;

    // Entry point: runs the full pipeline to completion.
    void run(const std::string& inputFile);

    // Destructor joins all threads before members are destroyed.
    ~ExchangeApplication();

private:
    // Each book is heap-allocated to avoid large stack frames; unique_ptr encodes ownership.
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> orderBooks_;

    // Per-instrument queues: producer (main) → consumer (worker)
    std::unordered_map<std::string,
                       std::unique_ptr<ThreadSafeQueue<Order>>> instrumentQueues_;

    // Single output queue: all workers → writer thread
    ThreadSafeQueue<ExecutionReport> outputQueue_;

    // We own the writer's lifetime exclusively via unique_ptr.
    // The IWriter interface lets us swap in any implementation
    // (CSV, database, network, etc.) at construction time.
    std::unique_ptr<IWriter> writer_;

    // ---- Threads ----
    // Worker threads are stored in a vector for simplicity.
    // We join them all in the destructor to ensure clean shutdown.
    std::vector<std::thread> workerThreads_;
    std::thread              writerThread_;

    static const std::vector<std::string> INSTRUMENTS;

    // ---- Private pipeline stages ----

    // Worker thread body: drains one instrument queue through its OrderBook
    void workerLoop(const std::string& instrument);

    // Writer thread body: drains outputQueue_ through the IWriter
    void writerLoop();
};