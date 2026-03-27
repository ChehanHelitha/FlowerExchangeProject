#include "ExchangeApplication.h"
#include "CSVReader.h"
#include "OrderValidator.h"
#include "Utils.h"
#include <iostream>

// The 5 instruments the system supports
const std::vector<std::string> ExchangeApplication::INSTRUMENTS = {
    "Rose", "Lavender", "Lotus", "Tulip", "Orchid"
};

ExchangeApplication::ExchangeApplication(std::unique_ptr<IWriter> writer)
    : writer_(std::move(writer))   // 'move' to take ownership from caller
{
    // Set up one OrderBook and one Queue for each of the 5 flowers.
    // We're building a collection of them, not inheriting from OrderBook.
    for (const auto& instr : INSTRUMENTS) {
        // We use make_unique here because it's safe if something goes
        // wrong. If the second one fails, the first one still cleans up
        // automatically — no memory leak. Using plain 'new' won't do that.
        orderBooks_[instr]       = std::make_unique<OrderBook>(instr);
        instrumentQueues_[instr] = std::make_unique<ThreadSafeQueue<Order>>();
    }
}

ExchangeApplication::~ExchangeApplication() {
    // Clean up threads before destroying queues and order books
    // Threads need these resources to shut down safely
    for (auto& t : workerThreads_) {
        if (t.joinable()) t.join();
    }
    if (writerThread_.joinable()) writerThread_.join();
}

void ExchangeApplication::run(const std::string& inputFile) {

    // ---- STEP 1: Start the writer thread ----
    // We start the writer thread first so it's ready to receive
    // reports as soon as the workers begin processing orders.
    // We capture 'this' so writerLoop can access outputQueue_ and writer_.
    writerThread_ = std::thread([this] { writerLoop(); });

    // ---- STEP 2: Start 5 worker threads (one per flower) ----
    // Each flower's order book is completely separate — no shared
    // data between them. Running them in parallel removes all lock
    // conflicts between books and makes processing faster.
    for (const auto& instr : INSTRUMENTS) {
        // WHY emplace_back with lambda: std::thread is move-only but emplace_back constructs it in-place, avoiding a copy.
        workerThreads_.emplace_back([this, instr] {
            workerLoop(instr);
        });
    }

    // ---- STEP 3: Producer — read CSV and route orders ----
    CSVReader reader(inputFile);

    // We use a lambda function here to keep the routing logic
    // right here in one place, rather than spreading it across
    // multiple methods.
    reader.readOrders([this](Order order, std::string /*rawLine*/) {

        // Rejected orders never enter a per-instrument queue (they go straight to output).
        std::string reason = OrderValidator::validate(order);

        if (!reason.empty()) {
            // Build and immediately push a Rejected report.
            ExecutionReport rejected = OrderValidator::makeRejected(order, reason);
            // Moving the rejected report into the queue is efficient as it transfers the data without making a copy.
            outputQueue_.push(std::move(rejected));
            return;
        }

        // Send the valid order to the right flower's queue.
        // We use std::move to avoid copying the order data.
        instrumentQueues_.at(order.instrument)->push(std::move(order));
    });

    // ---- STEP 4: Signal all instrument queues that no more orders ----
    for (const auto& instr : INSTRUMENTS) {
        instrumentQueues_.at(instr)->setDone();
    }

    // Wait for all worker threads to finish.
    // This ensures every order has been processed and every report
    // has been added to the output queue before we tell it to stop.
    for (auto& t : workerThreads_) {
        if (t.joinable()) t.join();
    }
    workerThreads_.clear();   // Prevent double-join in destructor

    // ---- STEP 5: Signal the output queue — no more reports ----
    outputQueue_.setDone();

    // Join writer thread — ensures all reports are flushed to disk
    if (writerThread_.joinable()) {
        writerThread_.join();
    }

    std::cout << "[Exchange] Processing complete.\n";
}

void ExchangeApplication::workerLoop(const std::string& instrument) {
    // This function runs on its own worker thread.
    // Each thread owns one OrderBook for its flower — no other thread
    // touches it, so we don't need any locks.

    auto& queue = *instrumentQueues_.at(instrument);
    auto& book  = *orderBooks_.at(instrument);

    Order order;
    // We create a reports vector outside the loop so we don't have to
    // allocate new memory for every order. We just clear it and reuse it.
    std::vector<ExecutionReport> reports;
    reports.reserve(8);   // Most orders generate ≤8 reports

    // Blocking pop returns false only when queue is done AND empty.
    while (queue.pop(order)) {
        reports.clear();
        book.processOrder(std::move(order), reports);

        // Push all generated reports to the shared output queue.
        for (auto& rep : reports) {
            // We use std::move here because rep is going out of scope
            // at the end of the loop anyway — moving it avoids making
            // an unnecessary copy of the report data.
            outputQueue_.push(std::move(rep));
        }
    }
}

void ExchangeApplication::writerLoop() {
    // This function runs on a single dedicated writer thread.
    // We use one writer thread because the output file is shared.
    // One thread writing means we don't need locks on the file itself —
    // only the queue's lock is needed when orders are added or removed.

    ExecutionReport report;
    while (outputQueue_.pop(report)) {
        // The writer_ pointer calls writeReport() on whatever concrete
        // implementation was provided (e.g., CSVWriter). This lets us
        // swap writers without changing this code.
        writer_->writeReport(std::move(report));
    }
}