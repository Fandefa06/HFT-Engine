//RingBuffer.hpp

#pragma once
#include <vector>
#include <atomic>
#include "Types.hpp"

// Single Producer Single Consumer Lock-Free Buffer
template<typename T, size_t Size>
class RingBuffer {
private:
    std::vector<T> buffer;
    
    // alignas(64) prevents "False Sharing" by ensuring these atomics 
    // sit on different cache lines.
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    
    const size_t capacity = Size;

public:
    RingBuffer() : buffer(Size) {}

    // Producer thread calls this
    bool push(const T& item) {
        size_t currentHead = head.load(std::memory_order_relaxed);
        size_t nextHead = (currentHead + 1) % capacity;

        if (nextHead == tail.load(std::memory_order_acquire)) {
            return false; // Buffer full
        }

        buffer[currentHead] = item;
        head.store(nextHead, std::memory_order_release);
        return true;
    }

    // Consumer thread calls this
    bool pop(T& item) {
        size_t currentTail = tail.load(std::memory_order_relaxed);
        if (currentTail == head.load(std::memory_order_acquire)) {
            return false; // Buffer empty
        }

        item = buffer[currentTail];
        tail.store((currentTail + 1) % capacity, std::memory_order_release);
        return true;
    }
};