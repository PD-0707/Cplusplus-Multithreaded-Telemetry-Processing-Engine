# C++ Multithreaded Telemetry Processing Engine

A high-performance, concurrent telemetry-processing pipeline developed in **C++17** utilizing a **producer-consumer architecture**. The engine ingests, validates, and aggregates high-frequency multi-sensor telemetry packets using a bounded, thread-safe circular ring buffer, condition variables, atomic operations, and robust lifecycle shutdown semantics.

Designed with production-grade systems engineering principles relevant to high-throughput data processing, avionics, UAV telemetry, and distributed infrastructure (e.g., Amazon Systems Development).

---

## Key Technical Highlights

- **Thread-Safe Bounded Ring Buffer**: Template-based circular queue with zero dynamic memory allocation during steady-state processing, preventing heap fragmentation and GC/allocation pauses.
- **Deadlock-Free Graceful Shutdown**: Uses atomic `is_shutdown_` coordination and `std::optional<T>` return semantics to ensure consumer threads drain in-flight packets completely and exit without deadlocks or thread starvation.
- **Two-Phase Validation**:
  - **Fletcher-16 Checksum Verification**: Validates hardware packet transmission integrity and detects corrupted frames.
  - **Domain Envelope Bounds Checking**: Filters sensor anomalies (temperature overheat, overpressure, bus under-voltage, extreme acceleration).
- **Non-blocking Telemetry Metrics**: Atomic counters (`std::memory_order_relaxed`) and atomic compare-and-swap (`compare_exchange_weak`) for minimum/maximum pipeline latency tracking.
- **Empirical Benchmarking**: Built-in benchmarking suite testing throughput and latency scaling across multiple thread topologies (1P×1C up to 8P×4C).

---

## System Architecture

```text
  [ Sensor Producer 1 ] ──┐
  [ Sensor Producer 2 ] ──┼──► [ Bounded RingBuffer<TelemetryPacket, 4096> ] ──┬──► [ Consumer Worker 1 ] ──► [ Validator & Stats ]
  [ Sensor Producer 3 ] ──┤     (std::mutex + condition_variable)             ├──► [ Consumer Worker 2 ] ──► [ Validator & Stats ]
  [ Sensor Producer N ] ──┘     (Zero heap allocations, Head/Tail wrap)        ├──► [ Consumer Worker 3 ] ──► [ Validator & Stats ]
                                                                               └──► [ Consumer Worker M ] ──► [ Validator & Stats ]
```

### Data Pipeline Flow
1. **Producer Threads (Sensors)**: Emulate sensor nodes generating high-frequency timestamped packets (Device ID, Timestamp ns, Sequence Number, Sensor Type, Float Value, Checksum).
2. **Synchronization Channel**: Packets enter the `RingBuffer`. If the ring is full, producers block on `not_full_` without burning CPU cycles.
3. **Consumer Threads (Processing)**: Consumers block on `not_empty_`. Once data is available, they pop packets, calculate end-to-end ingestion latency, verify checksums, and check sensor thresholds.
4. **Graceful Shutdown**: When producers finish, `buffer.shutdown()` notifies all condition variables. Consumers finish draining remaining packets and terminate cleanly.

---

## Project Structure

```text
C++ TELEMETRY/
│
├── include/
│   ├── RingBuffer.hpp           # Bounded thread-safe circular buffer with shutdown signaling
│   ├── TelemetryPacket.hpp      # Packed 32-byte packet struct, Fletcher-16 CRC, mock generator
│   ├── TelemetryValidator.hpp   # Integrity checking & sensor physical threshold boundaries
│   └── StatisticsAggregator.hpp # Thread-safe statistical aggregator & latency profiling
│
├── src/
│   └── main.cpp                 # Orchestration, live simulation dashboard & benchmark harness
│
├── build.bat                    # One-click Windows build script (MSVC C++17)
├── CMakeLists.txt               # Cross-platform CMake build configuration (Linux/Windows)
└── README.md                    # System documentation and interview guide
```

---

## Concurrency Primitives Deep Dive

### 1. `std::unique_lock` vs. `std::lock_guard`
In `push()` and `pop()`, `std::unique_lock<std::mutex>` is mandatory because `std::condition_variable::wait()` needs to temporarily release the lock while placing the thread to sleep, and atomically reacquire the lock upon waking up. `std::lock_guard` does not support explicit unlock/relock.

### 2. Guarding Against Spurious Wakeups
OS schedulers can wake threads from sleep without an explicit `notify`. Both `push()` and `pop()` pass a predicate lambda to `.wait()`:
```cpp
not_empty_.wait(lock, [this]() {
    return size_ > 0 || is_shutdown_;
});
```
The thread will only resume execution when the predicate evaluates to `true`.

### 3. Graceful Shutdown & Deadlock Prevention
A common flaw in naive producer-consumer queues is that consumer threads hang forever waiting on `not_empty_` after producers finish. In this engine:
- `shutdown()` flips `is_shutdown_ = true` and fires `notify_all()`.
- `pop()` drains any remaining elements (`size_ > 0`) first.
- When `size_ == 0 && is_shutdown_`, `pop()` returns `std::nullopt`.
- Consumers cleanly break their processing loops and `join()` with zero thread leakage.

---

## Empirical Benchmark Results

Measured directly on Windows x64 (MSVC 19.29, `/O2` Optimization):

| Topology | Total Packets | Time (s) | Processing Throughput |
| :--- | :--- | :--- | :--- |
| **1 Producer × 1 Consumer** | 100,000 | 0.025 s | **3,993,227 pkts/sec (~4.0 M/s)** |
| **2 Producers × 2 Consumers** | 200,000 | 0.060 s | **3,325,330 pkts/sec (~3.3 M/s)** |
| **4 Producers × 4 Consumers** | 400,000 | 0.144 s | **2,777,126 pkts/sec (~2.8 M/s)** |
| **8 Producers × 4 Consumers** | 400,000 | 0.196 s | **2,038,003 pkts/sec (~2.0 M/s)** |

### Live Simulation Metrics (4P × 4C, Sensor Polling Interval):
- **Ingestion-to-Processing Latency**:
  - Min Latency: **0.60 µs**
  - Average Latency: **27.93 µs**
  - Max Latency: **391.30 µs**
- **Packet Integrity**:
  - Total Processed: 4,000
  - Valid Packets: 80.15%
  - Checksum Corrupted Drops: 4.80%
  - Out-of-Bounds Physical Anomalies: 15.05%

---

## How to Build and Run

### On Windows (MSVC)
Run the automated build script:
```cmd
build.bat
```
Or compile manually:
```cmd
cl.exe /std:c++17 /O2 /EHsc /W3 /Iinclude src\main.cpp /Fe:TelemetryEngine.exe
```

Run Live Simulation:
```cmd
.\TelemetryEngine.exe
```

Run Throughput Stress Benchmark:
```cmd
.\TelemetryEngine.exe --benchmark
```

### On Linux (GCC / Clang / CMake)
Using `g++`:
```bash
g++ -std=c++17 -O3 -pthread -Iinclude src/main.cpp -o TelemetryEngine
./TelemetryEngine
./TelemetryEngine --benchmark
```

Using CMake:
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
./TelemetryEngine
```

---

## Amazon Systems Development / Interview Talking Points

1. **Why Ring Buffer over `std::queue`?**
   - `std::queue` is backed by `std::deque`, which allocates chunks of memory dynamically on the heap. In low-latency systems, heap allocations trigger locks in the memory allocator and cause memory fragmentation. A pre-allocated bounded ring buffer guarantees $O(1)$ operations with zero heap churn.
2. **Cache Locality**:
   - `TelemetryPacket` is tightly packed (32 bytes), fitting cleanly into modern 64-byte L1 cache lines (2 packets per cache line), reducing cache misses under high throughput.
3. **Lock Contention Under Scaling**:
   - As observed in our benchmark, throughput scales from 4.0M pkts/sec at 1P-1C to 2.0M pkts/sec at 8P-4C. This illustrates mutex contention on a single shared lock under extreme core load, leading into discussion of next-level optimizations such as Single-Producer Single-Consumer (SPSC) lock-free ring buffers or partitioned sharded queues.

