#define main arena_demo_main
#include "arenaAlloc.cpp"
#undef main

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory_resource>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

volatile std::uint64_t g_sink = 0;

static constexpr std::size_t kBufferSize = 128ull * 1024 * 1024;
static constexpr int kSmallN = 1'000'000;
static constexpr int kMixedN = 1'000'000;
static constexpr int kObjN   = 200'000;
static constexpr int kRepeats = 7;

static const char* NAME_SKUDA = "Skuda";
static const char* NAME_PMR   = "pmr::monotonic_buffer_resource";
static const char* NAME_NEW   = "new/delete";

template <typename F>
double bench(F&& f) {
    f();
    std::vector<double> times;
    for (int i = 0; i < kRepeats; i++) {
        auto start = Clock::now();
        f();
        auto end = Clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

struct Result {
    const char* name;
    double ms;
};

void print_table(const std::string& title, int ops, const std::vector<Result>& results) {
    std::printf("\n=== %s (%d ops) ===\n", title.c_str(), ops);
    std::printf("%-34s %12s %12s %12s\n", "allocator", "time (ms)", "ns/op", "vs Skuda");
    double skuda_ms = results[0].ms;
    for (const auto& r : results) {
        double ns_per_op = (r.ms * 1e6) / ops;
        double ratio = r.ms / skuda_ms;
        std::printf("%-34s %12.3f %12.2f %11.2fx\n", r.name, r.ms, ns_per_op, ratio);
    }
}

struct Obj {
    int v;
    explicit Obj(int x) : v(x) {}
    ~Obj() { g_sink = g_sink + v; }
};

int main() {
    std::printf("Arena benchmark (median of %d runs, 1 warmup run discarded)\n", kRepeats);

    std::unique_ptr<std::byte[]> pmr_buf(new std::byte[kBufferSize]);
    std::pmr::monotonic_buffer_resource pmr(pmr_buf.get(), kBufferSize,
                                            std::pmr::null_memory_resource());
    Arena skuda(kBufferSize);

    {
        double t_skuda = bench([&] {
            for (int i = 0; i < kSmallN; i++) {
                int* p = static_cast<int*>(skuda.allocate(sizeof(int), alignof(int)));
                *p = i;
                g_sink = g_sink + *p;
            }
            skuda.reset();
        });

        double t_pmr = bench([&] {
            for (int i = 0; i < kSmallN; i++) {
                int* p = static_cast<int*>(pmr.allocate(sizeof(int), alignof(int)));
                *p = i;
                g_sink = g_sink + *p;
            }
            pmr.release();
        });

        std::vector<int*> ptrs(kSmallN);
        double t_new = bench([&] {
            for (int i = 0; i < kSmallN; i++) {
                ptrs[i] = new int(i);
                g_sink = g_sink + *ptrs[i];
            }
            for (int i = 0; i < kSmallN; i++) delete ptrs[i];
        });

        print_table("Small allocs: 4-byte int, then free all", kSmallN,
                    {{NAME_SKUDA, t_skuda}, {NAME_PMR, t_pmr}, {NAME_NEW, t_new}});
    }

    {
        const std::size_t sizes[] = {8, 16, 32, 64, 128};
        auto align_for = [](std::size_t s) -> std::size_t { return s >= 16 ? 16 : 8; };

        double t_skuda = bench([&] {
            for (int i = 0; i < kMixedN; i++) {
                std::size_t s = sizes[i % 5];
                auto* p = static_cast<unsigned char*>(skuda.allocate(s, align_for(s)));
                p[0] = static_cast<unsigned char>(i);
                g_sink = g_sink + p[0];
            }
            skuda.reset();
        });

        double t_pmr = bench([&] {
            for (int i = 0; i < kMixedN; i++) {
                std::size_t s = sizes[i % 5];
                auto* p = static_cast<unsigned char*>(pmr.allocate(s, align_for(s)));
                p[0] = static_cast<unsigned char>(i);
                g_sink = g_sink + p[0];
            }
            pmr.release();
        });

        std::vector<unsigned char*> ptrs(kMixedN);
        double t_new = bench([&] {
            for (int i = 0; i < kMixedN; i++) {
                std::size_t s = sizes[i % 5];
                ptrs[i] = static_cast<unsigned char*>(::operator new(s));
                ptrs[i][0] = static_cast<unsigned char>(i);
                g_sink = g_sink + ptrs[i][0];
            }
            for (int i = 0; i < kMixedN; i++) ::operator delete(ptrs[i]);
        });

        print_table("Mixed sizes (8-128 B) and alignments (8/16)", kMixedN,
                    {{NAME_SKUDA, t_skuda}, {NAME_PMR, t_pmr}, {NAME_NEW, t_new}});
    }

    {
        double t_skuda = bench([&] {
            for (int i = 0; i < kObjN; i++) {
                Obj* o = skuda.create<Obj>(i);
                g_sink = g_sink + o->v;
            }
            skuda.reset();
        });

        double t_pmr_nodtor = bench([&] {
            for (int i = 0; i < kObjN; i++) {
                void* mem = pmr.allocate(sizeof(Obj), alignof(Obj));
                Obj* o = std::construct_at(static_cast<Obj*>(mem), i);
                g_sink = g_sink + o->v;
            }
            pmr.release();
        });

        std::vector<Obj*> tracked(kObjN);
        double t_pmr_dtor = bench([&] {
            for (int i = 0; i < kObjN; i++) {
                void* mem = pmr.allocate(sizeof(Obj), alignof(Obj));
                tracked[i] = std::construct_at(static_cast<Obj*>(mem), i);
                g_sink = g_sink + tracked[i]->v;
            }
            for (int i = kObjN - 1; i >= 0; i--) std::destroy_at(tracked[i]);
            pmr.release();
        });

        std::vector<Obj*> heap(kObjN);
        double t_new = bench([&] {
            for (int i = 0; i < kObjN; i++) {
                heap[i] = new Obj(i);
                g_sink = g_sink + heap[i]->v;
            }
            for (int i = kObjN - 1; i >= 0; i--) delete heap[i];
        });

        print_table("Non-trivial objects (with destructor)", kObjN,
                    {{NAME_SKUDA, t_skuda},
                     {"pmr (destructors NOT run)", t_pmr_nodtor},
                     {"pmr + manual destroy", t_pmr_dtor},
                     {NAME_NEW, t_new}});
    }

    std::printf("\n(sink = %llu, only stops the compiler deleting the loops)\n",
                static_cast<unsigned long long>(g_sink));
    return 0;
}