#include <wormedit/audio/waveform_job.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string_view>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto register_fixture(wormedit::audio::decoded_source_registry_t& registry,
                      wormedit::asset_id_t asset, std::string_view name)
    -> wormedit::audio::source_registration_result_t {
    return registry.register_source({
        .asset = asset,
        .path = fixture_path(name),
        .source = {"fixture:" + std::string(name), "waveform-job:v1"},
    });
}

} // namespace

TEST_CASE("waveform job publishes the newest bounded scheduler request",
          "[unit][audio][waveform-job][fixture]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    REQUIRE(register_fixture(sources, asset_id_t{1}, "sliced-seek-source.ogg"));
    REQUIRE(register_fixture(sources, asset_id_t{2}, "keysound-c4.ogg"));
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    task_scheduler_t scheduler{1, 4};
    waveform_job_t job{sources, blocks, waveforms, scheduler};

    const auto superseded = job.request(asset_id_t{1}, 64);
    REQUIRE(superseded);
    const auto current = job.request(asset_id_t{2}, 32);
    REQUIRE(current);
    CHECK(current.generation != superseded.generation);
    CHECK(job.busy());

    const auto completion = job.wait();
    REQUIRE(*completion);
    CHECK(completion->generation == current.generation);
    CHECK(completion->asset == asset_id_t{2});
    REQUIRE(completion->analysis->waveform.has_value());
    CHECK(completion->analysis->waveform->key.source.asset_identity == "fixture:keysound-c4.ogg");
    CHECK_FALSE(job.busy());
}

TEST_CASE("waveform job rejects invalid and zero-capacity requests",
          "[unit][audio][waveform-job][bounds]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    task_scheduler_t scheduler{1, 1};
    waveform_job_t job{sources, blocks, waveforms, scheduler, 0};

    CHECK(job.request(asset_id_t{1}, 32).status == waveform_job_start_status_t::invalid_request);
    CHECK(job.request({}, 32).status == waveform_job_start_status_t::invalid_request);
    CHECK_FALSE(job.poll().has_value());
    CHECK_FALSE(job.busy());
}
