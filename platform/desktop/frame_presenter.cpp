#include "frame_presenter.h"

#if defined(_WIN32)
#include <huxerui/windows/external_texture.h>
#else
#include <huxerui/linux/external_texture.h>
#endif
#include <bit>
#include <span>

namespace huxerui::lottie::detail {
namespace {

#if defined(_WIN32)
namespace platform = huxerui::windows;
#else
namespace platform = huxerui::linux;
#endif

class DesktopPresenter final : public FramePresenter {
public:
  explicit DesktopPresenter(Size size) : texture_(std::make_shared<platform::PixelTexture>(size)) {}
  std::shared_ptr<ExternalTexture> Texture() const override { return texture_; }

  void Publish(const PixelBuffer& frame) override {
    static_assert(std::endian::native == std::endian::little);
    texture_->Publish({
        .pixel_width = static_cast<int>(frame->width),
        .pixel_height = static_cast<int>(frame->height),
        .bytes_per_row = std::size_t(frame->width) * 4,
        .format = platform::PixelFormat::Rgba8888,
        .pixels = std::as_bytes(frame->Pixels()),
    });
  }

  void Finish() noexcept override { texture_->Finish(); }

private:
  std::shared_ptr<platform::PixelTexture> texture_;
};

} // namespace

void InstallFramePresentation(ApplicationContext&) {}
std::unique_ptr<FramePresenter> CreateFramePresenter(Size size) { return std::make_unique<DesktopPresenter>(size); }

} // namespace huxerui::lottie::detail
