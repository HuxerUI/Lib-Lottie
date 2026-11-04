#pragma once

#include <functional>
#include <memory>

namespace huxerui::lottie::detail {

/// Owns a serial execution context and runs its initialization and cleanup on that same context.
class WorkQueue final {
public:
  WorkQueue(std::function<void()> initialize, std::function<void()> finalize);
  ~WorkQueue();
  void Submit(std::function<void()> job);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace huxerui::lottie::detail
