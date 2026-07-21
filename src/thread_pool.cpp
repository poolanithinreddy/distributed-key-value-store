#include "kv/thread_pool.hpp"

namespace kv {

ThreadPool::ThreadPool(std::size_t threads, std::size_t capacity) : capacity_(capacity) {
  if (threads == 0 || capacity == 0) throw std::invalid_argument("pool sizes must be positive");
  workers_.reserve(threads);
  for (std::size_t i = 0; i < threads; ++i) {
    workers_.emplace_back([this] {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          available_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
          if (stopping_ && tasks_.empty()) return;
          task = std::move(tasks_.front());
          tasks_.pop();
        }
        space_.notify_one();
        task();
      }
    });
  }
}

ThreadPool::~ThreadPool() { Stop(); }

void ThreadPool::Stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  available_.notify_all();
  space_.notify_all();
  for (auto& worker : workers_)
    if (worker.joinable()) worker.join();
}

}  // namespace kv
