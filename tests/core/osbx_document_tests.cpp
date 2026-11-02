#include <wormedit/core/osbx_document.h>
#include <wormedit/core/storyboard.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {
using namespace wormedit;
using code_t = osbx_diagnostic_code_t;

auto make_document() -> osbx_document_t {
    osbx_document_t document;
    document.images.push_back({.id = "image", .file = "images/sprite.png"});
    document.nodes.push_back({.id = "group"});
    document.nodes.push_back({
        .id = "sprite", .kind = osbx_node_kind_t::sprite, .parent = "group",
        .order = -3, .image = "image",
        .ops = {{.id = "translate", .track = {
            .default_value = {0, 0},
            .keys = {{.id = "translation-key", .time_ms = -100, .value = {1, 2}}},
        }}},
        .provenance = {.file = "story/main.osbx.lua", .line = 17, .generator_key = "generated-sprite"},
    });
    document.nodes.back().tracks.opacity.keys.push_back({.id = "opacity-key", .value = {0.5}});
    return document;
}

auto has_code(const osbx_document_t& document, code_t code, osbx_validation_options_t options = {}) -> bool {
    const auto result = validate_osbx_document(document, options);
    CHECK_FALSE(result.value);
    CHECK_FALSE(result.ok());
    return std::ranges::any_of(result.diagnostics, [code](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}
void check_lifetime_contains(const osbx_lifetime_t& range, storyboard_time_t time, bool expected) {
    INFO("time=" << time.milliseconds << " fraction=" << time.remainder << '/' << time.denominator);
    CHECK(range.contains(time) == expected);
}
} // namespace

TEST_CASE("OSBX clips validate references cycles parameters and aggregate expansion", "[osbx][clips]") {
    auto document = make_document();
    document.clips = {{.id = "clip", .ops = {{.id = "clip-op", .track = {.default_value = {0, 0}, .parameter = "offset"}}},
        .parameters = {{.name = "offset", .default_value = {1, 2}}}}};
    document.nodes[0].uses = {{.id = "use", .clip = "clip"}};
    REQUIRE(validate_osbx_document(document).value);
    SECTION("missing") { document.nodes[0].uses[0].clip = "unknown"; }
    SECTION("recursive") { document.clips[0].uses = {{.id = "recursive", .clip = "clip"}}; }
    SECTION("scale") { document.nodes[0].uses[0].time_scale = 0; }
    SECTION("arity") { document.nodes[0].uses[0].parameters = {{.name = "offset", .value = {1}}}; }
    SECTION("unknown") { document.nodes[0].uses[0].parameters = {{.name = "other", .value = {1}}}; }
    SECTION("raw Lua") { document.clips[0].ops[0].track.lua = osbx_lua_track_link_t{}; }
    CHECK_FALSE(validate_osbx_document(document).value);
}

TEST_CASE("OSBX repeated acyclic clips obey expansion bounds", "[osbx][clips]") {
    auto document = make_document();
    document.clips = {{.id = "leaf", .ops = {{.id = "leaf-op"}}},
        {.id = "branch", .uses = {{.id = "left", .clip = "leaf"}, {.id = "right", .clip = "leaf"}}}};
    document.nodes[0].uses = {{.id = "root", .clip = "branch"}};
    CHECK(validate_osbx_document(document).value);
    CHECK_FALSE(validate_osbx_document(document, {.maximum_expanded_layers = 2}).value);
}

TEST_CASE("OSBX validation accepts typed scenes without mutation", "[unit][osbx]") {
    static_assert(std::is_same_v<decltype(osbx_key_t<>::time_ms), std::int64_t>);
    auto document = make_document();
    document.nodes.push_back({.id = "animation", .kind = osbx_node_kind_t::animation});
    document.nodes.push_back({.id = "sample", .kind = osbx_node_kind_t::sample});
    document.nodes.back().tracks.blend.default_value = osbx_blend_t::additive;
    document.nodes.back().tracks.visibility.keys.push_back({
        .id = "visible", .time_ms = -1, .value = false, .easing = osbx_step_easing_t{},
    });
    const auto before = document;
    const auto result = validate_osbx_document(document);
    CHECK(result.value);
    CHECK(result.ok());
    CHECK(result.diagnostics.empty());
    CHECK(document == before);
    CHECK(validate_osbx_document(osbx_document_t{}).ok());
}

TEST_CASE("OSBX IDs are unique across images nodes operations and all keys", "[unit][osbx]") {
    for (const std::string_view id : {"image", "group", "sprite", "translate", "translation-key", "opacity-key"}) {
        DYNAMIC_SECTION(id) {
            auto document = make_document();
            document.nodes.push_back({.id = std::string(id)});
            CHECK(has_code(document, code_t::duplicate_id));
        }
    }
    auto document = make_document();
    document.images.push_back({.id = "sprite", .file = "other.png"});
    document.nodes.back().ops.back().id = "image";
    document.nodes.back().tracks.blend.keys.push_back({.id = "group"});
    CHECK(has_code(document, code_t::duplicate_id));
}

TEST_CASE("OSBX rejects missing parents", "[unit][osbx]") {
    auto document = make_document();
    document.nodes.back().parent = "missing";
    CHECK(has_code(document, code_t::missing_parent));
}

TEST_CASE("OSBX rejects missing image references", "[unit][osbx]") {
    auto document = make_document();
    document.nodes.back().image = "missing";
    CHECK(has_code(document, code_t::missing_image));
}

TEST_CASE("OSBX rejects parent cycles including self cycles", "[unit][osbx]") {
    auto document = make_document();
    SECTION("self") { document.nodes.front().parent = "group"; }
    SECTION("two nodes") { document.nodes.front().parent = "sprite"; }
    CHECK(has_code(document, code_t::parent_cycle));
}

TEST_CASE("OSBX parent traversal handles long chains and shared ancestors", "[unit][osbx]") {
    osbx_document_t document;
    for (int index = 0; index < 1000; ++index) {
        document.nodes.push_back({.id = std::to_string(index)});
        if (index > 0) document.nodes.back().parent = std::to_string(index - 1);
    }
    document.nodes.push_back({.id = "sibling", .parent = "1"});
    CHECK(validate_osbx_document(document).ok());
    document.nodes.front().parent = "999";
    CHECK(has_code(document, code_t::parent_cycle));
}

TEST_CASE("OSBX rejects cross-layer parenting", "[unit][osbx]") {
    auto document = make_document();
    document.nodes.back().layer = osbx_layer_t::foreground;
    CHECK(has_code(document, code_t::cross_layer_parent));
}

TEST_CASE("OSBX sprites require an image", "[unit][osbx]") {
    auto document = make_document();
    document.nodes.back().image.reset();
    CHECK(has_code(document, code_t::missing_image));
}

TEST_CASE("OSBX groups cannot have images", "[unit][osbx]") {
    auto document = make_document();
    document.nodes.front().image = "image";
    CHECK(has_code(document, code_t::invalid_image_reference));
}

TEST_CASE("OSBX numeric defaults and keys must be finite", "[unit][osbx]") {
    for (const double value : {std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        auto document = make_document();
        document.nodes.back().ops.front().track.default_value[0] = value;
        CHECK(has_code(document, code_t::non_finite_value));
        document.nodes.back().ops.front().track.default_value[0] = 0;
        document.nodes.back().ops.front().track.keys.front().value[0] = value;
        CHECK(has_code(document, code_t::non_finite_value));
        document.nodes.back().ops.front().track.keys.front().value[0] = 0;
        document.nodes.back().tracks.color.default_value[0] = value;
        CHECK(has_code(document, code_t::non_finite_value));
        document.nodes.back().tracks.color.default_value[0] = 1;
        document.nodes.back().tracks.color.keys.push_back({.id = "color", .value = {1, 1, 1, value}});
        CHECK(has_code(document, code_t::non_finite_value));
    }
}

TEST_CASE("OSBX cubic Bezier X coordinates must stay in the unit interval", "[unit][osbx]") {
    auto document = make_document();
    auto& easing = document.nodes.back().ops.front().track.keys.front().easing;
    SECTION("first X") { easing = osbx_cubic_bezier_t{.x1 = -0.01}; }
    SECTION("second X") { easing = osbx_cubic_bezier_t{.x2 = 1.01}; }
    CHECK(has_code(document, code_t::invalid_easing));
}

TEST_CASE("OSBX cubic Bezier permits finite Y overshoot and rejects nonfinite points", "[unit][osbx]") {
    auto document = make_document();
    auto& easing = document.nodes.back().ops.front().track.keys.front().easing;
    easing = osbx_cubic_bezier_t{.x1 = 0, .y1 = -2, .x2 = 1, .y2 = 3};
    CHECK(validate_osbx_document(document).ok());
    for (int component = 0; component < 4; ++component) {
        osbx_cubic_bezier_t curve;
        switch (component) {
        case 0: curve.x1 = std::numeric_limits<double>::infinity(); break;
        case 1: curve.y1 = std::numeric_limits<double>::infinity(); break;
        case 2: curve.x2 = std::numeric_limits<double>::infinity(); break;
        case 3: curve.y2 = std::numeric_limits<double>::infinity(); break;
        }
        easing = curve;
        CHECK(has_code(document, code_t::non_finite_value));
    }
}

TEST_CASE("OSBX reuses inclusive OSB easing boundaries", "[unit][osbx]") {
    auto document = make_document();
    auto& easing = document.nodes.back().ops.front().track.keys.front().easing;
    for (const auto value : {-1, 35, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        easing = osbx_osb_easing_t{.value = value};
        CHECK(has_code(document, code_t::invalid_easing));
    }
    for (int value = 0; value <= 34; ++value) {
        easing = osbx_osb_easing_t{.value = value};
        CHECK(validate_osbx_document(document).ok());
    }
}

TEST_CASE("OSBX finite singular matrix defaults and keys are valid", "[unit][osbx]") {
    auto document = make_document();
    auto& op = document.nodes.back().ops.front();
    op.kind = osbx_transform_kind_t::matrix;
    op.track = {.default_value = {1, 0, 0, 1, 0, 0}, .keys = {
        {.id = "singular", .value = {0, 0, 0, 0, 0, 0}},
    }};
    CHECK(validate_osbx_document(document).diagnostics.empty());
    op.track.default_value = {1, 2, 2, 4, 0, 0};
    CHECK(validate_osbx_document(document).diagnostics.empty());
}

TEST_CASE("OSBX rejects malformed matrix defaults and keys", "[unit][osbx]") {
    auto document = make_document();
    auto& op = document.nodes.back().ops.front();
    op.kind = osbx_transform_kind_t::matrix;
    op.track = {.default_value = {1, 0, 0, 1, 0, 0}, .keys = {
        {.id = "matrix-key", .value = {1, 0, 0, 1, 0, 0}},
    }};
    op.track.default_value.pop_back();
    CHECK(has_code(document, code_t::invalid_value_shape));
    op.track.default_value.push_back(0);
    op.track.keys.front().value.push_back(0);
    CHECK(has_code(document, code_t::invalid_value_shape));
    op.track.keys.front().value.pop_back();
    op.track.default_value[0] = std::numeric_limits<double>::infinity();
    CHECK(has_code(document, code_t::non_finite_value));
    op.track.default_value[0] = 1;
    op.track.keys.front().value[0] = std::numeric_limits<double>::quiet_NaN();
    CHECK(has_code(document, code_t::non_finite_value));
}

TEST_CASE("OSBX numeric tracks validate each operation value shape", "[unit][osbx]") {
    constexpr std::array kinds{osbx_transform_kind_t::translate, osbx_transform_kind_t::rotate,
        osbx_transform_kind_t::scale, osbx_transform_kind_t::skew,
        osbx_transform_kind_t::matrix, osbx_transform_kind_t::pivot};
    constexpr std::array<std::size_t, 6> arities{2, 1, 2, 2, 6, 2};
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        auto document = make_document();
        auto& op = document.nodes.back().ops.front();
        op.kind = kinds[index];
        op.track.default_value.resize(arities[index]);
        op.track.keys.front().value.resize(arities[index]);
        CHECK(validate_osbx_document(document).ok());
        op.track.keys.front().value.push_back(0);
        CHECK(has_code(document, code_t::invalid_value_shape));
    }
    auto document = make_document();
    document.nodes.back().tracks.color.default_value = {1, 1, 1};
    CHECK(has_code(document, code_t::invalid_value_shape));
}

TEST_CASE("OSBX opacity defaults and keys must lie in the unit interval", "[unit][osbx]") {
    auto document = make_document();
    SECTION("default") { document.nodes.back().tracks.opacity.default_value = {-0.01}; }
    SECTION("key") { document.nodes.back().tracks.opacity.keys.front().value = {1.01}; }
    CHECK(has_code(document, code_t::invalid_value_range));
}

TEST_CASE("OSBX keys must have strictly increasing signed millisecond times", "[unit][osbx]") {
    auto document = make_document();
    auto& keys = document.nodes.back().ops.front().track.keys;
    keys.front().time_ms = std::numeric_limits<std::int64_t>::min();
    keys.push_back({.id = "last", .time_ms = std::numeric_limits<std::int64_t>::max(), .value = {3, 4}});
    CHECK(validate_osbx_document(document).ok());
    SECTION("equal") { keys.back().time_ms = keys.front().time_ms; }
    SECTION("decreasing") { std::swap(keys.front().time_ms, keys.back().time_ms); }
    CHECK(has_code(document, code_t::unordered_keys));
}

TEST_CASE("OSBX property tracks share strict key ordering validation", "[unit][osbx]") {
    auto document = make_document();
    SECTION("opacity") {
        document.nodes.back().tracks.opacity.keys.push_back({.id = "second", .value = {1}});
    }
    SECTION("color") {
        document.nodes.back().tracks.color.keys = {
            {.id = "first", .value = {1, 1, 1, 1}}, {.id = "second", .value = {1, 1, 1, 1}},
        };
    }
    SECTION("visibility") {
        document.nodes.back().tracks.visibility.keys = {{.id = "first"}, {.id = "second"}};
    }
    SECTION("blend") {
        document.nodes.back().tracks.blend.keys = {{.id = "first"}, {.id = "second"}};
    }
    CHECK(has_code(document, code_t::unordered_keys));
}

TEST_CASE("OSBX pivot nesting is bounded and only pivots own children", "[unit][osbx]") {
    auto document = make_document();
    auto& op = document.nodes.back().ops.front();
    op.kind = osbx_transform_kind_t::pivot;
    op.children.push_back({.id = "child"});
    CHECK(validate_osbx_document(document, {.maximum_nesting = 2}).ok());
    CHECK(has_code(document, code_t::nesting_limit, {.maximum_nesting = 1}));
    op.kind = osbx_transform_kind_t::translate;
    CHECK(has_code(document, code_t::invalid_children));
}

TEST_CASE("OSBX bounds node image op and key counts across the document", "[unit][osbx]") {
    auto document = make_document();
    CHECK(validate_osbx_document(document, {.maximum_nodes = 2, .maximum_images = 1,
                                          .maximum_ops = 1, .maximum_keys = 2}).ok());
    CHECK(has_code(document, code_t::node_limit, {.maximum_nodes = 1}));
    CHECK(has_code(document, code_t::image_limit, {.maximum_images = 0}));
    CHECK(has_code(document, code_t::op_limit, {.maximum_ops = 0}));
    CHECK(has_code(document, code_t::key_limit, {.maximum_keys = 1}));
    document.nodes.front().ops.push_back({.id = "other-op"});
    CHECK(has_code(document, code_t::op_limit, {.maximum_ops = 1}));
}

TEST_CASE("OSBX enforces bounded nonempty UTF-8 IDs and reference text", "[unit][osbx]") {
    auto document = make_document();
    SECTION("empty") { document.nodes.front().id.clear(); }
    SECTION("NUL") { document.nodes.front().id = std::string("a\0b", 3); }
    SECTION("UTF-8") { document.nodes.front().id = "\xff"; }
    SECTION("length") { document.nodes.front().id = std::string(1025, 'x'); }
    SECTION("parent reference") { document.nodes.back().parent = ""; }
    SECTION("image reference") { document.nodes.back().image = ""; }
    CHECK(has_code(document, code_t::invalid_id));
}

TEST_CASE("OSBX validates project-relative bounded image and provenance paths", "[unit][osbx]") {
    for (const auto file : {"", "/absolute.png", "../outside.png", "images/../outside.png",
                             "C:/absolute.png", "images\\file.png", "\xff"}) {
        auto document = make_document();
        document.images.front().file = file;
        CHECK(has_code(document, code_t::invalid_path));
    }
    auto document = make_document();
    document.images.front().file = std::string(4097, 'x');
    CHECK(has_code(document, code_t::invalid_path));
    document.images.front().file = "sprite.png";
    document.nodes.back().provenance.file = std::string(4097, 'x');
    CHECK(has_code(document, code_t::invalid_path));
    document.nodes.back().provenance.file.clear();
    CHECK(validate_osbx_document(document, {.maximum_path_bytes = 10}).ok());
    CHECK(has_code(document, code_t::invalid_path, {.maximum_path_bytes = 9}));
}

TEST_CASE("OSBX diagnostics carry responsible key and node provenance", "[unit][osbx]") {
    auto document = make_document();
    document.nodes.back().ops.front().track.keys.front().value.clear();
    const auto before = document;
    const auto result = validate_osbx_document(document);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == code_t::invalid_value_shape);
    CHECK(result.diagnostics.front().location.id == "translation-key");
    CHECK(result.diagnostics.front().location.provenance == document.nodes.back().provenance);
    CHECK(document == before);
}

TEST_CASE("OSBX validates API version and canvas dimensions", "[unit][osbx]") {
    auto document = make_document();
    document.api_version = "next";
    CHECK(has_code(document, code_t::invalid_version));
    document.api_version = "0.1";
    document.canvas.width = 0;
    CHECK(has_code(document, code_t::invalid_canvas));
    document.canvas.width = 640;
    document.canvas.height = std::numeric_limits<double>::quiet_NaN();
    CHECK(has_code(document, code_t::invalid_canvas));
}

TEST_CASE("OSBX rejects invalid enum values and unknown origins", "[unit][osbx]") {
    auto document = make_document();
    document.nodes.back().tracks.blend.default_value = static_cast<osbx_blend_t>(42);
    CHECK(has_code(document, code_t::invalid_enum));
    document.nodes.back().tracks.blend.default_value = osbx_blend_t::normal;
    document.nodes.back().kind = static_cast<osbx_node_kind_t>(42);
    CHECK(has_code(document, code_t::invalid_enum));
    document.nodes.back().kind = osbx_node_kind_t::sprite;
    document.nodes.back().layer = static_cast<osbx_layer_t>(42);
    CHECK(has_code(document, code_t::invalid_enum));
    document.nodes.back().layer = osbx_layer_t::background;
    document.nodes.back().ops.front().kind = static_cast<osbx_transform_kind_t>(42);
    CHECK(has_code(document, code_t::invalid_enum));
    document.nodes.back().ops.front().kind = osbx_transform_kind_t::translate;
    document.nodes.back().origin = "middle-ish";
    CHECK(has_code(document, code_t::invalid_origin));
}

TEST_CASE("OSBX lifetime validates inclusive signed ranges and exact fractional containment", "[unit][osbx]") {
    const osbx_lifetime_t range{.begin_ms = -10, .end_ms = 10};
    check_lifetime_contains(range, {.milliseconds = -10}, true);
    check_lifetime_contains(range, {.milliseconds = 10}, true);
    check_lifetime_contains(range, {.milliseconds = -11, .remainder = 999, .denominator = 1000}, false);
    check_lifetime_contains(range, {.milliseconds = 10, .remainder = 1, .denominator = 1000}, false);
    const osbx_lifetime_t instant{.begin_ms = -2, .end_ms = -2};
    check_lifetime_contains(instant, {.milliseconds = -2}, true);
    check_lifetime_contains(instant, {.milliseconds = -2, .remainder = 1, .denominator = 2}, false);
    auto document = make_document();
    document.nodes[0].lifetime = osbx_lifetime_t{.begin_ms = std::numeric_limits<std::int64_t>::min(),
        .end_ms = std::numeric_limits<std::int64_t>::max()};
    CHECK(validate_osbx_document(document).value);
    check_lifetime_contains(*document.nodes[0].lifetime, {.milliseconds = std::numeric_limits<std::int64_t>::min()}, true);
    check_lifetime_contains(*document.nodes[0].lifetime, {.milliseconds = std::numeric_limits<std::int64_t>::max()}, true);
    check_lifetime_contains(*document.nodes[0].lifetime, {.milliseconds = std::numeric_limits<std::int64_t>::max(),
        .remainder = 1, .denominator = 2}, false);
    document.nodes[0].lifetime = osbx_lifetime_t{.begin_ms = 3, .end_ms = 2};
    CHECK(has_code(document, code_t::invalid_value_range));
}

TEST_CASE("OSBX acyclic exponential clip graphs refuse before materializing tracks", "[osbx][clips]") {
    auto document = make_document();
    document.clips = {{.id = "level0", .ops = {{.id = "leaf-op"}}}};
    for (int i = 1; i < 8; ++i) {
        const auto name = "level" + std::to_string(i), previous = "level" + std::to_string(i - 1);
        document.clips.push_back({.id = name, .uses = {
            {.id = name + "left", .clip = previous}, {.id = name + "right", .clip = previous}}});
    }
    document.nodes[0].uses = {{.id = "root-use", .clip = "level7"}};
    CHECK(validate_osbx_document(document).value);
    CHECK_FALSE(validate_osbx_document(document, {.maximum_expanded_layers = 64}).value);
}

TEST_CASE("OSBX clip selectors reject ordinary tracks and conflicting bindings", "[osbx][clips]") {
    auto document = make_document();
    document.clips = {{.id = "clip", .parameters = {{.name = "offset", .default_value = {1,2}}}}};
    document.nodes[0].uses = {{.id = "use", .clip = "clip"}};
    SECTION("ordinary selector") { document.nodes[1].ops[0].track.parameter = "offset"; }
    SECTION("duplicate binding") { document.nodes[0].uses[0].parameters = {
        {.name = "offset", .value = {1,2}}, {.name = "offset", .value = {3,4}}}; }
    SECTION("unknown selector") { document.clips[0].tracks.opacity.parameter = "missing"; }
    SECTION("nonfinite parameter") { document.clips[0].parameters[0].default_value[0] = std::numeric_limits<double>::infinity(); }
    CHECK_FALSE(validate_osbx_document(document).value);
}

TEST_CASE("OSBX clip scale and storage refusals are explicit and leave source unchanged", "[osbx][clips]") {
    auto document = make_document();
    document.clips = {{.id = "clip"}};
    document.nodes[0].uses = {{.id = "use", .clip = "clip"}};
    const auto before = document;
    CHECK_FALSE(validate_osbx_document(document, {.maximum_expanded_storage_bytes = 1}).value);
    CHECK(document == before);
    for (const auto scale : std::array{-1.0, 0.0, std::numeric_limits<double>::infinity()}) {
        document.nodes[0].uses[0].time_scale = scale;
        CHECK_FALSE(validate_osbx_document(document).value);
    }
}

TEST_CASE("OSBX temporal programs validate every authored key without unrolling counts", "[osbx][program]") {
    auto document = make_document();
    osbx_loop_t<> loop{.id = "repeat", .start_ms = -100, .count = 4,
        .keys = {{.id = "repeat-a", .value = {0}}, {.id = "repeat-b", .time_ms = 10, .value = {1}}}};
    document.nodes.back().tracks.opacity.program.push_back({.data = loop});
    REQUIRE(validate_osbx_document(document).value);
    SECTION("zero count") { std::get<osbx_loop_t<>>(document.nodes.back().tracks.opacity.program[0].data).count = 0; }
    SECTION("empty") { std::get<osbx_loop_t<>>(document.nodes.back().tracks.opacity.program[0].data).keys.clear(); }
    SECTION("negative keys") { std::get<osbx_loop_t<>>(document.nodes.back().tracks.opacity.program[0].data).keys[0].time_ms = -1; }
    SECTION("duplicate") { std::get<osbx_loop_t<>>(document.nodes.back().tracks.opacity.program[0].data).keys[0].id = "sprite"; }
    SECTION("overflow") { std::get<osbx_loop_t<>>(document.nodes.back().tracks.opacity.program[0].data).start_ms = INT64_MAX; }
    CHECK_FALSE(validate_osbx_document(document).value);
}
TEST_CASE("OSBX trigger programs share names but refuse unsupported groups", "[osbx][program]") {
    auto document = make_document();
    document.nodes.back().tracks.opacity.program.push_back({.data = osbx_trigger_t<>{.id = "activate",
        .trigger = {.name = *storyboard_trigger_name_t::from("HitSoundClap"), .start_time_ms = -10, .end_time_ms = 100},
        .keys = {{.id = "activated", .value = {0.5}}}}});
    REQUIRE(validate_osbx_document(document).value);
    std::get<osbx_trigger_t<>>(document.nodes.back().tracks.opacity.program[0].data).trigger.group_number = 1;
    CHECK_FALSE(validate_osbx_document(document).value);
}

TEST_CASE("OSBX program budgets include ignored inactive and relative key storage", "[osbx][program]") {
    auto document = make_document();
    document.nodes.back().tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat", .count = UINT32_MAX,
        .keys = {{.id = "constant", .value = {0.5}}}}}};
    REQUIRE(validate_osbx_document(document).value);
    CHECK_FALSE(validate_osbx_document(document, {.maximum_keys = 2}).value);
    CHECK_FALSE(validate_osbx_document(document, {.maximum_expanded_storage_bytes = 1}).value);
    auto second = document.nodes.back().tracks.opacity.program[0];
    std::get<osbx_loop_t<>>(second.data).id = "second";
    document.nodes.back().tracks.opacity.program.push_back(std::move(second));
    CHECK_FALSE(validate_osbx_document(document).value); // duplicated child identity
}
TEST_CASE("OSBX temporal metadata rejects wrong shape range and linked ownership", "[osbx][program]") {
    auto document = make_document();
    auto& track = document.nodes.back().tracks.opacity;
    track.program = {{.data = osbx_loop_t<>{.id = "repeat", .keys = {{.id = "constant", .value = {0.5}}}}}};
    SECTION("wrong arity") { std::get<osbx_loop_t<>>(track.program[0].data).keys[0].value = {0.5, 0.5}; }
    SECTION("range") { std::get<osbx_loop_t<>>(track.program[0].data).keys[0].value = {2}; }
    SECTION("relative ordering") {
        auto& keys = std::get<osbx_loop_t<>>(track.program[0].data).keys;
        keys.push_back({.id = "second", .value = {1}});
    }
    CHECK_FALSE(validate_osbx_document(document).value);
}
TEST_CASE("OSBX clone cost charges temporal keys and repeated clip layers before copying", "[osbx][program][clips]") {
    auto source = make_document();
    source.nodes.back().tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat", .count = UINT32_MAX,
        .keys = {{.id = "constant", .value = {0.5}}}}}};
    const std::array<std::string_view, 1> duplicated{"sprite"};
    const auto original = osbx_clip_expansion_cost_t::from(source); REQUIRE(original.value);
    const auto copied = osbx_clip_expansion_cost_t::from(source, {}, duplicated); REQUIRE(copied.value);
    CHECK(copied.value->keys == original.value->keys * 2);
    CHECK(copied.value->storage > original.value->storage);
    CHECK_FALSE(osbx_clip_expansion_cost_t::from(source, {.maximum_keys = original.value->keys}, duplicated).value);
    CHECK(source.nodes.size() == 2);
}

namespace {
void check_media_model(bool value) { CHECK(value); }
}
TEST_CASE("OSBX media metadata validates only on its matching kind and retains absent placeholders", "[osbx][animation][sample]") {
    osbx_document_t document{.images = {{.id = "frames", .file = "hero.png"}},
        .nodes = {{.id = "hero", .kind = osbx_node_kind_t::animation, .image = "frames"}}};
    document.nodes.front().animation = storyboard_animation_t{.frame_count = 2, .frame_delay_ms = 2.5};
    check_media_model(validate_osbx_document(document).value);
    document.nodes.front().animation->frame_delay_ms = 0;
    check_media_model(!validate_osbx_document(document).value);
    document.nodes.front().animation.reset(); document.nodes.front().image.reset();
    check_media_model(validate_osbx_document(document).value);
    document.nodes.front().kind = osbx_node_kind_t::sample;
    document.nodes.front().sample.emplace(); document.nodes.front().sample->path = "tone.wav";
    check_media_model(validate_osbx_document(document).value);
    document.nodes.front().sample->volume = 0;
    check_media_model(!validate_osbx_document(document).value);
    document.nodes.front().sample->volume.reset(); document.nodes.front().kind = osbx_node_kind_t::group;
    check_media_model(!validate_osbx_document(document).value);
}

TEST_CASE("OSBX media payload storage shares authored and duplicate expansion bounds", "[osbx][media][bounds]") {
    osbx_document_t source{.nodes = {{.id = "cue", .kind = osbx_node_kind_t::sample,
        .sample = osbx_sample_t{.path = "tone.wav"}}}};
    osbx_validation_options_t options; options.maximum_expanded_storage_bytes = 1;
    CHECK_FALSE(validate_osbx_document(source, options).value);
    options.maximum_expanded_storage_bytes = 64 * 1024;
    const std::array<std::string_view,1> copied{"cue"};
    const auto cost = osbx_clip_expansion_cost_t::from(source, options, copied);
    REQUIRE(cost.value);
    CHECK(cost.value->storage >= 2 * (sizeof(osbx_sample_t) + source.nodes[0].sample->path.size()));
}
