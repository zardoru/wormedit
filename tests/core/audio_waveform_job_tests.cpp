#include <wormedit/audio/waveform_job.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <latch>
#include <span>
#include <stop_token>
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

auto has_failed_analysis_in_completed_job() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    task_scheduler_t scheduler{1, 1};
    waveform_job_t job{sources, blocks, waveforms, scheduler};
    const auto started = job.begin(asset_id_t{7}, 32);
    if (!started) return false;
    auto completion = job.wait_completion();
    if (!completion || !*completion) return false;
    const auto& completed = completion->value();
    if (!completed.payload || completed.payload->status != waveform_analysis_status_t::decoder_unavailable)
        return false;
    return completed.generation == *started && completed.identity == asset_id_t{7} &&
           completed.payload != nullptr && !job.is_busy() &&
           !job.poll_completion();
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

    const auto superseded = job.begin(asset_id_t{1}, 64);
    REQUIRE(superseded);
    const auto current = job.begin(asset_id_t{2}, 32);
    REQUIRE(current);
    CHECK(*current != *superseded);
    CHECK(job.is_busy());

    const auto completion = job.wait_completion();
    const auto& completed = completion.value().value();
    REQUIRE(completed.payload);
    CHECK(completed.generation == *current);
    CHECK(completed.identity == asset_id_t{2});
    REQUIRE(completed.payload->waveform.has_value());
    CHECK(completed.payload->waveform->key.source.asset_identity == "fixture:keysound-c4.ogg");
    CHECK_FALSE(job.is_busy());
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

    CHECK(job.begin(asset_id_t{1}, 32).error().code == waveform_job_start_status_t::invalid_request);
    CHECK(job.begin({}, 32).error().code == waveform_job_start_status_t::invalid_request);
    CHECK_FALSE(job.poll_completion().has_value());
    CHECK_FALSE(job.is_busy());
}

TEST_CASE("waveform job preserves scheduler capacity rejection",
          "[unit][audio][waveform-job][bounds]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    decoded_source_registry_t sources;
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    std::latch worker_started{1};
    std::latch release_worker{1};
    task_scheduler_t scheduler{1, 1};
    task_graph_t blocker{1};
    const auto blocker_task = blocker.add_task({}, [&](std::span<const task_result_t>,
                                                       std::stop_token) {
        worker_started.count_down();
        release_worker.wait();
        return task_result_t::success();
    });
    REQUIRE(blocker_task);
    auto blocker_submission = scheduler.submit(std::move(blocker));
    REQUIRE(blocker_submission);
    worker_started.wait();

    waveform_job_t job{sources, blocks, waveforms, scheduler};
    const auto rejected = job.begin(asset_id_t{1}, 32);
    release_worker.count_down();
    REQUIRE(blocker_submission.graph.wait());

    CHECK_FALSE(rejected);
    CHECK(rejected.error().code == waveform_job_start_status_t::capacity_exceeded);
    CHECK(rejected.error().message == "scheduler outstanding-task capacity exceeded");
    CHECK_FALSE(job.is_busy());
}

TEST_CASE("completed waveform worker retains an unavailable analysis payload",
          "[unit][audio][waveform-job][negative]") {
    CHECK(has_failed_analysis_in_completed_job());
}
