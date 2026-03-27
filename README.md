# 🌸 Flower Exchange — High-Performance C++ Matching Engine

<div align="center">

![C++17](https://img.shields.io/badge/C++-17-blue?style=for-the-badge&logo=cplusplus)
![CMake](https://img.shields.io/badge/CMake-3.14+-green?style=for-the-badge&logo=cmake)
![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey?style=for-the-badge)
![LSEG](https://img.shields.io/badge/LSEG-C++%20Workshop-orange?style=for-the-badge)

**A production-grade, multi-threaded order matching engine built in modern C++17.**  
Simulates a real financial exchange for five flower instruments with full Price-Time Priority matching.

[Features](#-features) • [Architecture](#-architecture) • [Build](#-build--run) • [Testing](#-testing-with-sample-data) • [Design](#-design-decisions)

</div>

---

## 📖 Overview

The **Flower Exchange** is a fully functional limit order book matching engine that processes buy and sell orders for five flower instruments — `Rose`, `Lavender`, `Lotus`, `Tulip`, and `Orchid`.

It reads orders from `orders.csv`, runs them through a multi-threaded matching pipeline, and produces `execution_rep.csv` containing one or more execution reports per order.

```
orders.csv  ──►  Exchange Engine  ──►  execution_rep.csv
               (5 parallel books)
```

This project was built as part of the **LSEG C++ Workshop Series** and demonstrates:
- Modern C++17 features (move semantics, smart pointers, lambdas)
- Multi-threaded Producer-Consumer architecture
- SOLID design principles
- STL container selection for performance

---

## ✨ Features

| Feature | Detail |
|---|---|
| **5 Independent Order Books** | One per instrument — zero cross-instrument lock contention |
| **Price-Time Priority** | `std::map` + `std::queue` enforces best price → oldest order |
| **Full & Partial Fills** | Handles Fill, PFill across multiple passive orders |
| **Input Validation** | Rejects invalid instrument, side, price, quantity with reason |
| **Multi-threaded Pipeline** | 7 threads: 1 producer + 5 workers + 1 writer |
| **Lock-free Order Books** | Each book owned exclusively by one thread — no book-level mutex |
| **Move Semantics** | Orders and reports transferred between threads with zero copies |
| **Virtual Writer Interface** | `IWriter` abstraction — swap CSV for DB/network with no engine changes |
| **Transaction Timestamps** | Every report stamped `YYYYMMDD-HHMMSS.sss` |

---

## 🏗 Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                         MAIN THREAD (Producer)                      │
│                                                                     │
│   orders.csv ──► CSVReader ──► OrderValidator ──► Route by Instr.  │
│                                      │                              │
│                               Rejected orders ──────────────────┐  │
└──────────────────────────────────────┼─────────────────────────┼──┘
                                       │ valid orders             │
               ┌───────────────────────▼─────────────────────┐   │
               │         5 × ThreadSafeQueue<Order>           │   │
               │  [Rose] [Lavender] [Lotus] [Tulip] [Orchid]  │   │
               └──┬───────┬──────────┬──────┬──────┬──────────┘   │
                  │       │          │      │      │               │
     ┌────────────▼─┐ ┌───▼──┐ ┌────▼─┐ ┌──▼──┐ ┌▼──────┐        │
     │  Rose Worker │ │ Lav. │ │Lotus │ │Tulip│ │Orchid │        │
     │  std::thread │ │ thr. │ │ thr. │ │ thr.│ │ thr.  │        │
     │              │ │      │ │      │ │     │ │       │        │
     │  OrderBook   │ │  OB  │ │  OB  │ │ OB  │ │  OB   │        │
     │  Buy: map↓   │ │      │ │      │ │     │ │       │        │
     │  Sell: map↑  │ │      │ │      │ │     │ │       │        │
     └──────┬───────┘ └──┬───┘ └──┬───┘ └──┬──┘ └──┬────┘        │
            │            │        │         │        │             │
            └────────────┴────────┴─────────┴────────┘            │
                                  │                                │
                         ┌────────▼──────────────────────────┐     │
                         │  ThreadSafeQueue<ExecutionReport>  │◄───┘
                         │  (single shared output queue)      │
                         └────────────────┬───────────────────┘
                                          │
                              ┌───────────▼──────────┐
                              │    Writer Thread      │
                              │    CSVWriter          │
                              │  (implements IWriter) │
                              └───────────┬───────────┘
                                          │
                                 execution_rep.csv
```

### Order Book Internals — Price-Time Priority

```
BUY SIDE (std::map, std::greater → highest price first)
┌─────────────────────────────────────────────────────────┐
│  Price   │  Queue (FIFO — oldest order matched first)   │
│──────────│─────────────────────────────────────────────│
│  65.00   │  [ord2: qty=100]                            │  ← best bid
│  55.00   │  [ord1: qty=100]                            │
│  45.00   │  [ord5: qty=200] → [ord7: qty=100]          │
└─────────────────────────────────────────────────────────┘

SELL SIDE (std::map, default → lowest price first)
┌─────────────────────────────────────────────────────────┐
│  Price   │  Queue (FIFO — oldest order matched first)   │
│──────────│─────────────────────────────────────────────│
│  45.00   │  [ord3: qty=100]                            │  ← best ask
│  55.00   │  [ord4: qty=100]                            │
│  70.00   │  [ord6: qty=50]                             │
└─────────────────────────────────────────────────────────┘

Matching condition:
  Incoming BUY  price >= best SELL price  →  EXECUTE at sell price
  Incoming SELL price <= best BUY  price  →  EXECUTE at buy  price
```

### Execution Status Flow

```
Incoming Order
      │
      ▼
 Validation
   Pass?
   /    \
 No      Yes
  │        │
  ▼        ▼
Reject    Can match against book?
           /              \
          No               Yes
          │                 │
          ▼                 ▼
      New (passive)    Full qty matched?
    added to book        /         \
                       Yes          No
                        │            │
                        ▼            ▼
                      Fill         PFill
                               (partial — remainder
                                stays in book as New)
```

### Class Diagram

```
«interface»
IWriter
+ writeReport(ExecutionReport) = 0    ◄─── CSVWriter
+ ~IWriter()                                + writeReport() override
                                            + writeHeader()
                                            - ofstream outputFile_

ExchangeApplication
+ run(inputFile)
- orderBooks_       : map<string, unique_ptr<OrderBook>>
- instrumentQueues_ : map<string, unique_ptr<ThreadSafeQueue<Order>>>
- outputQueue_      : ThreadSafeQueue<ExecutionReport>
- writer_           : unique_ptr<IWriter>
- workerThreads_    : vector<thread>
- writerThread_     : thread
      │
      │ composes
      ├──────────────────────────────┐
      ▼                              ▼
  OrderBook                    ThreadSafeQueue<T>
  + processOrder()             + push(T&&)
  - buyBook_  : map<double,    + pop(T&) → bool
                queue<Order>,  + setDone()
                greater>       - mutex_
  - sellBook_ : map<double,    - condVar_
                queue<Order>>  - queue_

  CSVReader              OrderValidator
  + readOrders(cb)       + validate(Order) → string
  - splitLine()          + makeRejected()  → ExecReport
  - trim()
```

---

## 📁 Project Structure

```
FlowerExchangeProject/
├── CMakeLists.txt                  # Build configuration
├── README.md                       # This file
├── requirements.txt                # Tool/dependency reference
├── orders.csv                      # Default sample input
│
├── include/
│   └── Exchange/
│       ├── Order.h                 # POD struct: incoming order
│       ├── ExecutionReport.h       # POD struct: output report + ExecStatus enum
│       ├── IWriter.h               # Pure virtual writer interface
│       ├── Utils.h                 # Timestamp, atomic ID generator
│       ├── ThreadSafeQueue.h       # Generic lock-based FIFO queue
│       ├── OrderValidator.h        # Validation rules
│       ├── OrderBook.h             # Price-Time priority matching engine
│       ├── CSVReader.h             # CSV ingestion
│       ├── CSVWriter.h             # CSV output (implements IWriter)
│       └── ExchangeApplication.h  # Orchestrator + thread management
│
└── src/
    ├── main.cpp                    # Entry point
    ├── OrderValidator.cpp
    ├── OrderBook.cpp
    ├── CSVReader.cpp
    ├── CSVWriter.cpp
    └── ExchangeApplication.cpp
```

---

## 🔧 Build & Run

### Prerequisites

| Tool | Minimum Version |
|---|---|
| C++ Compiler (GCC / Clang / MSVC) | GCC 7+ / Clang 5+ / MSVC 2017+ |
| CMake | 3.14+ |
| POSIX Threads | (pthreads — Linux/macOS built-in) |

### Build (Linux / macOS)

```bash
# Clone the repository
git clone https://github.com/<your-username>/FlowerExchangeProject.git
cd FlowerExchangeProject

# Create build directory and configure
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Compile with all available cores
cmake --build . -j$(nproc)
```

### Build (Windows — MSVC)

```powershell
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Run

```bash
# From the build directory (uses orders.csv and execution_rep.csv by default)
./FlowerExchange

# Custom input/output paths
./FlowerExchange ../orders.csv ../my_output.csv

# Or from project root
./build/FlowerExchange orders.csv execution_rep.csv
```

### Expected Console Output

```
[Exchange] Starting Flower Exchange Matching Engine
[Exchange] Input  : orders.csv
[Exchange] Output : execution_rep.csv
[Exchange] Processing complete.
[Exchange] Completed in 2.34 ms
```

---

## 🧪 Testing with Sample Data

Seven sample test cases are provided in the `test/` directory, each exercising a different engine behaviour. See [Testing Guide](#) for expected outputs.

### Test 1 — Passive Order (New)
`test/sample1_orders.csv` → order enters the book, no match possible

```csv
ClientOrderID,Instrument,Side,Quantity,Price
aa13,Rose,2,100,55.00
```
**Expected output:** `ord1, aa13, Rose, 2, New, 100, 55.00`

---

### Test 2 — No Match (Buy price below best ask)
`test/sample2_orders.csv`

```csv
ClientOrderID,Instrument,Side,Quantity,Price
aa13,Rose,2,100,55.00
aa14,Rose,2,100,45.00
aa15,Rose,1,100,35.00
```
**Expected:** All three orders become `New` — buy price 35.00 < cheapest sell 45.00.

---

### Test 3 — Full Fill
`test/sample3_orders.csv`

```csv
ClientOrderID,Instrument,Side,Quantity,Price
aa13,Rose,2,100,55.00
aa14,Rose,2,100,45.00
aa15,Rose,1,100,45.00
```
**Expected:** aa15 buy 45.00 matches aa14 sell 45.00 → both `Fill`. aa13 remains `New`.

---

### Test 4 — Partial Fill (PFill)
`test/sample4_orders.csv`

```csv
ClientOrderID,Instrument,Side,Quantity,Price
aa13,Rose,2,100,55.00
aa14,Rose,2,100,45.00
aa15,Rose,1,200,45.00
```
**Expected:** aa15 buy 200 matches aa14 sell 100 → aa15 gets `PFill 100`, aa14 gets `Fill 100`. Remainder of aa15 (100) stays in book.

---

### Test 5 — Fill with Passive Price Rule
`test/sample5_orders.csv`

```csv
ClientOrderID,Instrument,Side,Quantity,Price
aa13,Rose,1,100,55.00
aa14,Rose,1,100,65.00
aa15,Rose,2,300,1.00
```
**Expected:** sell @ 1.00 is aggressive. Execution price = passive buy price. aa14 filled @ 65.00, aa13 filled @ 55.00.

---

### Test 6 — Multi-Instrument Parallel
`test/sample6_orders.csv` — interleaved orders across Rose and Tulip

---

### Test 7 — Input Validations (Rejections)
`test/sample7_orders.csv`

```csv
ClientOrderID,Instrument,Side,Quantity,Price
aa13,,1,100,55.00
aa14,Rose,3,100,65.00
aa15,Lavender,2,101,1.00
aa16,Tulip,1,100,-1.00
aa17,Orchid,1,1000,-1.00
```
**Expected:** All five orders rejected with reasons: `Invalid instrument`, `Invalid side`, `Invalid size`, `Invalid price`, `Invalid size`.

---

## 📋 Input / Output Format

### Input — `orders.csv`

| Column | Type | Valid Values | Notes |
|---|---|---|---|
| `ClientOrderID` | String | Max 7 alphanumeric chars | Unique ID from trader |
| `Instrument` | String | Rose, Lavender, Lotus, Tulip, Orchid | Case-sensitive |
| `Side` | Int | `1` = Buy, `2` = Sell | |
| `Quantity` | Int | 10–1000, multiples of 10 | |
| `Price` | Double | > 0.0 | Limit price |

### Output — `execution_rep.csv`

| Column | Description |
|---|---|
| `Order ID` | System-generated sequential ID (`ord1`, `ord2`, …) |
| `Client Order ID` | Echo of input ClientOrderID |
| `Instrument` | Flower type |
| `Side` | 1 = Buy, 2 = Sell |
| `Exec Status` | `New` / `Reject` / `Fill` / `PFill` |
| `Quantity` | Quantity of this execution event |
| `Price` | Execution price (always the passive order's price) |
| `Reason` | Rejection reason (blank for non-rejected) |
| `Transaction Time` | `YYYYMMDD-HHMMSS.sss` |

### Rejection Reasons

| Reason | Trigger |
|---|---|
| `Invalid instrument` | Instrument not in {Rose, Lavender, Lotus, Tulip, Orchid} |
| `Invalid side` | Side not 1 or 2 |
| `Invalid price` | Price ≤ 0.0 |
| `Invalid size` | Quantity < 10, > 1000, or not a multiple of 10 |

---

## 🎨 Design Decisions

### Why `std::map` + `std::queue` for the Order Book?

```
std::map<double, std::queue<Order>, std::greater<double>>  ← Buy side

• map  → O(log n) insert/lookup by price; always sorted
• greater<double> → begin() = highest price (best bid) automatically
• queue → FIFO within price level = time priority
• No manual sorting ever needed
```

### Why Move Semantics throughout?

Every `std::string` in `Order` and `ExecutionReport` is heap-allocated. Without `std::move`, passing one order through the pipeline (reader → queue → book → output queue → writer) would copy 5 strings **4 times** = 20 allocations per order. With `std::move`, each string is transferred in O(1) — pointer swap only.

### Why 5 Separate Worker Threads?

Each instrument's order book is completely independent state. Running them in parallel means a large Rose order book doesn't delay Tulip processing. The key insight: **no mutex is needed on any order book** because each book is exclusively owned by one thread.

### Why `unique_ptr<IWriter>` instead of `CSVWriter` directly?

```cpp
// ExchangeApplication only knows this:
std::unique_ptr<IWriter> writer_;

// So you can inject any backend at construction:
auto engine = ExchangeApplication(std::make_unique<CSVWriter>("out.csv"));
auto engine = ExchangeApplication(std::make_unique<DatabaseWriter>(conn));
auto engine = ExchangeApplication(std::make_unique<NetworkWriter>(socket));
```

Zero changes to the engine when the output target changes. This is the Dependency Inversion Principle.

---

## 🔑 Key C++ Features Used

| Feature | Where | Purpose |
|---|---|---|
| `struct` for POD | `Order.h`, `ExecutionReport.h` | Passive data; signals "no invariants" |
| `std::move` | Queue pushes, book inserts | O(1) transfer of heap strings |
| `std::unique_ptr` | Books, writer, threads | Express ownership, auto-cleanup |
| `std::map<K,V,std::greater>` | `OrderBook` buy side | Descending sort — best bid at `begin()` |
| `std::queue<Order>` | Per price-level | FIFO time priority |
| `virtual` / `= 0` | `IWriter` | Dependency inversion, runtime polymorphism |
| `override` | `CSVWriter` | Compile-time verification of override |
| `std::atomic<int>` | `Utils.h` | Lock-free sequential order ID generation |
| `std::mutex` + `std::condition_variable` | `ThreadSafeQueue` | Producer-consumer synchronization |
| `std::thread` | `ExchangeApplication` | 5 workers + 1 writer = 6 background threads |
| `std::make_unique` | `ExchangeApplication` | Exception-safe heap allocation |
| `try/catch` | `CSVReader` | Parse errors → Rejected report, not crash |
| Lambda captures | `ExchangeApplication::run` | Thread bodies + CSV callback |
| Template class | `ThreadSafeQueue<T>` | Reused for both `Order` and `ExecutionReport` |

---

## 📊 Performance

The engine is designed to process large order files efficiently:

| Technique | Impact |
|---|---|
| 5 parallel order books | ~5× throughput for mixed-instrument workloads |
| `std::move` everywhere | Near-zero copy overhead per order |
| Pre-allocated `vector<ExecutionReport>` in worker loop | Zero heap allocation per order in the hot path |
| `std::map` O(log n) matching | Sub-microsecond match for typical book depths |
| Single writer thread (no file mutex) | Serialized, lock-free file writes |
| Buffered `ofstream` (no per-line flush) | Bulk I/O — flush only on close |

To benchmark:
```bash
# Generate a large input
python3 -c "
import random, sys
flowers = ['Rose','Lavender','Lotus','Tulip','Orchid']
print('ClientOrderID,Instrument,Side,Quantity,Price')
for i in range(10000):
    f = random.choice(flowers)
    s = random.randint(1,2)
    q = random.randint(1,100)*10
    p = round(random.uniform(10,100),2)
    print(f'cl{i:05d},{f},{s},{q},{p}')
" > big_orders.csv

time ./build/FlowerExchange big_orders.csv /dev/null
```

---

## 🤝 Contributing

This project was developed as part of the LSEG C++ Workshop at the University of Moratuwa. For issues or questions, please open a GitHub Issue.

---

## 📄 License

This project is released under the MIT License. See `LICENSE` for details.

---

<div align="center">
Built with ❤️ for the <strong>LSEG C++ Workshop Series</strong> — University of Moratuwa
</div>
