#include <wormedit/formats/bms.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/core/storyboard_preview.h>
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
    REQUIRE(reparsed.value);
    const auto source = wormedit::parse_stepmania_bytes(bytes_of(plain_sm));
    REQUIRE(source.value);
    CHECK(reparsed.value->metadata.title == source.value->metadata.title);
    CHECK(reparsed.value->metadata.artist == source.value->metadata.artist);
    const auto count_notes = [](const wormedit::song_document_t& song) {
        std::size_t notes{};
        for (const auto& measure : song.charts.front().measures) notes += measure.notes.size();
        return notes;
    };
    CHECK(count_notes(*reparsed.value) == count_notes(*source.value));
    CHECK(count_notes(*reparsed.value) == 4);
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
                  {"input_bms_lane_profile", "bme-7k-outside-scratch"},
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
                  {"input_bms_lane_profile", "bme-7k-outside-scratch"},
                  {"target", "nonsense"},
                  {"output_directory", output.string()}});
    CHECK(unknown_target.status != 0);
    REQUIRE(unknown_target.result.is_object());
    CHECK_FALSE(unknown_target.result["ok"].get<bool>());
    CHECK(unknown_target.result.contains("error"));

    const auto bad_chart = run_tool(
        scratch, {{"input", fixture("bms/repeated-bgm-ratio-stop-collision.bms").string()},
                  {"input_bms_lane_profile", "bme-7k-outside-scratch"},
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
                  {"input_bms_lane_profile", "bme-7k-outside-scratch"},
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
    require_cli(parsed.value.has_value());
    auto second = parsed.value->charts.front();
    second.id = wormedit::chart_id_t{second.id.value + 1000};
    parsed.value->charts.push_back(second);
    const auto saved = wormedit::write_native_project(*parsed.value);
    require_cli(saved.value.has_value());
    const auto input = scratch.path() / "multi.wormedit";
    std::ofstream(input) << *saved.value;
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
    require_cli(parsed.value.has_value());
    auto& song = *parsed.value;
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
    require_cli(saved.value.has_value());
    std::ofstream(input) << *saved.value;
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

namespace {
auto write_osbx_cli_project(const scratch_directory_t& scratch, bool skew) -> std::filesystem::path {
    using namespace wormedit;
    const std::string source = "osu file format v14\n[General]\nMode:3\n[Difficulty]\nCircleSize:4\n"
        "[TimingPoints]\n0,500,4,0,0,100,1,0\n[HitObjects]\n64,192,1000,1,0,0:0:0:0:\n";
    auto parsed = parse_osu_mania_bytes(bytes_of(source));
    require_cli(parsed.value.has_value());
    osbx_document_t scene{.images = {{.id = "image", .file = "images/sprite.ppm"}},
        .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite,
                   .parent = "parent", .image = "image",
                   .ops = {{.id = "offset", .track = {.default_value = {100, 0}}}}}}};
    osbx_transform_op_t turn{.id = "turn", .kind = osbx_transform_kind_t::rotate,
        .track = {.default_value = {0}, .keys = {{.id = "a", .value = {0}},
            {.id = "b", .time_ms = 2000, .value = {90}}}}};
    if (skew) turn = {.id = "offending-skew", .kind = osbx_transform_kind_t::skew,
                      .track = {.default_value = {10, 0}}};
    scene.nodes.push_back({.id = "parent", .ops = {turn}});
    parsed.value->storyboards = {{.id = storyboard_id_t{1}, .osbx = scene}};
    const auto saved = write_native_project(*parsed.value);
    require_cli(saved.value.has_value());
    return write_source(scratch, "osbx.wormedit", *saved.value);
}
auto write_native_actor_project(const scratch_directory_t& scratch) -> std::filesystem::path {
    using namespace wormedit;
    const auto input = write_osbx_cli_project(scratch, false);
    auto project = parse_native_project(read_file(input));
    require_cli(project.value.has_value());
    auto& scene = *project.value->storyboards.front().osbx;
    scene.nodes = {{.id = "pass-sprite", .kind = osbx_node_kind_t::sprite, .layer = osbx_layer_t::pass,
        .image = "image", .ops = {{.id = "position", .track = {.default_value = {320,240}}}}}};
    auto failed = scene.nodes.front();
    failed.id = "fail-sprite";
    failed.layer = osbx_layer_t::fail;
    failed.ops.front().id = "fail-position";
    scene.nodes.push_back(std::move(failed));
    const auto saved = write_native_project(*project.value);
    require_cli(saved.value.has_value());
    write_source(scratch, "osbx.wormedit", *saved.value);
    std::filesystem::create_directory(scratch.path() / "images");
    write_source(scratch, "images/sprite.ppm", "P6\n2 1\n255\nabcdef");
    return input;
}
auto native_actor_request(const std::filesystem::path& input, const std::filesystem::path& output,
    const char* mode = "actors", const char* conditional = "pass") -> json {
    return {{"input", input.string()}, {"output_directory", output.string()}, {"target", "stepmania"},
        {"loss_policy", "warn"}, {"overwrite", true},
        {"stepmania", {{"file_profile", "ssc"}, {"timing_syntax", "native_warps"},
            {"implementation_profile", "stepmania_5_1"}, {"storyboard", mode}, {"conditional_layer", conditional}}}};
}
}

TEST_CASE("wormedit-convert native actor mode selects the shared conditional layer", "[tools][convert][stepmania][storyboard]") {
    const auto* layer = GENERATE("pass", "fail", "neither");
    const scratch_directory_t scratch{std::string{"native-actors-"} + layer};
    const auto input = write_native_actor_project(scratch);
    const auto output = scratch.path() / "output";
    std::filesystem::create_directory(output);
    const auto run = run_tool(scratch, native_actor_request(input, output, "actors", layer));
    INFO(run.result.dump());
    require_cli(run.status == 0);
    check_cli(count_files(output, ".ssc") == 1);
    const auto script = output / "wormedit-storyboard/background/default.lua";
#ifdef WORMEDIT_TEST_HAS_IMAGE_DECODER
    if (std::string_view{layer} != "neither") {
        require_cli(std::filesystem::exists(script));
        const auto bytes = read_file(script);
        check_cli(bytes.find(std::string{layer} + "-sprite") != std::string::npos);
        const auto excluded = std::string_view{layer} == "pass" ? "fail-sprite" : "pass-sprite";
        check_cli(bytes.find(excluded) == std::string::npos);
        check_cli(read_file(output / "images/sprite.ppm") == "P6\n2 1\n255\nabcdef");
    } else check_cli(!std::filesystem::exists(script));
#else
    check_cli(!std::filesystem::exists(script));
#endif
}

TEST_CASE("wormedit-convert video and none modes keep the chart without a fake video", "[tools][convert][stepmania][storyboard]") {
    const auto* mode = GENERATE("video", "none");
    const scratch_directory_t scratch{std::string{"native-mode-"} + mode};
    const auto input = write_native_actor_project(scratch);
    const auto output = scratch.path() / "output";
    std::filesystem::create_directory(output);
    const auto run = run_tool(scratch, native_actor_request(input, output, mode));
    require_cli(run.status == 0);
    check_cli(count_files(output, ".ssc") == 1);
    check_cli(!std::filesystem::exists(output / "wormedit-storyboard"));
    if (std::string_view{mode} == "video")
        check_cli(run.result["diagnostics"].dump().find("video route is not yet available") != std::string::npos);
}

#ifdef WORMEDIT_TEST_HAS_IMAGE_DECODER
TEST_CASE("wormedit-convert atomically protects unrelated native actor files", "[tools][convert][stepmania][storyboard]") {
    const scratch_directory_t scratch{"native-ownership"};
    const auto input = write_native_actor_project(scratch);
    const auto output = scratch.path() / "output";
    const auto script = output / "wormedit-storyboard/background/default.lua";
    std::filesystem::create_directories(script.parent_path());
    std::ofstream(script) << "unrelated Lua";
    const auto request = native_actor_request(input, output);
    const auto refused = run_tool(scratch, request);
    check_cli(refused.status != 0);
    check_cli(read_file(script) == "unrelated Lua");
    check_cli(count_files(output, ".ssc") == 0);
    std::filesystem::remove(script);
    const auto published = run_tool(scratch, request);
    INFO(published.result.dump());
    require_cli(published.status == 0);
    check_cli(count_files(output, ".ssc") == 1);
    check_cli(read_file(script).find("GAMESTATE:GetSongPosition()") != std::string::npos);
    check_cli(published.result["artifacts"].dump().find("native-storyboard") != std::string::npos);
}
#endif

TEST_CASE("wormedit-convert rejects unknown native storyboard policy names", "[tools][convert][stepmania][storyboard]") {
    const auto* field = GENERATE("storyboard", "conditional_layer");
    const scratch_directory_t scratch{std::string{"native-invalid-"} + field};
    const auto input = write_native_actor_project(scratch);
    const auto output = scratch.path() / "output";
    auto request = native_actor_request(input, output);
    request["stepmania"][field] = "unknown";
    const auto refused = run_tool(scratch, request);
    check_cli(refused.status != 0);
    check_cli(refused.result.dump().find(std::string{"stepmania."} + field) != std::string::npos);
    check_cli(!std::filesystem::exists(output));
}

TEST_CASE("wormedit-convert atomically publishes OSBX with images and bake warnings",
          "[unit][tools][convert][osbx]") {
    scratch_directory_t scratch("osbx");
    const auto input = write_osbx_cli_project(scratch, false);
    auto project = wormedit::parse_native_project(read_file(input));
    require_cli(project.value.has_value());
    auto& scene = *project.value->storyboards.front().osbx;
    scene.nodes[1].ops.front().track.keys.back().time_ms = 500;
    scene.images.push_back({.id = "alias", .file = "images/./sprite.ppm"});
    scene.nodes.push_back({.id = "alias-sprite", .kind = wormedit::osbx_node_kind_t::sprite, .image = "alias"});
    const auto saved = wormedit::write_native_project(*project.value);
    require_cli(saved.value.has_value());
    std::ofstream(input) << *saved.value;

    const auto output = scratch.path() / "output";
    std::filesystem::create_directory(output);
    std::ofstream(output / "unrelated.txt") << "keep";
    std::filesystem::create_directory(scratch.path() / "images");
    const std::string image = "P6\n2 1\n255\nabcdef";
    write_source(scratch, "images/sprite.ppm", image);
    const auto run = run_tool(scratch, {{"input", input.string()}, {"output_directory", output.string()},
        {"target", "osu_mania"}, {"overwrite", true}});
#ifdef WORMEDIT_TEST_HAS_IMAGE_DECODER
    INFO(run.result.dump());
    require_cli(run.status == 0);
    check_cli(run.result["ok"] == true);
    check_cli(read_file(output / "images/sprite.ppm") == image);
    std::size_t storyboards{};
    for (const auto& artifact : run.result["artifacts"]) {
        const auto filename = artifact["filename"].get<std::string>();
        if (std::filesystem::path(filename).extension() != ".osb") continue;
        ++storyboards;
        const auto parsed = wormedit::parse_osb(read_file(output / filename));
        require_cli(parsed.value.has_value());
        check_cli(parsed.value->sprites.size() == 2);
        const auto index = wormedit::storyboard_preview_index_t::from(*parsed.value);
        require_cli(index.value.has_value());
        wormedit::storyboard_frame_arena_t arena;
        const auto frame = wormedit::evaluate_storyboard_frame(*parsed.value, *index.value, 1500, arena);
        require_cli(frame.is_success());
        check_cli(frame.instances.size() == 2);
    }
    check_cli(storyboards == 1);
    check_cli(run.result["diagnostics"].dump().find("worst normalized error") != std::string::npos);
    const auto repeated = run_tool(scratch, {{"input", input.string()}, {"output_directory", output.string()},
        {"target", "osu_mania"}, {"overwrite", true}});
    INFO(repeated.result.dump());
    check_cli(repeated.status == 0);
#else
    check_cli(run.status == 1);
    check_cli(run.result.dump().find("image decoding support") != std::string::npos);
    check_cli(std::distance(std::filesystem::directory_iterator(output), std::filesystem::directory_iterator{}) == 1);
#endif
    check_cli(read_file(output / "unrelated.txt") == "keep");
}

TEST_CASE("wormedit-convert OSBX preflight refusal leaves destination bytes untouched",
          "[unit][tools][convert][osbx]") {
    scratch_directory_t scratch("osbx-refusal");
    const auto input = write_osbx_cli_project(scratch, true);
    const auto output = scratch.path() / "output";
    std::filesystem::create_directory(output);
    std::ofstream(output / "sentinel.osb") << "original bytes";
    std::filesystem::create_directory(scratch.path() / "images");
    write_source(scratch, "images/sprite.ppm", "P6\n2 1\n255\nabcdef");
    const auto run = run_tool(scratch, {{"input", input.string()}, {"output_directory", output.string()},
        {"target", "osu_mania"}, {"overwrite", true}});
    check_cli(run.status == 1);
#ifdef WORMEDIT_TEST_HAS_IMAGE_DECODER
    check_cli(run.result["diagnostics"].dump().find("offending-skew") != std::string::npos);
#endif
    check_cli(read_file(output / "sentinel.osb") == "original bytes");
    check_cli(std::distance(std::filesystem::directory_iterator(output), std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("wormedit-convert OSBX matrix and image failures preserve the whole output set",
          "[unit][tools][convert][osbx]") {
    scratch_directory_t scratch("osbx-other-refusals");
    const auto input = write_osbx_cli_project(scratch, false);
    const auto output = scratch.path() / "output";
    std::filesystem::create_directory(output);
    std::ofstream(output / "sentinel.txt") << "unchanged";
    std::filesystem::create_directory(scratch.path() / "images");
    write_source(scratch, "images/sprite.ppm", "P6\n2 1\n255\nabcdef");
    auto parsed = wormedit::parse_native_project(read_file(input));
    require_cli(parsed.value.has_value());
    auto& scene = *parsed.value->storyboards.front().osbx;
    std::string expected;
    SECTION("matrix op") {
        scene.nodes.back().ops = {{.id = "offending-matrix", .kind = wormedit::osbx_transform_kind_t::matrix,
            .track = {.default_value = {1, 0, 0, 1, 0, 0}}}};
        expected = "offending-matrix";
    }
    SECTION("missing image") {
        std::filesystem::remove(scratch.path() / "images/sprite.ppm");
        expected = "unavailable";
    }
    SECTION("invalid encoded image") {
        write_source(scratch, "images/sprite.ppm", "broken image");
        expected = "image";
    }
    SECTION("image aliases the storyboard artifact") {
        const auto filename = "project-" + std::to_string(parsed.value->id.value) + ".osb";
        scene.images.front().file = filename;
        write_source(scratch, filename, "P6\n2 1\n255\nabcdef");
        expected = "collides";
    }
    const auto saved = wormedit::write_native_project(*parsed.value);
    require_cli(saved.value.has_value());
    std::ofstream(input) << *saved.value;
    const auto run = run_tool(scratch, {{"input", input.string()}, {"output_directory", output.string()},
        {"target", "osu_mania"}, {"overwrite", true}});
    INFO(run.result.dump());
    check_cli(run.status == 1);
#ifdef WORMEDIT_TEST_HAS_IMAGE_DECODER
    check_cli(run.result.dump().find(expected) != std::string::npos);
#endif
    check_cli(read_file(output / "sentinel.txt") == "unchanged");
    check_cli(std::distance(std::filesystem::directory_iterator(output), std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("wormedit-convert resolves frozen subtree links and publishes only their generated image set",
          "[unit][tools][convert][osbx-link]") {
    using namespace wormedit;
    scratch_directory_t scratch("osbx-linked");
    const auto input = write_osbx_cli_project(scratch,false);
    auto parsed = parse_native_project(read_file(input)); require_cli(static_cast<bool>(parsed.value));
    const std::string source = "return function(ctx) image{id='new',file='generated.ppm'}; sprite('generated'); image('new') end";
    parsed.value->storyboards.front().osbx = osbx_document_t{.nodes = {{.id = "hand-group",
        .children_link = osbx_lua_link_t{.id = "subtree-link", .source = {.frozen_source = source,
            .source_reference = "absent-module.lua", .source_hash = native_project_content_checksum(source)}}}}};
    const auto saved = write_native_project(*parsed.value); require_cli(static_cast<bool>(saved.value));
    std::ofstream(input) << *saved.value;
    const auto output = scratch.path()/"output"; std::filesystem::create_directory(output);
    write_source(scratch,"output/sentinel.txt","keep");
    const bool missing = GENERATE(false,true);
    const std::string bytes = "P6\n2 1\n255\nabcdef";
    if (!missing) write_source(scratch,"generated.ppm",bytes);
    const auto run = run_tool(scratch,{{"input",input.string()},{"output_directory",output.string()},
        {"target","osu_mania"},{"overwrite",true}});
    INFO(run.result.dump());
#if defined(WORMEDIT_TEST_HAS_LUA) && defined(WORMEDIT_TEST_HAS_IMAGE_DECODER)
    if (!missing) {
        require_cli(static_cast<bool>(run.status == 0));
        check_cli(static_cast<bool>(read_file(output/"generated.ppm") == bytes));
        std::size_t count{};
        for (const auto& artifact : run.result["artifacts"]) {
            const auto name = artifact["filename"].get<std::string>();
            if (std::filesystem::path{name}.extension() != ".osb") continue;
            const auto osb = parse_osb(read_file(output/name)); require_cli(static_cast<bool>(osb.value));
            require_cli(static_cast<bool>(osb.value->sprites.size() == 1)); check_cli(static_cast<bool>(osb.value->sprites.front().path == "generated.ppm"));
            ++count;
        }
        check_cli(static_cast<bool>(count == 1));
    } else {
        check_cli(static_cast<bool>(run.status != 0));
        check_cli(static_cast<bool>(std::distance(std::filesystem::directory_iterator(output),std::filesystem::directory_iterator{}) == 1));
    }
#else
    check_cli(static_cast<bool>(run.status != 0));
    check_cli(static_cast<bool>(std::distance(std::filesystem::directory_iterator(output),std::filesystem::directory_iterator{}) == 1));
#endif
    check_cli(static_cast<bool>(read_file(output/"sentinel.txt") == "keep"));
}

TEST_CASE("wormedit-convert NotITG profile publishes SM XML and Lua with existing atomic ownership", "[tools][convert][notitg]") {
    using namespace wormedit;
    const scratch_directory_t scratch{"notitg-profile"};
    const auto input = write_native_actor_project(scratch);
    auto project = parse_native_project(read_file(input));
    require_cli(project.value.has_value());
    project.value->storyboards.front().osbx->nodes.front().layer = osbx_layer_t::foreground;
    const auto saved = write_native_project(*project.value);
    require_cli(saved.value.has_value());
    write_source(scratch, "osbx.wormedit", *saved.value);
    const auto output = scratch.path() / "output";
    std::filesystem::create_directory(output);
    auto request = native_actor_request(input, output);
    request["stepmania"]["file_profile"] = "sm";
    request["stepmania"]["implementation_profile"] = "notitg_v4";
    const auto run = run_tool(scratch, request);
    INFO(run.result.dump());
    require_cli(run.status == 0);
    check_cli(count_files(output, ".sm") == 1);
#ifdef WORMEDIT_TEST_HAS_IMAGE_DECODER
    check_cli(std::filesystem::exists(output / "wormedit-storyboard/main.xml"));
    check_cli(std::filesystem::exists(output / "wormedit-storyboard/scene-1/foreground/main.lua"));
    check_cli(read_file(output / "wormedit-storyboard/main.xml").find("scene-1/foreground/main.xml") != std::string::npos);
#else
    check_cli(!std::filesystem::exists(output / "wormedit-storyboard/main.xml"));
#endif
}
