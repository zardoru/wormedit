#include <wormedit/core/osbx_document.h>
#include <wormedit/core/storyboard.h>
#include <wormedit/core/osbx_lua_link.h>

#include "text_util.h"
#include "wide_math.h"
#include "checked_math.h"
#include <numeric>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {

auto osbx_lifetime_t::contains(storyboard_time_t time) const -> bool {
    return time.is_valid() && time.milliseconds >= begin_ms && time.milliseconds <= end_ms &&
        (time.milliseconds != end_ms || time.remainder == 0);
}
auto osbx_document_t::is_valid_project_path(std::string_view file) -> bool {
    if (file.empty() || file.front() == '/' || file.find('\\') != std::string_view::npos ||
        file.find(':') != std::string_view::npos) return false;
    const auto path = text_util::path_from_utf8(file);
    return !path.is_absolute() && std::ranges::none_of(path, [](const auto& part) {
        return part == "..";
    });
}


namespace {
auto add_clock_whole(std::int64_t base, std::uint64_t magnitude, bool negative)
    -> result_t<std::int64_t, failure_code_t> {
    constexpr auto low = std::numeric_limits<std::int64_t>::min();
    const auto position = static_cast<std::uint64_t>(base) - static_cast<std::uint64_t>(low);
    if (negative ? magnitude > position : magnitude > UINT64_MAX - position)
        return std::unexpected(failure_code_t::capacity_exceeded);
    const auto shifted = negative ? position - magnitude : position + magnitude;
    if (shifted <= INT64_MAX) return low + static_cast<std::int64_t>(shifted);
    return static_cast<std::int64_t>(shifted - (std::uint64_t{1} << 63));
}
auto add_clock_fraction(storyboard_time_t base, std::uint64_t remainder, std::uint64_t denominator)
    -> result_t<storyboard_time_t, failure_code_t> {
    const auto divisor = std::gcd(remainder, denominator);
    remainder /= divisor; denominator /= divisor;
    if (denominator > UINT32_MAX) return std::unexpected(failure_code_t::capacity_exceeded);
    const auto common = std::gcd(std::uint64_t{base.denominator}, denominator);
    const auto total_denominator = std::uint64_t{base.denominator} * (denominator / common);
    const auto total = wide_math::add(wide_math::multiply(base.remainder, denominator / common),
        wide_math::multiply(remainder, base.denominator / common));
    const auto divided = wide_math::divide(total, {0, total_denominator});
    auto whole = add_clock_whole(base.milliseconds, divided.quotient.low, false);
    if (!whole) return std::unexpected(whole.error());
    const auto reduce = std::gcd(divided.remainder.low, total_denominator);
    if (total_denominator / reduce > UINT32_MAX) return std::unexpected(failure_code_t::capacity_exceeded);
    return storyboard_time_t{.milliseconds = *whole,
        .remainder = static_cast<std::uint32_t>(divided.remainder.low / reduce),
        .denominator = static_cast<std::uint32_t>(total_denominator / reduce)};
}
auto binary_scale(double scale) -> result_t<std::pair<std::uint64_t, std::uint64_t>, failure_code_t> {
    if (!std::isfinite(scale) || scale <= 0) return std::unexpected(failure_code_t::invalid_request);
    int exponent{};
    auto mantissa = static_cast<std::uint64_t>(std::ldexp(std::frexp(scale, &exponent), 53));
    exponent -= 53;
    while ((mantissa & 1U) == 0) { mantissa >>= 1; ++exponent; }
    if (exponent < -63 || exponent > 63 || (exponent > 0 && mantissa > (UINT64_MAX >> exponent)))
        return std::unexpected(failure_code_t::capacity_exceeded);
    return std::pair{exponent > 0 ? mantissa << exponent : mantissa,
        exponent < 0 ? std::uint64_t{1} << -exponent : std::uint64_t{1}};
}
} // namespace

auto osbx_clip_clock_t::map(std::int64_t time) const -> result_t<storyboard_time_t, failure_code_t> {
    if (!origin.is_valid() || !numerator || !denominator) return std::unexpected(failure_code_t::invalid_request);
    const bool negative = time < 0;
    const auto magnitude = negative ? std::uint64_t{0} - static_cast<std::uint64_t>(time) : static_cast<std::uint64_t>(time);
    const auto value = wide_math::divide(wide_math::multiply(magnitude, numerator), {0, denominator});
    if (!wide_math::can_fit_u64(value.quotient)) return std::unexpected(failure_code_t::capacity_exceeded);
    auto remainder = value.remainder.low;
    const bool borrow = negative && remainder != 0;
    if (borrow) remainder = denominator - remainder;
    const auto fraction = add_clock_fraction({.remainder = origin.remainder, .denominator = origin.denominator},
        remainder, denominator);
    if (!fraction) return std::unexpected(fraction.error());
    const auto correction = negative ? std::uint64_t{borrow} - static_cast<std::uint64_t>(fraction->milliseconds) :
        static_cast<std::uint64_t>(fraction->milliseconds);
    if (value.quotient.low > UINT64_MAX - correction) return std::unexpected(failure_code_t::capacity_exceeded);
    const auto whole = add_clock_whole(origin.milliseconds, value.quotient.low + correction, negative);
    if (!whole) return std::unexpected(whole.error());
    return storyboard_time_t{.milliseconds = *whole, .remainder = fraction->remainder, .denominator = fraction->denominator};
}
namespace {
auto clock_distance(storyboard_time_t a, storyboard_time_t b) -> std::pair<wide_math::u128_t, std::uint64_t> {
    const auto common = std::gcd(a.denominator, b.denominator);
    const auto denominator = std::uint64_t{a.denominator} * (b.denominator / common);
    const auto left = std::uint64_t{a.remainder} * (b.denominator / common);
    const auto right = std::uint64_t{b.remainder} * (a.denominator / common);
    auto whole = static_cast<std::uint64_t>(a.milliseconds) - static_cast<std::uint64_t>(b.milliseconds);
    auto fraction = left - right;
    if (left < right) { --whole; fraction = denominator - (right - left); }
    return {wide_math::add(wide_math::multiply(whole, denominator), {0, fraction}), denominator};
}
auto signed_clock_quotient(wide_math::u128_t numerator, wide_math::u128_t denominator, bool negative)
    -> result_t<storyboard_time_t, failure_code_t> {
    const auto divided = wide_math::divide(numerator, denominator);
    const auto common = checked_math::gcd(divided.remainder, denominator);
    const auto n = wide_math::divide(divided.remainder, common).quotient;
    const auto d = wide_math::divide(denominator, common).quotient;
    if (!wide_math::can_fit_u64(divided.quotient) || !wide_math::can_fit_u64(d) || d.low > UINT32_MAX)
        return std::unexpected(failure_code_t::capacity_exceeded);
    const bool borrow = negative && n != wide_math::u128_t{};
    if (borrow && divided.quotient.low == UINT64_MAX) return std::unexpected(failure_code_t::capacity_exceeded);
    const auto whole = add_clock_whole(0, divided.quotient.low + std::uint64_t{borrow}, negative);
    if (!whole) return std::unexpected(whole.error());
    return storyboard_time_t{.milliseconds = *whole,
        .remainder = static_cast<std::uint32_t>(borrow ? d.low - n.low : n.low), .denominator = static_cast<std::uint32_t>(d.low)};
}
} // namespace

auto osbx_clip_clock_t::unmap(storyboard_time_t time) const -> result_t<storyboard_time_t, failure_code_t> {
    if (!time.is_valid() || !origin.is_valid() || !numerator || !denominator)
        return std::unexpected(failure_code_t::invalid_request);
    const bool negative = is_before(time, origin);
    auto [value, divisor] = negative ? clock_distance(origin, time) : clock_distance(time, origin);
    const auto a = std::gcd(divisor, denominator);
    const auto b = checked_math::gcd(value, {0, numerator});
    value = wide_math::divide(value, b).quotient;
    const auto scaled = checked_math::checked_multiply(value, denominator / a);
    if (!scaled) return std::unexpected(failure_code_t::capacity_exceeded);
    return signed_clock_quotient(*scaled, wide_math::multiply(divisor / a, numerator / b.low), negative);
}
template<class Value> auto osbx_loop_t<Value>::iteration_start(std::uint32_t iteration) const
    -> result_t<std::int64_t, failure_code_t> {
    if (keys.empty() || keys.back().time_ms < 0 || !count || iteration > count)
        return std::unexpected(failure_code_t::invalid_request);
    const auto delta = wide_math::multiply(static_cast<std::uint64_t>(keys.back().time_ms), iteration);
    if (!wide_math::can_fit_u64(delta)) return std::unexpected(failure_code_t::capacity_exceeded);
    return add_clock_whole(start_ms, delta.low, false);
}
template struct osbx_loop_t<std::vector<double>>;
template struct osbx_loop_t<bool>;
template struct osbx_loop_t<osbx_blend_t>;

auto osbx_clip_clock_t::compose(const osbx_clip_instance_t& use) const
    -> result_t<osbx_clip_clock_t, failure_code_t> {
    const auto start = map(use.start_ms);
    const auto scale = binary_scale(use.time_scale);
    if (!start || !scale) return std::unexpected(failure_code_t::capacity_exceeded);
    const auto a = std::gcd(numerator, scale->second), b = std::gcd(denominator, scale->first);
    const auto n = wide_math::multiply(numerator / a, scale->first / b);
    const auto d = wide_math::multiply(denominator / b, scale->second / a);
    if (!wide_math::can_fit_u64(n) || !wide_math::can_fit_u64(d))
        return std::unexpected(failure_code_t::capacity_exceeded);
    return osbx_clip_clock_t{.origin = *start, .numerator = n.low, .denominator = d.low};
}

namespace {

using code_t = osbx_diagnostic_code_t;

auto transform_arity(osbx_transform_kind_t kind) -> std::size_t {
    switch (kind) {
    case osbx_transform_kind_t::translate:
    case osbx_transform_kind_t::scale:
    case osbx_transform_kind_t::skew:
    case osbx_transform_kind_t::pivot: return 2;
    case osbx_transform_kind_t::rotate: return 1;
    case osbx_transform_kind_t::matrix: return 6;
    }
    return 0;
}


class osbx_validator_t {
  public:
    osbx_validator_t(const osbx_document_t& document, osbx_validation_options_t options)
        : document_(document), options_(options) {}

    auto run() -> diagnosed_t<bool, std::vector<osbx_diagnostic_t>> {
        validate_header();
        if (validate_document_limits()) {
            validate_images();
            validate_clips();
            for (const auto& node : document_.nodes) {
                provenance_ = &node.provenance;
                validate_node(node);
                if (exhausted_) break;
            }
            if (!exhausted_) validate_parents();
        }
        const bool accepted = diagnostics_.empty();
        return {.value = accepted, .diagnostics = std::move(diagnostics_)};
    }

  private:
    const osbx_document_t& document_;
    osbx_validation_options_t options_;
    std::vector<osbx_diagnostic_t> diagnostics_;
    std::unordered_set<std::string_view> ids_;
    std::unordered_set<std::string_view> images_;
    std::unordered_map<std::string_view, std::size_t> nodes_;
    const osbx_provenance_t* provenance_{};
    std::size_t op_count_{};
    std::size_t key_count_{};
    std::size_t program_count_{}, program_storage_{};
    std::size_t use_count_{}, parameter_count_{}, binding_count_{};
    std::unordered_map<std::string_view, const osbx_clip_t*> clips_;
    std::unordered_map<const osbx_clip_t*, std::unordered_map<std::string_view, const osbx_clip_parameter_t*>> parameters_;
    bool exhausted_{};
    const osbx_clip_t* clip_{};

    void error(code_t code, std::string_view id, std::string message) {
        osbx_diagnostic_location_t location{
            .id = std::string(text_util::truncate_utf8(id, options_.maximum_id_bytes)),
        };
        if (provenance_) {
            location.provenance = {
                .file = std::string(text_util::truncate_utf8(provenance_->file, options_.maximum_path_bytes)),
                .line = provenance_->line,
                .generator_key = std::string(text_util::truncate_utf8(provenance_->generator_key,
                                                                     options_.maximum_id_bytes)),
            };
        }
        diagnostics_.push_back({.code = code, .message = std::move(message), .location = std::move(location)});
    }

    auto validate_id(std::string_view id) -> bool {
        if (id.empty() || !text_util::is_valid_bounded_text(id, options_.maximum_id_bytes)) {
            error(code_t::invalid_id, id, "ID must be nonempty bounded UTF-8 text");
            return false;
        }
        return true;
    }

    void register_id(std::string_view id) {
        if (validate_id(id) && !ids_.insert(id).second)
            error(code_t::duplicate_id, id, "ID is already used in this document");
    }

    void validate_path(std::string_view file, std::string_view id) {
        if (!text_util::is_valid_bounded_text(file, options_.maximum_path_bytes) ||
            !osbx_document_t::is_valid_project_path(file))
            error(code_t::invalid_path, id, "Path must be bounded project-relative UTF-8 text without parent traversal");
    }

    void validate_header() {
        if (document_.api_version != "0.1")
            error(code_t::invalid_version, {}, "Unsupported OSBX API version");
        if (!std::isfinite(document_.canvas.width) || !std::isfinite(document_.canvas.height) ||
            document_.canvas.width <= 0 || document_.canvas.height <= 0)
            error(code_t::invalid_canvas, {}, "Canvas dimensions must be finite and positive");
    }

    auto validate_document_limits() -> bool {
        if (document_.nodes.size() > options_.maximum_nodes)
            error(code_t::node_limit, {}, "Node limit exceeded");
        if (document_.images.size() > options_.maximum_images)
            error(code_t::image_limit, {}, "Image limit exceeded");
        return diagnostics_.empty();
    }

    void validate_images() {
        for (const auto& image : document_.images) {
            register_id(image.id);
            validate_path(image.file, image.id);
            images_.insert(image.id);
        }
        for (std::size_t index = 0; index < document_.nodes.size(); ++index)
            nodes_.emplace(document_.nodes[index].id, index);
    }

    void validate_easing(const osbx_easing_t& easing, std::string_view id) {
        if (const auto* osb = std::get_if<osbx_osb_easing_t>(&easing)) {
            if (!is_valid_easing(osb->value))
                error(code_t::invalid_easing, id, "OSB easing must be in [0,34]");
        }
        if (const auto* bezier = std::get_if<osbx_cubic_bezier_t>(&easing)) {
            const std::array values{bezier->x1, bezier->y1, bezier->x2, bezier->y2};
            if (!std::ranges::all_of(values, [](double value) { return std::isfinite(value); }))
                error(code_t::non_finite_value, id, "Bezier control points must be finite");
            else if (bezier->x1 < 0 || bezier->x1 > 1 || bezier->x2 < 0 || bezier->x2 > 1)
                error(code_t::invalid_easing, id, "Bezier X control points must be in [0,1]");
        }
    }

    void validate_value(const std::vector<double>& value, std::string_view id,
                        std::size_t arity, bool opacity) {
        if (value.size() != arity) {
            error(code_t::invalid_value_shape, id, "Value has the wrong number of components");
            return;
        }
        if (!std::ranges::all_of(value, [](double component) { return std::isfinite(component); }))
            error(code_t::non_finite_value, id, "Value components must be finite");
        else if (opacity && (value.front() < 0 || value.front() > 1))
            error(code_t::invalid_value_range, id, "Opacity must be in [0,1]");
    }

    void validate_value(bool, std::string_view, std::size_t, bool) {}

    void validate_value(osbx_blend_t value, std::string_view id, std::size_t, bool) {
        if (value != osbx_blend_t::normal && value != osbx_blend_t::additive)
            error(code_t::invalid_enum, id, "Unknown blend mode");
    }

    template<class Value> void validate_track_links(const osbx_track_t<Value>& track,
        std::string_view owner, std::size_t arity) {
        if (track.parameter) validate_parameter_selector(track, owner, arity);
        if (track.lua && clip_) error(code_t::invalid_lua_link, owner, "Clip templates cannot contain raw Lua links");
        if (track.lua) {
            if constexpr (!std::is_same_v<Value, std::vector<double>>) {
                error(code_t::invalid_lua_link, owner, "Only numeric tracks support Lua links");
            } else {
                const auto message = validate_osbx_lua_track_link(*track.lua);
                if (!message.empty()) error(code_t::invalid_lua_link, owner, message);
                register_id(track.lua->link.id);
            }
        }
    }

    template<class Value> void validate_keys(std::span<const osbx_key_t<Value>> keys,
        std::string_view owner, std::size_t arity, bool opacity, bool relative) {
        if (keys.size() > options_.maximum_keys - key_count_) {
            error(code_t::key_limit, owner, "Document key limit exceeded");
            exhausted_ = true;
            return;
        }
        key_count_ += keys.size();
        std::optional<std::int64_t> previous;
        for (const auto& key : keys) {
            register_id(key.id);
            if (relative && key.time_ms < 0) error(code_t::invalid_value_range, key.id, "Program keys require nonnegative relative times");
            if (previous && key.time_ms <= *previous)
                error(code_t::unordered_keys, key.id, "Key times must be strictly increasing within a track");
            previous = key.time_ms;
            validate_value(key.value, key.id, arity, opacity);
            validate_easing(key.easing, key.id);
        }
    }
    template<class Value> auto account_program_storage(const osbx_key_program_t<Value>& block) -> bool {
        auto bytes = sizeof(block) + block.id().size();
        if (const auto* trigger = std::get_if<osbx_trigger_t<Value>>(&block.data)) bytes += trigger->trigger.name.value.size();
        if (!checked_math::account_size(program_storage_, bytes, options_.maximum_expanded_storage_bytes)) return false;
        for (const auto& key : block.keys()) {
            bytes = sizeof(key) + key.id.size();
            if constexpr (std::is_same_v<Value, std::vector<double>>) bytes += key.value.size() * sizeof(double);
            if (!checked_math::account_size(program_storage_, bytes, options_.maximum_expanded_storage_bytes)) return false;
        }
        return true;
    }
    template<class Value> void validate_program_header(const osbx_key_program_t<Value>& block) {
        if (const auto* loop = std::get_if<osbx_loop_t<Value>>(&block.data)) {
            if (!loop->end_time()) error(code_t::invalid_value_range, block.id(), "Invalid loop count or endpoint overflow");
        } else {
            const auto& trigger = std::get<osbx_trigger_t<Value>>(block.data).trigger;
            if (!trigger.is_valid() || trigger.group_number.value_or(0) != 0)
                error(code_t::invalid_value_range, block.id(), "Invalid trigger header or unsupported nonzero group");
        }
    }
    template<class Value> void validate_program(const osbx_key_program_t<Value>& block,
        std::size_t arity, bool opacity) {
        register_id(block.id());
        if (++program_count_ > options_.maximum_nodes) {
            error(code_t::node_limit, block.id(), "Temporal program count exceeds node limit"); exhausted_ = true; return;
        }
        const auto keys = block.keys();
        if (keys.empty()) error(code_t::invalid_children, block.id(), "Temporal programs require at least one key");
        validate_keys(keys, block.id(), arity, opacity, true);
        if (exhausted_) return;
        if (!account_program_storage(block)) {
            error(code_t::clip_expansion_limit, block.id(), "Temporal program storage limit exceeded"); exhausted_ = true; return;
        }
        validate_program_header(block);
    }
    template <class Value>
    void validate_track(const osbx_track_t<Value>& track, std::string_view owner,
                        std::size_t arity = 0, bool opacity = false) {
        validate_track_links(track, owner, arity);
        validate_value(track.default_value, owner, arity, opacity);
        validate_keys(std::span{track.keys}, owner, arity, opacity, false);
        if (track.lua && !track.program.empty()) error(code_t::invalid_lua_link, owner, "A linked track cannot also own temporal programs");
        for (const auto& block : track.program) {
            if (exhausted_) break;
            validate_program(block, arity, opacity);
        }

    }

    void validate_ops(std::span<const osbx_transform_op_t> ops) {
        struct frame_t {
            std::span<const osbx_transform_op_t> ops;
            std::size_t index{};
        };
        std::vector<frame_t> stack{{.ops = ops}};
        while (!stack.empty() && !exhausted_) {
            auto& frame = stack.back();
            if (frame.index == frame.ops.size()) {
                stack.pop_back();
                continue;
            }
            const auto& op = frame.ops[frame.index++];
            if (!can_visit_op(op, stack.size())) return;
            register_id(op.id);
            const auto arity = transform_arity(op.kind);
            if (arity == 0) error(code_t::invalid_enum, op.id, "Unknown transform kind");
            validate_track(op.track, op.id, arity);
            if (op.kind == osbx_transform_kind_t::pivot && op.track.lua)
                error(code_t::invalid_lua_link, op.id, "Pivot anchors cannot be scripted");
            if (op.kind != osbx_transform_kind_t::pivot && !op.children.empty())
                error(code_t::invalid_children, op.id, "Only pivot operations may contain children");
            if (!op.children.empty()) stack.push_back({.ops = op.children});
        }
    }

    auto can_visit_op(const osbx_transform_op_t& op, std::size_t depth) -> bool {
        if (op_count_ == options_.maximum_ops) {
            error(code_t::op_limit, op.id, "Document transform operation limit exceeded");
            exhausted_ = true;
        } else if (depth > options_.maximum_nesting) {
            error(code_t::nesting_limit, op.id, "Transform nesting limit exceeded");
            exhausted_ = true;
        } else ++op_count_;
        return !exhausted_;
    }

    static auto is_parameter_name(std::string_view name) -> bool {
        const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
        return !name.empty() && letter(name.front()) && std::ranges::all_of(name, [&](char c) {
            return letter(c) || (c >= '0' && c <= '9');
        });
    }
    template<class Value> void validate_parameter_selector(const osbx_track_t<Value>& track,
        std::string_view owner, std::size_t arity) {
        if constexpr (!std::is_same_v<Value, std::vector<double>>) {
            error(code_t::invalid_clip, owner, "Only numeric clip-template tracks accept parameters");
        } else {
            if (!clip_) { error(code_t::invalid_clip, owner, "Parameters require a clip template"); return; }
            const auto& parameters = parameters_.at(clip_);
            const auto found = parameters.find(*track.parameter);
            if (found == parameters.end() || found->second->default_value.size() != arity)
                error(code_t::invalid_clip, owner, "Unknown or wrong-arity clip parameter selector");
        }
    }
    void validate_uses(std::span<const osbx_clip_instance_t> uses) {
        if (uses.size() > options_.maximum_expanded_layers - use_count_) {
            error(code_t::clip_expansion_limit, {}, "Clip use count exceeds limit"); exhausted_ = true; return;
        }
        use_count_ += uses.size();
        for (const auto& use : uses) {
            register_id(use.id);
            if (!std::isfinite(use.time_scale) || use.time_scale <= 0)
                error(code_t::invalid_clip, use.id, "Clip time_scale must be finite and positive");
            if (!validate_id(use.clip)) continue;
            const auto found = clips_.find(use.clip);
            if (found == clips_.end()) { error(code_t::invalid_clip, use.id, "Unknown clip reference"); continue; }
            validate_bindings(use, *found->second);
            if (exhausted_) break;
        }
    }
    void validate_bindings(const osbx_clip_instance_t& use, const osbx_clip_t& definition) {
        if (use.parameters.size() > options_.maximum_keys - binding_count_) {
            error(code_t::clip_expansion_limit, use.id, "Clip binding limit exceeded"); exhausted_ = true; return;
        }
        binding_count_ += use.parameters.size();
        std::set<std::string_view> names;
        const auto& parameters = parameters_.at(&definition);
        for (const auto& binding : use.parameters) {
            const auto found = parameters.find(binding.name);
            if (!names.insert(binding.name).second || found == parameters.end()) {
                error(code_t::invalid_clip, use.id, "Unknown or duplicate clip parameter binding"); continue;
            }
            validate_value(binding.value, use.id, found->second->default_value.size(), false);
        }
    }
    void validate_parameters(const osbx_clip_t& definition) {
        if (definition.parameters.size() > options_.maximum_keys - parameter_count_) {
            error(code_t::clip_expansion_limit, definition.id, "Clip parameter limit exceeded"); exhausted_ = true; return;
        }
        parameter_count_ += definition.parameters.size();
        std::set<std::string_view> names;
        for (const auto& parameter : definition.parameters) {
            if (!is_parameter_name(parameter.name) || parameter.name.size() > options_.maximum_id_bytes || !names.insert(parameter.name).second ||
                parameter.default_value.empty() || parameter.default_value.size() > 6)
                error(code_t::invalid_clip, definition.id, "Invalid or duplicate clip parameter definition");
            validate_value(parameter.default_value, definition.id, parameter.default_value.size(), false);
        }
    }
    void validate_clips() {
        if (document_.clips.size() > options_.maximum_nodes) {
            error(code_t::clip_expansion_limit, {}, "Clip definition limit exceeded"); exhausted_ = true; return;
        }
        for (const auto& definition : document_.clips) {
            validate_parameters(definition);
            if (exhausted_) return;
            clips_.emplace(definition.id, &definition);
            auto& parameters = parameters_[&definition];
            for (const auto& parameter : definition.parameters) parameters.emplace(parameter.name, &parameter);
        }
        for (const auto& definition : document_.clips) {
            if (exhausted_) break;
            register_id(definition.id); clip_ = &definition;
            validate_ops(definition.ops);
            validate_track(definition.tracks.opacity, definition.id, 1, true);
            validate_track(definition.tracks.color, definition.id, 4);
            validate_track(definition.tracks.visibility, definition.id);
            if (definition.tracks.blend) validate_track(*definition.tracks.blend, definition.id);
            validate_uses(definition.uses);
        }
        clip_ = nullptr;
    }

    void validate_node(const osbx_node_t& node) {
        register_id(node.id);
        if (node.lifetime && node.lifetime->begin_ms > node.lifetime->end_ms)
            error(code_t::invalid_value_range, node.id, "Lifetime endpoints must be ordered");
        if (!text_util::is_valid_bounded_text(node.provenance.file, options_.maximum_path_bytes))
            error(code_t::invalid_path, node.id, "Provenance file exceeds bounded UTF-8 text limits");
        if (!text_util::is_valid_bounded_text(node.provenance.generator_key, options_.maximum_id_bytes))
            error(code_t::invalid_id, node.id, "Generator key exceeds bounded UTF-8 text limits");
        validate_node_vocabulary(node);
        validate_media(node);
        validate_node_link(node);
        if (node.parent) (void)validate_id(*node.parent);
        if (node.kind == osbx_node_kind_t::sprite && !node.image)
            error(code_t::missing_image, node.id, "Sprite requires an image reference");
        if (node.image) {
            (void)validate_id(*node.image);
            if (node.kind != osbx_node_kind_t::sprite && node.kind != osbx_node_kind_t::animation)
                error(code_t::invalid_image_reference, node.id, "Only Sprite/Animation nodes may reference an image");
            if (!images_.contains(*node.image))
                error(code_t::missing_image, node.id, "Referenced image does not exist");
        }
        validate_uses(node.uses);
        validate_ops(node.ops);
        if (exhausted_) return;
        validate_track(node.tracks.opacity, node.id, 1, true);
        if (exhausted_) return;
        validate_track(node.tracks.color, node.id, 4);
        if (exhausted_) return;
        validate_track(node.tracks.visibility, node.id);
        if (exhausted_) return;
        validate_track(node.tracks.blend, node.id);
    }

    void validate_media(const osbx_node_t& node) {
        const auto maximum = options_.maximum_expanded_storage_bytes;
        const auto animation_bytes = node.animation ? sizeof(storyboard_animation_t) : 0;
        const auto sample_bytes = node.sample ? sizeof(osbx_sample_t) : 0;
        if (!checked_math::account_size(program_storage_, animation_bytes, maximum) ||
            !checked_math::account_size(program_storage_, sample_bytes, maximum) ||
            (node.sample && !checked_math::account_size(program_storage_, node.sample->path.size(), maximum))) {
            exhausted_ = true;
            error(code_t::clip_expansion_limit, node.id, "Media metadata exceeds shared authored storage budget");
            return;
        }
        if (node.animation && (node.kind != osbx_node_kind_t::animation || !node.animation->is_valid()))
            error(code_t::invalid_value_range, node.id, "Animation metadata requires matching kind and valid frame count/delay/loop");
        if (node.animation && !node.image)
            error(code_t::missing_image, node.id, "Complete Animation requires a base-template image");
        if (node.sample) {
            if (node.kind != osbx_node_kind_t::sample || !node.sample->is_valid(options_.maximum_path_bytes))
                error(code_t::invalid_value_range, node.id, "Sample metadata requires matching kind and valid path/volume");
            validate_path(node.sample->path, node.id);
        }
    }
    void validate_node_link(const osbx_node_t& node) {
        if (node.children_link) {
            register_id(node.children_link->id);
            const auto message = validate_osbx_lua_link(*node.children_link);
            if (!message.empty()) error(code_t::invalid_lua_link, node.id, message);
            if (node.kind != osbx_node_kind_t::group)
                error(code_t::invalid_lua_link, node.id, "Only groups support generated children");
        }
        if (node.parent) {
            const auto found = nodes_.find(*node.parent);
            if (found != nodes_.end() && document_.nodes[found->second].children_link)
                error(code_t::invalid_children, node.id, "Linked group children are generated and read-only");
        }
    }

    void validate_node_vocabulary(const osbx_node_t& node) {
        switch (node.kind) {
        case osbx_node_kind_t::group:
        case osbx_node_kind_t::sprite:
        case osbx_node_kind_t::animation:
        case osbx_node_kind_t::sample: break;
        default: error(code_t::invalid_enum, node.id, "Unknown node kind");
        }
        switch (node.layer) {
        case osbx_layer_t::background:
        case osbx_layer_t::fail:
        case osbx_layer_t::pass:
        case osbx_layer_t::foreground: break;
        default: error(code_t::invalid_enum, node.id, "Unknown layer");
        }
        if (!storyboard_origin_t::from(node.origin))
            error(code_t::invalid_origin, node.id, "Unknown OSB origin");
    }

    void validate_parents() {
        // Each node is visited once. Iteration avoids recursion on long chains.
        const auto absent = document_.nodes.size();
        std::vector<std::size_t> parents(absent, absent);
        for (std::size_t index = 0; index < absent; ++index) {
            const auto& node = document_.nodes[index];
            provenance_ = &node.provenance;
            if (!node.parent) continue;
            const auto parent = nodes_.find(*node.parent);
            if (parent == nodes_.end()) error(code_t::missing_parent, node.id, "Parent does not exist");
            else {
                parents[index] = parent->second;
                if (document_.nodes[parent->second].layer != node.layer)
                    error(code_t::cross_layer_parent, node.id, "Parent must be in the same layer");
            }
        }
        validate_cycles(parents);
    }

    void validate_cycles(std::span<const std::size_t> parents) {
        std::vector<unsigned char> state(parents.size());
        for (std::size_t root = 0; root < parents.size(); ++root) {
            auto index = root;
            while (index < parents.size() && state[index] == 0) {
                state[index] = 1;
                index = parents[index];
            }
            if (index < parents.size() && state[index] == 1) {
                const auto& node = document_.nodes[index];
                provenance_ = &node.provenance;
                error(code_t::parent_cycle, node.id, "Parent references contain a cycle");
            }
            index = root;
            while (index < parents.size() && state[index] == 1) {
                state[index] = 2;
                index = parents[index];
            }
        }
    }
};

} // namespace

namespace {
struct clip_plan_entry_t {
    std::size_t node{};
    const osbx_clip_t* definition{};
    const osbx_clip_instance_t* use{};
    osbx_clip_clock_t clock;
    storyboard_time_t begin;
};
class clip_compiler_t {
  public:
    const osbx_document_t& source;
    osbx_validation_options_t limits;
    std::vector<osbx_diagnostic_t> diagnostics;
    std::vector<clip_plan_entry_t> plan;
    std::map<std::string_view, std::size_t> definitions;
    using parameter_values_t = std::map<std::string_view, const std::vector<double>*>;
    std::map<const osbx_clip_t*, parameter_values_t> defaults;
    std::map<const osbx_clip_instance_t*, parameter_values_t> overrides;
    std::vector<unsigned char> states;
    std::vector<std::size_t> heights;
    std::size_t ops{}, keys{}, storage{};

    auto run(bool materialize_output, bool include_authored = false) -> attempt_t<std::vector<osbx_compiled_clip_t>, std::vector<osbx_diagnostic_t>> {
        if (!include_authored && source.clips.empty() && std::ranges::all_of(source.nodes, [](const auto& node) { return node.uses.empty(); }))
            return {.value = std::vector<osbx_compiled_clip_t>{}};
        plan_source(include_authored);
        if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
        std::vector<osbx_compiled_clip_t> output;
        if (!materialize_output) return {.value = std::move(output)};
        output.reserve(plan.size());
        for (const auto& entry : plan) output.push_back(materialize(entry));
        return {.value = std::move(output)};
    }
    void plan_source(bool include_authored) {
        for (std::size_t i = 0; i < source.clips.size(); ++i) definitions.emplace(source.clips[i].id, i);
        states.resize(source.clips.size()); heights.resize(source.clips.size(), 1);
        for (std::size_t i = 0; i < source.clips.size(); ++i)
            if (!acyclic(i, 0)) return;
        if (!include_authored && std::ranges::all_of(source.nodes, [](const auto& node) { return node.uses.empty(); })) return;
        index_parameters();
        for (const auto& clip : source.clips)
            if (!charge_ops(clip.ops, {}) || !charge_tracks(clip.tracks, {}) || !charge_definition(clip)) return;
        for (std::size_t i = 0; i < source.nodes.size(); ++i) {
            if (!charge_ops(source.nodes[i].ops, {}) || !charge_tracks(source.nodes[i].tracks, {}) ||
                !charge_uses(source.nodes[i].uses) || !charge_media(source.nodes[i])) break;
            for (const auto& use : source.nodes[i].uses)
                if (!visit(use, i, {}, {}, 0)) break;
            if (!diagnostics.empty()) break;
        }
    }
    auto charge_duplicates(std::span<const std::string_view> ids) -> bool {
        if (ids.size() > limits.maximum_nodes - source.nodes.size()) return fail({}, "Duplicate node count exceeds limit");
        for (const auto id : ids) {
            const auto found = std::ranges::find(source.nodes, id, &osbx_node_t::id);
            if (found == source.nodes.end()) return fail(id, "Duplicate node does not exist", code_t::invalid_children);
            if (!charge(sizeof(*found) + found->id.size()) || !charge_ops(found->ops, {}) ||
                !charge_tracks(found->tracks, {}) || !charge_uses(found->uses) || !charge_media(*found)) return false;
            const auto position = static_cast<std::size_t>(found - source.nodes.begin());
            for (const auto& use : found->uses) if (!visit(use, position, {}, {}, 0)) return false;
        }
        return true;
    }
    void index_uses(std::span<const osbx_clip_instance_t> uses) {
        for (const auto& use : uses) {
            auto& values = overrides[&use];
            for (const auto& binding : use.parameters) values.emplace(binding.name, &binding.value);
        }
    }
    void index_parameters() {
        for (const auto& clip : source.clips) {
            auto& values = defaults[&clip];
            for (const auto& parameter : clip.parameters) values.emplace(parameter.name, &parameter.default_value);
            index_uses(clip.uses);
        }
        for (const auto& node : source.nodes) index_uses(node.uses);
    }
    auto fail(std::string_view id, std::string message, code_t code = code_t::clip_expansion_limit) -> bool {
        if (diagnostics.empty()) diagnostics.push_back({.code = code, .message = std::move(message),
            .location = {.id = std::string(id)}});
        return false;
    }
    auto acyclic(std::size_t index, std::size_t depth) -> bool {
        if (states[index] == 2) return heights[index] <= limits.maximum_nesting - std::min(depth, limits.maximum_nesting) ||
            fail(source.clips[index].id, "Clip nesting exceeds limit");
        if (states[index] == 1) return fail(source.clips[index].id, "Recursive clip references", code_t::clip_cycle);
        if (depth >= limits.maximum_nesting) return fail(source.clips[index].id, "Clip nesting exceeds limit");
        states[index] = 1;
        for (const auto& use : source.clips[index].uses) {
            const auto found = definitions.find(use.clip);
            if (found == definitions.end()) return fail(use.id, "Unknown clip reference", code_t::invalid_clip);
            if (!acyclic(found->second, depth + 1)) return false;
            heights[index] = std::max(heights[index], heights[found->second] + 1);
        }
        states[index] = 2;
        return true;
    }
    auto charge(std::size_t bytes) -> bool {
        if (bytes > limits.maximum_expanded_storage_bytes - storage) return fail({}, "Expanded clip storage limit exceeded");
        storage += bytes; return true;
    }
    auto charge_media(const osbx_node_t& node) -> bool {
        return (!node.animation || charge(sizeof(storyboard_animation_t))) &&
            (!node.sample || (charge(sizeof(osbx_sample_t)) && charge(node.sample->path.size())));
    }
    template<class Value> auto charge_keys(std::span<const osbx_key_t<Value>> values, const osbx_clip_clock_t& clock) -> bool {
        if (values.size() > limits.maximum_keys - keys) return fail({}, "Expanded clip key limit exceeded");
        keys += values.size();
        for (const auto& key : values) {
            if (!clock.map(key.time_ms)) return fail(key.id, "Clip key clock exceeds exact fractional capacity");
            if (!charge(sizeof(key) + key.id.size())) return false;
            if constexpr (std::is_same_v<Value, std::vector<double>>)
                if (!charge(key.value.size() * sizeof(double))) return false;
        }
        return true;
    }
    template<class Value> auto charge_program(const osbx_key_program_t<Value>& block, const osbx_clip_clock_t& clock) -> bool {
        if (!charge(sizeof(block) + block.id().size()) || !charge_keys(block.keys(), clock)) return false;
        if (const auto* loop = std::get_if<osbx_loop_t<Value>>(&block.data)) {
            const auto end = loop->end_time();
            if (!end || !clock.map(loop->start_ms) || !clock.map(*end)) return fail(block.id(), "Loop clip clock exceeds exact capacity");
        } else {
            const auto& header = std::get<osbx_trigger_t<Value>>(block.data).trigger;
            if (!charge(header.name.value.size())) return false;
            if ((header.start_time_ms && !clock.map(*header.start_time_ms)) ||
                (header.end_time_ms && !clock.map(*header.end_time_ms))) return fail(block.id(), "Trigger clip clock exceeds exact capacity");
        }
        return true;
    }
    template<class Value> auto charge_track(const osbx_track_t<Value>& track, const osbx_clip_clock_t& clock) -> bool {
        if (!charge(sizeof(track) + (track.parameter ? track.parameter->size() : 0))) return false;
        if constexpr (std::is_same_v<Value, std::vector<double>>)
            if (!charge(track.default_value.size() * sizeof(double))) return false;
        if (!charge_keys(std::span{track.keys}, clock)) return false;
        for (const auto& block : track.program) if (!charge_program(block, clock)) return false;
        return true;
    }
    auto charge_ops(std::span<const osbx_transform_op_t> values, const osbx_clip_clock_t& clock) -> bool {
        for (const auto& op : values) {
            if (ops == limits.maximum_ops) return fail(op.id, "Expanded clip operation limit exceeded");
            ++ops;
            if (!charge(sizeof(op) + op.id.size()) || !charge_track(op.track, clock) || !charge_ops(op.children, clock)) return false;
        }
        return true;
    }
    auto charge_definition(const osbx_clip_t& clip) -> bool {
        if (!charge(sizeof(clip) + clip.id.size())) return false;
        for (const auto& parameter : clip.parameters)
            if (!charge(sizeof(parameter) + parameter.name.size() + parameter.default_value.size() * sizeof(double))) return false;
        return charge_uses(clip.uses);
    }
    auto charge_uses(std::span<const osbx_clip_instance_t> uses) -> bool {
        for (const auto& use : uses) {
            if (!charge(sizeof(use) + use.id.size() + use.clip.size())) return false;
            for (const auto& binding : use.parameters)
                if (!charge(sizeof(binding) + binding.name.size() + binding.value.size() * sizeof(double))) return false;
        }
        return true;
    }
    auto charge_tracks(const osbx_clip_tracks_t& value, const osbx_clip_clock_t& clock) -> bool {
        return charge_track(value.opacity, clock) && charge_track(value.color, clock) &&
            charge_track(value.visibility, clock) && (!value.blend || charge_track(*value.blend, clock));
    }
    auto charge_tracks(const osbx_tracks_t& value, const osbx_clip_clock_t& clock) -> bool {
        return charge_track(value.opacity, clock) && charge_track(value.color, clock) &&
            charge_track(value.visibility, clock) && charge_track(value.blend, clock);
    }
    auto visit(const osbx_clip_instance_t& use, std::size_t node, const osbx_clip_clock_t& parent,
        std::optional<storyboard_time_t> gate, std::size_t depth) -> bool {
        if (depth >= limits.maximum_nesting || plan.size() == limits.maximum_expanded_layers)
            return fail(use.id, "Expanded clip layer/nesting limit exceeded");
        const auto found = definitions.find(use.clip);
        if (found == definitions.end()) return fail(use.id, "Unknown clip reference", code_t::invalid_clip);
        const auto clock = parent.compose(use);
        if (!clock) return fail(use.id, "Clip clock composition exceeds exact capacity");
        const auto begin = gate && osbx_clip_clock_t::is_before(clock->origin, *gate) ? *gate : clock->origin;
        const auto& definition = source.clips[found->second];
        if (!charge(sizeof(clip_plan_entry_t) + sizeof(osbx_compiled_clip_t)) ||
            !charge_ops(definition.ops, *clock) || !charge_tracks(definition.tracks, *clock)) return false;
        if (!bound_opacity(definition, use)) return false;
        plan.push_back({.node = node, .definition = &definition, .use = &use, .clock = *clock, .begin = begin});
        for (const auto& child : definition.uses)
            if (!visit(child, node, *clock, begin, depth + 1)) return false;
        return true;
    }
    auto parameter_value(const osbx_clip_t& definition, const osbx_clip_instance_t& use,
        std::string_view name) const -> const std::vector<double>& {
        const auto& bindings = overrides.at(&use);
        const auto found = bindings.find(name);
        return found == bindings.end() ? *defaults.at(&definition).at(name) : *found->second;
    }
    auto bound_opacity(const osbx_clip_t& definition, const osbx_clip_instance_t& use) -> bool {
        if (!definition.tracks.opacity.parameter) return true;
        const auto& value = parameter_value(definition, use, *definition.tracks.opacity.parameter);
        return (value.size() == 1 && value[0] >= 0 && value[0] <= 1) ||
            fail(use.id, "Bound opacity parameter must be in [0,1]", code_t::invalid_clip);
    }
    void bind(osbx_track_t<>& track, const clip_plan_entry_t& entry) {
        if (track.parameter) track.default_value = parameter_value(*entry.definition, *entry.use, *track.parameter);
        track.parameter.reset();
    }
    void bind_ops(std::vector<osbx_transform_op_t>& values, const clip_plan_entry_t& entry) {
        for (auto& op : values) { bind(op.track, entry); bind_ops(op.children, entry); }
    }
    auto materialize(const clip_plan_entry_t& entry) -> osbx_compiled_clip_t {
        osbx_compiled_clip_t result{.node_index = entry.node, .begin = entry.begin, .clock = entry.clock,
            .ops = entry.definition->ops, .tracks = entry.definition->tracks};
        bind_ops(result.ops, entry);
        bind(result.tracks.opacity, entry); bind(result.tracks.color, entry);
        return result;
    }
};
} // namespace

auto compile_osbx_clips(const osbx_document_t& source, osbx_validation_options_t limits)
    -> attempt_t<std::vector<osbx_compiled_clip_t>, std::vector<osbx_diagnostic_t>> {
    return clip_compiler_t{.source = source, .limits = limits}.run(true);
}

auto osbx_clip_expansion_cost_t::from(const osbx_document_t& source, osbx_validation_options_t options,
    std::span<const std::string_view> additional_nodes)
    -> attempt_t<osbx_clip_expansion_cost_t, std::vector<osbx_diagnostic_t>> {
    const auto structural = osbx_validator_t(source, options).run();
    if (!structural.value) return {.diagnostics = structural.diagnostics};
    clip_compiler_t compiler{.source = source, .limits = options};
    auto planned = compiler.run(false, true);
    if (!planned.value) return {.diagnostics = std::move(planned.diagnostics)};
    if (!compiler.charge_duplicates(additional_nodes)) return {.diagnostics = std::move(compiler.diagnostics)};
    return {.value = osbx_clip_expansion_cost_t{.ops = compiler.ops, .keys = compiler.keys,
        .layers = compiler.plan.size(), .storage = compiler.storage}};
}

auto validate_osbx_document(const osbx_document_t& document, osbx_validation_options_t options)
    -> diagnosed_t<bool, std::vector<osbx_diagnostic_t>> {
    auto result = osbx_validator_t(document, options).run();
    if (result.value) {
        auto compiled = clip_compiler_t{.source = document, .limits = options}.run(false);
        if (!compiled.value) { result.value = false; result.diagnostics = std::move(compiled.diagnostics); }
    }
    return result;
}

} // namespace wormedit
