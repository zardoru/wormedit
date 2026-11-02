#include <wormedit/visual/image_job.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <latch>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace wormedit;
using namespace wormedit::visual;

auto has_item_error(const image_job_item_outcome_t& item, failure_code_t status,
                    const resolved_visual_asset_t& requested) -> bool {
    return !item && item.error().cause.code == status && !item.error().cause.message.empty() &&
           item.error().context.resource == requested.resource &&
           item.error().context.asset == requested.asset && item.error().context.path == requested.path;
}

constexpr std::array<std::uint8_t, 62> two_pixel_bmp{
    0x42, 0x4d, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00,
};

struct temporary_image_t {
    std::filesystem::path path;

    explicit temporary_image_t(std::string suffix, std::uint8_t red = 255) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("wormedit-image-job-" + std::to_string(nonce) + "-" + std::move(suffix) + ".bmp");
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        auto pixels = two_pixel_bmp;
        pixels[56] = red;
        output.write(reinterpret_cast<const char*>(pixels.data()),
                     static_cast<std::streamsize>(pixels.size()));
    }

    ~temporary_image_t() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

auto source(resource_id_t resource, asset_id_t asset, const std::filesystem::path& path)
    -> resolved_visual_asset_t {
    return {.resource = resource,
            .asset = asset,
            .kind = asset_kind_t::image,
            .path = path,
            .declared_width = 2,
            .declared_height = 1};
}

auto image_for(const resolved_visual_asset_t& item, std::size_t byte_count = 8)
    -> std::shared_ptr<const rgba_image_t> {
    return std::make_shared<const rgba_image_t>(
        rgba_image_t{.resource = item.resource,
                     .asset = item.asset,
                     .path = item.path,
                     .width = 2,
                     .height = 1,
                     .pixels = std::vector<std::uint8_t>(byte_count)});
}

} // namespace

TEST_CASE("image cache is revision-aware and evicts within byte and entry bounds",
          "[unit][visual][image-cache]") {
    temporary_image_t first{"first"};
    temporary_image_t second{"second"};
    const auto source_a = source(resource_id_t{1}, asset_id_t{11}, first.path);
    const auto source_b = source(resource_id_t{2}, asset_id_t{22}, second.path);
    image_cache_t cache{8, 1};

    REQUIRE(cache.insert(song_id_t{3}, 4, source_a, image_for(source_a)));
    CHECK(cache.size() == 1);
    CHECK(cache.rgba_bytes() == 8);
    CHECK_FALSE(cache.find(song_id_t{3}, 5, source_a));
    CHECK_FALSE(cache.find(song_id_t{4}, 4, source_a));
    REQUIRE(cache.find(song_id_t{3}, 4, source_a));

    REQUIRE(cache.insert(song_id_t{3}, 4, source_b, image_for(source_b)));
    CHECK_FALSE(cache.find(song_id_t{3}, 4, source_a));
    REQUIRE(cache.find(song_id_t{3}, 4, source_b));
    CHECK(cache.size() == 1);
    CHECK(cache.rgba_bytes() == 8);
}

TEST_CASE("image job publishes only the newest revision-bound request",
          "[unit][visual][image-job]") {
    temporary_image_t first{"superseded"};
    temporary_image_t second{"current"};
    const auto source_a = source(resource_id_t{1}, asset_id_t{11}, first.path);
    const auto source_b = source(resource_id_t{2}, asset_id_t{22}, second.path);
    image_cache_t cache{64, 4};
    task_scheduler_t scheduler{1, 4};
    image_job_t job{cache, scheduler};

    const auto superseded = job.request(song_id_t{3}, 10, std::span{&source_a, 1U});
    REQUIRE(superseded);
    const auto current = job.request(song_id_t{3}, 11, std::span{&source_b, 1U});
    REQUIRE(current);
    CHECK(*current != *superseded);
    CHECK(job.is_busy());

    const auto completion = job.wait();
    REQUIRE(completion);
    REQUIRE(*completion);
    CHECK(completion->value().generation == *current);
    CHECK(completion->value().identity.project == song_id_t{3});
    CHECK(completion->value().identity.document_revision == 11);
    REQUIRE(completion->value().payload->items.size() == 1);
    REQUIRE(completion->value().payload->items.front());
    CHECK(completion->value().payload->items.front()->payload->pixels.size() == 8);
    CHECK_FALSE(job.is_busy());
}

TEST_CASE("image job retains per-source failures and bounds aggregate publication",
          "[unit][visual][image-job][negative]") {
    temporary_image_t image{"bounded"};
    const auto valid = source(resource_id_t{1}, asset_id_t{11}, image.path);
    auto missing = source(resource_id_t{2}, asset_id_t{22}, image.path.string() + ".missing");
    const std::array sources{valid, missing};
    image_cache_t cache{64, 4};
    task_scheduler_t scheduler{1, 2};
    image_job_t job{cache, scheduler};
    image_job_options_t options;
    options.maximum_result_rgba_bytes = 7;

    REQUIRE(job.request(song_id_t{3}, 20, sources, options));
    const auto completion = job.wait();
    REQUIRE(completion);
    REQUIRE(*completion);
    REQUIRE(completion->value().payload->items.size() == 2);
    CHECK(has_item_error(completion->value().payload->items[0], failure_code_t::capacity_exceeded, valid));
    CHECK(has_item_error(completion->value().payload->items[1], failure_code_t::io_failed, missing));
    CHECK(cache.size() == 0);

    CHECK(job.request(song_id_t{}, 20, sources).error().code ==
          failure_code_t::invalid_request);
    CHECK(job.request(song_id_t{3}, 0, sources).error().code ==
          failure_code_t::invalid_request);
    CHECK(job.request(song_id_t{3}, 20, std::span<const resolved_visual_asset_t>{}).error().code ==
          failure_code_t::invalid_request);

    image_job_t disabled{cache, scheduler, 5, 0};
    CHECK(disabled.request(song_id_t{3}, 20, sources).error().code ==
          failure_code_t::invalid_request);
}

TEST_CASE("visual job publication requires a payload and preserves success messages",
          "[unit][visual][image-job][negative]") {
    const auto item_source = source(resource_id_t{4}, asset_id_t{5}, "synthetic.bmp");
    auto missing = image_job_item_value_t::from(item_source, {}, "missing payload");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().cause.code == failure_code_t::invalid_request);
    auto present = image_job_item_value_t::from(item_source,
        std::make_shared<const rgba_image_t>(), "cached image");
    REQUIRE(present.has_value());
    CHECK(present->message == "cached image");
    const image_job_item_outcome_t failed = std::unexpected(image_job_item_outcome_t::error_type{
        .context = item_source, .payload = present->payload,
        .cause = {failure_code_t::decode_failed, "failed image"}});
    REQUIRE_FALSE(failed);
    CHECK(failed.error().payload == present->payload);
}

TEST_CASE("image batch lookup preserves the first source result",
          "[unit][visual][image-job][negative]") {
    const auto first = source(resource_id_t{4}, asset_id_t{5}, "first.bmp");
    const auto other = source(resource_id_t{6}, asset_id_t{7}, "other.bmp");
    const auto image = std::make_shared<const rgba_image_t>();
    const image_job_result_t batch{.project = song_id_t{1}, .document_revision = 2, .items = {
        std::unexpected(image_job_item_outcome_t::error_type{
            .context = first, .payload = {}, .cause = {failure_code_t::io_failed, "unreadable"}}),
        image_job_item_value_t::from(first, image),
        image_job_item_value_t::from(other, image)}};
    CHECK(batch.find(first.resource) == nullptr);
    REQUIRE(batch.find(other.resource));
    CHECK(batch.find(other.resource)->payload == image);
    CHECK(batch.find(resource_id_t{99}) == nullptr);
}

namespace {
struct tiny_image_batch_t {
    std::vector<std::unique_ptr<temporary_image_t>> files;
    std::vector<resolved_visual_asset_t> sources;
    [[nodiscard]] static auto from(std::size_t count) -> tiny_image_batch_t {
        tiny_image_batch_t result;
        result.files.reserve(count); result.sources.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            result.files.push_back(std::make_unique<temporary_image_t>("distinct-" + std::to_string(i),
                static_cast<std::uint8_t>(i % 251 + 1)));
            result.sources.push_back(source(resource_id_t{i + 1}, asset_id_t{i + 11}, result.files.back()->path));
        }
        return result;
    }
};
void check_count_admission_refused(image_job_t& job, const task_scheduler_t& scheduler,
    const image_cache_t& cache, std::span<const resolved_visual_asset_t> sources) {
    const auto rejected = job.request(song_id_t{3}, 20, sources);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == failure_code_t::invalid_request);
    CHECK(scheduler.outstanding_tasks() == 0);
    CHECK(cache.size() == 0);
    CHECK_FALSE(job.is_busy());
}
void check_distinct_decoded_item(const image_job_item_outcome_t& item,
    const resolved_visual_asset_t& requested, std::size_t index) {
    REQUIRE(item);
    CHECK(item->context.resource == requested.resource);
    CHECK(item->context.asset == requested.asset);
    CHECK(item->payload->path == requested.path);
    REQUIRE(item->payload->pixels.size() == 8);
    CHECK(item->payload->pixels[0] == static_cast<std::uint8_t>(index % 251 + 1));
}
void check_published_image_cache(image_cache_t& cache, const image_job_t& job,
    std::span<const resolved_visual_asset_t> sources) {
    const auto count = sources.size();
    CHECK(cache.size() == count);
    CHECK(cache.rgba_bytes() == 8 * count);
    REQUIRE(cache.find(song_id_t{3}, 20, sources.front()));
    REQUIRE(cache.find(song_id_t{3}, 20, sources[count - 1]));
    CHECK_FALSE(job.is_busy());
}
void check_disabled_count_admission(image_cache_t& cache, task_scheduler_t& scheduler,
    std::span<const resolved_visual_asset_t> sources) {
    image_job_t disabled{cache, scheduler, 0};
    CHECK_FALSE(disabled.request(song_id_t{3}, 20, sources.first(1)));
    CHECK(scheduler.outstanding_tasks() == 0);
}
} // namespace

TEST_CASE("Image jobs honor explicit large count budgets with distinct decoded sources and bounded publication",
          "[unit][visual][image-job][configured-budget]") {
    const auto count = GENERATE(std::size_t{33}, std::size_t{1030});
    const auto batch = tiny_image_batch_t::from(count + 1);
    // Keep the normal byte ceiling; only the explicit entry/count budgets differ.
    image_cache_t cache{256U * 1024U * 1024U, count};
    task_scheduler_t scheduler{1, 4};
    image_job_t job{cache, scheduler, count};
    check_count_admission_refused(job, scheduler, cache, batch.sources);
    const auto admitted = job.request(song_id_t{3}, 20, std::span{batch.sources}.first(count));
    REQUIRE(admitted);
    const auto completion = job.wait();
    REQUIRE(completion);
    REQUIRE(*completion);
    CHECK(completion->value().generation == *admitted);
    const auto& items = completion->value().payload->items;
    REQUIRE(items.size() == count);
    for (std::size_t i = 0; i < count; ++i)
        check_distinct_decoded_item(items[i], batch.sources[i], i);
    check_published_image_cache(cache, job, std::span{batch.sources}.first(count));
}
TEST_CASE("Image jobs retain default five and zero count admission bounds", "[unit][visual][image-job][negative]") {
    const auto batch = tiny_image_batch_t::from(6);
    image_cache_t cache;
    task_scheduler_t scheduler{1, 4};
    image_job_t defaults{cache, scheduler};
    CHECK_FALSE(defaults.request(song_id_t{3}, 20, batch.sources));
    CHECK(scheduler.outstanding_tasks() == 0);
    REQUIRE(defaults.request(song_id_t{3}, 20, std::span{batch.sources}.first(5)));
    const auto completion = defaults.wait();
    REQUIRE(completion);
    REQUIRE(*completion);
    CHECK(completion->value().payload->items.size() == 5);
    check_disabled_count_admission(cache, scheduler, batch.sources);
}

TEST_CASE("Configured image batches retain cancellation before worker decode", "[unit][visual][image-job][configured-budget]") {
    const auto batch = tiny_image_batch_t::from(33);
    image_cache_t cache;
    std::latch release{1};
    task_scheduler_t scheduler{1, 4, {.on_worker_start = [&] { release.wait(); }}};
    image_job_t job{cache, scheduler, 33};
    const auto started = job.request(song_id_t{3}, 20, batch.sources);
    job.cancel();
    release.count_down(); // Release before assertions so failure cannot strand the worker.
    job.shutdown();
    REQUIRE(started);
    CHECK_FALSE(job.is_busy());
    CHECK_FALSE(job.poll());
    CHECK(cache.size() == 0);
    CHECK(cache.rgba_bytes() == 0);
    CHECK(scheduler.outstanding_tasks() == 0);
}
