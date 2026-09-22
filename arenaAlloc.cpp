#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <memory>
#include <cassert>

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
public:
    explicit Arena(std::size_t capacity)
        : buffer(new std::byte[capacity]),
          capacity(capacity),
          offset(0){}
    
    ~Arena(){
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
        return ptr;
    }

    void reset() { offset = 0; }

    std::size_t used() const { return offset; }
    
    std::size_t remaining_capacity() const { return capacity - offset; }
};

int main(){
    Arena arena(1024);

    Player* p = arena.create<Player>(100);
    if(p != nullptr){
        std::cout << p->health << '\n';
        std::destroy_at(p);
    }
}