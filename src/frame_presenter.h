#pragma once

#include "backend.h"
#include <huxerui/external_texture.h>

namespace huxerui::lottie::detail {

/// Publishes portable CPU frames through the host SDK's external-texture API on the UI thread.
class FramePresenter {
public:
  virtual ~FramePresenter() = default;
  virtual std::shared_ptr<ExternalTexture> Texture() const = 0;
  /// Requests the native alpha representation directly from the rasterizer to avoid pixel conversion on the UI thread.
  virtual PixelAlpha Alpha() const noexcept { return PixelAlpha::Straight; }
  virtual void Publish(const PixelBuffer& pixels) = 0;
  virtual void Finish() noexcept = 0;
};

void InstallFramePresentation(ApplicationContext& context);
std::unique_ptr<FramePresenter> CreateFramePresenter(Size intrinsic_size);

} // namespace huxerui::lottie::detail
