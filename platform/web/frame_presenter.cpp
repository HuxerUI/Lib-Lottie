#include "frame_presenter.h"

#include <huxerui/web/external_texture.h>
#include <emscripten.h>
#include <stdexcept>

namespace huxerui::lottie::detail {
namespace {

using emscripten::EM_VAL;
using emscripten::val;

EM_JS(EM_VAL, CreateVideoFrame, (const void* pixels, int width, int height), {
  try {
    const bytes = HEAPU8.subarray(pixels, pixels + width * height * 4);
    return Emval.toHandle(new VideoFrame(bytes, {
      format: "RGBA",
      codedWidth: width,
      codedHeight: height,
      timestamp: 0,
      colorSpace: {primaries: "bt709", transfer: "iec61966-2-1", matrix: "rgb", fullRange: true}
    }));
  } catch (error) {
    return Emval.toHandle(String(error));
  }
});

class WebPresenter final : public FramePresenter {
public:
  explicit WebPresenter(Size size) : texture_(std::make_shared<web::VideoFrameTexture>(size)) {
    if (val::global("VideoFrame").typeOf().as<std::string>() != "function") {
      throw std::runtime_error("Lottie requires browser WebCodecs VideoFrame support in a secure context");
    }
  }

  std::shared_ptr<ExternalTexture> Texture() const override { return texture_; }

  void Publish(const PixelBuffer& pixels) override {
    auto frame = val::take_ownership(CreateVideoFrame(pixels->Pixels().data(), pixels->width, pixels->height));
    if (frame.typeOf().as<std::string>() == "string") throw std::runtime_error(frame.as<std::string>());
    try {
      texture_->Publish(frame);
    } catch (...) {
      frame.call<void>("close");
      throw;
    }
    frame.call<void>("close");
  }

  void Finish() noexcept override { texture_->Finish(); }

private:
  std::shared_ptr<web::VideoFrameTexture> texture_;
};

} // namespace

void InstallFramePresentation(ApplicationContext&) {}
std::unique_ptr<FramePresenter> CreateFramePresenter(Size size) { return std::make_unique<WebPresenter>(size); }

} // namespace huxerui::lottie::detail
