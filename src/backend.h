#pragma once

#include <huxerui/lottie.h>
#include <huxerui/task.h>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace huxerui::lottie::detail {

enum class PixelAlpha { Straight, Premultiplied };

/// Immutable RGBA pixels, stored as 0xAABBGGRR words with the top row first.
struct PixelFrame {
  std::uint32_t width;
  std::uint32_t height;
  std::unique_ptr<const std::uint32_t[]> pixels;
  PixelAlpha alpha = PixelAlpha::Straight;

  std::span<const std::uint32_t> Pixels() const noexcept {
    return {pixels.get(), std::size_t(width) * height};
  }
};

using PixelBuffer = std::shared_ptr<const PixelFrame>;
using LoadSource = std::variant<RawAsset, File>;

struct LoadResult {
  LottieMetadata metadata;
  std::optional<LottieError> failure;
};

struct RenderRequest {
  std::uint32_t frame = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t revision = 0;
  PixelAlpha alpha = PixelAlpha::Straight;
  bool operator==(const RenderRequest&) const = default;
};

struct RenderResult {
  RenderRequest request;
  PixelBuffer pixels;
  std::optional<LottieError> failure;
};

/// All ThorVG objects, including their destruction, remain on the application's serial execution queue.
class Backend final {
public:
  Backend();
  ~Backend();
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;

  std::uint64_t Allocate();
  void Load(std::uint64_t id, LoadSource source, std::shared_ptr<std::atomic_bool> alive,
            TaskScope tasks, std::function<void(LoadResult)> callback);
  void Render(std::uint64_t id, RenderRequest request, std::shared_ptr<std::atomic_bool> alive,
              TaskScope tasks, std::function<void(RenderResult)> callback);
  void Release(std::uint64_t id);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::uint64_t next_id_ = 0;
};

} // namespace huxerui::lottie::detail
