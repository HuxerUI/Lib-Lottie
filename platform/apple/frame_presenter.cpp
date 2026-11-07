#include "frame_presenter.h"

#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <huxerui/ios/external_texture.h>
#else
#include <huxerui/macos/external_texture.h>
#endif
#include <stdexcept>

namespace huxerui::lottie::detail {
namespace {

#if TARGET_OS_IPHONE
using PlatformTexture = ios::PixelBufferTexture;
#else
using PlatformTexture = macos::PixelBufferTexture;
#endif

class ApplePresenter final : public FramePresenter {
public:
  explicit ApplePresenter(Size size) : texture_(std::make_shared<PlatformTexture>(size)) {}
  std::shared_ptr<ExternalTexture> Texture() const override { return texture_; }

  void Publish(const PixelBuffer& frame) override {
    CVPixelBufferRef raw = nullptr;
    if (CVPixelBufferCreate(kCFAllocatorDefault, frame->width, frame->height, kCVPixelFormatType_32BGRA,
                            nullptr, &raw) != kCVReturnSuccess) {
      throw std::runtime_error("Cannot allocate Lottie pixel buffer");
    }
    const auto release = [](CVPixelBufferRef value) { CVPixelBufferRelease(value); };
    const std::unique_ptr<__CVBuffer, decltype(release)> buffer(raw, release);
    if (CVPixelBufferLockBaseAddress(raw, 0) != kCVReturnSuccess) {
      throw std::runtime_error("Cannot lock Lottie pixel buffer");
    }
    auto* bytes = static_cast<std::byte*>(CVPixelBufferGetBaseAddress(raw));
    const auto stride = CVPixelBufferGetBytesPerRow(raw);
    const auto* source = frame->Pixels().data();
    for (std::uint32_t y = 0; y < frame->height; ++y) {
      auto* row = reinterpret_cast<std::uint32_t*>(bytes + y * stride);
      for (std::uint32_t x = 0; x < frame->width; ++x) {
        const auto rgba = source[std::size_t(y) * frame->width + x];
        const auto alpha = rgba >> 24;
        const auto red = ((rgba & 255) * alpha + 127) / 255;
        const auto green = (((rgba >> 8) & 255) * alpha + 127) / 255;
        const auto blue = (((rgba >> 16) & 255) * alpha + 127) / 255;
        row[x] = (alpha << 24) | (red << 16) | (green << 8) | blue;
      }
    }
    CVPixelBufferUnlockBaseAddress(raw, 0);
    texture_->Publish(raw);
  }

  void Finish() noexcept override { texture_->Finish(); }

private:
  std::shared_ptr<PlatformTexture> texture_;
};

} // namespace

void InstallFramePresentation(ApplicationContext&) {}
std::unique_ptr<FramePresenter> CreateFramePresenter(Size size) { return std::make_unique<ApplePresenter>(size); }

} // namespace huxerui::lottie::detail
