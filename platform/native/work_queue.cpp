#include "work_queue.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace huxerui::lottie::detail {

struct WorkQueue::Impl {
  std::mutex mutex;
  std::condition_variable ready;
  std::deque<std::function<void()>> jobs;
  bool stopping = false;
  std::thread thread;

  Impl(std::function<void()> initialize, std::function<void()> finalize)
      : thread([this, initialize = std::move(initialize), finalize = std::move(finalize)] {
          initialize();
          for (;;) {
            std::function<void()> job;
            {
              std::unique_lock lock(mutex);
              ready.wait(lock, [this] { return stopping || !jobs.empty(); });
              if (jobs.empty()) break;
              job = std::move(jobs.front());
              jobs.pop_front();
            }
            job();
          }
          finalize();
        }) {}

  ~Impl() {
    {
      std::lock_guard lock(mutex);
      stopping = true;
      ready.notify_one();
    }
    thread.join();
  }
};

WorkQueue::WorkQueue(std::function<void()> initialize, std::function<void()> finalize)
    : impl_(std::make_unique<Impl>(std::move(initialize), std::move(finalize))) {}
WorkQueue::~WorkQueue() = default;

void WorkQueue::Submit(std::function<void()> job) {
  {
    std::lock_guard lock(impl_->mutex);
    impl_->jobs.push_back(std::move(job));
  }
  impl_->ready.notify_one();
}

} // namespace huxerui::lottie::detail
