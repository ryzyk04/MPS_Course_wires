#pragma once

#include <stdint.h>

// Один производитель и один потребитель. Индексы атомарны на 8-битном AVR.
template<uint8_t Capacity>
class Ring {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Ring capacity must be a power of two");

public:
    bool push(uint8_t value)
    {
        const uint8_t next = uint8_t(tail_ + 1) & (Capacity - 1);
        if (next == head_) {
            return false;
        }
        bytes_[tail_] = value;
        asm volatile("" ::: "memory");
        tail_ = next;
        return true;
    }

    bool pop(uint8_t &value)
    {
        if (head_ == tail_) {
            return false;
        }
        asm volatile("" ::: "memory");
        value = bytes_[head_];
        asm volatile("" ::: "memory");
        head_ = uint8_t(head_ + 1) & (Capacity - 1);
        return true;
    }

    bool empty() const { return head_ == tail_; }
    void clear() { head_ = tail_; }

private:
    uint8_t bytes_[Capacity] = {};
    volatile uint8_t head_ = 0;
    volatile uint8_t tail_ = 0;
};
