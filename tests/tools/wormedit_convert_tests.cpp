#include <wormedit/formats/bms.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

namespace {

using json = nlohmann::json;

struct tool_run_t {
    int status{};
    json result;
};

class scratch_directory_t {
  public:
    explicit scratch_directory_t(const std::string& name)
        : path_(std::filesystem::temp_directory_path() / ("wormedit-convert-test-" + name)) {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~scratch_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }

  private:
    std::filesystem::path path_;
};

auto fixture(const std::string& relative) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / relative;
}

auto read_file(const std::filesystem::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto run_tool(const scratch_directory_t& scratch, json request) -> tool_run_t {
    const auto request_path = scratch.path() / "request.json";
    const auto result_path = scratch.path() / "result.json";
    std::ofstream(request_path) << request.dump();
    auto command = "\"" + std::string(WORMEDIT_CONVERT_EXE) + "\" \"" + request_path.string() +
                   "\" > \"" + result_path.string() + "\"";
#if defined(_WIN32)
    // cmd.exe /c strips the first and last quote of a command that starts with a quote.
    command = "\"" + command + "\"";
#endif
    const auto raw = std::system(command.c_str());
#if defined(_WIN32)
    const auto status = raw;
#else
    const auto status = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
#endif
    return {status, json::parse(read_file(result_path), nullptr, false)};
}

constexpr const char* plain_sm = R"(#TITLE:Plain;
#ARTIST:Tester;
#MUSIC:song.ogg;
#OFFSET:0.000;
#BPMS:0.000=120.000;
#NOTES:
     dance-single:
     :
     Challenge:
     7:
     0,0,0,0,0:
1000
0100
0010
0001
;
)";

constexpr const char* plain_bms = "#PLAYER 1\n#TITLE Plain BMS\n#ARTIST Tester\n#BPM 120\n"
                                  "#WAV01 a.wav\n#00011:01010101\n#00012:01000100\n#00013:01\n#00014:01\n"
                                  "#00015:01\n#00016:01\n#00018:01\n#00019:01\n";

auto write_source(const scratch_directory_t& scratch, const std::string& name,
                  const std::string& text) -> std::filesystem::path {
    const auto path = scratch.path() / name;
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

auto bytes_of(const std::string& text) -> std::span<const std::byte> {
    return std::as_bytes(std::span{text.data(), text.size()});
}

auto count_files(const std::filesystem::path& directory, const std::string& extension) -> int {
    int count{};
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == extension) ++count;
    }
    return count;
}

} // namespace

TEST_CASE("wormedit-convert turns a StepMania chart into osu!mania and re-imports equal",
          "[unit][tools][convert]") {
    const scratch_directory_t scratch{"sm-osu"};
    const auto output = scratch.path() / "out";
    std::filesystem::create_directories(output);
    const auto input = write_source(scratch, "plain.sm", plain_sm);
    const auto run = run_tool(scratch, {{"input", input.string()},
                                        {"target", "osu_mania"},
                                        {"target_profile", "stable-v14"},
                                        {"loss_policy", "allow"},
                                        {"output_directory", output.string()}});
    INFO(run.result.dump(2));
    REQUIRE(run.result.is_object());
    CHECK(run.status == 0);
    REQUIRE(run.result["ok"].get<bool>());
    REQUIRE(run.result["artifacts"].size() == 1);
    const auto written = read_file(run.result["artifacts"][0]["path"].get<std::string>());
    const auto reparsed = wormedit::parse_osu_mania_bytes(bytes_of(written));
    REQUIRE(reparsed.song);
    const auto source = wormedit::parse_stepmania_bytes(bytes_of(plain_sm));
    REQUIRE(source.song);
    CHECK(reparsed.song->metadata.title == source.song->metadata.title);
    CHECK(reparsed.song->metadata.artist == source.song->metadata.artist);
    const auto count_notes = [](const wormedit::song_document_t& song) {
        std::size_t notes{};
        for (const auto& measure : song.charts.front().measures) notes += measure.notes.size();
        return notes;
    };
    CHECK(count_notes(*reparsed.song) == count_notes(*source.song));
    CHECK(count_notes(*reparsed.song) == 4);
}

TEST_CASE("wormedit-convert reports import quantization without rejecting the chart",
          "[unit][tools][convert][osu-mania]") {
    const scratch_directory_t scratch{"osu-import-warning"};
    const auto output = scratch.path() / "out";
    std::filesystem::create_directories(output);
    const auto input = write_source(
        scratch, "quantized.osu",
        "osu file format v14\n[General]\nMode:3\n"
        "[Metadata]\nTitle:Quantized\nArtist:Test\nCreator:Test\nVersion:Test\n"
        "[Difficulty]\nCircleSize:4\n[TimingPoints]\n"
        "0,500,4,2,0,100,1,0\n1000.123456789,400,4,2,0,100,1,0\n"
        "[HitObjects]\n64,192,1000,1,0,0:0:0:0:\n");
    const auto run = run_tool(scratch, {{"input", input.string()},
                                       {"target", "native_project"},
                                       {"output_directory", output.string()}});
    INFO(run.result.dump(2));
    CHECK(run.status == 0);
    REQUIRE(run.result["ok"].get<bool>());
    REQUIRE(run.result["artifacts"].size() == 1);
    REQUIRE(run.result["import_warnings"].size() == 1);
    CHECK(run.result["import_warnings"][0].get<std::string>().find("960") != std::string::npos);
}

TEST_CASE("wormedit-convert re-exports an unedited BMS chart byte for byte",
          "[unit][tools][convert]") {
    const scratch_directory_t scratch{"bms-bms"};
    const auto output = scratch.path() / "out";
    std::filesystem::create_directories(output);
    const auto input = write_source(scratch, "plain.bms", plain_bms);
    const auto run = run_tool(scratch, {{"input", input.string()},
                                        {"target", "bms"},
                                        {"loss_policy", "allow"},
                                        {"output_directory", output.string()}});
    INFO(run.result.dump(2));
    REQUIRE(run.result.is_object());
    CHECK(run.status == 0);
    REQUIRE(run.result["ok"].get<bool>());
    REQUIRE(run.result["artifacts"].size() == 1);
    CHECK(read_file(run.result["artifacts"][0]["path"].get<std::string>()) == plain_bms);
}

TEST_CASE("wormedit-convert rejects losses by default and writes nothing",
          "[unit][tools][convert]") {
    const scratch_directory_t scratch{"reject"};
    const auto output = scratch.path() / "out";
    std::filesystem::create_directories(output);
    const auto run = run_tool(
        scratch, {{"input", fixture("bms/repeated-bgm-ratio-stop-collision.bms").string()},
                  {"target", "stepmania"},
                  {"stepmania", {{"file_profile", "ssc"}, {"timing_syntax", "native_warps"},
                                 {"implementation_profile", "stepmania_5_1"}}},
                  {"branches", "active_projection"},
                  {"output_directory", output.string()}});
    REQUIRE(run.result.is_object());
    CHECK(run.status != 0);
    CHECK(run.result["diagnostics"].dump().find("loss") != std::string::npos);
    CHECK_FALSE(run.result["ok"].get<bool>());
    CHECK(std::filesystem::is_empty(output));
}

TEST_CASE("wormedit-convert reports invalid requests as JSON errors without writing",
          "[unit][tools][convert]") {
    const scratch_directory_t scratch{"invalid"};
    const auto output = scratch.path() / "out";
    std::filesystem::create_directories(output);
    const auto unknown_target = run_tool(
        scratch, {{"input", fixture("bms/repeated-bgm-ratio-stop-collision.bms").string()},
                  {"target", "nonsense"},
                  {"output_directory", output.string()}});
    CHECK(unknown_target.status != 0);
    REQUIRE(unknown_target.result.is_object());
    CHECK_FALSE(unknown_target.result["ok"].get<bool>());
    CHECK(unknown_target.result.contains("error"));

    const auto bad_chart = run_tool(
        scratch, {{"input", fixture("bms/repeated-bgm-ratio-stop-collision.bms").string()},
                  {"target", "bms"},
                  {"charts", json::array({7})},
                  {"output_directory", output.string()}});
    CHECK(bad_chart.status != 0);
    CHECK(std::filesystem::is_empty(output));

    const auto missing_input = run_tool(
        scratch, {{"input", (scratch.path() / "absent.bms").string()},
                  {"target", "bms"},
                  {"output_directory", output.string()}});
    CHECK(missing_input.status != 0);
    CHECK(std::filesystem::is_empty(output));
}

TEST_CASE("wormedit-convert keeps existing files unless overwrite is requested",
          "[unit][tools][convert]") {
    const scratch_directory_t scratch{"overwrite"};
    const auto output = scratch.path() / "out";
    std::filesystem::create_directories(output);
    const json request{{"input", fixture("bms/repeated-bgm-ratio-stop-collision.bms").string()},
                       {"target", "bms"},
                       {"loss_policy", "allow"},
                       {"output_directory", output.string()}};
    const auto first = run_tool(scratch, request);
    INFO(first.result.dump(2));
    REQUIRE(first.status == 0);
    const auto path = first.result["artifacts"][0]["path"].get<std::string>();
    std::ofstream(path, std::ios::trunc) << "sentinel";

    const auto second = run_tool(scratch, request);
    CHECK(second.status != 0);
    CHECK(read_file(path) == "sentinel");

    auto allowed = request;
    allowed["overwrite"] = true;
    const auto third = run_tool(scratch, allowed);
    CHECK(third.status == 0);
    CHECK(read_file(path) != "sentinel");
}

namespace {
void require_cli(bool value) { REQUIRE(value); }
void check_cli(bool value) { CHECK(value); }
auto write_two_silent_bmson_charts(const scratch_directory_t& scratch) -> std::filesystem::path {
    constexpr std::string_view text = R"({"version":"1.0.0","info":{"title":"silent","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[]})";
    auto parsed = wormedit::parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    require_cli(parsed.song.has_value());
    auto second = parsed.song->charts.front();
    second.id = wormedit::chart_id_t{second.id.value + 1000};
    parsed.song->charts.push_back(second);
    const auto saved = wormedit::write_native_project(*parsed.song);
    require_cli(saved.bytes.has_value());
    const auto input = scratch.path() / "multi.wormedit";
    std::ofstream(input) << *saved.bytes;
    return input;
}
}

TEST_CASE("wormedit-convert publishes multiple charts inside dotted directories",
          "[unit][tools][convert][conversion]") {
    scratch_directory_t scratch("dotted-multi");
    const auto input = write_two_silent_bmson_charts(scratch);
    const auto output = scratch.path() / "kick-v1.2";
    std::filesystem::create_directory(output);
    const auto run = run_tool(scratch, {{"input", input.string()},
        {"output_directory", output.string()}, {"target", "osu_mania"},
        {"target_profile", "lazer-v128"}, {"loss_policy", "warn"}});
    require_cli(run.status == 0);
    require_cli(run.result.at("ok").get<bool>());
    require_cli(run.result.at("artifacts").size() == 2);
    for (const auto& artifact : run.result.at("artifacts")) {
        const std::filesystem::path path{artifact.at("path").get<std::string>()};
        check_cli(path.parent_path() == output);
        check_cli(std::filesystem::is_regular_file(path));
        check_cli(path.extension() == ".osu");
    }
    check_cli(!std::filesystem::exists(scratch.path() / "chart-1.osu"));
}

namespace {
auto write_overcapacity_silent_bmson(const scratch_directory_t& scratch) -> std::filesystem::path {
    const auto input = write_two_silent_bmson_charts(scratch);
    auto parsed = wormedit::parse_native_project(read_file(input));
    require_cli(parsed.song.has_value());
    auto& song = *parsed.song;
    song.charts.resize(1);
    auto& chart = song.charts.front();
    chart.measures.front().notes.push_back({.id = wormedit::object_id_t{10000},
        .position = {chart.measures.front().id, 0}, .lane = chart.lane_layouts.front().layout.lanes.front().id});
    song.audio_assets.push_back({.id = wormedit::asset_id_t{90000}, .source_path = "missing.wav"});
    for (std::uint64_t index = 0; index < 1295; ++index)
        song.resources.push_back({.id = wormedit::resource_id_t{90001 + index},
            .name = "missing.wav", .asset = song.audio_assets.front().id,
            .roles = {wormedit::resource_role_t::music}});
    const auto saved = wormedit::write_native_project(song);
    require_cli(saved.bytes.has_value());
    std::ofstream(input) << *saved.bytes;
    return input;
}
}
TEST_CASE("wormedit-convert rejects BMS sound capacity before any generated writes",
          "[unit][tools][convert][conversion]") {
    scratch_directory_t scratch("bms-capacity");
    const auto input = write_overcapacity_silent_bmson(scratch);
    const auto output = scratch.path() / "bms-v1.2";
    std::filesystem::create_directory(output);
    std::ofstream(output / "manual.txt") << "keep";
    const auto run = run_tool(scratch, {{"input", input.string()},
        {"output_directory", output.string()}, {"target", "bms"}, {"loss_policy", "warn"}});
    check_cli(run.status != 0);
    check_cli(!run.result.at("ok").get<bool>());
    check_cli(run.result.dump().find("needs 1296 sounds, BMS holds 1295") != std::string::npos);
    check_cli(read_file(output / "manual.txt") == "keep");
    check_cli(std::distance(std::filesystem::directory_iterator(output), std::filesystem::directory_iterator{}) == 1);
}
