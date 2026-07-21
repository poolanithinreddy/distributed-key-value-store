#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace kv {

class ThreadPool {
 public:
  ThreadPool(std::size_t threads, std::size_t capacity);
  ~ThreadPool();
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template <typename Function>
  auto Submit(Function&& function) -> std::future<typename std::invoke_result_t<Function>> {
    using Return = typename std::invoke_result_t<Function>;
    auto task = std::make_shared<std::packaged_task<Return()>>(std::forward<Function>(function));
    auto future = task->get_future();
    {
      std::unique_lock<std::mutex> lock(mutex_);
      space_.wait(lock, [this] { return stopping_ || tasks_.size() < capacity_; });
      if (stopping_) throw std::runtime_error("thread pool is stopping");
      tasks_.emplace([task] { (*task)(); });
    }
    available_.notify_one();
    return future;
  }

  void Stop();

 private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::size_t capacity_;
  std::mutex mutex_;
  std::condition_variable available_;
  std::condition_variable space_;
  bool stopping_{false};
};

}  // namespace kv
