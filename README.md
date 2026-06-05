# Micro-DNS: High-Performance Lock-Free DNS Storage Engine

Micro-DNS is a sub-microsecond, lock-free, zero-allocation DNS storage engine and IPC transport
layer written from scratch in C++20. It forgoes `std::malloc`, `std::mutex`, and standard file I/O
in favor of `mmap`, bump allocation, and C++20 atomics to achieve cross-process query resolution in
under 500 nanoseconds.

## Table of Contents

- [Performance](#performance)
- [Architecture](#architecture)
  - [Transport Layer: IPC Lock-Free Ring Buffer](#1-transport-layer-ipc-lock-free-ring-buffer)
  - [Storage Engine: Persistent Patricia Trie](#2-storage-engine-persistent-patricia-trie)
  - [Concurrency: Double-Buffered Atomic Updates](#3-concurrency-double-buffered-atomic-updates)
  - [Durability: Write-Ahead Log](#4-durability-write-ahead-log)
- [Cross-Platform Compatibility](#cross-platform-compatibility)
- [Project Structure](#project-structure)
- [Building](#building)
- [Running the Benchmark](#running-the-benchmark)
- [Running the Tests](#running-the-tests)
- [Editor Setup](#editor-setup)

---

## Performance

Measured end-to-end over IPC across two separate processes on a 1,000,000 query stress test
(Apple M-series, macOS 14, single-core client, single-core daemon):

| Metric         | Latency |
|----------------|---------|
| p50 (median)   | ~417 ns |
| p99            | ~500 ns |

The entire round-trip -- query serialization, shared-memory transfer, trie traversal, and response
-- completes in under half a microsecond with near-zero OS preemption skew.

---

## Architecture

The engine is composed of four distinct layers, each targeting a specific performance bottleneck.

### 1. Transport Layer: IPC Lock-Free Ring Buffer

Local queries travel through raw RAM rather than a loopback network socket, eliminating kernel
TCP/UDP overhead entirely.

- **Shared Memory:** `shm_open` and `mmap` create a globally addressable memory region shared
  between the daemon and any number of client processes.
- **Lock-Free Concurrency:** The ring buffer uses C++20 `std::atomic` with explicit
  `memory_order_acquire` / `memory_order_release` fences. No spinlocks, no futexes.
- **False-Sharing Prevention:** Producer and consumer indices are each padded to
  `std::hardware_destructive_interference_size` (typically 64 bytes) so they occupy separate cache
  lines and never invalidate each other's L1 entries.

### 2. Storage Engine: Persistent Patricia Trie

The database lives entirely in a memory-mapped file, bypassing the heap and the kernel page cache.

- **Memory-Mapped File:** The database is a single 100 MB file mapped directly into the process
  address space with `mmap`. Dirty pages are flushed to disk by the OS on a clean shutdown or
  crash, providing durability without explicit `write()` calls on the hot path.
- **Bump Allocator:** Nodes are placed sequentially by advancing a single integer offset stored in
  the file header. There are no free-lists, no fragmentation, and no `malloc` latency. Because
  offsets are relative to the base of the mapped region, the structure survives process restarts and
  address-space relocation.
- **Patricia Trie (Prefix Compression):** Common prefixes such as the `com.` shared by
  `api.google.com` and `api.github.com` are stored once rather than duplicated per entry. Lookups
  run in O(k) time where k is the length of the query, with no `std::string` allocations on the
  hot path.

### 3. Concurrency: Double-Buffered Atomic Updates

Allowing concurrent lock-free reads while a write mutates the trie requires that readers never
observe a partially constructed tree.

- **Two Root Offsets:** The file header holds two root offsets and a single `active_idx` atomic.
  Readers load `active_idx` with `memory_order_acquire` and traverse the corresponding tree.
- **Copy-on-Write Writes:** A write deep-copies the active tree into the inactive slot, applies the
  mutation to the copy, and then publishes it with a single `std::atomic_ref` store using
  `memory_order_release`. In-flight readers finish on the old tree uninterrupted; all subsequent
  readers see the new one.
- **No `std::mutex`:** Locks force a context switch when contended. A context switch on a modern
  kernel costs roughly 1-5 microseconds -- an order of magnitude more than the entire query budget.

### 4. Durability: Write-Ahead Log

Writes are appended to a WAL before the trie is mutated, providing crash recovery without
sacrificing read-path performance.

- **Sequential Appends Only:** The WAL is append-only, so writes are a single `write()` syscall
  followed by an fsync. Random-access I/O is never needed.
- **CRC32 Integrity:** Each WAL record carries a CRC32 checksum over its header and payload so
  corrupted or partial records written during a crash can be detected and discarded on replay.
- **Separation of Concerns:** The WAL and the mmap database are independent files. The WAL
  guarantees no operation is lost; the mmap file is the fast read index. On startup, the daemon
  replays any WAL records not yet reflected in the index.

---

## Cross-Platform Compatibility

The engine targets both Linux (x86-64) and macOS (Apple Silicon) via preprocessor guards.

- **Disk Syncing:** Linux uses `fdatasync()` for WAL flushes. macOS does not provide `fdatasync`,
  so the engine substitutes `fcntl(fd, F_FULLFSYNC)` to guarantee a physical hardware flush rather
  than a filesystem cache flush.
- **CPU Spin-Wait Backoff:** The daemon's polling loop uses an architecture-specific pause
  instruction to yield execution to the CPU pipeline without a syscall. On x86-64 this is
  `asm volatile("pause")`; on ARM64 (Apple Silicon) it is `asm volatile("yield")`.
- **Timers:** Latency measurement uses `std::chrono::high_resolution_clock` rather than the x86-only
  `__rdtscp` instruction, so benchmark output is accurate on both architectures.

---

## Project Structure

Core logic is implemented as a header-only library so the compiler can inline the entire hot path
without crossing translation-unit boundaries.

```text
micro-dns/
├── CMakeLists.txt
├── include/
│   └── microdns/
│       ├── ring_buffer.hpp     # Lock-free IPC ring buffer template
│       ├── storage.hpp         # Patricia trie, bump allocator, double-buffering
│       └── wire_protocol.hpp   # DnsQuery wire format and layout structs
├── src/
│   ├── daemon.cpp              # Database server and lock-free poll loop
│   └── client.cpp              # 1,000,000 query benchmark driver
└── tests/
    ├── ring_buffer_test.cpp    # Unit and stress tests for the ring buffer
    └── storage_test.cpp        # Trie insertion, lookup, and atomic-flip tests
```

---

## Building

### Prerequisites

- C++20-capable compiler: GCC 10+, Clang 10+, or Apple Clang 13+
- CMake 3.20+
- POSIX-compliant OS: Linux or macOS

### Build Steps

```bash
# Clone the repository
git clone https://github.com/yourusername/micro-dns.git
cd micro-dns

# Configure (generates build files and compile_commands.json)
cmake -B build -S .

# Compile
cmake --build build
```

---

## Running the Benchmark

Start the daemon and the client in two separate terminals.

**Terminal 1 -- start the server:**

```bash
./build/micro_dns_daemon
```

Expected output:
```
Creating new database file...
Waiting for queries...
```

**Terminal 2 -- run the benchmark:**

```bash
./build/micro_dns_client
```

The client fires 1,000,000 queries through the shared-memory ring buffer and prints per-query
round-trip latency statistics when finished.

---

## Running the Tests

The test suite uses GoogleTest, which CMake fetches automatically via `FetchContent`.

```bash
# Build and run all tests
cmake --build build --target micro_dns_tests
./build/micro_dns_tests
```

Or run through CTest for structured output:

```bash
cd build && ctest --output-on-failure
```

---

## Editor Setup

The project ships with a `.clangd` configuration. For clangd to resolve third-party headers such as
GoogleTest, it needs the `compile_commands.json` that CMake generates. The standard CMake configure
step above writes this file to `build/`. To point clangd at it, you may add a symlink to it using 

```bash
ln -sf build/compile_commands.json compile_commands.json
```

`.clangd`:
```yaml
CompileFlags:
  Add:
    - -std=c++20
    - -I./include/microdns
```

After updating `.clangd`, restart your language server. Most editors (VS Code, Neovim, CLion) pick
up the new configuration automatically.
