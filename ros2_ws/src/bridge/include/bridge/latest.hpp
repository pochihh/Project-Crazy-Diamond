#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace diamond {
// One ROS executor writes; one RT worker reads. Each owns a buffer, with a
// third buffer exchanged atomically. Readers never wait and values never tear.
template<class T> class Latest {
    std::array<T,3> buffers_{};
    uint8_t writer_=0,reader_=1;
    std::atomic<uint8_t> middle_{2};
    static constexpr uint8_t dirty=4;
public:
    void publish(const T& value) {
        buffers_[writer_]=value;
        writer_=middle_.exchange(writer_|dirty,std::memory_order_acq_rel)&3;
    }
    bool take(T& value) {
        if(!(middle_.load(std::memory_order_acquire)&dirty))return false;
        reader_=middle_.exchange(reader_,std::memory_order_acq_rel)&3;
        value=buffers_[reader_];
        return true;
    }
};
}
