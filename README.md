# Arena Allocator (C++20)

A custom memory allocator built from scratch to understand how allocators
work under the hood, instead of relying on `new`/`malloc`.

An arena allocator ("bump allocator") grabs one large block of memory up
front and hands out chunks of it by just moving a pointer/offset forward —
no per-allocation bookkeeping, no searching for free blocks. The tradeoff:
individual allocations can't be freed one at a time — the whole arena is
freed/reset together. This makes arenas extremely fast for cases where a
batch of objects share the same lifetime (e.g. one game frame, one parsing
pass, one request/response cycle).

## Features

- **Alignment-correct allocation** — padding is computed from the real
  memory address (`buffer + offset`), not just a running offset, so it
  stays correct even for over-aligned types (e.g. SIMD types).
- **Overflow-safe bounds checking** — bounds checks are ordered to avoid
  `std::size_t` unsigned wraparound.
- **`create<T>(args...)`** — combines allocation and construction in one
  call, equivalent to `new T(args...)` but backed by the arena.
- **Automatic destructor tracking** — non-trivial types are registered at
  construction time via a type-erased finalizer (object pointer + function
  pointer). `reset()` and the destructor walk this list in reverse
  construction order and call each destructor before reclaiming memory.
  Trivially destructible types (like `int`) pay zero runtime cost for this,
  since the check happens at compile time.
- **O(1) bulk reset** via `reset()`, plus `used()` / `remaining_capacity()`
  for introspection.

## Example

```cpp
Arena arena(1024);

Player* p1 = arena.create<Player>(100);
Player* p2 = arena.create<Player>(50);

std::cout << p1->health << '\n';  // 100
std::cout << p2->health << '\n';  // 50

arena.reset();  // ~Player() runs for p2, then p1, then offset resets to 0
```

## Build

Requires a C++20 compiler (uses `std::construct_at`).

```bash
g++ -std=c++20 arenaAlloc.cpp -o arenaAlloc
./arenaAlloc
```

## Design notes

- Alignment is computed from the buffer's real runtime address, not from
  `offset` alone — a naive `offset % alignment` approach silently assumes
  the buffer itself starts at an aligned address, which isn't guaranteed.
- Destructor tracking uses **type erasure**: a capture-less lambda written
  inside `create<T>()` (where `T` is known) converts implicitly to a plain
  `void(*)(void*)` function pointer, letting a single `std::vector` store
  finalizers for many different types without the `Arena` class itself
  needing to be templated on every type it might store.

## Benchmarks

Compared against `std::pmr::monotonic_buffer_resource` (the standard
library's arena allocator, configured with a fixed buffer and
`null_memory_resource` as upstream so it cannot fall back to the heap) and
plain `new`/`delete` as a baseline. Each result is the median of 7 runs
after one discarded warmup run. Measured on Windows, built with
`g++ -std=c++20 -O2`.

```bash
g++ -std=c++20 -O2 benchmark.cpp -o benchmark
./benchmark
```

**Small allocations** — 1,000,000 × 4-byte `int`, then free all

| Allocator                        | Time (ms) | ns/op | vs Skuda |
|----------------------------------|----------:|------:|---------:|
| Skuda                            |     2.409 |  2.41 |    1.00x |
| pmr::monotonic_buffer_resource   |     2.373 |  2.37 |    0.99x |
| new/delete                       |    73.728 | 73.73 |   30.61x |

**Mixed sizes (8–128 B) and alignments (8/16)** — 1,000,000 allocations

| Allocator                        | Time (ms) | ns/op  | vs Skuda |
|----------------------------------|----------:|-------:|---------:|
| Skuda                            |    13.836 |  13.84 |    1.00x |
| pmr::monotonic_buffer_resource   |    19.710 |  19.71 |    1.42x |
| new/delete                       |   190.337 | 190.34 |   13.76x |

**Non-trivial objects (with destructor)** — 200,000 objects

| Allocator                        | Time (ms) | ns/op | vs Skuda |
|----------------------------------|----------:|------:|---------:|
| Skuda (destructors run on reset) |     1.219 |  6.09 |    1.00x |
| pmr (destructors NOT run)        |     0.598 |  2.99 |    0.49x |
| pmr + manual destroy             |     0.685 |  3.42 |    0.56x |
| new/delete                       |    13.668 | 68.34 |   11.22x |

**Notes**

- Skuda matches `pmr` on simple allocations and is faster on mixed sizes,
  partly because its `allocate` is a non-virtual, inlinable call and it
  does not grow on exhaustion (it returns `nullptr` when full).
- For objects with destructors, Skuda is slower than `pmr` with manual
  destruction because each `create<T>` records a finalizer in a
  `std::vector`. In exchange, destructors run automatically on `reset()`.
- Results are from a single machine and workload (allocate many, free all
  at once, single-threaded) — the best case for arena allocators. The
  `new/delete` gap in particular reflects the Windows default heap and
  should not be read as a universal figure.