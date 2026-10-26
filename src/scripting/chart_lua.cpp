#include "chart_lua.h"
#include "../core/checked_math.h"
#include "../core/text_util.h"
#include "lua_value_reader.h"
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/numeric_text.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/sv_recipe.h>
namespace wormedit::scripting {
namespace {
namespace values = lua_value_detail;
constexpr std::string_view declarations = R"lua(
local chart_type, chart_error, chart_pairs, chart_next, chart_ipairs = type, error, pairs, next, ipairs
local chart_current
local chart_result = {}
local function current()
  if not chart_current then chart_error('Declare difficulty before chart settings or measures', 0) end
  return chart_current
end
local function descriptor(kind, value)
  if chart_type(value) ~= 'table' then chart_error(kind .. ' requires a table', 0) end
  local copy = {_kind = kind}
  for key, item in chart_pairs(value) do
    if key == '_kind' then chart_error('Descriptor kind is reserved', 0) end
    copy[key] = item
  end
  return copy
end
function difficulty(name)
  if chart_type(name) ~= 'string' or name == '' then chart_error('difficulty needs a name', 0) end
  chart_current = {name = name, key = name, measures = {}, timing = {}}
  chart_result[#chart_result+1] = chart_current
end
function stable_id(value) current().key = value end
function resolution(value) current().resolution = value end
function offset(value) current().offset = value end
function columns(value) current().columns = value end
function mode(value) current().mode = value end
function ratio(num, den) return {_kind='ratio', num=num, den=den} end
function measure_length(value) return {_kind='length', text=value} end
function measure_time(value) return {_kind='time', text=value} end
function meter(num, den) return {_kind='meter', num=num, den=den} end
function rows(value) return {_kind='rows', value=value} end
function at_measure(id, row) return {_kind='position', measure=id, row=row} end
function note(value) return descriptor('note', value) end
function hold(value) return descriptor('hold', value) end
function bgm(value) return descriptor('bgm', value) end
function measure(value)
  local values = current().measures
  values[#values+1] = descriptor('measure', value)
  return values[#values]
end
local function append_measure(value)
  if chart_type(value) ~= 'table' then chart_error('Measure sequence entries must be tables',0) end
  if value._kind == 'measure' then
    for _, existing in chart_ipairs(current().measures) do if existing == value then return end end
    current().measures[#current().measures+1] = value
  else measure(value) end
end
function measures(sequence)
  if chart_type(sequence) == 'function' then
    while true do
      local value = sequence()
      if value == nil then return end
      append_measure(value)
    end
  end
  if chart_type(sequence) ~= 'table' then chart_error('measures requires a dense sequence or iterator',0) end
  local count = 0
  for key in chart_pairs(sequence) do
    if chart_type(key) ~= 'number' or key < 1 or key % 1 ~= 0 then chart_error('measures requires a dense sequence',0) end
    count = count+1
  end
  for i=1,count do
    local value=sequence[i]
    if value == nil then chart_error('measures requires a dense sequence',0) end
    append_measure(value)
  end
end
local function timing(kind, value)
  local values = current().timing
  values[#values+1] = descriptor(kind, value)
  return values[#values]
end
function bpm(value) return timing('bpm',value) end
function stop(value) return timing('stop',value) end
function delay(value) return timing('delay',value) end
function warp(value) return timing('warp',value) end
function scroll(value) return timing('scroll',value) end
function sv_point(value)
  if chart_current then return timing('scroll',value) end
  return descriptor('sv_point',value)
end
function sv_span(value)
  if chart_current then return timing('sv_span',value) end
  return descriptor('sv_span',value)
end
local function finish_chart_declarations(value)
  if #chart_result == 0 then return value end
  if value ~= nil then
    if chart_type(value) ~= 'table' then chart_error('Chart declarations require a table or no return value',0) end
    for key, items in chart_pairs(value) do
      if key ~= 'notes' and key ~= 'layers' and key ~= 'sv_points' and key ~= 'sv_spans' then chart_error('Unknown output mixed with chart declarations',0) end
      if chart_type(items) ~= 'table' or chart_next(items) ~= nil then
        chart_error('Chart declarations cannot be mixed with loose note/layer output',0)
      end
    end
  end
  return {charts=chart_result}
end
)lua";

class chart_decoder_t {
  public:
    explicit chart_decoder_t(std::vector<std::string>& rationalizations)
        : rationalizations_(rationalizations) {}
    auto decode(const values::table_t& value) -> generator_chart_binding_t {
        values::only_fields(value, {"name", "key", "resolution", "offset", "columns", "mode",
                                    "measures", "timing"});
        result_.key = values::text(values::required(value, "key"));
        result_.content.name = values::text(values::required(value, "name"));
        if (!valid_name(result_.key) || !valid_name(result_.content.name))
            values::malformed("Difficulty names and stable IDs need bounded nonempty UTF8 text.");
        if (const auto* item = values::field(value, "resolution"))
            result_.content.resolution = values::integer(*item);
        if (!result_.content.resolution)
            values::malformed("Chart resolution must be positive.");
        if (const auto* item = values::field(value, "offset"))
            result_.content.offset = signed_value(*item, "offset");
        profile(value);
        parse_measures(values::children(values::required(value, "measures")));
        parse_notes(values::children(values::required(value, "measures")));
        parse_timing(values::children(values::required(value, "timing")));
        return std::move(result_);
    }

    void decode_sv(const values::table_t& table, generator_output_t& output) {
        if (const auto* points = values::field(table, "sv_points")) {
            const auto& entries = values::children(*points); values::dense(entries);
            if (entries.size() > maximum_generator_notes) values::malformed("Generator SV point limit exceeded.");
            for (const auto& entry : entries) {
                const auto& value = values::children(entry);
                values::only_fields(value, {"_kind", "key", "at", "value"});
                output.scrolls.push_back({.key = values::text(values::required(value, "key")),
                    .value = {.position = position_value(values::required(value, "at")),
                        .multiplier = signed_value(values::required(value, "value"), "SV point")}});
            }
        }
        if (const auto* spans = values::field(table, "sv_spans")) {
            const auto& entries = values::children(*spans); values::dense(entries);
            if (entries.size() > maximum_sv_recipes_per_chart) values::malformed("Generator SV span limit exceeded.");
            for (const auto& entry : entries) {
                const auto& value = values::children(entry);
                output.sv_recipes.push_back({.key = values::text(values::required(value, "key")),
                    .value = span_value(value)});
            }
        }
    }

  private:
    static auto valid_name(const std::string& value) -> bool {
        return !value.empty() && text_util::is_valid_bounded_text(value, 128);
    }
    auto identity(std::string key) -> std::uint64_t {
        if (!identity_keys_.insert(key).second)
            values::malformed("Duplicate generated declaration key: " + key);
        const auto id = static_cast<std::uint64_t>(result_.identities.size() + 1);
        result_.identities.push_back({.key = std::move(key), .value = id});
        return id;
    }
    auto positive_value(const values::node_t& value, std::string_view context,
                        bool negative = false) -> rational_t {
        if (const auto* object = std::get_if<values::table_t>(&value.value))
            return exact_positive(*object, context);
        const auto& item = values::scalar(value);
        if (const auto* number = std::get_if<std::int64_t>(&item)) {
            if (*number <= 0)
                values::malformed(std::string(context) + " must be positive.");
            return {static_cast<std::uint64_t>(*number), 1};
        }
        const auto* number = std::get_if<double>(&item);
        if (!number)
            values::malformed(std::string(context) + " needs a number or an exact helper.");
        const auto converted =
            rationalized_value_t::from(*number, {.maximum_absolute_error = 1e-9L});
        if (!converted)
            values::malformed(
                std::string(context) +
                " cannot be rationalized within bounded limits; use an exact helper.");
        std::array<char, 96> error_text{};
        const auto formatted =
            std::to_chars(error_text.data(), error_text.data() + error_text.size(),
                          converted->absolute_error_bound, std::chars_format::scientific);
        const auto error = formatted.ec == std::errc{}
                               ? std::string(error_text.data(), formatted.ptr)
                               : std::string{"bounded"};
        rationalizations_.push_back(std::string(context) + " = " + (negative ? "-" : "") +
                                    std::to_string(converted->value.numerator) + "/" +
                                    std::to_string(converted->value.denominator) +
                                    (converted->status == rationalization_phase_t::exact
                                         ? " (exact)"
                                         : " (bounded approximation)") +
                                    "; error <= " + error);
        return converted->value;
    }
    auto exact_positive(const values::table_t& object, std::string_view context) -> rational_t {
        const auto kind = values::text(values::required(object, "_kind"));
        if (kind == "ratio") {
            values::only_fields(object, {"_kind", "num", "den"});
            const auto num = values::integer(values::required(object, "num")),
                       den = values::integer(values::required(object, "den"));
            if (!num || !den)
                values::malformed(std::string(context) + " ratio must be positive.");
            return reduce_fraction(rational_t{num, den});
        }
        if (kind != "length" && kind != "time")
            values::malformed(std::string(context) + " has the wrong exact helper.");
        values::only_fields(object, {"_kind", "text"});
        const auto parsed = rational_t::from(values::text(values::required(object, "text")));
        if (!parsed)
            values::malformed(std::string(context) + " has an invalid exact decimal/fraction.");
        return *parsed;
    }
    auto signed_object(const values::table_t& object, std::string_view context)
        -> signed_rational_t {
        const auto kind = values::text(values::required(object, "_kind"));
        if (kind == "ratio") {
            values::only_fields(object, {"_kind", "num", "den"});
            const auto num = values::signed_integer(values::required(object, "num"));
            const auto den = values::integer(values::required(object, "den"));
            if (!den)
                values::malformed("Invalid signed ratio.");
            const auto parsed =
                parse_exact_rational(std::to_string(num) + "/" + std::to_string(den));
            if (!parsed)
                values::malformed("Signed ratio overflows.");
            return *parsed;
        }
        if (kind == "time" || kind == "length") {
            values::only_fields(object, {"_kind", "text"});
            const auto parsed =
                parse_exact_rational(values::text(values::required(object, "text")));
            if (!parsed)
                values::malformed(std::string(context) + " has invalid exact text.");
            return *parsed;
        }
        values::malformed(std::string(context) + " has the wrong exact helper.");
    }
    auto signed_value(const values::node_t& value, std::string_view context) -> signed_rational_t {
        if (const auto* object = std::get_if<values::table_t>(&value.value))
            return signed_object(*object, context);
        if (const auto* item = std::get_if<lua_sandbox_value_t>(&value.value)) {
            if (const auto* number = std::get_if<std::int64_t>(item))
                return {*number, 1};
            if (const auto* number = std::get_if<double>(item); number && *number <= 0) {
                if (*number == 0)
                    return {};
                values::node_t magnitude = lua_sandbox_node_t::from("", -*number);
                const auto positive = positive_value(magnitude, context, true);
                if (positive.numerator > INT64_MAX)
                    values::malformed("Signed rational overflows.");
                return {-static_cast<std::int64_t>(positive.numerator), positive.denominator};
            }
        }
        const auto positive = positive_value(value, context);
        if (positive.numerator > INT64_MAX)
            values::malformed("Signed rational overflows.");
        return {static_cast<std::int64_t>(positive.numerator), positive.denominator};
    }
    void profile(const values::table_t& value) {
        const auto keys =
            values::field(value, "columns") ? values::integer(*values::field(value, "columns")) : 4;
        if (keys > maximum_osu_mania_keys || keys == 0)
            values::malformed("columns must be between 1 and 18.");
        auto profile = make_osu_mania_profile(static_cast<std::uint32_t>(keys));
        if (const auto* item = values::field(value, "mode")) {
            const auto requested = values::text(*item);
            const auto step = make_stepmania_profile_for(requested);
            if (!step || step->layout.lanes.size() != keys)
                values::malformed("mode and columns disagree or mode is unsupported.");
            profile = step;
        }
        for (std::size_t i = 0; i < profile->layout.lanes.size(); ++i) {
            const auto old = profile->layout.lanes[i].id;
            const auto fresh = lane_id_t{identity("lane:" + std::to_string(i + 1))};
            profile->layout.lanes[i].id = fresh;
            for (auto& binding : profile->input.bindings)
                if (binding.lane == old)
                    binding.lane = fresh;
        }
        result_.content.input = std::move(profile->input);
        layout_ = std::move(profile->layout);
    }
    auto measure_definition(const values::table_t& value, std::string_view context)
        -> measure_definition_t {
        measure_definition_t definition{.length = {1, 1}, .meter = meter_t{4, 4}};
        const auto* length = values::field(value, "length");
        const auto* meter = values::field(value, "meter");
        if (length) {
            definition.length = positive_value(*length, context);
            definition.meter.reset();
        }
        if (meter) {
            const auto& fields = values::children(*meter);
            values::only_fields(fields, {"_kind", "num", "den"});
            if (values::text(values::required(fields, "_kind")) != "meter")
                values::malformed("meter needs meter(n,d).");
            const auto num = values::integer(values::required(fields, "num")),
                       den = values::integer(values::required(fields, "den"));
            if (!num || !den || num > UINT32_MAX || den > UINT32_MAX)
                values::malformed("Invalid meter.");
            definition.meter =
                meter_t{static_cast<std::uint32_t>(num), static_cast<std::uint32_t>(den)};
            if (!length)
                definition.length = reduce_fraction(rational_t{num, den});
        }
        const auto capacity = measure_capacity_t::from(result_.content.resolution, definition);
        if (!capacity)
            values::malformed("Measure length/meter disagree, overflow, or need fractional rows.");
        return definition;
    }
    void parse_measures(const values::table_t& values) {
        values::dense(values);
        if (values.empty() || values.size() > maximum_generator_measures)
            values::malformed("Difficulty needs a bounded nonempty measure list.");
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto& value = values::children(values[i]);
            if (values::text(values::required(value, "_kind")) != "measure")
                values::malformed("Invalid measure descriptor.");
            const auto key = values::field(value, "id") ? values::text(*values::field(value, "id"))
                                                        : std::to_string(i + 1);
            if (!valid_name(key) || positions_.contains(key))
                values::malformed("Measure IDs must be unique bounded strings.");
            const auto id = measure_id_t{identity("measure:" + key)};
            positions_.emplace(key, id);
            order_.push_back(key);
            result_.content.measures.push_back(
                {.id = id,
                 .definition = measure_definition(value, result_.key + "/" + key + "/length")});
        }
        result_.content.layouts.push_back({.id = object_id_t{identity("layout")},
                                           .position = {result_.content.measures.front().id, 0},
                                           .layout = std::move(layout_)});
    }
    auto position_value(const values::node_t& value) -> local_position_t {
        const auto& data = values::children(value);
        values::only_fields(data, {"_kind", "measure", "row"});
        if (values::text(values::required(data, "_kind")) != "position")
            values::malformed("Use at_measure(id,row) for chart-level positions.");
        const auto& measure = values::required(data, "measure");
        if (result_.content.measures.empty())
            return {.measure = measure_id_t{values::integer(measure)},
                    .row = values::integer(values::required(data, "row"))};
        std::string key;
        if (const auto* text_value = std::get_if<std::string>(&values::scalar(measure)))
            key = *text_value;
        else {
            const auto ordinal = values::integer(measure);
            if (!ordinal || ordinal > order_.size())
                values::malformed("Measure index is out of range.");
            key = order_[static_cast<std::size_t>(ordinal - 1)];
        }
        const auto found = positions_.find(key);
        if (found == positions_.end())
            values::malformed("Position names an unknown measure anchor.");
        local_position_t result{.measure = found->second,
                                .row = values::integer(values::required(data, "row"))};
        const auto target =
            std::ranges::find(result_.content.measures, result.measure, &measure_chunk_t::id);
        const auto capacity =
            measure_capacity_t::from(result_.content.resolution, target->definition);
        if (result.row >= capacity->row_capacity)
            values::malformed("row = capacity is a boundary alias; use the next measure's row 0.");
        return result;
    }
    auto local_position(std::size_t measure, const values::node_t& row) -> local_position_t {
        const auto& target = result_.content.measures[measure];
        const auto capacity =
            measure_capacity_t::from(result_.content.resolution, target.definition);
        const auto value = values::integer(row);
        if (value >= capacity->row_capacity)
            values::malformed("Note row must be inside its measure; row = capacity is rejected.");
        return {.measure = target.id, .row = value};
    }
    auto event_key(const values::table_t& value, std::string prefix, std::size_t index)
        -> std::string {
        if (const auto* key = values::field(value, "key")) {
            const auto name = values::text(*key);
            if (!valid_name(name))
                values::malformed("Event keys need bounded nonempty UTF8 text.");
            return prefix + name;
        }
        return prefix + std::to_string(index + 1);
    }
    void parse_note(const values::table_t& value, std::size_t measure, std::size_t ordinal) {
        values::only_fields(value, {"_kind", "key", "row", "lane", "end_at", "sound"});
        const auto kind = values::text(values::required(value, "_kind"));
        if (kind != "note" && kind != "hold" && kind != "bgm")
            values::malformed("Only note/hold/bgm belong inside measure tables.");
        const auto where = local_position(measure, values::required(value, "row"));
        const auto id = object_id_t{identity(event_key(
            value, "event:" + std::to_string(order_[measure].size()) + ":" + order_[measure] + ":",
            ordinal))};
        const auto sound =
            values::field(value, "sound")
                ? std::optional{resource_id_t{values::integer(*values::field(value, "sound"))}}
                : std::nullopt;
        auto& target = result_.content.measures[measure];
        if (kind == "bgm") {
            if (values::field(value, "lane") || values::field(value, "end_at"))
                values::malformed("BGM descriptors do not take a playable lane or hold end.");
            if (result_.content.bgm_lanes.empty())
                result_.content.bgm_lanes.push_back(
                    {.id = object_id_t{identity("bgm-lane")}, .name = "Generated BGM"});
            target.bgm_events.push_back({.id = id,
                                         .position = where,
                                         .source_lane = result_.content.bgm_lanes.front().id,
                                         .sound = sound,
                                         .structural_slot = ordinal});
            return;
        }
        const auto column = values::integer(values::required(value, "lane"));
        const auto& lanes = result_.content.layouts.front().layout.lanes;
        if (!column || column > lanes.size())
            values::malformed("Note lane is outside columns.");
        note_t note{.id = id,
                    .position = where,
                    .lane = lanes[static_cast<std::size_t>(column - 1)].id,
                    .kind = kind == "hold" ? note_kind_t::hold : note_kind_t::tap,
                    .sound = sound};
        if (kind == "hold")
            note.end = position_value(values::required(value, "end_at"));
        else if (values::field(value, "end_at"))
            values::malformed("Only holds take end_at.");
        target.notes.push_back(std::move(note));
    }
    void parse_notes(const values::table_t& values) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            const auto& measure = values::children(values[i]);
            values::table_t entries;
            for (const auto& entry : measure) {
                if (const auto* key = std::get_if<std::string>(&entry.key)) {
                    if (*key != "_kind" && *key != "id" && *key != "length" && *key != "meter")
                        values::malformed(
                            "Timing and unknown parameters do not belong inside a measure.");
                } else
                    entries.push_back(entry);
            }
            values::dense(entries);
            for (std::size_t j = 0; j < entries.size(); ++j)
                parse_note(values::children(entries[j]), i, j);
        }
    }
    auto pause_duration(const values::node_t& value) -> std::pair<signed_rational_t, pause_unit_t> {
        if (const auto* object = std::get_if<values::table_t>(&value.value);
            object && values::text(values::required(*object, "_kind")) == "rows") {
            values::only_fields(*object, {"_kind", "value"});
            auto duration = rows_to_fraction(values::integer(values::required(*object, "value")),
                                             result_.content.resolution);
            const auto divisor = std::gcd(duration.denominator, std::uint64_t{4});
            duration.denominator /= divisor;
            if (duration.numerator > static_cast<std::uint64_t>(INT64_MAX) / (4 / divisor))
                values::malformed("Pause duration overflows.");
            return {{static_cast<std::int64_t>(duration.numerator * (4 / divisor)),
                     duration.denominator},
                    pause_unit_t::beats};
        }
        return {signed_value(value, "pause duration"), pause_unit_t::seconds};
    }
    void parse_timing(const values::table_t& values) {
        values::dense(values);
        for (std::size_t i = 0; i < values.size(); ++i)
            parse_timing_event(values::children(values[i]), i);
        const auto origin = local_position_t{result_.content.measures.front().id, 0};
        if (std::ranges::none_of(result_.content.bpms,
                                 [&](const auto& bpm) { return bpm.position == origin; }))
            result_.content.bpms.insert(result_.content.bpms.begin(),
                                        {.id = object_id_t{identity("default:bpm:origin")},
                                         .position = origin,
                                         .beats_per_minute = {120, 1}});
    }
    auto span_value(const values::table_t& value) -> sv_recipe_t {
        values::only_fields(value, {"_kind", "key", "name", "from", "to", "curve", "value_from", "value_to", "shape", "points", "step_rows", "output"});
        sv_recipe_t recipe{.name = values::field(value, "key") ? values::text(*values::field(value, "key")) : "Generated SV",
                           .range = {position_value(values::required(value, "from")),
                                     position_value(values::required(value, "to"))}};
        if (const auto* item = values::field(value, "name")) recipe.name = values::text(*item);
        const auto curve = values::field(value, "curve") ? values::text(*values::field(value, "curve")) : "linear";
        if (curve == "constant") recipe.curve = sv_recipe_curve_t::constant;
        else if (curve == "linear") recipe.curve = sv_recipe_curve_t::linear;
        else if (curve == "exponential") recipe.curve = sv_recipe_curve_t::exponential;
        else values::malformed("SV span curve must be constant, linear or exponential; emit explicit points for arbitrary Lua curves.");
        const auto output = values::field(value, "output") ? values::text(*values::field(value, "output")) : "sv";
        if (output == "speeds") recipe.output = sv_recipe_output_t::speeds;
        else if (output != "sv") values::malformed("SV span output must be sv or speeds.");
        recipe.parameters.value_from = signed_value(values::required(value, "value_from"), "SV span value_from");
        if (const auto* item = values::field(value, "value_to")) recipe.parameters.value_to = signed_value(*item, "SV span value_to");
        else recipe.parameters.value_to = recipe.parameters.value_from;
        if (const auto* item = values::field(value, "shape")) recipe.parameters.shape = signed_value(*item, "SV span shape");
        span_sampling(value, recipe);
        return recipe;
    }
    void span_sampling(const values::table_t& value, sv_recipe_t& recipe) {
        if (values::field(value, "points") && values::field(value, "step_rows"))
            values::malformed("SV span needs points or step_rows, never both.");
        if (const auto* item = values::field(value, "points")) {
            const auto count = values::integer(*item);
            if (count < 2 || count > maximum_sv_recipe_sample_points) values::malformed("SV span points must be 2 through 4096.");
            recipe.sampling.fixed_point_count = static_cast<std::uint32_t>(count);
        }
        if (const auto* item = values::field(value, "step_rows")) {
            recipe.sampling.fixed_point_count.reset();
            recipe.sampling.fixed_step_rows = values::integer(*item);
            if (!*recipe.sampling.fixed_step_rows) values::malformed("SV span step_rows must be positive.");
        }
    }
    void parse_timing_event(const values::table_t& value, std::size_t ordinal) {
        const auto kind = values::text(values::required(value, "_kind"));
        const auto id = object_id_t{identity(event_key(value, "timing:", ordinal))};
        if (kind == "sv_span") {
            if (!result_.content.sv_recipes) result_.content.sv_recipes.emplace();
            if (result_.content.sv_recipes->size() >= maximum_sv_recipes_per_chart) values::malformed("Too many SV spans.");
            auto recipe = span_value(value);
            recipe.id = recipe_id_t{id.value};
            result_.content.sv_recipes->push_back(std::move(recipe));
            return;
        }
        if (kind == "warp") {
            values::only_fields(value, {"_kind", "key", "from", "to"});
            result_.content.warps.push_back(
                {.id = id,
                 .span = {position_value(values::required(value, "from")),
                          position_value(values::required(value, "to"))}});
            return;
        }
        values::only_fields(value, {"_kind", "key", "at", "value", "length"});
        const auto where = position_value(values::required(value, "at"));
        if (kind == "bpm")
            result_.content.bpms.push_back(
                {.id = id,
                 .position = where,
                 .beats_per_minute = signed_value(values::required(value, "value"), "BPM")});
        else if (kind == "scroll")
            result_.content.scrolls.push_back(
                {.id = id,
                 .position = where,
                 .multiplier = signed_value(values::required(value, "value"), "scroll")});
        else if (kind == "stop" || kind == "delay") {
            const auto [duration, unit] = pause_duration(values::required(value, "length"));
            result_.content.pauses.push_back(
                {.id = id,
                 .position = where,
                 .kind = kind == "stop" ? pause_kind_t::stop : pause_kind_t::delay,
                 .duration = duration,
                 .source_unit = unit});
        } else
            values::malformed("Unknown chart timing declaration.");
    }
    generator_chart_binding_t result_;
    lane_layout_t layout_;
    std::map<std::string, measure_id_t> positions_;
    std::vector<std::string> order_;
    std::set<std::string> identity_keys_;
    std::vector<std::string>& rationalizations_;
};
} // namespace
auto chart_lua_prelude() -> std::string_view {
    return declarations;
}
void decode_lua_sv(const lua_sandbox_context_t& values, generator_output_t& output) {
    chart_decoder_t decoder(output.rationalizations);
    decoder.decode_sv(values, output);
}
auto decode_lua_charts(const lua_sandbox_context_t& values) -> generator_output_t {
    values::dense(values);
    if (values.size() > maximum_generator_charts)
        values::malformed("Too many generated difficulties.");
    generator_output_t output;
    std::set<std::string> keys;
    for (const auto& value : values) {
        chart_decoder_t decoder(output.rationalizations);
        auto chart = decoder.decode(values::children(value));
        if (!keys.insert(chart.key).second)
            values::malformed("Difficulty stable IDs must be unique.");
        output.charts.push_back(std::move(chart));
    }
    return output;
}
} // namespace wormedit::scripting
