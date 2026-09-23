#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <memory>
#include <cassert>
#include <vector>
#include <type_traits>

struct Player{
    int health;

    Player(int h) : health(h){
        std::cout << "Player constructed!" << '\n';
    }

    ~Player(){
        std::cout << "Player destroyed" << '\n';
    }
};

class Arena{
private:
    std::byte* buffer;
    std::size_t capacity;
    std::size_t offset;

    struct Finalizer{
        void* object;
        void (*destroy)(void*);
    };

    std::vector<Finalizer> finalizers;

public:
    explicit Arena(std::size_t capacity)
        : buffer(new std::byte[capacity]),
          capacity(capacity),
          offset(0){}
    
    ~Arena(){
        run_finalizers();
        delete[] buffer;
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* allocate(std::size_t size, std::size_t alignment){
        //sanity check: alignment must be a power of 2
        assert(alignment > 0 && (alignment & (alignment-1)) == 0);

        std::uintptr_t current = reinterpret_cast<std::uintptr_t> (buffer + offset);
        std::uintptr_t aligned = (current + alignment-1) & ~(alignment-1);
        std::size_t padding = static_cast<std::size_t> (aligned-current);

        if(offset > capacity || padding > capacity - offset){
            return nullptr;
        }

        std::size_t remaining = capacity - offset - padding;
        if(size > remaining){
            return nullptr;
        }

        void* result = reinterpret_cast<void*> (aligned);
        offset += padding + size;
        return result;
    }

    template<typename T, typename... Args>
    T* create(Args&&... args){
        T* ptr = static_cast<T*>(
            allocate(sizeof(T), alignof(T))
        );
        if(ptr == nullptr) return nullptr;

        std::construct_at(ptr, std::forward<Args>(args)...);

        if constexpr(!std::is_trivially_destructible_v<T>){
            finalizers.push_back(Finalizer{ptr, [](void* p){ std::destroy_at(static_cast<T*> (p));
            }});
        }

        return ptr;
    }

    void reset() {
        run_finalizers();
        offset = 0;
    }

    std::size_t used() const { return offset; }
    
    std::size_t remaining_capacity() const { return capacity - offset; }

private:
    void run_finalizers(){
        for(auto it = finalizers.rbegin(); it != finalizers.rend(); it++){
            it->destroy(it->object);
        }
        finalizers.clear();
    }
};

int main(){
    Arena arena(1024);

    Player* p1 = arena.create<Player>(100);
    Player* p2 = arena.create<Player>(50);
    std::cout << "p1 health: "<< p1->health << '\n';
    std::cout << "p2 health: " << p2->health << '\n';

    std::cout << "---resetting---" << '\n';
    arena.reset();
    std::cout << "offset after reset: " << arena.used() << '\n';
}