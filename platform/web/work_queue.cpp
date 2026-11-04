#include "work_queue.h"

#include <emscripten/eventloop.h>
#include <deque>

namespace huxerui::lottie::detail {

struct WorkQueue::Impl {
  std::deque<std::function<void()>> jobs;
  std::function<void()> finalize;
  int timer = 0;

  Impl(std::function<void()> initialize, std::function<void()> cleanup) : finalize(std::move(cleanup)) {
    initialize();
  }

  ~Impl() {
    if (timer) emscripten_clear_timeout(timer);
    jobs.clear();
    finalize();
  }

  void Schedule() {
    if (timer || jobs.empty()) return;
    timer = emscripten_set_timeout([](void* value) {
      auto& self = *static_cast<Impl*>(value);
      self.timer = 0;
      auto job = std::move(self.jobs.front());
      self.jobs.pop_front();
      job();
      self.Schedule();
    }, 0, this);
  }
};

WorkQueue::WorkQueue(std::function<void()> initialize, std::function<void()> finalize)
    : impl_(std::make_unique<Impl>(std::move(initialize), std::move(finalize))) {}
WorkQueue::~WorkQueue() = default;

void WorkQueue::Submit(std::function<void()> job) {
  impl_->jobs.push_back(std::move(job));
  impl_->Schedule();
}

} // namespace huxerui::lottie::detail
