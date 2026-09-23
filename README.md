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
