#include "stepmania_storyboard.h"
#include "../core/text_util.h"

#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/osbx_export_metric.h>
#include <wormedit/core/keyframe_simplify.h>
#include <numbers>
#include <type_traits>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string_view>

namespace wormedit {
namespace {

// Lua numbers are doubles. A bounded finite song domain also makes the keep-
// alive duration meaningful rather than serializing an effectively infinite sleep.
constexpr auto maximum_music_time_ms = stepmania_storyboard_options_t::maximum_music_time_ms;
static_assert(static_cast<int>(osbx_transform_kind_t::translate) == 0 && static_cast<int>(osbx_transform_kind_t::rotate) == 1 &&
    static_cast<int>(osbx_transform_kind_t::scale) == 2 && static_cast<int>(osbx_transform_kind_t::skew) == 3 &&
    static_cast<int>(osbx_transform_kind_t::matrix) == 4 && static_cast<int>(osbx_transform_kind_t::pivot) == 5);

class lua_text_t {
  public:
    explicit lua_text_t(std::size_t maximum) : maximum_(maximum) {}
    void append(std::string_view value) {
        if (failed_) return;
        if (value.size() > maximum_ - text_.size()) {
            failed_ = true;
            return;
        }
        text_.append(value);
    }
    void quoted(std::string_view value) {
        if (failed_) return;
        append("\"");
        for (const auto character : value) escaped(static_cast<unsigned char>(character));
        append("\"");
    }
    void xml_attribute(std::string_view value) {
        for (const auto character : value) {
            const auto byte = static_cast<unsigned char>(character);
            switch (byte) {
            case '&': append("&amp;"); break;
            case '<': append("&lt;"); break;
            case '>': append("&gt;"); break;
            case '"': append("&quot;"); break;
            case '\'': append("&apos;"); break;
            case '\n': append("&#10;"); break;
            case '\r': append("&#13;"); break;
            case '\t': append("&#9;"); break;
            default:
                if (byte < 32) failed_ = true;
                else append({&character, 1});
                break;
            }
        }
    }
    void number(double value) {
        if (failed_) return;
        std::array<char, 64> buffer{};
        const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
            value, std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (error != std::errc{} || !std::isfinite(value)) failed_ = true;
        else append({buffer.data(), static_cast<std::size_t>(end - buffer.data())});
    }
    void integer(std::int64_t value) {
        if (failed_) return;
        std::array<char, 32> buffer{};
        const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (error != std::errc{}) failed_ = true;
        else append({buffer.data(), static_cast<std::size_t>(end - buffer.data())});
    }
    [[nodiscard]] auto is_success() const -> bool { return !failed_; }
    [[nodiscard]] auto take() -> std::string { return std::move(text_); }
  private:
    void escaped(unsigned char value) {
        if (value == '"' || value == '\\') {
            const std::array bytes{'\\', static_cast<char>(value)};
            append({bytes.data(), bytes.size()});
        } else if (value < 32 || value == 127) {
            const std::array bytes{'\\', static_cast<char>('0' + value / 100),
                static_cast<char>('0' + value / 10 % 10), static_cast<char>('0' + value % 10)};
            append({bytes.data(), bytes.size()});
        } else {
            const auto byte = static_cast<char>(value);
            append({&byte, 1});
        }
    }
    std::string text_;
    std::size_t maximum_{};
    bool failed_{};
};

auto option_failure(const stepmania_storyboard_options_t& options) -> std::string {
    const auto& span = options.playback_span;
    if (span.begin_time_ms > span.end_time_ms || span.begin_time_ms < -maximum_music_time_ms ||
        span.end_time_ms > maximum_music_time_ms || options.activation_time_ms > span.begin_time_ms ||
        options.activation_time_ms < -maximum_music_time_ms)
        return "Native actor export requires an ordered, bounded playback/activation domain within one day";
    if (!osbx_document_t::is_valid_project_path(options.relative_directory))
        return "Native actor directory must be a safe project-relative path";
    if (options.maximum_actor_frames == 0 || options.maximum_work == 0)
        return "Native actor export work/frame budget is empty";
    if (static_cast<unsigned>(options.conditional_layer) > static_cast<unsigned>(storyboard_conditional_layer_t::neither))
        return "Native actor conditional layer choice is invalid";
    if (static_cast<unsigned>(options.implementation_profile) > static_cast<unsigned>(stepmania_implementation_profile_t::notitg_v4))
        return "Native actor implementation profile is invalid";
    return {};
}

template <class Value> auto clock_failure(const osbx_track_t<Value>& track) -> std::string {
    for (const auto& key : track.keys)
        if (key.time_ms < -maximum_music_time_ms || key.time_ms > maximum_music_time_ms)
            return "Authored key exceeds the bounded native Lua song-clock domain";
    return {};
}

auto has_linear_easing(const osbx_easing_t& easing) -> bool {
    if (std::holds_alternative<osbx_step_easing_t>(easing) || std::holds_alternative<osbx_linear_easing_t>(easing)) return true;
    const auto* osb = std::get_if<osbx_osb_easing_t>(&easing);
    return osb && osb->value == 0;
}
auto needs_bake(const osbx_track_t<>& track) -> bool {
    for (std::size_t i = 0; i + 1 < track.keys.size(); ++i)
        if (track.keys[i].value != track.keys[i+1].value && !has_linear_easing(track.keys[i].easing)) return true;
    return false;
}
auto numeric_failure(const osbx_track_t<>& track) -> std::string {
    if (auto reason = clock_failure(track); !reason.empty()) return reason;
    const auto native_range = [](const auto& values) {
        return std::ranges::all_of(values, [](double value) {
            return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max() / 32;
        });
    };
    if (!native_range(track.default_value) ||
        !std::ranges::all_of(track.keys, [&](const auto& key) { return native_range(key.value); }))
        return "Numeric property exceeds the conservative native float range";
    return needs_bake(track) ? "Nonlinear native track requires shared metric-certified baking" : "";
}

auto ops_failure(std::span<const osbx_transform_op_t> ops) -> std::string {
    for (const auto& op : ops) {
        if (auto reason = numeric_failure(op.track); !reason.empty()) return op.id + ": " + reason;
        if (auto reason = ops_failure(op.children); !reason.empty()) return reason;
    }
    return {};
}

auto has_unit_color(const osbx_track_t<>& track) -> bool {
    const auto unit = [](const auto& values) {
        return std::ranges::all_of(values, [](double v) { return v >= 0 && v <= 1; });
    };
    return unit(track.default_value) && std::ranges::all_of(track.keys, [&](const auto& key) { return unit(key.value); });
}
auto node_failure(const osbx_node_t& node) -> std::string {
    if (node.kind != osbx_node_kind_t::group && node.kind != osbx_node_kind_t::sprite)
        return "Animation/sample actor export is unsupported until OSBX-10";
    if (!has_unit_color(node.tracks.color)) return "Native actor tint outside unit RGBA is not certified";
    if (node.lifetime && (node.lifetime->begin_ms < -maximum_music_time_ms || node.lifetime->end_ms > maximum_music_time_ms))
        return "Authored lifetime exceeds the bounded native Lua song-clock domain";
    if (auto reason = ops_failure(node.ops); !reason.empty()) return reason;
    if (auto reason = numeric_failure(node.tracks.opacity); !reason.empty()) return reason;
    if (auto reason = numeric_failure(node.tracks.color); !reason.empty()) return reason;
    if (auto reason = clock_failure(node.tracks.visibility); !reason.empty()) return reason;
    return clock_failure(node.tracks.blend);
}

template <class Value, class Emit>
void emit_track(lua_text_t& text, const osbx_track_t<Value>& track, Emit emit) {
    text.append("{default=");
    emit(track.default_value);
    text.append(",keys={");
    for (const auto& key : track.keys) {
        text.append("{time=");
        text.integer(key.time_ms);
        text.append(",value=");
        emit(key.value);
        text.append(",step=");
        text.append(std::holds_alternative<osbx_step_easing_t>(key.easing) ? "true}," : "false},");
    }
    text.append("}}");
}
void emit_numeric(lua_text_t& text, const osbx_track_t<>& track) {
    emit_track(text, track, [&](const auto& value) {
        text.append("{");
        for (const auto component : value) {
            text.number(component);
            text.append(",");
        }
        text.append("}");
    });
}

void emit_ops(lua_text_t& text, std::span<const osbx_transform_op_t> ops) {
    text.append("{");
    for (const auto& op : ops) {
        text.append("{kind=");
        text.integer(static_cast<std::int64_t>(op.kind));
        text.append(",track=");
        emit_numeric(text, op.track);
        text.append(",children=");
        emit_ops(text, op.children);
        text.append("},");
    }
    text.append("}");
}

void emit_node(lua_text_t& text, const osbx_node_t& node) {
    text.append("{id=");
    text.quoted(node.id);
    text.append(",ops=");
    emit_ops(text, node.ops);
    text.append(",opacity=");
    emit_numeric(text, node.tracks.opacity);
    text.append(",color=");
    emit_numeric(text, node.tracks.color);
    text.append(",visibility=");
    emit_track(text, node.tracks.visibility, [&](bool value) { text.append(value ? "true" : "false"); });
    if (node.lifetime) {
        text.append(",begin=");
        text.integer(node.lifetime->begin_ms);
        text.append(",finish=");
        text.integer(node.lifetime->end_ms);
    }
    text.append("}");
}

// These are emitted Lua operations, not another C++ scene evaluator. Numeric
// interpolation is linear/step only; nonlinear curves enter via the shared bake
// owner, never a copied easing implementation in this adapter.
constexpr std::string_view actor_runtime = R"WORMEDIT_LUA(
-- Every binding samples absolute music milliseconds, never callback delta.
local array_length = table.getn
local function set_visible(self, visible)
  if notitg then
    self:hidden(visible and 0 or 1)
  else
    self:visible(visible)
  end
end
local bindings = {}
local song = GAMESTATE:GetCurrentSong()
local song_directory = song:GetSongDir()
local function frame(child, update)
  local binding = {update = update}
  bindings[array_length(bindings) + 1] = binding
  return Def.ActorFrame{
    InitCommand = function(self)
      binding.actor = self
    end,
    child
  }
end
local function value(track, time, discrete)
  local keys = track.keys
  if array_length(keys) == 0 then
    return track.default
  end
  if time < keys[1].time then
    return keys[1].value
  end
  local low, high = 1, array_length(keys) + 1
  while low + 1 < high do
    local middle = math.floor((low + high) / 2)
    if keys[middle].time <= time then
      low = middle
    else
      high = middle
    end
  end
  local left = keys[low]
  if discrete or left.step or high > array_length(keys) then
    return left.value
  end
  local right = keys[high]
  local progress = (time - left.time) / (right.time - left.time)
  local result = {}
  for component = 1, array_length(left.value) do
    result[component] = left.value[component] * (1 - progress) + right.value[component] * progress
  end
  return result
end
local function translation(track, child, sign)
  return frame(child, function(self, time)
    local v = value(track, time)
    self:x(sign * v[1])
    self:y(sign * v[2])
  end)
end
local function affine(op, child)
  local right_rotation, scale
  local inner = Def.ActorFrame{
    InitCommand = function(self)
      right_rotation = self
    end,
    child
  }
  local middle = Def.ActorFrame{
    InitCommand = function(self)
      scale = self
    end,
    inner
  }
  return frame(middle, function(self, time)
    local v = value(op.track, time)
    local a, b, c, d, x, y = v[1], v[2], v[3], v[4], v[5], v[6]
    if op.kind == 3 then
      local x_angle = math.rad(v[1])
      local y_angle = math.rad(v[2])
      a, d = 1, 1
      b = math.tan(y_angle)
      c = math.tan(x_angle)
      x, y = 0, 0
    end
    -- R(left) * signed diagonal scale * R(right), including rank zero/one.
    -- No division by a matrix coefficient or determinant is needed.
    local sum = math.sqrt((a + d) * (a + d) + (b - c) * (b - c))
    local difference = math.sqrt((a - d) * (a - d) + (b + c) * (b + c))
    local first = math.atan2(b - c, a + d)
    local second = math.atan2(b + c, a - d)
    self:x(x)
    self:y(y)
    local left_angle = math.deg((first + second) / 2)
    self:rotationz(left_angle)
    scale:zoomx((sum + difference) / 2)
    scale:zoomy((sum - difference) / 2)
    local right_angle = math.deg((first - second) / 2)
    right_rotation:rotationz(right_angle)
  end)
end
local operations
local function operation(op, child)
  if op.kind == 0 then
    return translation(op.track, child, 1)
  end
  if op.kind == 1 then
    return frame(child, function(self, time)
      local v = value(op.track, time)
      self:rotationz(v[1])
    end)
  end
  if op.kind == 2 then
    return frame(child, function(self, time)
      local v = value(op.track, time)
      self:zoomx(v[1])
      self:zoomy(v[2])
    end)
  end
  if op.kind == 5 then
    local shifted = translation(op.track, child, -1)
    local transformed = operations(op.children, shifted)
    return translation(op.track, transformed, 1)
  end
  return affine(op, child)
end
operations = function(ops, child)
  for index = 1, array_length(ops) do
    child = operation(ops[index], child)
  end
  return child
end
local function node(source, child)
  local transformed = operations(source.ops, child)
  return frame(transformed, function(self, time)
    local color = value(source.color, time)
    local opacity_values = value(source.opacity, time)
    local opacity = opacity_values[1]
    local visible = value(source.visibility, time, true)
    if source.begin then
      visible = visible and time >= source.begin and time <= source.finish
    end
    set_visible(self, visible)
    self:diffuse(color[1], color[2], color[3], color[4] * opacity)
  end)
end
local function sprite(source)
  local binding = {}
  bindings[array_length(bindings) + 1] = binding
  return Def.Sprite{
    Name = source.id,
    Texture = song_directory .. source.file,
    InitCommand = function(self)
      binding.actor = self
      if notitg then
        self:animate(0)
        assert(self:GetWidth() > 0 and self:GetHeight() > 0, "Invalid native image geometry")
        self:zoomtowidth(source.width)
        self:zoomtoheight(source.height)
        local horizontal = {"left", "center", "right"}
        local vertical = {"top", "middle", "bottom"}
        self:horizalign(horizontal[math.floor(source.origin[1] * 2) + 1])
        self:vertalign(vertical[math.floor(source.origin[2] * 2) + 1])
      else
        self:animate(false)
        self:setsize(source.width, source.height)
        local texture = self:GetTexture()
        local image_width = texture:GetImageWidth()
        local image_height = texture:GetImageHeight()
        local texture_width = texture:GetTextureWidth()
        local texture_height = texture:GetTextureHeight()
        self:customtexturerect(0, 0, image_width / texture_width, image_height / texture_height)
        self:halign(source.origin[1])
        self:valign(source.origin[2])
      end
    end,
    OnCommand = function(self)
      self:stoptweening()
    end
  }
end
local function update()
  local time
  if notitg then
    time = native_root:GetSecsIntoEffect() * 1000
  else
    local position = GAMESTATE:GetSongPosition()
    time = position:GetMusicSeconds() * 1000
  end
  for index = 1, array_length(bindings) do
    local binding = bindings[index]
    if binding.actor and binding.update then
      binding.update(binding.actor, time)
    end
  end
  if notitg then
    native_root:hidden(0)
  end
end
local root = Def.ActorFrame{
  InitCommand = function(self)
    local fit = math.min(SCREEN_WIDTH / canvas_width, SCREEN_HEIGHT / canvas_height)
    self:x((SCREEN_WIDTH - canvas_width * fit) / 2)
    self:y((SCREEN_HEIGHT - canvas_height * fit) / 2)
    self:zoom(fit)
    if notitg then
      self:effectclock("music")
      -- Wait for the first actor update to refresh its absolute music clock.
      self:hidden(1)
    end
    self:SetUpdateFunction(update)
  end,
  OnCommand = function(self)
    self:stoptweening()
    self:sleep(keep_alive_seconds)
    if not notitg then
      update()
    end
  end
}
for index = 1, array_length(sprites) do
  local source = sprites[index]
  local child = sprite(source)
  local binding = bindings[array_length(bindings)]
  binding.update = function(self, time)
    local blend = value(source.blend, time, true)
    self:blend(blend)
    set_visible(self, time >= playback_begin_ms and time <= playback_end_ms)
  end
  for path = 1, array_length(source.chain) do
    child = node(nodes[source.chain[path]], child)
  end
  root[array_length(root) + 1] = child
end
return root
)WORMEDIT_LUA";

// Outward-rounded interval arithmetic is a containment proof, not a second
// time evaluator. Independent component hulls intentionally over-approximate:
// rejecting an uncertain drawable is safer than leaking into letterbox space.
struct range_t {
    double low{}, high{};
    [[nodiscard]] static auto point(double value) -> range_t { return {value, value}; }
    [[nodiscard]] auto operator+(range_t other) const -> range_t {
        return {std::nextafter(low + other.low, -std::numeric_limits<double>::infinity()),
                std::nextafter(high + other.high, std::numeric_limits<double>::infinity())};
    }
    [[nodiscard]] auto operator*(range_t other) const -> range_t {
        const std::array products{low * other.low, low * other.high, high * other.low, high * other.high};
        if (!std::ranges::all_of(products, [](double value) { return std::isfinite(value); }))
            return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
        const auto [begin, end] = std::minmax_element(products.begin(), products.end());
        return {std::nextafter(*begin, -std::numeric_limits<double>::infinity()),
                std::nextafter(*end, std::numeric_limits<double>::infinity())};
    }
    [[nodiscard]] auto negative() const -> range_t { return {-high, -low}; }
};
using affine_range_t = std::array<range_t, 6>;
auto identity_range() -> affine_range_t {
    return {range_t::point(1), range_t{}, range_t{}, range_t::point(1), range_t{}, range_t{}};
}
auto multiply(affine_range_t a, affine_range_t b) -> affine_range_t {
    return {a[0]*b[0]+a[2]*b[1], a[1]*b[0]+a[3]*b[1],
        a[0]*b[2]+a[2]*b[3], a[1]*b[2]+a[3]*b[3],
        a[0]*b[4]+a[2]*b[5]+a[4], a[1]*b[4]+a[3]*b[5]+a[5]};
}
auto track_range(const osbx_track_t<>& track) -> std::array<range_t, 6> {
    std::array<range_t, 6> ranges{};
    const auto& initial = track.keys.empty() ? track.default_value : track.keys.front().value;
    for (std::size_t i = 0; i < initial.size(); ++i) ranges[i] = range_t::point(initial[i]);
    for (const auto& key : track.keys)
        for (std::size_t i = 0; i < key.value.size(); ++i) {
            ranges[i].low = std::min(ranges[i].low, key.value[i]);
            ranges[i].high = std::max(ranges[i].high, key.value[i]);
        }
    return ranges;
}
auto tangent_range(range_t degrees) -> range_t {
    constexpr auto radians = std::numbers::pi / 180;
    if (std::floor((degrees.low + 90) / 180) != std::floor((degrees.high + 90) / 180))
        return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    return {std::nextafter(std::tan(degrees.low * radians), -std::numeric_limits<double>::infinity()),
            std::nextafter(std::tan(degrees.high * radians), std::numeric_limits<double>::infinity())};
}
auto ranges_for_ops(std::span<const osbx_transform_op_t>) -> affine_range_t;
auto range_for_op(const osbx_transform_op_t& op) -> affine_range_t {
    const auto v = track_range(op.track);
    auto result = identity_range();
    switch (op.kind) {
    case osbx_transform_kind_t::translate: result[4] = v[0]; result[5] = v[1]; break;
    case osbx_transform_kind_t::scale: result[0] = v[0]; result[3] = v[1]; break;
    case osbx_transform_kind_t::rotate: {
        const auto fixed = v[0].low == v[0].high;
        const auto angle = v[0].low * std::numbers::pi / 180;
        const range_t cosine = fixed ? range_t::point(std::cos(angle)) : range_t{-1, 1};
        const range_t sine = fixed ? range_t::point(std::sin(angle)) : range_t{-1, 1};
        result[0] = result[3] = cosine; result[1] = sine; result[2] = sine.negative();
        break;
    }
    case osbx_transform_kind_t::skew: result[1] = tangent_range(v[1]); result[2] = tangent_range(v[0]); break;
    case osbx_transform_kind_t::matrix: result = v; break;
    case osbx_transform_kind_t::pivot: {
        auto before = identity_range(), after = identity_range();
        before[4] = v[0]; before[5] = v[1];
        after[4] = v[0].negative(); after[5] = v[1].negative();
        result = multiply(multiply(before, ranges_for_ops(op.children)), after);
        break;
    }
    }
    return result;
}
auto ranges_for_ops(std::span<const osbx_transform_op_t> ops) -> affine_range_t {
    auto result = identity_range();
    for (const auto& op : ops) result = multiply(range_for_op(op), result);
    return result;
}
auto has_containment(const affine_range_t& world, const osbx_canvas_t& canvas,
    const osbx_osb_image_size_t& size, std::string_view origin_name) -> bool {
    const auto origin = storyboard_origin_t::from(origin_name);
    if (!origin || !size.width || !size.height) return false;
    const auto local = visual_image_quad_t::from({.source_width = size.width, .source_height = size.height,
        .destination = {.width = canvas.width, .height = canvas.height},
        .logical_width = canvas.width, .logical_height = canvas.height,
        .origin_x = origin->x, .origin_y = origin->y});
    if (!local) return false;
    for (const auto point : local->points) {
        const auto x = world[0] * range_t::point(point.x) + world[2] * range_t::point(point.y) + world[4];
        const auto y = world[1] * range_t::point(point.x) + world[3] * range_t::point(point.y) + world[5];
        if (!std::isfinite(x.low) || !std::isfinite(x.high) || !std::isfinite(y.low) || !std::isfinite(y.high) ||
            x.low < 0 || x.high > canvas.width || y.low < 0 || y.high > canvas.height) return false;
    }
    return true;
}
struct node_analysis_t {
    std::string failure;
    affine_range_t world;
    std::size_t frames{};
};

struct sprite_plan_t {
    std::size_t node{};
    std::size_t frames{};
    std::vector<std::size_t> chain; // Leaf to root, as required by wrapper nesting.
};
struct actor_plan_t {
    std::vector<sprite_plan_t> background;
    std::vector<sprite_plan_t> foreground;
    std::vector<osbx_osb_report_t> report;
    std::size_t actors{};
};

auto op_cost(std::span<const osbx_transform_op_t> ops) -> std::size_t {
    std::size_t result = 0;
    for (const auto& op : ops) {
        if (op.kind == osbx_transform_kind_t::pivot) result += 2 + op_cost(op.children);
        else result += op.kind == osbx_transform_kind_t::matrix || op.kind == osbx_transform_kind_t::skew ? 3 : 1;
    }
    return result;
}

auto image_for(const osbx_document_t& document, const osbx_node_t& node) -> const osbx_image_t* {
    if (!node.image) return nullptr;
    const auto found = std::ranges::find(document.images, *node.image, &osbx_image_t::id);
    return found == document.images.end() ? nullptr : &*found;
}
auto has_inferred_image_grid(std::string_view file) -> bool {
    const auto digit = [](char value) { return value >= '0' && value <= '9'; };
    for (std::size_t i = 1; i + 1 < file.size(); ++i)
        if (file[i] == 'x' && digit(file[i - 1]) && digit(file[i + 1])) return true;
    return false;
}
auto native_profile_failure(const osbx_node_t& node, const osbx_image_t* image,
    const stepmania_storyboard_options_t& options) -> std::string {
    if (options.implementation_profile != stepmania_implementation_profile_t::notitg_v4) return {};
    if (node.layer != osbx_layer_t::foreground)
        return "NotITG background lifecycle is unverified; foreground-only dialect never moves backgrounds above the notefield";
    if (image && has_inferred_image_grid(image->file))
        return "NotITG filename-inferred texture grids require an engine-certified full-image UV binding";
    return {};
}
auto is_selected(const osbx_node_t& node, storyboard_conditional_layer_t conditional) -> bool {
    return is_storyboard_layer_visible(osbx_layer_names[static_cast<std::size_t>(node.layer)], conditional);
}

auto plan_sprite(const osbx_evaluation_index_t& index, std::size_t position,
    actor_plan_t& plan, const stepmania_storyboard_options_t& options, std::span<const node_analysis_t> analysis,
    osbx_export_budget_t& budget) -> result_t<sprite_plan_t, std::string> {
    sprite_plan_t sprite{.node = position};
    std::size_t actors = 1;
    for (auto current = std::optional{position}; current; current = index.nodes[*current].parent) {
        if (!budget.consume(1) || sprite.chain.size() >= 128)
            return std::unexpected("Native actor ancestry/work budget exhausted");
        const auto& source = *index.nodes[*current].source;
        if (!analysis[*current].failure.empty()) return std::unexpected(source.id + ": " + analysis[*current].failure);
        sprite.chain.push_back(*current);
        actors += analysis[*current].frames;
        if (options.implementation_profile == stepmania_implementation_profile_t::notitg_v4 && actors > 64)
            return std::unexpected("NotITG XML binding depth exceeds the conservative 64-actor path bound");
        if (actors > options.maximum_actor_frames - std::min(plan.actors, options.maximum_actor_frames))
            return std::unexpected("Native actor frame budget exhausted");
    }
    sprite.frames = actors;
    return sprite;
}

auto build_actor_plan(const osbx_document_t& document, const osbx_evaluation_index_t& index,
    const stepmania_storyboard_options_t& options, osbx_export_budget_t& budget) -> actor_plan_t {
    // Reserve both possible layer roots as part of the aggregate actor bound.
    actor_plan_t plan{.actors = std::min(std::size_t{2}, options.maximum_actor_frames)};
    std::vector<node_analysis_t> analysis;
    analysis.reserve(index.nodes.size());
    for (const auto& node : index.nodes) {
        const auto local = ranges_for_ops(node.source->ops);
        analysis.push_back({.failure = node_failure(*node.source),
            .world = node.parent ? multiply(analysis[*node.parent].world, local) : local,
            .frames = 1 + op_cost(node.source->ops)});
    }
    for (const auto position : index.draw_order) {
        const auto& source = *index.nodes[position].source;
        if (!is_selected(source, options.conditional_layer) || source.kind == osbx_node_kind_t::group) continue;
        osbx_osb_report_t report{.node_id = source.id};
        const auto* image = image_for(document, source);
        if (auto reason = native_profile_failure(source, image, options); !reason.empty()) {
            report.reason = std::move(reason);
            plan.report.push_back(std::move(report));
            continue;
        }
        auto sprite = plan_sprite(index, position, plan, options, analysis, budget);
        const auto* geometry = image ? osbx_export_image_size(document, image->id, options.image_sizes) : nullptr;
        if (sprite && (!geometry || !has_containment(analysis[position].world, document.canvas, *geometry, source.origin)))
            sprite = std::unexpected("Native logical canvas clipping is unsupported: conservative containment proof failed");
        if (!sprite) report.reason = sprite.error();
        else {
            plan.actors += sprite->frames;
            report.classification = osbx_osb_class_t::exact;
            report.reason = "Native absolute-time actor structure; target engine runtime verification is separate";
            auto& layer = source.layer == osbx_layer_t::foreground ? plan.foreground : plan.background;
            layer.push_back(std::move(*sprite));
        }
        plan.report.push_back(std::move(report));
    }
    return plan;
}

void emit_sprite(lua_text_t& text, const osbx_document_t& document, const osbx_evaluation_index_t& index,
    const sprite_plan_t& plan, const stepmania_storyboard_options_t& options) {
    const auto& node = *index.nodes[plan.node].source;
    const auto* image = image_for(document, node);
    const auto origin = storyboard_origin_t::from(node.origin).value();
    text.append("  {id=");
    text.quoted(node.id);
    text.append(",file=");
    text.quoted(image->file);
    const auto* geometry = osbx_export_image_size(document, image->id, options.image_sizes);
    text.append(",width=");
    text.integer(geometry->width);
    text.append(",height=");
    text.integer(geometry->height);
    text.append(",origin={");
    text.number(origin.x);
    text.append(",");
    text.number(origin.y);
    text.append("},blend=");
    emit_track(text, node.tracks.blend, [&](osbx_blend_t blend) {
        if (options.implementation_profile == stepmania_implementation_profile_t::notitg_v4)
            text.quoted(blend == osbx_blend_t::additive ? "add" : "normal");
        else text.quoted(blend == osbx_blend_t::additive ? "BlendMode_Add" : "BlendMode_Normal");
    });
    text.append(",chain={");
    for (const auto position : plan.chain) {
        text.integer(static_cast<std::int64_t>(position + 1));
        text.append(",");
    }
    text.append("}},\n");
}

void emit_declarations(lua_text_t& text, const osbx_document_t& document,
    const osbx_evaluation_index_t& index, std::span<const sprite_plan_t> sprites,
    const stepmania_storyboard_options_t& options) {
    text.append(options.implementation_profile == stepmania_implementation_profile_t::notitg_v4
        ? "-- Generated by wormedit for conservative NotITG v4 XML/Lua.\n"
        : "-- Generated by wormedit for SM5.1 / ITGmania.\n");
    text.append("-- Target runtime verification is separate from export classification.\n");
    text.append(options.implementation_profile == stepmania_implementation_profile_t::notitg_v4
        ? "local notitg = true\n" : "local notitg = false\n");
    text.append("local canvas_width = ");
    text.number(document.canvas.width);
    text.append("\nlocal canvas_height = ");
    text.number(document.canvas.height);
    text.append("\nlocal playback_begin_ms = ");
    text.integer(options.playback_span.begin_time_ms);
    text.append("\nlocal playback_end_ms = ");
    text.integer(options.playback_span.end_time_ms);
    text.append("\nlocal keep_alive_seconds = ");
    // Keep the host FG runner alive across seeks past the exported drawable
    // span and back. The accepted music-clock domain is finite (one day).
    text.number((static_cast<double>(maximum_music_time_ms) - options.activation_time_ms) / 1000 + 1);
    text.append("\nlocal nodes = {\n");
    std::set<std::size_t> nodes;
    for (const auto& sprite : sprites) nodes.insert(sprite.chain.begin(), sprite.chain.end());
    for (const auto position : nodes) {
        text.append("  [");
        text.integer(static_cast<std::int64_t>(position + 1));
        text.append("] = ");
        emit_node(text, *index.nodes[position].source);
        text.append(",\n");
    }
    text.append("}\nlocal sprites = {\n");
    for (const auto& sprite : sprites) emit_sprite(text, document, index, sprite, options);
    text.append("}\n");
}

auto emit_layer(const osbx_document_t& document, const osbx_evaluation_index_t& index,
    std::span<const sprite_plan_t> sprites, const stepmania_storyboard_options_t& options,
    bool foreground, std::size_t available_bytes) -> std::optional<stepmania_storyboard_file_t> {
    if (sprites.empty()) return std::nullopt;
    const auto reference = options.relative_directory + (foreground ? "/foreground" : "/background");
    lua_text_t text{available_bytes};
    emit_declarations(text, document, index, sprites, options);
    text.append(actor_runtime);
    if (!text.is_success()) return std::nullopt;
    std::set<std::string> images;
    for (const auto& sprite : sprites) images.insert(image_for(document, *index.nodes[sprite.node].source)->file);
    return stepmania_storyboard_file_t{.relative_path = reference + "/default.lua",
        .actor_reference = reference, .foreground = foreground, .bytes = text.take(),
        .images = {images.begin(), images.end()}};
}

// Structural serialization only: all values and transforms still belong to the
// shared descriptor/runtime and canonical evaluator/metric above.
struct xml_actor_t {
    const osbx_node_t* sprite{};
    std::vector<xml_actor_t> children;
};
auto xml_wrap(xml_actor_t child, std::size_t count) -> xml_actor_t {
    for (std::size_t i = 0; i < count; ++i) {
        xml_actor_t parent;
        parent.children.push_back(std::move(child));
        child = std::move(parent);
    }
    return child;
}
auto xml_ops(std::span<const osbx_transform_op_t>, xml_actor_t) -> xml_actor_t;
auto xml_op(const osbx_transform_op_t& op, xml_actor_t child) -> xml_actor_t {
    if (op.kind == osbx_transform_kind_t::pivot)
        return xml_wrap(xml_ops(op.children, xml_wrap(std::move(child), 1)), 1);
    const auto affine = op.kind == osbx_transform_kind_t::matrix || op.kind == osbx_transform_kind_t::skew;
    return xml_wrap(std::move(child), affine ? 3 : 1);
}
auto xml_ops(std::span<const osbx_transform_op_t> ops, xml_actor_t child) -> xml_actor_t {
    for (const auto& op : ops) child = xml_op(op, std::move(child));
    return child;
}
auto xml_sprite_tree(const osbx_evaluation_index_t& index, const sprite_plan_t& sprite) -> xml_actor_t {
    xml_actor_t child{.sprite = index.nodes[sprite.node].source};
    for (const auto position : sprite.chain)
        child = xml_wrap(xml_ops(index.nodes[position].source->ops, std::move(child)), 1);
    return child;
}
void emit_xml_actor(lua_text_t& text, const xml_actor_t& actor, const osbx_document_t& document,
    const std::filesystem::path& directory, std::size_t depth) {
    const std::string indent(depth * 2, ' ');
    if (actor.sprite) {
        const auto* image = image_for(document, *actor.sprite);
        const auto relative = text_util::path_from_utf8(image->file).lexically_relative(directory);
        text.append(indent + "<Sprite Texture=\"");
        text.xml_attribute(text_util::path_utf8(relative));
        text.append("\" />\n");
        return;
    }
    text.append(indent + "<ActorFrame>\n" + indent + "  <children>\n");
    for (const auto& child : actor.children) emit_xml_actor(text, child, document, directory, depth + 2);
    text.append(indent + "  </children>\n" + indent + "</ActorFrame>\n");
}
constexpr std::string_view notitg_initializer_prefix = R"NITG(return function(native_root)
local Def = {ActorFrame = function(value) return value end, Sprite = function(value) return value end}
local function build()
)NITG";
constexpr std::string_view notitg_initializer_suffix = R"NITG(
end
local definition = build()
local function bind_tree(description, actor)
  if description.InitCommand then
    description.InitCommand(actor)
  end
  local count = table.getn(description)
  if count > 0 then
    assert(actor:GetNumChildren() == count, "NotITG actor topology mismatch")
    for index = 1, count do
      bind_tree(description[index], actor:GetChildAt(index - 1))
    end
  end
end
local function start_tree(description, actor)
  if description.OnCommand then
    description.OnCommand(actor)
  end
  for index = 1, table.getn(description) do
    start_tree(description[index], actor:GetChildAt(index - 1))
  end
end
bind_tree(definition, native_root)
start_tree(definition, native_root)
end
)NITG";

auto emit_notitg_layer(const osbx_document_t& document, const osbx_evaluation_index_t& index,
    std::span<const sprite_plan_t> sprites, const stepmania_storyboard_options_t& options,
    std::size_t available_bytes) -> std::vector<stepmania_storyboard_file_t> {
    if (sprites.empty()) return {};
    const auto directory = options.relative_directory + "/foreground";
    const auto script = directory + "/main.lua";
    lua_text_t lua{available_bytes};
    lua.append(notitg_initializer_prefix);
    emit_declarations(lua, document, index, sprites, options);
    lua.append(actor_runtime);
    lua.append(notitg_initializer_suffix);
    if (!lua.is_success()) return {};
    auto bytes = lua.take();
    lua_text_t command{available_bytes};
    command.append("%function(self) local initialize = assert(loadfile(GAMESTATE:GetCurrentSong():GetSongDir() .. ");
    command.quoted(script);
    command.append(")); initialize()(self) end");
    lua_text_t xml{available_bytes - bytes.size()};
    xml.append("<ActorFrame OnCommand=\"");
    xml.xml_attribute(command.take());
    xml.append("\">\n  <children>\n");
    std::set<std::string> images;
    for (const auto& sprite : sprites) {
        emit_xml_actor(xml, xml_sprite_tree(index, sprite), document, text_util::path_from_utf8(directory), 2);
        images.insert(image_for(document, *index.nodes[sprite.node].source)->file);
    }
    xml.append("  </children>\n</ActorFrame>\n");
    if (!xml.is_success() || !command.is_success()) return {};
    std::vector<stepmania_storyboard_file_t> files;
    files.push_back({.relative_path = text_util::path_from_utf8(directory + "/main.xml"), .actor_reference = directory + "/main.xml",
        .foreground = true, .bytes = xml.take(), .images = {images.begin(), images.end()}});
    files.push_back({.relative_path = text_util::path_from_utf8(script), .foreground = true, .bytes = std::move(bytes)});
    return files;
}

auto link_storage(const osbx_lua_link_t& link) -> std::size_t {
    const auto& source = link.source;
    std::size_t bytes = link.id.size() + source.frozen_source.size() + source.source_hash.size() + source.entry_point.size();
    if (source.source_reference) bytes += source.source_reference->native().size() * sizeof(std::filesystem::path::value_type);
    bytes += source.parameters.size() * sizeof(lua_parameter_t);
    for (const auto& parameter : source.parameters) bytes += parameter.name.size();
    return bytes;
}
template<class Value> auto program_storage(const osbx_key_program_t<Value>& block) -> std::size_t {
    std::size_t bytes = sizeof(block) + block.id().size();
    for (const auto& key : block.keys()) {
        bytes += sizeof(key) + key.id.size();
        if constexpr (std::is_same_v<Value, std::vector<double>>) bytes += key.value.size() * sizeof(double);
    }
    if (const auto* trigger = std::get_if<osbx_trigger_t<Value>>(&block.data)) bytes += trigger->trigger.name.value.size();
    return bytes;
}
template <class Value> auto track_storage(const osbx_track_t<Value>& track) -> std::size_t {
    std::size_t bytes = track.keys.size() * sizeof(osbx_key_t<Value>);
    if constexpr (std::is_same_v<Value, std::vector<double>>) bytes += track.default_value.size() * sizeof(double);
    for (const auto& key : track.keys) {
        bytes += key.id.size();
        if constexpr (std::is_same_v<Value, std::vector<double>>) bytes += key.value.size() * sizeof(double);
    }
    for (const auto& block : track.program) bytes += program_storage(block);
    if (track.lua) bytes += link_storage(track.lua->link);
    return bytes;
}
auto ops_storage(std::span<const osbx_transform_op_t> ops) -> std::size_t {
    std::size_t bytes = ops.size() * sizeof(osbx_transform_op_t);
    for (const auto& op : ops) bytes += op.id.size() + track_storage(op.track) + ops_storage(op.children);
    return bytes;
}
auto scene_storage(const osbx_document_t& document) -> std::size_t {
    std::size_t bytes = document.api_version.size() + document.nodes.size() * sizeof(osbx_node_t) +
        document.images.size() * sizeof(osbx_image_t);
    for (const auto& image : document.images) bytes += image.id.size() + image.file.size();
    for (const auto& node : document.nodes) {
        bytes += node.id.size() + node.origin.size() + node.provenance.file.size() + node.provenance.generator_key.size();
        if (node.parent) bytes += node.parent->size();
        if (node.image) bytes += node.image->size();
        if (node.children_link) bytes += link_storage(*node.children_link);
        bytes += ops_storage(node.ops) + track_storage(node.tracks.opacity) + track_storage(node.tracks.color) +
            track_storage(node.tracks.visibility) + track_storage(node.tracks.blend);
    }
    return bytes;
}

struct bake_context_t {
    const osbx_document_t& source;
    osbx_document_t& candidate;
    const stepmania_storyboard_options_t& options;
    osbx_export_budget_t& budget;
    std::map<std::string, osbx_osb_report_t> report;

    void prepare_track(const osbx_track_t<>& original, osbx_track_t<>& output,
        const std::string& owner, const std::string& identity) {
        if (!needs_bake(original) || !clock_failure(original).empty()) return;
        auto baked = osbx_export_track_bake_t::from({.source = source, .candidate = candidate,
            .source_track = original, .candidate_track = output, .image_sizes = options.image_sizes,
            .frame_options = {.conditional_layer = options.conditional_layer},
            .playback_span = options.playback_span, .node_id = identity}, budget);
        auto& status = report[owner];
        status.node_id = owner;
        if (!baked) {
            status.classification = osbx_osb_class_t::unsupported;
            status.reason = baked.error();
            return;
        }
        output = std::move(baked->track);
        if (status.reason.empty() || status.classification != osbx_osb_class_t::unsupported) {
            status.classification = osbx_osb_class_t::baked;
            status.reason = baked->report.reason;
        }
        status.worst_error = std::max(status.worst_error, baked->report.worst_error);
        status.key_count += baked->report.key_count;
        status.keys_before_simplify += baked->report.keys_before_simplify;
    }
    void prepare_ops(std::span<const osbx_transform_op_t> original,
        std::span<osbx_transform_op_t> output, const std::string& owner) {
        for (std::size_t i = 0; i < original.size(); ++i) {
            prepare_track(original[i].track, output[i].track, owner, owner + "/" + original[i].id);
            prepare_ops(original[i].children, output[i].children, owner);
        }
    }
    void run() {
        for (std::size_t i = 0; i < source.nodes.size(); ++i) {
            const auto& node = source.nodes[i];
            auto& output = candidate.nodes[i];
            prepare_ops(node.ops, output.ops, node.id);
            prepare_track(node.tracks.opacity, output.tracks.opacity, node.id, node.id + "/opacity");
            prepare_track(node.tracks.color, output.tracks.color, node.id, node.id + "/color");
        }
    }
};

void apply_bake_reports(actor_plan_t& plan, const osbx_evaluation_index_t& index,
    const std::map<std::string, osbx_osb_report_t>& baked) {
    for (auto& report : plan.report) {
        const auto position = std::ranges::find_if(index.nodes, [&](const auto& node) { return node.source->id == report.node_id; });
        if (position == index.nodes.end()) continue;
        for (auto current = std::optional{static_cast<std::size_t>(position - index.nodes.begin())}; current;
             current = index.nodes[*current].parent) {
            const auto found = baked.find(index.nodes[*current].source->id);
            if (found == baked.end()) continue;
            if (report.classification != osbx_osb_class_t::unsupported) {
                report.classification = found->second.classification;
                report.reason = found->second.reason;
            }
            report.worst_error = std::max(report.worst_error, found->second.worst_error);
            report.key_count += found->second.key_count;
            report.keys_before_simplify += found->second.keys_before_simplify;
        }
    }
}

auto refused(const osbx_document_t& document, std::string reason) -> stepmania_storyboard_outcome_t {
    stepmania_storyboard_outcome_t result;
    for (const auto& node : document.nodes) {
        auto& report = result.value.report.emplace_back(osbx_osb_report_t{.node_id = node.id});
        report.reason = reason;
    }
    result.diagnostics.push_back({.code = failure_code_t::unsupported, .message = std::move(reason)});
    return result;
}

} // namespace

namespace {
auto ops_have_program(std::span<const osbx_evaluation_op_t> ops) -> bool {
    return std::ranges::any_of(ops, [](const auto& op) { return !op.track.program.empty() || ops_have_program(op.children); });
}
auto node_has_program(const osbx_evaluation_node_t& node) -> bool {
    if (ops_have_program(node.ops) || !node.opacity.program.empty() || !node.color.program.empty() ||
        !node.visibility.program.empty() || !node.blend.program.empty()) return true;
    return std::ranges::any_of(node.clips, [](const auto& clip) {
        return !clip.opacity.program.empty() || !clip.color.program.empty() || !clip.visibility.program.empty() ||
            (clip.blend && !clip.blend->program.empty());
    });
}
template<class Feature> auto selected_actor_feature(const osbx_evaluation_index_t& index,
    storyboard_conditional_layer_t choice, Feature feature) -> bool {
    std::vector<bool> uses(index.nodes.size());
    for (std::size_t i = 0; i < index.nodes.size(); ++i) {
        const auto& node = index.nodes[i];
        uses[i] = feature(node) || (node.parent && uses[*node.parent]);
        if (uses[i] && node.source->kind != osbx_node_kind_t::group && is_selected(*node.source, choice)) return true;
    }
    return false;
}
} // namespace

auto emit_stepmania_storyboard(const osbx_document_t& source, stepmania_storyboard_options_t options)
    -> stepmania_storyboard_outcome_t {
    if (auto reason = option_failure(options); !reason.empty()) return refused(source, std::move(reason));
    auto prepared = osbx_lua_prepared_scope_t::find(source);
    if (has_osbx_lua_links(source) && !prepared)
        return refused(source, "Storyboard Lua links require a matching immutable prepared result");
    const auto& document = prepared ? *prepared : source;
    const auto index = osbx_evaluation_index_t::from(document,
        {.maximum_nodes = std::min(options.maximum_work, std::size_t{250'000}),
         .maximum_keys = std::min(options.maximum_work, std::size_t{500'000})});
    if (!index.value) return refused(document, "Invalid or unprepared native actor source scene");
    if (selected_actor_feature(*index.value, options.conditional_layer, [](const auto& node) { return !node.clips.empty(); }))
        return refused(document, "Used clip actors require compiled native actor integration; no sidecars emitted");
    if (selected_actor_feature(*index.value, options.conditional_layer, node_has_program))
        return refused(document, "Temporal actor programs require native actor integration; no sidecars emitted");
    if (scene_storage(document) > options.maximum_scene_storage_bytes)
        return refused(document, "Native scene storage budget exhausted before detached preparation");
    auto candidate = document;
    auto budget = osbx_export_budget_t::from(*index.value, document.images.size(), options.maximum_work);
    if (!budget.consume(budget.frame_work)) return refused(document, "Native source analysis work budget exhausted");
    bake_context_t baker{.source = document, .candidate = candidate, .options = options, .budget = budget};
    baker.run();
    const auto output_index = osbx_evaluation_index_t::from(candidate);
    if (!output_index.value) return refused(document, "Native baked candidate failed validation");
    auto plan = build_actor_plan(candidate, *output_index.value, options, budget);
    apply_bake_reports(plan, *output_index.value, baker.report);
    stepmania_storyboard_outcome_t result;
    if (options.implementation_profile == stepmania_implementation_profile_t::notitg_v4) {
        result.value.files = emit_notitg_layer(candidate, *output_index.value, plan.foreground,
            options, options.maximum_output_bytes);
        if (!plan.foreground.empty() && result.value.files.empty())
            return refused(document, "NotITG XML/Lua byte budget or XML encoding bound exhausted");
        result.value.report = std::move(plan.report);
        return result;
    }
    std::size_t bytes = 0;
    for (const auto foreground : {false, true}) {
        const auto& sprites = foreground ? plan.foreground : plan.background;
        if (sprites.empty()) continue;
        auto file = emit_layer(candidate, *output_index.value, sprites, options, foreground, options.maximum_output_bytes - bytes);
        if (!file) return refused(document, "Native actor Lua byte budget exhausted");
        bytes += file->bytes.size();
        result.value.files.push_back(std::move(*file));
    }
    result.value.report = std::move(plan.report);
    return result;
}

} // namespace wormedit
