#include <wormedit/core/storyboard_raster.h>

#include "checked_math.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <thread>

namespace wormedit {

auto storyboard_raster_clock_t::time_at(std::uint64_t frame_index) const
    -> result_t<storyboard_time_t, failure_code_t> {
    return storyboard_time_t::from_frame(frame_index, numerator, denominator);
}

auto storyboard_raster_images_t::from(song_id_t project, std::uint64_t revision,
    std::span<const storyboard_raster_image_source_t> sources, visual::image_cache_t& cache,
    visual::image_job_options_t options) -> attempt_t<storyboard_raster_images_t> {
    attempt_t<storyboard_raster_images_t> result;
    if (sources.size() > storyboard_raster_options_t::maximum_supported_instances) {
        result.diagnostics.push_back({.severity = diagnostic_severity_t::error, .code = diagnostic_code_t::invalid_binding,
            .message = "Storyboard image binding count exceeds the frame limit"});
        return result;
    }
    storyboard_raster_images_t images;
    images.bindings.reserve(sources.size());
    std::size_t bytes{};
    for (const auto& source : sources) {
        auto image = cache.find(project, revision, source.source);
        if (!image) {
            auto decoded = visual::open_image_rgba(source.source, options.decode_limits);
            if (decoded) image = std::make_shared<const visual::rgba_image_t>(std::move(*decoded));
            else result.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                .code = diagnostic_code_t::dangling_asset,
                .message = "Storyboard image " + source.path + ": " + decoded.error().message});
        }
        if (image) {
            if (!checked_math::account_size(bytes, image->pixels.size(), options.maximum_result_rgba_bytes)) {
                result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                    .code = diagnostic_code_t::invalid_binding, .message = "Storyboard images exceed the decoded byte budget"});
                return result;
            }
            image = cache.insert(project, revision, source.source, std::move(image));
        }
        images.bindings.push_back({.path = source.path, .image = std::move(image)});
    }
    result.value = std::move(images);
    return result;
}

namespace {
constexpr std::uint32_t band_height = 32;

struct raster_quad_t {
    [[nodiscard]] static auto from(const storyboard_draw_instance_t& instance,
        const visual::rgba_image_t& image, const storyboard_raster_options_t& options)
        -> std::optional<raster_quad_t>;
    const visual::rgba_image_t* image{};
    visual_point_t top_left;
    // Inverse of the two full-quad edge vectors, mapping screen to unit UV.
    std::array<double, 4> inverse{};
    std::array<double, 4> multiply{};
    bool additive{};
    std::uint32_t left{}, top{}, right{}, bottom{};
};

auto color_unit(double value) -> double { return std::clamp(value, 0.0, 1.0); }
auto color_byte(double value) -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(std::floor(value * 255 + 0.5), 0.0, 255.0));
}

auto raster_quad_t::from(const storyboard_draw_instance_t& instance, const visual::rgba_image_t& image,
    const storyboard_raster_options_t& options) -> std::optional<raster_quad_t> {
    if (image.width == 0 || image.height == 0 ||
        image.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        image.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        static_cast<std::uint64_t>(image.width) * image.height * 4 != image.pixels.size() ||
        !std::isfinite(instance.opacity) || !std::isfinite(instance.red) ||
        !std::isfinite(instance.green) || !std::isfinite(instance.blue)) return std::nullopt;
    const auto origin = storyboard_origin_t::from(instance.origin).value_or(storyboard_origin_t{});
    const auto quad = visual_image_quad_t::from({.source_width = image.width, .source_height = image.height,
        .destination = {.width = static_cast<double>(options.width), .height = static_cast<double>(options.height)},
        .logical_width = instance.logical_width, .logical_height = instance.logical_height,
        .origin_x = origin.x, .origin_y = origin.y, .transform = instance.transform});
    if (!quad) return std::nullopt;
    const auto& p = quad->points;
    const auto ax = p[1].x - p[0].x, ay = p[1].y - p[0].y;
    const auto bx = p[3].x - p[0].x, by = p[3].y - p[0].y;
    const auto determinant = ax * by - ay * bx;
    if (determinant == 0 || !std::isfinite(determinant) || !std::isfinite(1 / determinant)) return std::nullopt;
    const auto [min_x, max_x] = std::ranges::minmax(p, {}, &visual_point_t::x);
    const auto [min_y, max_y] = std::ranges::minmax(p, {}, &visual_point_t::y);
    const auto bound = [](double value, std::uint32_t limit) {
        return static_cast<std::uint32_t>(std::clamp(std::ceil(value - 0.5), 0.0, static_cast<double>(limit)));
    };
    // The exported logical canvas is contained in the frame. Clip sprites at
    // its border too, so translated/oversized artwork cannot paint the bars.
    const auto canvas_scale = std::min(options.width / instance.logical_width,
        options.height / instance.logical_height);
    const auto canvas_left = (options.width - instance.logical_width * canvas_scale) * 0.5;
    const auto canvas_top = (options.height - instance.logical_height * canvas_scale) * 0.5;
    return raster_quad_t{.image = &image, .top_left = p[0],
        .inverse = {by / determinant, -bx / determinant, -ay / determinant, ax / determinant},
        .multiply = {color_unit(instance.red / 255), color_unit(instance.green / 255),
                     color_unit(instance.blue / 255), color_unit(instance.opacity)},
        .additive = instance.blend == osbx_blend_t::additive,
        .left = bound(std::max(min_x.x, canvas_left), options.width),
        .top = bound(std::max(min_y.y, canvas_top), options.height),
        .right = bound(std::min(max_x.x, options.width - canvas_left), options.width),
        .bottom = bound(std::min(max_y.y, options.height - canvas_top), options.height)};
}

// Filter premultiplied texels, preventing transparent RGB from bleeding into
// the edge. Source images remain at full native resolution.
auto texel(const visual::rgba_image_t& image, int x, int y) -> std::array<double, 4> {
    const auto ix = static_cast<std::uint32_t>(std::clamp(x, 0, static_cast<int>(image.width) - 1));
    const auto iy = static_cast<std::uint32_t>(std::clamp(y, 0, static_cast<int>(image.height) - 1));
    const auto* pixel = image.pixels.data() + (static_cast<std::size_t>(iy) * image.width + ix) * 4;
    const auto alpha = pixel[3] / 255.0;
    return {pixel[0] / 255.0 * alpha, pixel[1] / 255.0 * alpha, pixel[2] / 255.0 * alpha, alpha};
}

auto sample(const visual::rgba_image_t& image, double u, double v) -> std::array<double, 4> {
    const auto x = u * image.width - 0.5, y = v * image.height - 0.5;
    const auto ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const auto a = texel(image, ix, iy), b = texel(image, ix + 1, iy);
    const auto c = texel(image, ix, iy + 1), d = texel(image, ix + 1, iy + 1);
    std::array<double, 4> result;
    for (std::size_t channel = 0; channel < 4; ++channel)
        result[channel] = std::lerp(std::lerp(a[channel], b[channel], x - ix),
            std::lerp(c[channel], d[channel], x - ix), y - iy);
    return result;
}

void draw_quad(const raster_quad_t& quad, std::span<std::uint8_t> output,
    std::uint32_t width, std::uint32_t band_top, std::uint32_t band_bottom) {
    const auto top = std::max(band_top, quad.top), bottom = std::min(band_bottom, quad.bottom);
    for (auto y = top; y < bottom; ++y) {
        for (auto x = quad.left; x < quad.right; ++x) {
            const auto dx = x + 0.5 - quad.top_left.x, dy = y + 0.5 - quad.top_left.y;
            const auto u = quad.inverse[0] * dx + quad.inverse[1] * dy;
            const auto v = quad.inverse[2] * dx + quad.inverse[3] * dy;
            if (!std::isfinite(u) || !std::isfinite(v) || u < 0 || u >= 1 || v < 0 || v >= 1) continue;
            const auto source = sample(*quad.image, u, v);
            const auto alpha = source[3] * quad.multiply[3];
            auto* destination = output.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto foreground = source[channel] * quad.multiply[channel] * quad.multiply[3];
                const auto background = destination[channel] / 255.0 * (quad.additive ? 1.0 : 1.0 - alpha);
                destination[channel] = color_byte(foreground + background);
            }
        }
    }
}
} // namespace

struct storyboard_raster_t::impl_t {
    storyboard_raster_options_t options;
    std::array<std::vector<std::uint8_t>, storyboard_raster_options_t::maximum_frame_slots> frames;
    std::size_t next_slot{};
    std::span<std::uint8_t> output;
    std::span<const raster_quad_t> quads;
    std::barrier<> barrier;
    std::vector<std::thread> workers;
    bool stopping{};

    explicit impl_t(storyboard_raster_options_t value)
        : options(value), barrier(static_cast<std::ptrdiff_t>(value.threads)) {
        for (std::size_t slot = 0; slot < options.frame_slots; ++slot)
            frames[slot].resize(static_cast<std::size_t>(options.width) * options.height * 4);
    }
    void run_bands(std::size_t worker) {
        for (auto top = static_cast<std::uint32_t>(worker) * band_height; top < options.height;
             top += static_cast<std::uint32_t>(options.threads) * band_height) {
            const auto bottom = std::min(top + band_height, options.height);
            auto band = output.subspan(static_cast<std::size_t>(top) * options.width * 4,
                static_cast<std::size_t>(bottom - top) * options.width * 4);
            std::ranges::fill(band, 0);
            for (std::size_t alpha = 3; alpha < band.size(); alpha += 4) band[alpha] = 255;
            for (const auto& quad : quads) draw_quad(quad, output, options.width, top, bottom);
        }
    }
    void start_workers() {
        try {
            workers.reserve(options.threads - 1);
            for (std::size_t worker = 1; worker < options.threads; ++worker)
                workers.emplace_back([this, worker] {
                    for (;;) {
                        barrier.arrive_and_wait();
                        if (stopping) break;
                        run_bands(worker);
                        barrier.arrive_and_wait();
                    }
                });
        } catch (...) {
            // Drop never-created participants, then release/join the live ones.
            for (std::size_t i = workers.size() + 1; i < options.threads; ++i) barrier.arrive_and_drop();
            throw;
        }
    }
    ~impl_t() {
        stopping = true;
        barrier.arrive_and_wait();
        for (auto& worker : workers) worker.join();
    }
};

storyboard_raster_t::storyboard_raster_t(std::unique_ptr<impl_t> impl) : impl_(std::move(impl)) {}
storyboard_raster_t::~storyboard_raster_t() = default;
storyboard_raster_t::storyboard_raster_t(storyboard_raster_t&&) noexcept = default;
auto storyboard_raster_t::operator=(storyboard_raster_t&&) noexcept -> storyboard_raster_t& = default;

auto storyboard_raster_t::from(storyboard_raster_options_t options)
    -> result_t<storyboard_raster_t, error_t<failure_code_t>> {
    if (options.width == 0 || options.height == 0 || options.width > storyboard_raster_options_t::maximum_width ||
        options.height > storyboard_raster_options_t::maximum_height ||
        options.threads == 0 || options.threads > storyboard_raster_options_t::maximum_threads ||
        options.frame_slots == 0 || options.frame_slots > storyboard_raster_options_t::maximum_frame_slots ||
        options.maximum_instances == 0 || options.maximum_instances > storyboard_raster_options_t::maximum_supported_instances)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Invalid storyboard raster bounds"});
    try {
        auto impl = std::make_unique<impl_t>(options);
        impl->start_workers();
        return storyboard_raster_t{std::move(impl)};
    } catch (const std::exception& error) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::allocation_failed, error.what()});
    }
}

auto storyboard_raster_t::render(std::span<const storyboard_draw_instance_t> instances,
    std::span<const storyboard_raster_image_t> images, frame_arena_t& arena,
    storyboard_conditional_layer_t conditional_layer)
    -> result_t<storyboard_raster_frame_t, error_t<failure_code_t>> {
    if (!impl_ || instances.size() > impl_->options.maximum_instances)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded, "Storyboard raster instance limit exceeded"});
    // The evaluator may have used this same arena: do not reset its borrowed
    // draw data. The caller resets once at the next evaluate/render boundary.
    auto memory = arena.allocate(instances.size() * sizeof(raster_quad_t), alignof(raster_quad_t));
    if (!memory) return std::unexpected(error_t<failure_code_t>{memory.error(), "Storyboard raster arena exhausted"});
    auto* quads = static_cast<raster_quad_t*>(*memory);
    std::size_t count{};
    for (const auto& instance : instances) {
        if (!is_storyboard_layer_visible(instance.layer, conditional_layer) || instance.opacity <= 0) continue;
        const auto image = std::ranges::find(images, instance.path, &storyboard_raster_image_t::path);
        if (image == images.end() || !image->image) continue;
        const auto quad = raster_quad_t::from(instance, *image->image, impl_->options);
        if (quad) std::construct_at(quads + count++, *quad);
    }
    impl_->quads = {quads, count};
    impl_->output = impl_->frames[impl_->next_slot];
    impl_->next_slot = (impl_->next_slot + 1) % impl_->options.frame_slots;
    impl_->barrier.arrive_and_wait();
    impl_->run_bands(0);
    impl_->barrier.arrive_and_wait();
    return storyboard_raster_frame_t{.width = impl_->options.width, .height = impl_->options.height,
        .pixels = impl_->output};
}
} // namespace wormedit
