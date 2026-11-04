#include "backend.h"
#include "work_queue.h"

#include <thorvg.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace huxerui::lottie::detail {
namespace {

void Check(tvg::Result result, const char* operation) {
  if (result != tvg::Result::Success) {
    throw std::runtime_error(std::string(operation) + ": ThorVG result " + std::to_string(static_cast<int>(result)));
  }
}

class Animation {
public:
  explicit Animation(const std::string& json) {
    if (json.empty() || json.size() > std::numeric_limits<std::uint32_t>::max()) {
      throw std::runtime_error("Lottie JSON is empty or too large");
    }
    animation_.reset(tvg::Animation::gen());
    canvas_.reset(tvg::SwCanvas::gen());
    if (!animation_ || !canvas_) throw std::runtime_error("Cannot allocate ThorVG animation");
    auto* picture = animation_->picture();
    Check(picture->load(json.data(), static_cast<std::uint32_t>(json.size()), "lottie", nullptr, true), "Load JSON");
    Check(picture->size(&metadata.size.width, &metadata.size.height), "Read size");
    const double frames = animation_->totalFrame();
    metadata.duration = animation_->duration();
    if (!std::isfinite(frames) || frames < 1 || frames > std::numeric_limits<std::uint32_t>::max() ||
        !std::isfinite(metadata.duration) || metadata.duration <= 0 ||
        !std::isfinite(metadata.size.width) || metadata.size.width <= 0 ||
        !std::isfinite(metadata.size.height) || metadata.size.height <= 0) {
      throw std::runtime_error("Lottie has invalid duration, frame count, or dimensions");
    }
    metadata.frames = static_cast<std::uint32_t>(frames);
    Check(canvas_->add(picture), "Attach picture");
  }

  PixelBuffer Render(const RenderRequest& request) {
    if (!request.width || !request.height || request.width > 2048 || request.height > 2048) {
      throw std::invalid_argument("Lottie raster dimensions must be in [1, 2048]");
    }
    // draw(true) clears the complete target; value-initializing these pixels would clear them twice.
    auto pixels = std::make_unique_for_overwrite<std::uint32_t[]>(std::size_t(request.width) * request.height);
    auto* picture = animation_->picture();
    const float scale = std::min(request.width / metadata.size.width, request.height / metadata.size.height);
    const tvg::Matrix matrix{scale, 0, (request.width - metadata.size.width * scale) * 0.5F,
                             0, scale, (request.height - metadata.size.height * scale) * 0.5F,
                             0, 0, 1};
    Check(picture->transform(matrix), "Fit picture");
    const auto colorspace = request.alpha == PixelAlpha::Premultiplied
        ? tvg::ColorSpace::ABGR8888 : tvg::ColorSpace::ABGR8888S;
    Check(canvas_->target(pixels.get(), request.width, request.width, request.height, colorspace), "Set target");
    const auto result = animation_->frame(static_cast<float>(std::min(request.frame, metadata.frames - 1)));
    if (result != tvg::Result::InsufficientCondition) Check(result, "Seek");
    Check(canvas_->update(), "Update");
    Check(canvas_->draw(true), "Draw");
    Check(canvas_->sync(), "Synchronize");
    return std::make_shared<PixelFrame>(PixelFrame{request.width, request.height, std::move(pixels), request.alpha});
  }

  LottieMetadata metadata;

private:
  std::unique_ptr<tvg::Animation> animation_;
  std::unique_ptr<tvg::SwCanvas> canvas_;
};

} // namespace

struct Backend::Impl {
  std::unordered_map<std::uint64_t, std::unique_ptr<Animation>> animations;
  std::string initialization_error;
  bool initialized = false;
  WorkQueue queue{
      [this] {
        initialized = tvg::Initializer::init(0) == tvg::Result::Success;
        if (!initialized) initialization_error = "Cannot initialize ThorVG";
      },
      [this] {
        animations.clear();
        if (initialized) tvg::Initializer::term();
      }};
};

Backend::Backend() : impl_(std::make_unique<Impl>()) {}
Backend::~Backend() = default;
std::uint64_t Backend::Allocate() { return ++next_id_; }

void Backend::Load(std::uint64_t id, LoadSource source, std::shared_ptr<std::atomic_bool> alive,
                   TaskScope tasks, std::function<void(LoadResult)> callback) {
  impl_->queue.Submit([impl = impl_.get(), id, source, alive, tasks, callback = std::move(callback)] {
    if (!alive->load()) return;
    LoadResult result;
    std::string json;
    try {
      if (const auto* asset = std::get_if<RawAsset>(&source)) {
        if (!asset->HasValue()) throw std::runtime_error("Lottie resource is empty");
        json = asset->ReadString(false);
      } else {
        auto read = std::get<File>(source).ReadBytes();
        if (!read.Succeeded()) throw std::runtime_error("Cannot read Lottie file: " + read.Error().message);
        const auto& bytes = read.Value();
        if (!bytes.empty()) json.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      }
    } catch (const std::exception& error) {
      result.failure = LottieError{LottieError::Stage::Resource, error.what()};
    }
    if (!result.failure && alive->load()) {
      try {
        if (!impl->initialization_error.empty()) throw std::runtime_error(impl->initialization_error);
        auto animation = std::make_unique<Animation>(json);
        result.metadata = animation->metadata;
        impl->animations[id] = std::move(animation);
      } catch (const std::exception& error) {
        result.failure = LottieError{LottieError::Stage::Decode, error.what()};
      }
    }
    if (alive->load()) tasks.Post([callback, result] { callback(result); });
  });
}

void Backend::Render(std::uint64_t id, RenderRequest request, std::shared_ptr<std::atomic_bool> alive,
                     TaskScope tasks, std::function<void(RenderResult)> callback) {
  impl_->queue.Submit([impl = impl_.get(), id, request, alive, tasks, callback = std::move(callback)] {
    if (!alive->load()) return;
    RenderResult result{.request = request};
    try {
      const auto found = impl->animations.find(id);
      if (found == impl->animations.end()) throw std::runtime_error("Lottie animation is no longer loaded");
      result.pixels = found->second->Render(request);
    } catch (const std::exception& error) {
      result.failure = LottieError{LottieError::Stage::Render, error.what()};
    }
    if (alive->load()) tasks.Post([callback, result] { callback(result); });
  });
}

void Backend::Release(std::uint64_t id) {
  impl_->queue.Submit([impl = impl_.get(), id] { impl->animations.erase(id); });
}

} // namespace huxerui::lottie::detail
