#pragma once

#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

namespace performance {

class MemoryMappedFile {
public:
    MemoryMappedFile(const std::string& filepath, size_t size, bool create = false)
        : filepath_(filepath), size_(size) {
        
        int flags = O_RDWR;
        if (create) {
            flags |= O_CREAT;
        }
        
        fd_ = open(filepath.c_str(), flags, 0644);
        if (fd_ == -1) {
            throw std::runtime_error("Failed to open file: " + filepath);
        }
        
        if (create) {
            if (ftruncate(fd_, size) == -1) {
                close(fd_);
                throw std::runtime_error("Failed to resize file");
            }
        }
        
        mapped_data_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (mapped_data_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to mmap file");
        }
    }
    
    ~MemoryMappedFile() {
        if (mapped_data_ != nullptr && mapped_data_ != MAP_FAILED) {
            munmap(mapped_data_, size_);
        }
        if (fd_ != -1) {
            close(fd_);
        }
    }
    
    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;
    
    void* data() { return mapped_data_; }
    const void* data() const { return mapped_data_; }
    size_t size() const { return size_; }
    
    void sync() {
        if (msync(mapped_data_, size_, MS_SYNC) == -1) {
            throw std::runtime_error("Failed to sync file");
        }
    }
    
    template<typename T>
    T* as() { return static_cast<T*>(mapped_data_); }
    
    template<typename T>
    const T* as() const { return static_cast<const T*>(mapped_data_); }
    
private:
    std::string filepath_;
    size_t size_;
    int fd_ = -1;
    void* mapped_data_ = nullptr;
};

}
