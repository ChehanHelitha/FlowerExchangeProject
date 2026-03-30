# 🌸 Flower Exchange — High-Performance C++ Matching Engine

<div align="center">

![C++17](https://img.shields.io/badge/C++-17-blue?style=for-the-badge&logo=cplusplus)
![CMake](https://img.shields.io/badge/CMake-3.14+-green?style=for-the-badge&logo=cmake)
![Python](https://img.shields.io/badge/Python-3.10+-yellow?style=for-the-badge&logo=python)
![Flask](https://img.shields.io/badge/Flask-3.0+-lightgrey?style=for-the-badge&logo=flask)
![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey?style=for-the-badge)
![LSEG](https://img.shields.io/badge/LSEG-C++%20Workshop-orange?style=for-the-badge)

**A production-grade, multi-threaded order matching engine built in modern C++17.**  
Simulates a real financial exchange for five flower instruments with full Price-Time Priority matching.  
Includes a web-based Trader Application frontend for interactive order submission and report visualisation.

[Features](#-features) • [Architecture](#-architecture) • [Build](#-build--run) • [Frontend](#-trader-application--frontend) • [Bugfix](#-bugfix--pfill-remainder-new-report) • [Testing](#-testing-with-sample-data) • [Design](#-design-decisions)

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
| **Web Trader Application** | Flask + HTML/JS/TailwindCSS frontend for interactive order submission |

---

## 🏗 Architecture

### C++ Engine — System Overview

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

### Full System Architecture (with Frontend)

```
┌─────────────────────────────────────────────────────────────────┐
│                    Web Browser (Trader Terminal)                 │
│   Drag & drop orders.csv  →  Preview table  →  Submit button   │
│   ←  Colour-coded execution report table  ←  Download button   │
└───────────────────────────┬─────────────────────────────────────┘
                            │  HTTP  (multipart upload / JSON)
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│               frontend/app.py  (Flask Bridge Server)            │
│   POST /api/process  →  save orders.csv  →  subprocess.run()   │
│   ←  parse execution_rep.csv  ←  return JSON                   │
│   GET  /api/download →  stream execution_rep.csv               │
└───────────────────────────┬─────────────────────────────────────┘
                            │  filesystem  (orders.csv / execution_rep.csv)
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│            build/FlowerExchange  (C++ Matching Engine)          │
│   CSVReader → OrderValidator → 5×OrderBook → CSVWriter          │
└─────────────────────────────────────────────────────────────────┘
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
          No               Yes (aggressive)
          │                 │
          ▼                 ▼
      New (passive)    Full qty matched?
    added to book        /         \
                       Yes          No
                        │            │
                        ▼            ▼
                      Fill         PFill
                               (remainder rests in
                                book silently —
                                NO second New report)
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
├── CMakeLists.txt                  # C++ build configuration
├── README.md                       # This file
├── requirements.txt                # C++ toolchain reference
├── orders.csv                      # Default sample input
├── sample1_orders.csv              # Test: passive New
├── sample2_orders.csv              # Test: no match
├── sample3_orders.csv              # Test: full Fill
├── sample4_orders.csv              # Test: partial PFill
├── sample5_orders.csv              # Test: passive price rule
├── sample6_orders.csv              # Test: multi-instrument
├── sample7_orders.csv              # Test: all rejection types
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
├── src/
│   ├── main.cpp                    # Entry point
│   ├── OrderValidator.cpp
│   ├── OrderBook.cpp               
│   ├── CSVReader.cpp
│   ├── CSVWriter.cpp
│   └── ExchangeApplication.cpp
│
└── frontend/                       # Web-based Trader Application
    ├── app.py                      # Flask bridge server
    ├── requirements.txt            # Python dependencies (Flask only)
    ├── .gitignore
    ├── exchange_data/              # Runtime: uploaded & generated CSVs (git-ignored)
    └── templates/
        └── index.html              # Complete SPA — dark-mode trading dashboard
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

### Run (CLI — direct)

```bash
# Default paths (orders.csv → execution_rep.csv in current directory)
./Release/FlowerExchange

# Explicit input/output paths
./Release/FlowerExchange orders.csv execution_rep.csv
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

## 🖥 Trader Application — Frontend

The `frontend/` directory contains a lightweight web interface that wraps the C++ engine, providing a visual trading dashboard for interactive order submission and execution report review. The C++ backend remains the primary deliverable; the frontend is a complementary interface layer.

### Frontend Features

| Feature | Detail |
|---|---|
| **Drag & Drop Upload** | Drop zone with hover animation; falls back to click-to-select |
| **Pre-execution Preview** | PapaParse renders `orders.csv` in a styled table before submission |
| **Colour-coded Reports** | Blue = New · Green = Fill · Yellow = PFill · Red = Reject |
| **Session Statistics** | Live counts of orders, fills, rejections, and engine runtime (ms) |
| **Instrument Breakdown** | Per-flower proportion bar chart in the stats panel |
| **Engine Log** | Raw C++ stdout/stderr shown in a terminal-style box |
| **Download Button** | Streams `execution_rep.csv` directly from Flask |
| **Server Status Indicator** | Polls `/api/status` every 10 s — confirms binary is found |
| **Dark-mode Trading UI** | Space Mono + Syne fonts, cyan accent, animated grid background |

### Setup & Run

```bash
# 1 — Compile the C++ engine first (see Build section above)

# 2 — Enter the frontend directory
cd frontend

# 3 — Create and activate a virtual environment
python3 -m venv venv
source venv/bin/activate        # Windows: venv\Scripts\activate

# 4 — Install dependencies (Flask only)
pip install -r requirements.txt

# 5 — Start the server
python app.py
```

Open **http://127.0.0.1:5000** in your browser.

```
============================================================
  Flower Exchange — Trader Application Server
============================================================
  C++ Binary   : ../build/FlowerExchange
  Binary exists: True
  Data dir     : frontend/exchange_data/
  Open         : http://127.0.0.1:5000
============================================================
```

### API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/` | Serves the Trader Application SPA |
| `POST` | `/api/process` | Accepts `orders.csv` upload, runs C++ engine, returns JSON |
| `GET` | `/api/download` | Streams the latest `execution_rep.csv` as a file download |
| `GET` | `/api/status` | Health check — confirms server is up and binary exists |

---

## 🧪 Testing with Sample Data

Seven sample CSV files cover every engine behaviour. Run each with:

```bash
./build/FlowerExchange sample1_orders.csv out.csv && cat out.csv
```

### Test 1 — Passive Order (New)
```csv
ClientOrderID,Instrument,Side,Quantity,Price
aa13,Rose,2,100,55.00
```
**Expected:** `ord1, aa13, Rose, 2, New, 100, 55.00` — order rests in book, no match.

---

### Test 2 — No Match (Buy price below best ask)
```csv
aa13,Rose,2,100,55.00
aa14,Rose,2,100,45.00
aa15,Rose,1,100,35.00
```
**Expected:** All three orders `New` — buy price 35.00 is below cheapest sell 45.00.

---

### Test 3 — Full Fill
```csv
aa13,Rose,2,100,55.00
aa14,Rose,2,100,45.00
aa15,Rose,1,100,45.00
```
**Expected:** aa15 buy @ 45.00 matches aa14 sell @ 45.00 → both `Fill`. aa13 stays `New`.

---

### Test 4 — Partial Fill (PFill)
```csv
aa13,Rose,2,100,55.00
aa14,Rose,2,100,45.00
aa15,Rose,1,200,45.00
```
**Expected:** aa15 buy 200 vs aa14 sell 100 → aa15 `PFill 100`, aa14 `Fill 100`. Remainder of aa15 rests in book — **no second New report** (bugfix verified here).

---

### Test 5 — Passive Price Rule
```csv
aa13,Rose,1,100,55.00
aa14,Rose,1,100,65.00
aa15,Rose,2,300,1.00
```
**Expected:** Sell @ 1.00 is aggressive; execution price = passive buy price. aa14 filled @ 65.00, aa13 filled @ 55.00. Remaining 100 of aa15 rests at 1.00 — **no New report for the residual** (bugfix verified here).

---

### Test 6 — Multi-Instrument Parallel
Interleaved orders across Rose, Tulip, and Lavender — exercises all 5 independent order books running concurrently on separate threads.

---

### Test 7 — Input Validations (all rejection types)
```csv
aa13,,1,100,55.00         → Invalid instrument
aa14,Rose,3,100,65.00     → Invalid side
aa15,Lavender,2,101,1.00  → Invalid size
aa16,Tulip,1,100,-1.00    → Invalid price
aa17,Orchid,1,1000,-1.00  → Invalid size
```
**Expected:** All five orders `Reject` with their respective reason strings.

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
| `Invalid instrument` | Not in {Rose, Lavender, Lotus, Tulip, Orchid} |
| `Invalid side` | Side not 1 or 2 |
| `Invalid price` | Price ≤ 0.0 |
| `Invalid size` | Quantity < 10, > 1000, or not a multiple of 10 |

---

## 🎨 Design Decisions

### Why `std::map` + `std::queue` for the Order Book?

```
std::map<double, std::queue<Order>, std::greater<double>>  ← Buy side

• map             → O(log n) insert/lookup by price; always sorted
• greater<double> → begin() = highest price (best bid) automatically
• queue           → FIFO within price level = time priority enforced
• No manual sorting ever needed
```

### Why Move Semantics throughout?

Every `std::string` in `Order` and `ExecutionReport` is heap-allocated. Without `std::move`, passing one order through the full pipeline (reader → queue → book → output queue → writer) would copy all string fields at each transfer point. With `std::move`, each string is transferred in O(1) — a pointer swap only, with zero additional heap allocations.

### Why 5 Separate Worker Threads?

Each instrument's order book is completely independent state. Running them in parallel means a large Rose order book never delays Tulip processing. The key insight: **no mutex is needed on any order book** because each book is exclusively owned by one thread — contention only occurs at the single shared output queue.

### Why `unique_ptr<IWriter>` instead of `CSVWriter` directly?

```cpp
// ExchangeApplication only knows this abstraction:
std::unique_ptr<IWriter> writer_;

// Any output backend can be injected at construction:
auto engine = ExchangeApplication(std::make_unique<CSVWriter>("out.csv"));
auto engine = ExchangeApplication(std::make_unique<DatabaseWriter>(conn));
auto engine = ExchangeApplication(std::make_unique<NetworkWriter>(socket));
```

Zero changes to the engine when the output target changes. This is the Dependency Inversion Principle (SOLID).

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

| Technique | Impact |
|---|---|
| 5 parallel order books | ~5× throughput for mixed-instrument workloads |
| `std::move` everywhere | Near-zero copy overhead per order |
| Pre-allocated `vector<ExecutionReport>` in worker loop | Zero heap allocation in the hot path |
| `std::map` O(log n) matching | Sub-microsecond match for typical book depths |
| Single writer thread | Serialized, lock-free file writes |
| Buffered `ofstream` (no per-line flush) | Bulk I/O — flush only on close |

To benchmark with a large synthetic file:

```bash
python3 -c "
import random
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
