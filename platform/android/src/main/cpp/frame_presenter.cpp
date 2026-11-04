#include "frame_presenter.h"

#include <huxerui/android/external_texture.h>
#include <huxerui/android/platform_registry.h>
#include <android/bitmap.h>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace huxerui::lottie::detail {
namespace {

constexpr char java_vm_module[] = "lottie/android-java-vm";

void CheckJava(JNIEnv* env, bool valid, const char* operation) {
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    throw std::runtime_error(operation);
  }
  if (!valid) throw std::runtime_error(operation);
}

class AndroidPresenter final : public FramePresenter {
public:
  explicit AndroidPresenter(Size size)
      : vm_(OpenPlatformModule<JavaVM*>(java_vm_module)), texture_(std::make_shared<android::BitmapTexture>(size)) {}

  std::shared_ptr<ExternalTexture> Texture() const override { return texture_; }
  PixelAlpha Alpha() const noexcept override { return PixelAlpha::Premultiplied; }

  void Publish(const PixelBuffer& frame) override {
    static_assert(std::endian::native == std::endian::little);
    if (frame->alpha != PixelAlpha::Premultiplied) {
      throw std::invalid_argument("Android bitmap publication requires premultiplied pixels");
    }
    JNIEnv* env = nullptr;
    if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
      throw std::runtime_error("Lottie publication requires the Android application thread");
    }
    android::LocalRef<jclass> bitmap_class(env, env->FindClass("android/graphics/Bitmap"));
    CheckJava(env, bool(bitmap_class), "Cannot find Android Bitmap");
    android::LocalRef<jclass> config_class(env, env->FindClass("android/graphics/Bitmap$Config"));
    CheckJava(env, bool(config_class), "Cannot find Android Bitmap.Config");
    const auto config_field = env->GetStaticFieldID(config_class.Get(), "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    CheckJava(env, config_field != nullptr, "Cannot find ARGB_8888 bitmap configuration");
    android::LocalRef<jobject> config(env, env->GetStaticObjectField(config_class.Get(), config_field));
    CheckJava(env, bool(config), "Cannot read ARGB_8888 bitmap configuration");
    const auto create = env->GetStaticMethodID(bitmap_class.Get(), "createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    CheckJava(env, create != nullptr, "Cannot find Bitmap.createBitmap");

    android::LocalRef<jobject> bitmap(env, env->CallStaticObjectMethod(bitmap_class.Get(), create,
        static_cast<jint>(frame->width), static_cast<jint>(frame->height), config.Get()));
    CheckJava(env, bool(bitmap), "Cannot create Android bitmap");

    AndroidBitmapInfo info{};
    CheckJava(env, AndroidBitmap_getInfo(env, bitmap.Get(), &info) == ANDROID_BITMAP_RESULT_SUCCESS,
        "Cannot read Android bitmap layout");
    const auto row_bytes = std::size_t(frame->width) * sizeof(std::uint32_t);
    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 || info.width != frame->width || info.height != frame->height ||
        info.stride < row_bytes || (info.flags & ANDROID_BITMAP_FLAGS_ALPHA_MASK) != ANDROID_BITMAP_FLAGS_ALPHA_PREMUL) {
      throw std::runtime_error("Unexpected Android bitmap layout");
    }
    void* pixels = nullptr;
    CheckJava(env, AndroidBitmap_lockPixels(env, bitmap.Get(), &pixels) == ANDROID_BITMAP_RESULT_SUCCESS,
        "Cannot lock Android bitmap pixels");
    if (info.stride == row_bytes) {
      std::memcpy(pixels, frame->Pixels().data(), row_bytes * frame->height);
    } else {
      for (std::uint32_t y = 0; y < frame->height; ++y) {
        std::memcpy(static_cast<std::byte*>(pixels) + std::size_t(y) * info.stride,
            frame->Pixels().data() + std::size_t(y) * frame->width, row_bytes);
      }
    }
    CheckJava(env, AndroidBitmap_unlockPixels(env, bitmap.Get()) == ANDROID_BITMAP_RESULT_SUCCESS,
        "Cannot unlock Android bitmap pixels");
    texture_->Publish(env, bitmap.Get());
  }

  void Finish() noexcept override { texture_->Finish(); }

private:
  JavaVM* vm_;
  std::shared_ptr<android::BitmapTexture> texture_;
};

} // namespace

void InstallFramePresentation(ApplicationContext& context) {
  context.RegisterPlatformModule<JavaVM*>(java_vm_module, [](Runtime& runtime) {
    JavaVM* vm = nullptr;
    const auto env = android::GetPlatformEnv(runtime);
    if (env.jni->GetJavaVM(&vm) != JNI_OK || !vm) throw std::runtime_error("Cannot access the Android Java VM");
    return vm;
  });
}

std::unique_ptr<FramePresenter> CreateFramePresenter(Size size) { return std::make_unique<AndroidPresenter>(size); }

} // namespace huxerui::lottie::detail
