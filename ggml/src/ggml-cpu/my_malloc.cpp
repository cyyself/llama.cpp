#include <sys/mman.h>
#include <fcntl.h>
#include <cstddef>
#include <unistd.h>
#include <set>
#include <mutex>
#include <utility>
#include <map>
#include <stdexcept>


#ifndef RESERVED_MEMORY_SIZE
#ifdef RESERVED_PHYS_BASE_ADDR
#error "RESERVED_MEMORY_SIZE not defined"
#else
#define RESERVED_MEMORY_SIZE (1024l * 1024l * 1024l) // 1 GB
#warning "RESERVED_MEMORY_SIZE not defined, using 1 GB"
#endif
#endif

bool my_malloc_initialized = false;
void *mapped_memory = nullptr;
std::map <void*, size_t> allocated_memory;
std::set < std::pair <size_t, void*> > free_memory;
std::set < std::pair <void*, size_t> > free_memory_by_addr;

static int my_malloc_init() {
    size_t map_size = RESERVED_MEMORY_SIZE;
#ifdef RESERVED_PHYS_BASE_ADDR
    int fd = open("/dev/mem", O_RDWR);
    if (fd < 0) {
        throw std::runtime_error("Unable to open /dev/mem");
        return -1;
    }
    mapped_memory = mmap(nullptr, RESERVED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base_addr);
#else
    mapped_memory = mmap(nullptr, RESERVED_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    fprintf (stderr, "mapped_memory = %p\n", mapped_memory);
#endif
    if (mapped_memory == MAP_FAILED) {
#ifdef RESERVED_PHYS_BASE_ADDR
        close(fd);
#endif
        throw std::runtime_error("Unable to mmap");
        return -1;
    }
    free_memory.insert(std::make_pair(map_size, mapped_memory));
    free_memory_by_addr.insert(std::make_pair(mapped_memory, map_size));
    my_malloc_initialized = true;
    return 0;
}

std::mutex malloc_mutex;

extern "C" void* my_malloc(size_t size) {
    if (size == 0) return nullptr;
    std::lock_guard<std::mutex> lock(malloc_mutex);
    if (! my_malloc_initialized) {
        my_malloc_init();
    }
    auto it = free_memory.lower_bound(std::make_pair(size, nullptr));
    if (it == free_memory.end()) {
        return nullptr;
    }
    void *addr = it->second;
    size_t block_size = it->first;
    if (block_size > size) {
        // Split the block
        free_memory.erase(it);
        free_memory_by_addr.erase(std::make_pair(addr, block_size));
        void *new_addr = reinterpret_cast<char*>(addr) + size;
        size_t new_size = block_size - size;
        free_memory.insert(std::make_pair(new_size, new_addr));
        free_memory_by_addr.insert(std::make_pair(new_addr, new_size));
    } else {
        // Use the whole block
        free_memory.erase(it);
        free_memory_by_addr.erase(std::make_pair(addr, block_size));
    }
    allocated_memory.insert(std::make_pair(addr, size));
    return addr;
}

extern "C" void my_free(void *ptr) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(malloc_mutex);
    auto it = allocated_memory.find(ptr);
    if (it == allocated_memory.end())
        throw std::runtime_error("Pointer not found in allocated memory");
    void *addr = it->first;
    size_t size = it->second;
    allocated_memory.erase(it);
    // Try to merge free blocks
    auto left_iter = free_memory_by_addr.lower_bound(std::make_pair(addr, 0));
    if (left_iter != free_memory_by_addr.begin()) {
        --left_iter;
        if (reinterpret_cast<char*>(left_iter->first) + left_iter->second == addr) {
            size += left_iter->second;
            addr = left_iter->first;
            free_memory.erase(std::make_pair(left_iter->second, left_iter->first));
            free_memory_by_addr.erase(left_iter);
        }
    }
    auto right_iter = free_memory_by_addr.lower_bound(std::make_pair(reinterpret_cast<char*>(addr) + size, 0));
    if (right_iter != free_memory_by_addr.end()) {
        if (reinterpret_cast<char*>(addr) + size == right_iter->first) {
            // Merge with the right block
            size += right_iter->second;
            free_memory.erase(std::make_pair(right_iter->second, right_iter->first));
            free_memory_by_addr.erase(right_iter);
        }
    }
    // Insert the merged block back into the free memory
    free_memory.insert(std::make_pair(size, addr));
    free_memory_by_addr.insert(std::make_pair(addr, size));
}