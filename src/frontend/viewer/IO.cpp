#include "IO.h"
#include "Image.h"
#include "ImageIO.h"
#include "Range.h"

#include "Runtime.h"

#ifndef IG_NO_EXECUTION_H
#include <execution>
#endif

namespace IG {
bool saveImageRGB(const std::filesystem::path& path, const float* rgb, size_t width, size_t height, float scale)
{
    ImageRgba32 img;
    img.width  = width;
    img.height = height;
    img.pixels.reset(new float[width * height * 4]);

    const RangeS imageRange(0, width * height);

    const auto pixelF = [&](size_t ind) {
        auto r = rgb[ind * 3 + 0];
        auto g = rgb[ind * 3 + 1];
        auto b = rgb[ind * 3 + 2];

        img.pixels[4 * ind + 0] = r * scale;
        img.pixels[4 * ind + 1] = g * scale;
        img.pixels[4 * ind + 2] = b * scale;
        img.pixels[4 * ind + 3] = 1.0f;
    };

#ifndef IG_NO_EXECUTION_H
    std::for_each(std::execution::par_unseq, imageRange.begin(), imageRange.end(), pixelF);
#else
    for (size_t i : imageRange)
        pixelF(i);
#endif

    return img.save(path);
}

bool saveImageRGBA(const std::filesystem::path& path, const float* rgb, size_t width, size_t height, float scale)
{
    ImageRgba32 img;
    img.width  = width;
    img.height = height;
    img.pixels.reset(new float[width * height * 4]);

    const RangeS imageRange(0, width * height);
    const auto pixelF = [&](size_t ind) {
        auto r = rgb[ind * 4 + 0];
        auto g = rgb[ind * 4 + 1];
        auto b = rgb[ind * 4 + 2];
        auto a = rgb[ind * 4 + 3];

        img.pixels[4 * ind + 0] = r * scale;
        img.pixels[4 * ind + 1] = g * scale;
        img.pixels[4 * ind + 2] = b * scale;
        img.pixels[4 * ind + 3] = a * scale;
    };

#ifndef IG_NO_EXECUTION_H
    std::for_each(std::execution::par_unseq, imageRange.begin(), imageRange.end(), pixelF);
#else
    for (size_t i : imageRange)
        pixelF(i);
#endif

    return img.save(path);
}

bool saveImageOutput(const std::filesystem::path& path, const Runtime& runtime)
{
    size_t width  = runtime.loadedRenderSettings().FilmWidth;
    size_t height = runtime.loadedRenderSettings().FilmHeight;
    float scale   = 1.0f / runtime.currentIterationCount();
    if (runtime.currentIterationCount() == 0)
        scale = 0;

    size_t aov_count = runtime.aovs().size() + 1;

    const RangeS imageRange(0, width * height);
    std::vector<float> images(width * height * 3 * aov_count);

    // Copy data
    for (size_t aov = 0; aov < aov_count; ++aov) {
        const float* src = runtime.getFramebuffer((int)aov);
        float* dst_r     = &images[width * height * (3 * aov + 0)];
        float* dst_g     = &images[width * height * (3 * aov + 1)];
        float* dst_b     = &images[width * height * (3 * aov + 2)];

        const auto pixelF = [&](size_t ind) {
            float r = src[ind * 3 + 0];
            float g = src[ind * 3 + 1];
            float b = src[ind * 3 + 2];

            dst_r[ind] = r * scale;
            dst_g[ind] = g * scale;
            dst_b[ind] = b * scale;
        };

#ifndef IG_NO_EXECUTION_H
        std::for_each(std::execution::par_unseq, imageRange.begin(), imageRange.end(), pixelF);
#else
        for (size_t i : imageRange)
            pixelF(i);
#endif
    }

    std::vector<const float*> image_ptrs(3 * aov_count);
    std::vector<std::string> image_names(3 * aov_count);
    for (size_t aov = 0; aov < aov_count; ++aov) {
        // Swizzle RGB to BGR as some viewers expect it per default
        image_ptrs[3 * aov + 2] = &images[width * height * (3 * aov + 0)];
        image_ptrs[3 * aov + 1] = &images[width * height * (3 * aov + 1)];
        image_ptrs[3 * aov + 0] = &images[width * height * (3 * aov + 2)];

        // Framebuffer
        if (aov == 0) {
            image_names[3 * aov + 0] = "Default.B";
            image_names[3 * aov + 1] = "Default.G";
            image_names[3 * aov + 2] = "Default.R";
        } else {
            std::string name         = runtime.aovs()[aov - 1];
            image_names[3 * aov + 0] = name + ".B";
            image_names[3 * aov + 1] = name + ".G";
            image_names[3 * aov + 2] = name + ".R";
        }
    }

    return ImageIO::save(path, width, height, image_ptrs, image_names);
}
} // namespace IG