#pragma once

#include <atomic>
#include <memory>
#include <optional>

namespace performance {

template<typename T>
class LockFreeQueue {
public:
    LockFreeQueue() : head_(new Node()), tail_(head_.load()) {}
    
    ~LockFreeQueue() {
        while (auto item = dequeue()) {}
        delete head_.load();
    }
    
    void enqueue(T value) {
        Node* new_node = new Node(std::move(value));
        Node* prev_tail = tail_.exchange(new_node);
        prev_tail->next.store(new_node);
    }
    
    std::optional<T> dequeue() {
        Node* head = head_.load();
        Node* next = head->next.load();
        
        if (next == nullptr) {
            return std::nullopt;
        }
        
        std::optional<T> result = std::move(next->data);
        head_.store(next);
        delete head;
        
        return result;
    }
    
    bool empty() const {
        Node* head = head_.load();
        Node* next = head->next.load();
        return next == nullptr;
    }
    
private:
    struct Node {
        std::optional<T> data;
        std::atomic<Node*> next{nullptr};
        
        Node() = default;
        explicit Node(T value) : data(std::move(value)) {}
    };
    
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};

}
