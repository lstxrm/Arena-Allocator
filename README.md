# Arena Allocator (C++20)

A custom memory allocator built from scratch as a systems programming project.

An arena allocator ("bump allocator") grabs one large block of memory up
front and hands out chunks of it by just moving a pointer/offset forward —
no per-allocation bookkeeping, no searching for free blocks. The tradeoff:
individual allocations can't be freed one at a time — you free the whole
arena at once. This makes arenas extremely fast for cases where a batch of
objects all share the same lifetime (e.g. one game frame, one parsing pass,
one request).

## Current status

**Base version working.** Supports:
- Raw byte-buffer backed allocation (`allocate(size, alignment)`)
- Alignment-correct bump allocation (computed from the real memory address,
  not just a running offset — avoids UB on over-aligned types)
- Overflow-safe bounds checking (no unsigned wraparound)
- Manual object construction/destruction via `std::construct_at` /
  `std::destroy_at`
- `reset()`, `used()`, `remaining_capacity()`

### Example

```cpp
Arena arena(1024);
Player* p = static_cast<Player*>(arena.allocate(sizeof(Player), alignof(Player)));
if (p) {
    std::construct_at(p, 100);
    std::cout << p->health << '\n';
    std::destroy_at(p);
}
```

## Build

Requires a C++20 compiler (uses `std::construct_at`).

```bash
g++ -std=c++20 arenaAlloc.cpp -o arenaAlloc
./arenaAlloc
```
