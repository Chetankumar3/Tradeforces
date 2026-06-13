#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <cstddef>

namespace performance {

template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t initial_size = 100) {
        pool_.reserve(initial_size);
        for (size_t i = 0; i < initial_size; ++i) {
            pool_.push_back(std::make_unique<T>());
        }
    }
    
    std::unique_ptr<T> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.empty()) {
            return std::make_unique<T>();
        }
        auto obj = std::move(pool_.back());
        pool_.pop_back();
        return obj;
    }
    
    void release(std::unique_ptr<T> obj) {
        if (!obj) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.size() < max_size_) {
            pool_.push_back(std::move(obj));
        }
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }
    
    void setMaxSize(size_t max_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_size_ = max_size;
        while (pool_.size() > max_size_) {
            pool_.pop_back();
        }
    }
    
private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<T>> pool_;
    size_t max_size_ = 1000;
};

}
