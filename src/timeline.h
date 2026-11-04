#pragma once

#include <huxerui/lottie.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace huxerui::lottie::detail {

inline LottieControl Validate(LottieControl control) {
  if (auto* playback = std::get_if<LottiePlayback>(&control)) {
    if (!std::isfinite(playback->speed) || playback->speed <= 0.0 || playback->speed > 1000.0) {
      throw std::invalid_argument("Lottie playback speed must be finite and in (0, 1000]");
    }
  } else {
    auto& progress = std::get<LottieProgress>(control).value;
    if (!std::isfinite(progress)) throw std::invalid_argument("Lottie progress must be finite");
    progress = std::clamp(progress, 0.0, 1.0);
  }
  return control;
}

class Timeline {
public:
  void Reset(LottieControl control) {
    control_ = control;
    restart_ = std::holds_alternative<LottiePlayback>(control) ? std::get<LottiePlayback>(control).restart : 0;
    position_ = std::holds_alternative<LottieProgress>(control) ? std::get<LottieProgress>(control).value : 0.0;
    passes_ = 0;
    complete_ = false;
  }

  bool Apply(LottieControl control) {
    if (control == control_) return false;
    if (auto* progress = std::get_if<LottieProgress>(&control)) {
      position_ = progress->value;
      passes_ = 0;
      complete_ = false;
    } else if (restart_ != std::get<LottiePlayback>(control).restart) {
      Reset(control);
    } else if (std::holds_alternative<LottieProgress>(control_)) {
      passes_ = 0;
      complete_ = false;
    }
    control_ = control;
    return true;
  }

  bool Running() const {
    const auto* playback = std::get_if<LottiePlayback>(&control_);
    return playback && playback->playing && !complete_;
  }

  void Advance(double seconds, double duration) {
    if (!Running() || seconds <= 0.0) return;
    const auto& playback = std::get<LottiePlayback>(control_);
    const long double next = position_ + static_cast<long double>(seconds) * playback.speed / duration;
    if (playback.iterations && (passes_ >= playback.iterations || next >= playback.iterations - passes_)) {
      position_ = 1.0;
      complete_ = true;
    } else {
      if (playback.iterations) passes_ += static_cast<std::uint32_t>(std::floor(next));
      position_ = static_cast<double>(std::fmod(next, 1.0L));
    }
  }

  std::uint32_t Frame(std::uint32_t count) const {
    return std::min(count - 1, static_cast<std::uint32_t>(std::floor(position_ * count)));
  }
  bool Complete() const { return complete_; }
  double Position() const { return position_; }

private:
  LottieControl control_ = LottiePlayback{};
  double position_ = 0.0;
  std::uint32_t passes_ = 0;
  std::uint64_t restart_ = 0;
  bool complete_ = false;
};

} // namespace huxerui::lottie::detail
