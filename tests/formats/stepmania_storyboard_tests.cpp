#include "formats/stepmania_storyboard.h"

#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/keyframe_simplify.h>
#include <wormedit/core/visual_layout.h>
#include <wormedit/formats/native_project_storage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace {
using namespace wormedit;
const std::array geometry{osbx_osb_image_size_t{.image_id = "image", .width = 64, .height = 48}};

auto example() -> osbx_document_t {
    osbx_document_t scene{.images = {{.id = "image", .file = "images/sprite.png"}}};
    scene.nodes.push_back({.id = "sprite", .kind = osbx_node_kind_t::sprite,
        .image = "image", .ops = {{.id = "position", .kind = osbx_transform_kind_t::translate,
            .track = {.default_value = {320, 240}}}}});
    return scene;
}
auto options() -> stepmania_storyboard_options_t {
    return {.playback_span = {.begin_time_ms = 0, .end_time_ms = 2000},
        .activation_time_ms = -1000, .image_sizes = geometry};
}
auto emit(const osbx_document_t& source) -> stepmania_storyboard_outcome_t {
    return emit_stepmania_storyboard(source, options());
}
auto sprite_report(const stepmania_storyboard_outcome_t& output, std::string_view id)
    -> const osbx_osb_report_t& {
    const auto found = std::ranges::find(output.value.report, id, &osbx_osb_report_t::node_id);
    REQUIRE((static_cast<bool>(found != output.value.report.end())));
    return *found;
}
void require_unsupported(const stepmania_storyboard_outcome_t& output, std::string_view reason) {
    REQUIRE((static_cast<bool>(output.value.files.empty())));
    REQUIRE((!static_cast<bool>(output.value.report.empty())));
    CHECK((static_cast<bool>(output.value.report.front().classification == osbx_osb_class_t::unsupported)));
    CHECK((static_cast<bool>(output.value.report.front().reason.find(reason) != std::string::npos)));
}
class prepared_opacity_t final : public osbx_lua_backend_t {
  public:
    auto prepare_track(const osbx_lua_track_link_t&, std::size_t) const
        -> diagnosed_t<std::vector<osbx_key_t<>>> override {
        return {.value = {{.id = "prepared-first", .value = {0.25}},
            {.id = "prepared-last", .time_ms = 2000, .value = {0.75}}}};
    }
    auto prepare_children(const osbx_lua_link_t&) const -> diagnosed_t<osbx_document_t> override {
        return {.diagnostics = {{.code = diagnostic_code_t::unsupported_feature, .message = "Unused children backend"}}};
    }
};
void check_absolute_actor_clock_and_sandbox(const std::string& bytes) {
    CHECK((static_cast<bool>(bytes.find("GAMESTATE:GetSongPosition()") != std::string::npos)));
    CHECK((static_cast<bool>(bytes.find("loadfile") == std::string::npos)));
    CHECK((static_cast<bool>(bytes.find("dofile") == std::string::npos)));
    CHECK((static_cast<bool>(bytes.find("io.") == std::string::npos)));
    CHECK((static_cast<bool>(bytes.find("os.") == std::string::npos)));
    CHECK((static_cast<bool>(bytes.find("accumulated") == std::string::npos)));
}
} // namespace

TEST_CASE("SM used clips report unsupported before sidecars while unused definitions preserve bytes", "[stepmania][storyboard][clips]") {
    auto source = example();
    const auto original = emit(source);
    source.clips = {{.id = "clip", .ops = {{.id = "clip-op"}}}};
    const auto unused = emit(source);
    REQUIRE(unused.value.files.size() == original.value.files.size());
    CHECK(unused.value.files[0].bytes == original.value.files[0].bytes);
    source.nodes[0].uses = {{.id = "use", .clip = "clip"}};
    const auto output = emit(source);
    require_unsupported(output, "clip");
}

TEST_CASE("SM native actor declarations are deterministic readable and use absolute song time", "[stepmania][storyboard]") {
    const auto source = example();
    const auto first = emit(source), second = emit(source);
    REQUIRE((static_cast<bool>(first.value.files.size() == 1)));
    REQUIRE((static_cast<bool>(second.value.files.size() == 1)));
    CHECK((static_cast<bool>(first.value.files.front().bytes == second.value.files.front().bytes)));
    CHECK((static_cast<bool>(first.value.files.front().relative_path == "wormedit-storyboard/background/default.lua")));
    const auto& bytes = first.value.files.front().bytes;
    check_absolute_actor_clock_and_sandbox(bytes);
    CHECK((static_cast<bool>(sprite_report(first, "sprite").classification == osbx_osb_class_t::exact)));
}

TEST_CASE("SM native actor strings escape quotes and backslashes without code injection", "[stepmania][storyboard]") {
    auto source = example();
    source.nodes.front().id = "sprite\"\\id";
    source.images.front().file = "images/sprite\".png";
    const auto output = emit(source);
    REQUIRE((output.value.files.size() == 1));
    const auto& bytes = output.value.files.front().bytes;
    CHECK((bytes.find("sprite\\\"\\\\id") != std::string::npos));
    CHECK((bytes.find("images/sprite\\\".png") != std::string::npos));
}

TEST_CASE("SM native actor declaration block matches the example golden Lua", "[stepmania][storyboard]") {
    const auto output = emit(example());
    REQUIRE((static_cast<bool>(output.value.files.size() == 1)));
    const auto& bytes = output.value.files.front().bytes;
    const auto begin = bytes.find("local nodes = {");
    const auto end = bytes.find("\n-- Every binding");
    REQUIRE((static_cast<bool>(begin != std::string::npos)));
    REQUIRE((static_cast<bool>(end != std::string::npos)));
    constexpr std::string_view golden = R"LUA(local nodes = {
  [1] = {id="sprite",ops={{kind=0,track={default={320,240,},keys={}},children={}},},opacity={default={1,},keys={}},color={default={1,1,1,1,},keys={}},visibility={default=true,keys={}}},
}
local sprites = {
  {id="sprite",file="images/sprite.png",width=64,height=48,origin={0.5,0.5},blend={default="BlendMode_Normal",keys={}},chain={1,}},
}
)LUA";
    CHECK((static_cast<bool>(std::string_view{bytes}.substr(begin, end - begin) == golden)));
}

TEST_CASE("SM native clipping refusal is per object and output images follow accepted actors", "[stepmania][storyboard]") {
    auto source = example();
    source.nodes.push_back({.id = "clipped", .kind = osbx_node_kind_t::sprite, .image = "image"});
    const auto output = emit(source);
    REQUIRE((static_cast<bool>(output.value.files.size() == 1)));
    CHECK((static_cast<bool>(sprite_report(output, "sprite").classification == osbx_osb_class_t::exact)));
    CHECK((static_cast<bool>(sprite_report(output, "clipped").classification == osbx_osb_class_t::unsupported)));
    CHECK((static_cast<bool>(sprite_report(output, "clipped").reason.find("clipping") != std::string::npos)));
    CHECK((static_cast<bool>(output.value.files.front().images == std::vector<std::string>{"images/sprite.png"})));
}

TEST_CASE("SM ancestor baking cannot hide a drawable clipping refusal reason", "[stepmania][storyboard]") {
    auto source = example();
    source.nodes.front().parent = "group";
    source.nodes.push_back({.id = "clipped", .kind = osbx_node_kind_t::sprite,
        .parent = "group", .image = "image"});
    source.nodes.push_back({.id = "group", .tracks = {.opacity = {.default_value = {1}, .keys = {
        {.id = "fade-first", .value = {0.25}, .easing = osbx_cubic_bezier_t{0.42,0,0.58,1}},
        {.id = "fade-last", .time_ms = 2000, .value = {0.75}}}}}});
    const auto output = emit(source);
    REQUIRE((output.value.files.size() == 1));
    CHECK((sprite_report(output, "sprite").classification == osbx_osb_class_t::baked));
    const auto& clipped = sprite_report(output, "clipped");
    CHECK((clipped.classification == osbx_osb_class_t::unsupported));
    CHECK((clipped.reason.find("clipping") != std::string::npos));
    CHECK((clipped.reason.find("quarter-ms") == std::string::npos));
}

TEST_CASE("SM actor export validates finite playback and output bounds before artifacts", "[stepmania][storyboard]") {
    auto settings = options();
    settings.playback_span.end_time_ms = std::numeric_limits<std::int64_t>::max();
    require_unsupported(emit_stepmania_storyboard(example(), settings), "domain");
    settings = options();
    settings.maximum_output_bytes = 16;
    require_unsupported(emit_stepmania_storyboard(example(), settings), "byte");
    settings = options();
    settings.relative_directory = "../outside";
    require_unsupported(emit_stepmania_storyboard(example(), settings), "path");
    settings = options();
    settings.maximum_scene_storage_bytes = 1;
    require_unsupported(emit_stepmania_storyboard(example(), settings), "storage");
    settings = options();
    settings.maximum_actor_frames = 1;
    require_unsupported(emit_stepmania_storyboard(example(), settings), "frame budget");
    settings = options();
    settings.maximum_work = 1;
    require_unsupported(emit_stepmania_storyboard(example(), settings), "work budget");
}

TEST_CASE("SM actor export preserves the shared Pass Fail Neither choice", "[stepmania][storyboard]") {
    auto source = example();
    source.nodes.front().layer = osbx_layer_t::pass;
    CHECK((static_cast<bool>(emit(source).value.files.size() == 1)));
    auto settings = options();
    settings.conditional_layer = storyboard_conditional_layer_t::fail;
    CHECK((static_cast<bool>(emit_stepmania_storyboard(source, settings).value.files.empty())));
    settings.conditional_layer = storyboard_conditional_layer_t::neither;
    CHECK((static_cast<bool>(emit_stepmania_storyboard(source, settings).value.files.empty())));
    source.nodes.front().layer = osbx_layer_t::foreground;
    const auto foreground = emit_stepmania_storyboard(source, settings);
    REQUIRE((static_cast<bool>(foreground.value.files.size() == 1)));
    CHECK((static_cast<bool>(foreground.value.files.front().foreground)));
}

TEST_CASE("SM actor export refuses individually overflowing native properties even after cancellation", "[stepmania][storyboard]") {
    auto source = example();
    source.nodes.front().ops.insert(source.nodes.front().ops.begin(), {
        {.id = "huge", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {1e200,1e200}}},
        {.id = "tiny", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {1e-200,1e-200}}}});
    require_unsupported(emit(source), "native float range");
}

TEST_CASE("SM unsupported nodes leave supported storyboard objects available", "[stepmania][storyboard]") {
    auto source = example();
    source.nodes.push_back({.id = "unavailable", .kind = osbx_node_kind_t::sample});
    const auto output = emit(source);
    REQUIRE((static_cast<bool>(output.value.files.size() == 1)));
    CHECK((static_cast<bool>(sprite_report(output, "sprite").classification == osbx_osb_class_t::exact)));
    CHECK((static_cast<bool>(sprite_report(output, "unavailable").classification == osbx_osb_class_t::unsupported)));
}

TEST_CASE("SM actor export uses immutable prepared Lua samples without embedding the generator", "[stepmania][storyboard]") {
    auto source = example();
    osbx_lua_track_link_t binding;
    binding.link.id = "opacity-link";
    binding.link.source.source_reference = "opacity.lua";
    binding.link.source.frozen_source = "return function(t,ctx) return {t/2000} end";
    binding.link.source.source_hash = native_project_content_checksum(binding.link.source.frozen_source);
    binding.end_ms = 2000;
    source.nodes.front().tracks.opacity.lua = binding;
    const prepared_opacity_t backend;
    const std::array prepared{osbx_lua_prepared_scene_t::from(source,
        {.project = song_id_t{1}, .revision = 1}, backend)};
    REQUIRE((prepared.front().complete));
    const osbx_lua_prepared_scope_t scope{prepared};
    const auto output = emit(source);
    REQUIRE((output.value.files.size() == 1));
    CHECK((output.value.files.front().bytes.find(binding.link.source.frozen_source) == std::string::npos));
    CHECK((output.value.files.front().bytes.find("0.25") != std::string::npos));
}

TEST_CASE("SM actor export never executes missing prepared Lua links", "[stepmania][storyboard]") {
    auto source = example();
    osbx_lua_track_link_t link;
    link.link.id = "link";
    link.link.source.frozen_source = "return function(t,ctx) return {1} end";
    link.link.source.source_reference = "scene.lua";
    link.link.source.source_hash = native_project_content_checksum(link.link.source.frozen_source);
    source.nodes.front().tracks.opacity.lua = link;
    const auto output = emit(source);
    REQUIRE((static_cast<bool>(output.value.files.empty())));
    REQUIRE((!static_cast<bool>(output.value.report.empty())));
    CHECK((static_cast<bool>(output.value.report.front().classification == osbx_osb_class_t::unsupported)));
}

#if defined(WORMEDIT_HAS_LUA)
#include <wormedit/scripting/lua_sandbox.h>

namespace {
constexpr std::string_view engine_mock = R"LUA(
-- This is a mathematical actor fixture, not execution of an SM engine binary.
math.atan2 = math.atan
local methods = {}
for _, name in ipairs({'x','y','rotationz','zoomx','zoomy','halign','valign','blend','visible'}) do
  local property = name
  methods[property] = function(self, v)
    self['_' .. property] = v
    return self
  end
end
table.getn = table.getn or function(values) return #values end
function methods:hidden(value) self._visible = value == 0 end
function methods:horizalign(value) self._halign = ({left=0,center=0.5,right=1})[value] end
function methods:vertalign(value) self._valign = ({top=0,middle=0.5,bottom=1})[value] end
function methods:GetWidth() return 64 end
function methods:GetHeight() return 48 end
function methods:zoomtowidth(value) self._zoomx = value / 64 end
function methods:zoomtoheight(value) self._zoomy = value / 48 end
function methods:effectclock(value) self._clock = value end
function methods:GetSecsIntoEffect() return __time / 1000 end
function methods:GetNumChildren() return #self end
function methods:GetChildAt(index) return self[index + 1] end
function methods:blend(value)
  self._blend = ({normal='BlendMode_Normal',add='BlendMode_Add'})[value] or value
end
function methods:animate(_) return self end
function methods:setsize(width,height)
  self._width,self._height = width,height
  return self
end
function methods:GetTexture()
  return {GetImageWidth=function() return 64 end,GetImageHeight=function() return 48 end,
    GetTextureWidth=function() return 128 end,GetTextureHeight=function() return 64 end}
end
function methods:customtexturerect(left,top,right,bottom)
  self._texture_rect = {left,top,right,bottom}
  return self
end
function methods:zoom(v)
  self._zoomx, self._zoomy = v, v
  return self
end
function methods:diffuse(r,g,b,a)
  self._color = {r,g,b,a}
  return self
end
function methods:SetUpdateFunction(fn)
  self.callback = fn
end
function methods:stoptweening()
  self._sleep = 0
  return self
end
function methods:sleep(duration)
  self._sleep = duration
  return self
end
local function constructor(kind)
  return function(t)
    t._kind = kind
    for name, fn in pairs(methods) do t[name] = fn end
    return t
  end
end
Def = {ActorFrame=constructor('frame'), Sprite=constructor('sprite')}
SCREEN_WIDTH, SCREEN_HEIGHT = 640, 480
__time = 0
GAMESTATE = {
  GetCurrentSong = function() return {GetSongDir=function() return 'Songs/Pack/Song/' end} end,
  GetSongPosition = function() return {GetMusicSeconds=function() return __time / 1000 end} end
}
local function multiply(a,b)
  return {a[1]*b[1]+a[3]*b[2], a[2]*b[1]+a[4]*b[2],
    a[1]*b[3]+a[3]*b[4], a[2]*b[3]+a[4]*b[4],
    a[1]*b[5]+a[3]*b[6]+a[5], a[2]*b[5]+a[4]*b[6]+a[6]}
end
local function local_matrix(actor)
  local angle = math.rad(actor._rotationz or 0)
  local c,s = math.cos(angle),math.sin(angle)
  local sx,sy = actor._zoomx or 1,actor._zoomy or 1
  return {c*sx,s*sx,-s*sy,c*sy,actor._x or 0,actor._y or 0}
end
function __init(actor)
  if actor.InitCommand then actor.InitCommand(actor) end
  for _, child in ipairs(actor) do __init(child) end
end
local function collect(actor, world, color, visible, result)
  world = multiply(world, local_matrix(actor))
  local own = actor._color or {1,1,1,1}
  local combined = {}
  for channel = 1,4 do combined[channel] = color[channel]*own[channel] end
  visible = visible and actor._visible ~= false
  if visible and actor._kind == 'sprite' then
    result[#result + 1] = {id=actor.Name, world=world, color=combined,
      origin={actor._halign or 0.5,actor._valign or 0.5}, blend=actor._blend}
  end
  for _, child in ipairs(actor) do collect(child,world,combined,visible,result) end
end
function __sample(time, delta)
  __time = time
  __root.callback(__root, delta)
  local result = {}
  collect(__root,{1,0,0,1,0,0},{1,1,1,1},true,result)
  return result
end
)LUA";
using context_t = scripting::lua_sandbox_context_t;

auto field(const context_t& object, std::string_view name) -> const scripting::lua_sandbox_node_t& {
    const auto found = std::ranges::find_if(object, [&](const auto& node) {
        const auto* key = std::get_if<std::string>(&node.key);
        return key && *key == name;
    });
    REQUIRE((static_cast<bool>(found != object.end())));
    return *found;
}
auto children(const scripting::lua_sandbox_node_t& node) -> const context_t& {
    const auto* value = std::get_if<context_t>(&node.value);
    REQUIRE((static_cast<bool>(value)));
    return *value;
}
auto scalar_number(const scripting::lua_sandbox_node_t& node) -> double {
    const auto& value = std::get<scripting::lua_sandbox_value_t>(node.value);
    if (const auto* number = std::get_if<double>(&value)) return *number;
    return static_cast<double>(std::get<std::int64_t>(value));
}
auto scalar_text(const scripting::lua_sandbox_node_t& node) -> std::string {
    return std::get<std::string>(std::get<scripting::lua_sandbox_value_t>(node.value));
}
auto actor_vm(const std::string& script) -> scripting::lua_sandbox_t {
    auto vm = scripting::lua_sandbox_t::from({.maximum_instructions = 20'000'000});
    REQUIRE((static_cast<bool>(vm)));
    std::string source{engine_mock};
    source += "\n__root = (function()\n" + script + "\nend)()\n";
    source += "__init(__root)\n__root.OnCommand(__root)\nreturn true";
    const auto initialized = vm->evaluate(source);
    INFO((initialized ? "" : initialized.error().message));
    REQUIRE((static_cast<bool>(initialized)));
    return std::move(*vm);
}
auto check_actor_origin(const context_t& actual, const osbx_draw_instance_t& expected) -> storyboard_origin_t {
    const auto& origin = children(field(actual, "origin"));
    const auto fraction = storyboard_origin_t::from(expected.origin);
    REQUIRE((static_cast<bool>(fraction)));
    CHECK((static_cast<bool>(scalar_number(origin[0]) == fraction->x)));
    CHECK((static_cast<bool>(scalar_number(origin[1]) == fraction->y)));
    return *fraction;
}
void check_actor_corners(const context_t& actual, const osbx_draw_instance_t& expected, double tolerance) {
    const auto& values = children(field(actual, "world"));
    REQUIRE((static_cast<bool>(values.size() == 6)));
    visual_affine_t matrix;
    for (std::size_t i = 0; i < values.size(); ++i) matrix.values[i] = scalar_number(values[i]);
    const auto fraction = check_actor_origin(actual, expected);
    visual_image_quad_request_t request{.source_width = 64, .source_height = 48,
        .destination = {.width = 640, .height = 480}, .origin_x = fraction.x, .origin_y = fraction.y};
    const auto local = visual_image_quad_t::from(request);
    REQUIRE((static_cast<bool>(local)));
    for (const auto& point : local->points) {
        const auto wanted = expected.world.apply(point), obtained = matrix.apply(point);
        CHECK((static_cast<bool>(std::hypot(obtained.x - wanted.x, obtained.y - wanted.y) <= tolerance)));
    }
}
void check_actor_instance(const context_t& actor, const osbx_draw_instance_t& instance, double tolerance) {
    CHECK((static_cast<bool>(scalar_text(field(actor, "id")) == instance.id)));
    check_actor_corners(actor, instance, tolerance);
    const auto& color = children(field(actor, "color"));
    for (std::size_t channel = 0; channel < 3; ++channel)
        CHECK((static_cast<bool>(scalar_number(color[channel]) == Catch::Approx(instance.color[channel]))));
    CHECK((static_cast<bool>(scalar_number(color[3]) == Catch::Approx(instance.color[3] * instance.opacity))));
    const auto blend = instance.blend == osbx_blend_t::additive ? "BlendMode_Add" : "BlendMode_Normal";
    CHECK((static_cast<bool>(scalar_text(field(actor, "blend")) == blend)));
}
void check_actor_frame(const osbx_evaluation_index_t& index, const context_t& actual, storyboard_time_t time, double tolerance) {
    frame_arena_t arena;
    const auto expected = evaluate_osbx_frame(index, time, arena);
    REQUIRE((static_cast<bool>(expected.is_success())));
    REQUIRE((static_cast<bool>(actual.size() == expected.instances.size())));
    for (std::size_t i = 0; i < actual.size(); ++i)
        check_actor_instance(children(actual[i]), expected.instances[i], tolerance);
}
void compare_actor_scene(const osbx_document_t& scene, double tolerance = 1e-7) {
    const auto output = emit(scene);
    REQUIRE((static_cast<bool>(output.value.files.size() == 1)));
    REQUIRE((static_cast<bool>(output.value.report.front().classification != osbx_osb_class_t::unsupported)));
    const auto index = osbx_evaluation_index_t::from(scene);
    REQUIRE((static_cast<bool>(index.value)));
    auto vm = actor_vm(output.value.files.front().bytes);
    const std::array clocks{storyboard_time_t{}, storyboard_time_t{.milliseconds = 500, .remainder = 1, .denominator = 2},
        storyboard_time_t{.milliseconds = 1500, .remainder = 3, .denominator = 4},
        storyboard_time_t{.milliseconds = 1000}, storyboard_time_t{}, storyboard_time_t{.milliseconds = 2000}};
    for (const auto time : clocks) {
        const auto milliseconds = static_cast<double>(time.milliseconds) + time.fraction();
        const auto call = "return __sample(" + std::to_string(milliseconds) + ", 7.5)";
        const auto result = vm.evaluate_context(call);
        INFO((result ? "" : result.error().message));
        REQUIRE((static_cast<bool>(result)));
        check_actor_frame(*index.value, *result, time, tolerance);
    }
}
void check_actor_tail_seek_and_rewind(scripting::lua_sandbox_t& vm) {
    const auto hidden = vm.evaluate_context("return __sample(100000, 200)");
    REQUIRE((static_cast<bool>(hidden)));
    CHECK((hidden->empty()));
    const auto rewound = vm.evaluate_context("return __sample(500, 0.001)");
    REQUIRE((static_cast<bool>(rewound)));
    CHECK((rewound->size() == 1));
}
void check_actor_reactivation(scripting::lua_sandbox_t& vm) {
    const auto restarted = vm.evaluate("__root.OnCommand(__root); return true");
    REQUIRE((static_cast<bool>(restarted)));
    const auto first = vm.evaluate_context("return __sample(0, 999)");
    REQUIRE((static_cast<bool>(first)));
    CHECK((first->size() == 1));
}
auto notitg_vm(const std::string& script, std::size_t wrapper_count) -> scripting::lua_sandbox_t {
    auto vm = scripting::lua_sandbox_t::from({.maximum_instructions = 20'000'000});
    REQUIRE((static_cast<bool>(vm)));
    std::string source{engine_mock};
    source += "\nlocal initialize = (function()\n" + script + "\nend)()\n";
    // Fixed XML golden topology: root/node/translate/left/scale/right/sprite.
    // This fixture exercises the binding bridge, not an engine XML parser.
    source += "__root = Def.Sprite{Name='sprite'}\n";
    source += "for i=1," + std::to_string(wrapper_count) + " do __root=Def.ActorFrame{__root} end\n";
    source += "__root.OnCommand=function(self) initialize(self) end\n";
    source += "__root.OnCommand(__root)\nreturn true";
    const auto initialized = vm->evaluate(source);
    INFO((initialized ? "" : initialized.error().message));
    REQUIRE((static_cast<bool>(initialized)));
    return std::move(*vm);
}
void compare_notitg_scene(const osbx_document_t& scene, double tolerance = 1e-7, std::size_t wrapper_count = 6) {
    auto settings = options();
    settings.implementation_profile = stepmania_implementation_profile_t::notitg_v4;
    const auto output = emit_stepmania_storyboard(scene, settings);
    for (const auto& report : output.value.report) UNSCOPED_INFO(report.node_id << ": " << report.reason);
    REQUIRE((output.value.files.size() == 2));
    const auto index = osbx_evaluation_index_t::from(scene);
    REQUIRE((index.value.has_value()));
    auto vm = notitg_vm(output.value.files[1].bytes, wrapper_count);
    const std::array clocks{storyboard_time_t{}, storyboard_time_t{.milliseconds = 500, .remainder = 1, .denominator = 2},
        storyboard_time_t{.milliseconds = 1500, .remainder = 3, .denominator = 4}, storyboard_time_t{}};
    for (const auto time : clocks) {
        const auto milliseconds = static_cast<double>(time.milliseconds) + time.fraction();
        const auto actual = vm.evaluate_context("return __sample(" + std::to_string(milliseconds) + ", 999)");
        INFO((actual ? "" : actual.error().message));
        REQUIRE((static_cast<bool>(actual)));
        check_actor_frame(*index.value, *actual, time, tolerance);
    }
    check_actor_tail_seek_and_rewind(vm);
    check_actor_reactivation(vm);
}
} // namespace

TEST_CASE("SM actor wrappers preserve singular matrices simultaneous shear and signed scale", "[stepmania][storyboard][lua]") {
    auto source = example();
    source.nodes.front().origin = "TopLeft";
    source.nodes.front().ops.front().track.default_value = {200,120};
    source.nodes.front().ops.insert(source.nodes.front().ops.begin(),
        {.id = "shear", .kind = osbx_transform_kind_t::skew, .track = {.default_value = {20, -15}}});
    compare_actor_scene(source);
    const std::array matrices{std::vector<double>{1,2,2,4,0,0}, std::vector<double>{0,0,2,4,0,0},
        std::vector<double>{0,0,0,0,0,0}, std::vector<double>{-1,2,3,4,1,2}};
    for (const auto& matrix : matrices) {
        source.nodes.front().ops.front() = {.id = "affine", .kind = osbx_transform_kind_t::matrix,
            .track = {.default_value = matrix}};
        compare_actor_scene(source);
    }
}

TEST_CASE("SM actor property sampling preserves fractional clocks and backward seeks", "[stepmania][storyboard][lua]") {
    auto source = example();
    source.nodes.front().ops.front().track.keys = {
        {.id = "move-a", .value = {300, 200}}, {.id = "move-b", .time_ms = 2000, .value = {340, 260}}};
    source.nodes.front().tracks.opacity.keys = {
        {.id = "opacity-a", .value = {0.25}}, {.id = "opacity-b", .time_ms = 2000, .value = {0.75}}};
    source.nodes.front().tracks.color.default_value = {0.5, 0.7, 0.9, 0.5};
    source.nodes.front().tracks.blend.default_value = osbx_blend_t::additive;
    compare_actor_scene(source);
}

TEST_CASE("SM nonlinear easing is baked by the shared full-scene metric", "[stepmania][storyboard][lua]") {
    auto source = example();
    source.nodes.front().ops.front().track.keys = {
        {.id = "first", .value = {300,220}, .easing = osbx_cubic_bezier_t{0.42,0,0.58,1}},
        {.id = "last", .time_ms = 2000, .value = {340,260}}};
    const auto emitted = emit(source);
    REQUIRE((static_cast<bool>(emitted.value.files.size() == 1)));
    const auto& report = sprite_report(emitted, "sprite");
    CHECK((static_cast<bool>(report.classification == osbx_osb_class_t::baked)));
    CHECK((static_cast<bool>(report.worst_error <= 1 + 1e-9)));
    CHECK((static_cast<bool>(report.key_count < report.keys_before_simplify)));
    CHECK((static_cast<bool>(report.reason.find("quarter-ms") != std::string::npos)));
    compare_actor_scene(source, simplify_corner_error_px);
}

TEST_CASE("SM nested pivots preserve transform stack order and negative scale", "[stepmania][storyboard][lua]") {
    auto source = example();
    osbx_transform_op_t pivot{.id = "pivot", .kind = osbx_transform_kind_t::pivot,
        .track = {.default_value = {20,10}}, .children = {
            {.id = "flip", .kind = osbx_transform_kind_t::scale, .track = {.default_value = {-1,0.5}}},
            {.id = "rotate", .kind = osbx_transform_kind_t::rotate, .track = {.default_value = {30}}}}};
    source.nodes.front().ops.insert(source.nodes.front().ops.begin(), pivot);
    compare_actor_scene(source);
}

TEST_CASE("SM actor inherited lifetimes remain inclusive only at the exact end instant", "[stepmania][storyboard][lua]") {
    auto source = example();
    source.nodes.front().parent = "parent";
    source.nodes.push_back({.id = "parent", .lifetime = osbx_lifetime_t{100,1500}});
    compare_actor_scene(source);
}

TEST_CASE("SM finite foreground keeper permits tail seeks and reactivation within its clock domain", "[stepmania][storyboard][lua]") {
    const auto source = example();
    const auto output = emit(source);
    REQUIRE((output.value.files.size() == 1));
    const auto& script = output.value.files.front().bytes;
    CHECK((script.find("local keep_alive_seconds = 86402") != std::string::npos));
    auto vm = actor_vm(script);
    check_actor_tail_seek_and_rewind(vm);
    check_actor_reactivation(vm);
}

TEST_CASE("SM actor ancestor paths preserve interleaved global sprite order", "[stepmania][storyboard][lua]") {
    auto source = example();
    source.nodes.front().parent = "parent";
    source.nodes.push_back({.id = "parent", .tracks = {.opacity = {.default_value = {0.5}}}});
    auto other = source.nodes.front();
    other.id = "other";
    other.ops.front().id = "other-position";
    other.parent.reset();
    other.order = 1;
    source.nodes.push_back(other);
    auto last = source.nodes.front();
    last.id = "last";
    last.ops.front().id = "last-position";
    last.order = 2;
    source.nodes.push_back(last);
    compare_actor_scene(source);
}
TEST_CASE("NotITG mock bindings preserve canonical fractional affine corners and restart", "[notitg][storyboard][lua]") {
    auto source = example();
    source.nodes.front().layer = osbx_layer_t::foreground;
    source.nodes.front().ops.insert(source.nodes.front().ops.begin(),
        {.id = "shear", .kind = osbx_transform_kind_t::skew, .track = {.default_value = {20,-15}}});
    source.nodes.front().tracks.blend.default_value = osbx_blend_t::additive;
    source.nodes.front().origin = "TopLeft";
    source.nodes.front().tracks.opacity.keys = {{.id = "opacity-first", .value = {0.25}},
        {.id = "opacity-last", .time_ms = 2000, .value = {0.75}}};
    source.nodes.front().tracks.color.default_value = {0.5,0.7,0.9,0.5};
    compare_notitg_scene(source);
    const std::array matrices{std::vector<double>{0,0,0,0,0,0}, std::vector<double>{0,0,0.5,1,0,0},
        std::vector<double>{-1,0,0,1,0,0}};
    for (const auto& matrix : matrices) {
        source.nodes.front().ops.front() = {.id = "affine", .kind = osbx_transform_kind_t::matrix,
            .track = {.default_value = matrix}};
        compare_notitg_scene(source);
    }
    source.nodes.front().parent = "parent";
    source.nodes.push_back({.id = "parent", .kind = osbx_node_kind_t::group, .layer = osbx_layer_t::foreground,
        .ops = {{.id = "parent-position", .track = {.default_value = {10,20}}}},
        .tracks = {.opacity = {.default_value = {0.5}}}});
    compare_notitg_scene(source, 1e-7, 8);
}

TEST_CASE("NotITG nonlinear tracks reuse the bounded shared metric with inherited shear", "[notitg][storyboard][lua]") {
    auto source = example();
    source.nodes.front().layer = osbx_layer_t::foreground;
    source.nodes.front().ops.insert(source.nodes.front().ops.begin(),
        {.id = "shear", .kind = osbx_transform_kind_t::skew, .track = {.default_value = {10,-5}}});
    source.nodes.front().ops.back().track.keys = {
        {.id = "first", .value = {300,220}, .easing = osbx_cubic_bezier_t{0.42,0,0.58,1}},
        {.id = "last", .time_ms = 2000, .value = {340,260}}};
    auto settings = options();
    settings.implementation_profile = stepmania_implementation_profile_t::notitg_v4;
    const auto output = emit_stepmania_storyboard(source, settings);
    REQUIRE((sprite_report(output, "sprite").classification == osbx_osb_class_t::baked));
    CHECK((sprite_report(output, "sprite").reason.find("no continuous-time certificate") != std::string::npos));
    compare_notitg_scene(source, simplify_corner_error_px);
}

#endif

namespace {
void check_notitg_xml_entry(const stepmania_storyboard_file_t& xml) {
    CHECK((xml.actor_reference == "wormedit-storyboard/foreground/main.xml"));
    CHECK((xml.bytes.find("<ActorFrame") != std::string::npos));
    CHECK((xml.bytes.find("main.lua") != std::string::npos));
}
void check_notitg_lua_entry(const stepmania_storyboard_file_t& lua) {
    CHECK((lua.actor_reference.empty()));
    CHECK((lua.bytes.find("GetSecsIntoEffect()") != std::string::npos));
    CHECK((lua.bytes.find("#") == std::string::npos));
    CHECK((lua.bytes.find("local Def =") != std::string::npos));
    CHECK((lua.bytes.find("GetChildAt") != std::string::npos));
}
}
TEST_CASE("NotITG foreground emits bounded XML and Lua50 sidecar and refuses background", "[stepmania][storyboard][notitg]") {
    auto source = example();
    auto settings = options();
    settings.implementation_profile = stepmania_implementation_profile_t::notitg_v4;
    const auto background = emit_stepmania_storyboard(source, settings);
    require_unsupported(background, "background lifecycle");
    source.nodes.front().layer = osbx_layer_t::foreground;
    const auto foreground = emit_stepmania_storyboard(source, settings);
    REQUIRE((foreground.value.files.size() == 2));
    check_notitg_xml_entry(foreground.value.files[0]);
    check_notitg_lua_entry(foreground.value.files[1]);
}

TEST_CASE("NotITG XML matches the single-sprite golden and escapes image attributes", "[notitg][storyboard]") {
    auto source = example();
    source.nodes.front().layer = osbx_layer_t::foreground;
    auto settings = options();
    settings.implementation_profile = stepmania_implementation_profile_t::notitg_v4;
    const auto output = emit_stepmania_storyboard(source, settings);
    REQUIRE((output.value.files.size() == 2));
    constexpr std::string_view golden = R"XML(<ActorFrame OnCommand="%function(self) local initialize = assert(loadfile(GAMESTATE:GetCurrentSong():GetSongDir() .. &quot;wormedit-storyboard/foreground/main.lua&quot;)); initialize()(self) end">
  <children>
    <ActorFrame>
      <children>
        <ActorFrame>
          <children>
            <Sprite Texture="../../images/sprite.png" />
          </children>
        </ActorFrame>
      </children>
    </ActorFrame>
  </children>
</ActorFrame>
)XML";
    CHECK((output.value.files[0].bytes == golden));
    source.images.front().file = "images/sprite&\".png";
    const auto escaped = emit_stepmania_storyboard(source, settings);
    REQUIRE((escaped.value.files.size() == 2));
    CHECK((escaped.value.files[0].bytes.find("sprite&amp;&quot;.png") != std::string::npos));
}

TEST_CASE("NotITG refuses unproved frame grids and bounded XML depth without a partial sidecar", "[notitg][storyboard]") {
    auto source = example();
    source.nodes.front().layer = osbx_layer_t::foreground;
    auto settings = options();
    settings.implementation_profile = stepmania_implementation_profile_t::notitg_v4;
    source.images.front().file = "images/sprite 2x2.png";
    require_unsupported(emit_stepmania_storyboard(source, settings), "texture grids");
    source = example();
    source.nodes.front().layer = osbx_layer_t::foreground;
    for (std::size_t i = 0; i < 64; ++i)
        source.nodes.front().ops.push_back({.id = "scale-" + std::to_string(i),
            .kind = osbx_transform_kind_t::scale, .track = {.default_value = {1,1}}});
    require_unsupported(emit_stepmania_storyboard(source, settings), "64-actor");
    source = example();
    source.nodes.front().layer = osbx_layer_t::foreground;
    settings.maximum_output_bytes = 64;
    require_unsupported(emit_stepmania_storyboard(source, settings), "byte budget");
}

TEST_CASE("Etterna uses identical canonical native actor runtime and layer planning", "[stepmania][storyboard][etterna]") {
    auto scene = example();
    auto foreground = scene.nodes.front(); foreground.id = "foreground"; foreground.layer = osbx_layer_t::foreground;
    foreground.ops.front().id = "foreground-position";
    scene.nodes.push_back(std::move(foreground));
    auto settings = options();
    const auto shared = emit_stepmania_storyboard(scene, settings);
    settings.implementation_profile = stepmania_implementation_profile_t::etterna_0_75_1;
    const auto etterna = emit_stepmania_storyboard(scene, settings);
    REQUIRE(etterna.value.files.size() == 2); REQUIRE(shared.value.files.size() == 2);
    CHECK(etterna.value.files[0].bytes == shared.value.files[0].bytes);
    CHECK(etterna.value.files[1].bytes == shared.value.files[1].bytes);
    CHECK_FALSE(etterna.value.files[0].foreground); CHECK(etterna.value.files[1].foreground);
    check_absolute_actor_clock_and_sandbox(etterna.value.files[0].bytes);
}

TEST_CASE("SM conditional selection does not refuse an unselected clip actor", "[stepmania][storyboard][clips]") {
    auto source = example();
    source.nodes[0].layer = osbx_layer_t::fail;
    source.nodes.push_back({.id = "visible", .kind = osbx_node_kind_t::sprite, .image = "image",
        .ops = {{.id = "visible-position", .track = {.default_value = {320,240}}}}});
    const auto original = emit(source);
    source.clips = {{.id = "clip"}};
    source.nodes[0].uses = {{.id = "use", .clip = "clip"}};
    const auto output = emit(source);
    REQUIRE(original.value.files.size() == 1); REQUIRE(output.value.files.size() == 1);
    CHECK(original.value.files[0].bytes == output.value.files[0].bytes);
}

TEST_CASE("Native actors report temporal programs instead of dropping their keys", "[stepmania][storyboard][program]") {
    auto source = example();
    source.nodes[0].tracks.opacity.program = {{.data = osbx_loop_t<>{.id = "repeat", .count = 2,
        .keys = {{.id = "constant", .value = {0.5}}}}}};
    const auto result = emit(source);
    CHECK(result.value.files.empty());
    REQUIRE_FALSE(result.value.report.empty());
    CHECK(result.value.report[0].classification == osbx_osb_class_t::unsupported);
}
