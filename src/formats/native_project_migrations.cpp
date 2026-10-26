#include "native_project_migrations.h"
#include <wormedit/core/document.h>
#include <wormedit/core/song_preview.h>
#include "../core/text_util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit::native_project_internal {

namespace {

// Schema 2 adds `expression_source` to SV recipes (the reserved Lua expression curve). Every
// schema 1 recipe gets an empty one, which is what a non-expression curve carries.
auto migrate_v1_to_v2(nlohmann::json& document) -> std::string {
    if (!document.contains("charts")) return {};
    if (!document.at("charts").is_array()) return "document charts must be an array";
    for (auto& chart : document.at("charts")) {
        if (!chart.is_object() || !chart.contains("sv_recipes")) continue;
        if (!chart.at("sv_recipes").is_array()) return "chart sv_recipes must be an array";
        for (auto& recipe : chart.at("sv_recipes")) {
            if (!recipe.is_object()) return "sv recipe must be an object";
            if (!recipe.contains("expression_source")) recipe["expression_source"] = "";
        }
    }
    return {};
}

// Schema 3 adds the project-owned quantization preset list. Older projects start with none.
auto migrate_v2_to_v3(nlohmann::json& document) -> std::string {
    if (!document.contains("quantization_presets")) {
        document["quantization_presets"] = nlohmann::json::array();
    } else if (!document.at("quantization_presets").is_array()) {
        return "document quantization_presets must be an array";
    }
    return {};
}

// Schema 4 gives BMS channel 0A its own visual kind. Older imports stored both
// BMS 07 and 0A as `layer`; source provenance distinguishes the latter.
auto is_legacy_bms_layer2(const nlohmann::json& event) -> bool {
    if (!event.is_object() || !event.contains("kind") || !event.at("kind").is_number_integer() ||
        event.at("kind") != 2 || !event.contains("provenance") ||
        !event.at("provenance").is_object())
        return false;
    const auto& provenance = event.at("provenance");
    if (!provenance.contains("family") || !provenance.at("family").is_number_integer() ||
        provenance.at("family") != 1 || !provenance.contains("source_field") ||
        !provenance.at("source_field").is_string())
        return false;
    return provenance.at("source_field").get_ref<const std::string&>().starts_with("0A slot ");
}

auto migrate_v3_to_v4(nlohmann::json& document) -> std::string {
    if (!document.contains("charts")) return {};
    if (!document.at("charts").is_array()) return "document charts must be an array";
    for (auto& chart : document.at("charts")) {
        if (!chart.is_object()) return "chart must be an object";
        if (!chart.contains("bga_events")) continue;
        if (!chart.at("bga_events").is_array()) return "chart bga_events must be an array";
        for (auto& event : chart.at("bga_events")) {
            if (is_legacy_bms_layer2(event)) event["kind"] = std::uint64_t{5};
        }
    }
    return {};
}

auto link_legacy_bmson_lanes(nlohmann::json& chart) -> std::string {
    if (!chart.contains("bgm_source_lanes")) return {};
    if (!chart.at("bgm_source_lanes").is_array() || !chart.contains("sound_channels") ||
        !chart.at("sound_channels").is_array()) return "BMSON lanes and channels must be arrays";
    const auto& channels = chart.at("sound_channels");
    for (auto& lane : chart.at("bgm_source_lanes")) {
        if (!lane.is_object() || !lane.contains("source_order") ||
            !lane.at("source_order").is_number_unsigned()) return "BGM lane source order must be unsigned";
        if (lane.contains("stem_channel")) continue;
        const auto order = lane.at("source_order").get<std::uint64_t>();
        if (order >= channels.size()) continue;
        const auto& channel = channels.at(static_cast<std::size_t>(order));
        if (!channel.is_object() || !channel.contains("id")) return "BMSON channel must have an ID";
        lane["stem_channel"] = channel.at("id");
    }
    return {};
}

auto migrate_v4_to_v5(nlohmann::json& document) -> std::string {
    if (!document.contains("charts")) return {};
    if (!document.at("charts").is_array()) return "document charts must be an array";
    for (auto& chart : document.at("charts")) {
        if (!chart.is_object()) return "chart must be an object";
        if (chart.value("source_family", nlohmann::json{}) !=
            static_cast<std::uint64_t>(format_family_t::bmson)) continue;
        if (const auto error = link_legacy_bmson_lanes(chart); !error.empty()) return error;
    }
    return {};
}

auto migrate_v5_bgm_event(nlohmann::json& event) -> std::string {
    if (!event.is_object()) return "BGM event must be an object";
    if (!event.contains("end")) event["end"] = nullptr;
    return {};
}

auto migrate_v5_measure(nlohmann::json& measure) -> std::string {
    if (!measure.is_object() || !measure.contains("bgm_events")) return {};
    if (!measure.at("bgm_events").is_array()) return "BGM events must be an array";
    for (auto& event : measure.at("bgm_events"))
        if (const auto error = migrate_v5_bgm_event(event); !error.empty()) return error;
    return {};
}

auto migrate_v5_chart(nlohmann::json& chart) -> std::string {
    if (!chart.is_object()) return "chart must be an object";
    if (!chart.contains("measures")) return {};
    if (!chart.at("measures").is_array()) return "chart measures must be an array";
    for (auto& measure : chart.at("measures"))
        if (const auto error = migrate_v5_measure(measure); !error.empty()) return error;
    return {};
}

auto migrate_v5_to_v6(nlohmann::json& document) -> std::string {
    if (!document.contains("charts")) return {};
    if (!document.at("charts").is_array()) return "document charts must be an array";
    for (auto& chart : document.at("charts"))
        if (const auto error = migrate_v5_chart(chart); !error.empty()) return error;
    return {};
}

auto migrate_v6_to_v7(nlohmann::json& document) -> std::string {
    if (!document.contains("output_groups"))
        return {};
    if (!document.at("output_groups").is_array())
        return "document output_groups must be an array";
    for (auto& group : document.at("output_groups")) {
        if (!group.is_object())
            return "output group must be an object";
        if (!group.contains("existing_files"))
            group["existing_files"] = std::uint64_t{0};
    }
    return {};
}

auto migrate_v7_to_v8(nlohmann::json& document) -> std::string {
    if (!document.contains("output_groups"))
        return {};
    if (!document.at("output_groups").is_array())
        return "document output_groups must be an array";
    for (auto& group : document.at("output_groups")) {
        if (!group.is_object())
            return "output group must be an object";
        if (group.contains("stepmania"))
            continue;
        if (group.value("family", std::uint64_t{}) != std::uint64_t{2}) {
            group["stepmania"] = nullptr;
            continue;
        }
        if (!group.contains("relative_output_path") ||
            !group.at("relative_output_path").is_string())
            return "StepMania output group path must be a string";
        const auto path =
            std::filesystem::path{group.at("relative_output_path").get<std::string>()};
        const auto file_profile =
            path.extension() == ".sm" || path == "sm" ? std::uint64_t{0} : std::uint64_t{1};
        group["stepmania"] = {{"file_profile", file_profile},
                              {"timing_syntax", std::uint64_t{1}},
                              {"implementation_profile", std::uint64_t{0}}};
    }
    return {};
}

auto legacy_output_slug(std::string_view label, std::size_t capacity) -> std::string {
    std::string slug;
    bool separator{};
    for (const auto byte : label) {
        const auto value = static_cast<unsigned char>(byte);
        if (std::isalnum(value) != 0) {
            if (separator && !slug.empty() && slug.size() < capacity) slug.push_back('-');
            separator = false;
            if (slug.size() < capacity)
                slug.push_back(static_cast<char>(std::tolower(value)));
        } else {
            separator = true;
        }
        if (slug.size() >= capacity) break;
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    if (slug.empty()) slug.assign(std::string_view{"project"}.substr(0, capacity));
    return slug;
}

auto migrate_legacy_file_destination(const nlohmann::json& document, nlohmann::json& group,
                                     std::uint64_t family) -> std::string {
    if (!group.contains("relative_output_path") ||
        !group.at("relative_output_path").is_string())
        return "file output group path must be a string";
    auto path = std::filesystem::path{group.at("relative_output_path").get<std::string>()};
    if (path.has_extension()) return {};
    if (!document.contains("metadata") || !document.at("metadata").is_object() ||
        !document.contains("id") || !document.at("id").is_number_unsigned())
        return "document identity and metadata are required to migrate file output groups";
    std::uint64_t stepmania_profile{1};
    if (group.contains("stepmania") && group.at("stepmania").is_object())
        stepmania_profile = group.at("stepmania").value("file_profile", std::uint64_t{1});
    const auto extension = family == std::uint64_t{0}
                               ? std::string{".wormedit"}
                               : stepmania_profile == std::uint64_t{0} ? std::string{".sm"}
                                                                       : std::string{".ssc"};
    const auto suffix = "-" + std::to_string(document.at("id").get<std::uint64_t>()) + extension;
    const auto title = document.at("metadata").value("title", std::string{});
    path /= legacy_output_slug(title, 240 - suffix.size()) + suffix;
    group["relative_output_path"] = path.generic_string();
    return {};
}

auto migrate_v8_to_v9(nlohmann::json& document) -> std::string {
    if (!document.contains("output_groups")) return {};
    if (!document.at("output_groups").is_array())
        return "document output_groups must be an array";
    for (auto& group : document.at("output_groups")) {
        if (!group.is_object()) return "output group must be an object";
        if (group.contains("destination_kind")) continue;
        const auto family = group.value("family", std::uint64_t{});
        const auto file = family == std::uint64_t{0} || family == std::uint64_t{2};
        group["destination_kind"] = file ? std::uint64_t{1} : std::uint64_t{0};
        if (file)
            if (const auto error = migrate_legacy_file_destination(document, group, family);
                !error.empty()) return error;
    }
    return {};
}

auto migrate_v9_to_v10(nlohmann::json& document) -> std::string {
    if (!document.contains("project_settings")) document["project_settings"] = nlohmann::json::object();
    return {};
}

auto migrate_v10_to_v11(nlohmann::json& document) -> std::string {
    if (!document.contains("charts")) return {};
    if (!document.at("charts").is_array()) return "document charts must be an array";
    for (auto& chart : document.at("charts")) {
        if (!chart.is_object() || !chart.contains("sv_recipes")) continue;
        if (!chart.at("sv_recipes").is_array()) return "chart sv_recipes must be an array";
        for (auto& recipe : chart.at("sv_recipes")) {
            if (!recipe.is_object()) return "sv recipe must be an object";
            if (!recipe.contains("postprocess")) recipe["postprocess"] = nlohmann::json::array();
        }
    }
    return {};
}

auto migrate_v11_to_v12(nlohmann::json& document) -> std::string {
    if (!document.contains("charts") || !document.at("charts").is_array()) return "document charts must be an array";
    for (auto& chart : document.at("charts")) {
        if (!chart.is_object() || !chart.contains("metadata") || !chart.at("metadata").is_object())
            return "chart metadata must be an object";
        if (!chart["metadata"].contains("overrides")) chart["metadata"]["overrides"] =
            nlohmann::json::array({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
    }
    return {};
}

auto migrate_v12_to_v13(nlohmann::json& document) -> std::string {
    if (!document.contains("charts") || !document.at("charts").is_array()) return "document charts must be an array";
    for (auto& chart : document.at("charts")) {
        if (!chart.is_object()) return "chart must be an object";
        if (!chart.contains("timing_links")) chart["timing_links"] =
            nlohmann::json::array({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr});
    }
    return {};
}

auto migrate_v13_to_v14(nlohmann::json& document) -> std::string {
    // Legacy ownership remains authoritative until the explicit T-744 cutover.
    if (document.contains("prepared_bms_programs"))
        return "schema 13 cannot contain scoped BMS owners";
    return {};
}

auto migrate_v14_to_v15(nlohmann::json&) -> std::string {
    // Existing payloads are unchanged; v15 additionally supports selectorless scopes.
    return {};
}

auto migrate_v15_to_v16(nlohmann::json& document) -> std::string {
    if (!document.contains("prepared_bms_programs")) return {};
    for (auto& owner : document.at("prepared_bms_programs")) {
        for (auto& node : owner.at("nodes")) {
            if (node.at("value").at("type") != "chart") continue;
            for (auto& item : node.at("value").at("data").at("items"))
                if (item.at("value").at("type") == "measure_length") item["value"]["data"]["meter"] = nullptr;
        }
        owner["settings"] = {{"name", nullptr}, {"author", nullptr}, {"rating", nullptr},
            {"overrides", nlohmann::json::array({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr})},
            {"divisions_per_standard_measure", default_divisions_per_standard_measure},
            {"layout_segment", nullptr},
            {"show_empty_bgm_columns", false},
            {"timing_links", nlohmann::json::array({nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr})}};
        owner["source"] = {{"detected", 0U}, {"user_override", nullptr},
            {"original_bytes_hex", nullptr}, {"unchanged_since_import", false}};
    }
    return {};
}

void migrate_lua_recipe(nlohmann::json& recipe) {
    if (recipe.contains("lua") && !recipe.at("lua").is_null()) return;
    recipe["lua"] = nullptr;
    if (recipe.value("curve", std::uint64_t{}) !=
        static_cast<std::uint64_t>(sv_recipe_curve_t::lua_expression)) return;
    recipe["lua"] = {{"api_version", 1U}, {"entry_point", ""},
        {"source_reference", nullptr}, {"source_hash", ""},
        {"parameters", nlohmann::json::array()}, {"inputs", nlohmann::json::array()},
        {"seed", 0U}, {"mode", 0U}, {"cycles", nlohmann::json::array()}};
}

auto migrate_chart_recipes(nlohmann::json& document, void (*migrate)(nlohmann::json&)) -> std::string {
    if (!document.contains("charts")) return {};
    if (!document.at("charts").is_array()) return "document charts must be an array";
    for (auto& chart : document.at("charts")) {
        if (!chart.is_object() || !chart.contains("sv_recipes")) continue;
        if (!chart.at("sv_recipes").is_array()) return "chart sv_recipes must be an array";
        for (auto& recipe : chart.at("sv_recipes")) {
            if (!recipe.is_object()) return "sv recipe must be an object";
            migrate(recipe);
        }
    }
    return {};
}

void migrate_scoped_recipes(nlohmann::json& document, void (*migrate)(nlohmann::json&)) {
    if (!document.contains("prepared_bms_programs")) return;
    for (auto& owner : document.at("prepared_bms_programs")) {
        for (auto& node : owner.at("nodes")) {
            if (node.at("value").at("type") != "chart") continue;
            for (auto& item : node.at("value").at("data").at("items")) {
                auto& value = item.at("value");
                if (value.at("type") != "native") continue;
                auto& payload = value.at("data").at("payload");
                if (payload.at("type") == "sv_recipe") migrate(payload.at("data"));
            }
        }
    }
}

auto migrate_v16_to_v17(nlohmann::json& document) -> std::string {
    if (auto error = migrate_chart_recipes(document, migrate_lua_recipe); !error.empty()) return error;
    migrate_scoped_recipes(document, migrate_lua_recipe);
    return {};
}

void migrate_impulse_recipe(nlohmann::json& recipe) {
    if (!recipe.contains("impulses")) recipe["impulses"] = nlohmann::json::array();
    if (!recipe.contains("finite_box_width_milliseconds"))
        recipe["finite_box_width_milliseconds"] = {{"numerator", 1U}, {"denominator", 1U}};
}

auto migrate_v17_to_v18(nlohmann::json& document) -> std::string {
    if (auto error = migrate_chart_recipes(document, migrate_impulse_recipe); !error.empty()) return error;
    migrate_scoped_recipes(document, migrate_impulse_recipe);
    return {};
}

auto migrate_v18_to_v19(nlohmann::json& document) -> std::string {
    if (!document.contains("generator_recipes")) return {};
    if (!document.at("generator_recipes").is_array()) return "generator_recipes must be an array";
    for (auto& recipe : document.at("generator_recipes")) {
        if (!recipe.is_object()) return "generator recipe must be an object";
        if (!recipe.at("input_assets").is_array()) return "generator inputs must be an array";
        for (auto& input : recipe.at("input_assets"))
            if (!input.is_object()) input = {{"asset", input}, {"content_hash", ""}};
        recipe["source"] = "";
        recipe["chart"] = 0U;
        recipe["bms_scope"] = nullptr;
        recipe["bms_chart"] = nullptr;
        recipe["generated_notes"] = nlohmann::json::array();
        recipe["generated_layers"] = nlohmann::json::array();
    }
    return {};
}

// Reads the preview that retained osu! or StepMania fields encode, the same
// way the importers do.
auto retained_preview(const nlohmann::json& groups) -> std::optional<song_preview_t> {
    if (!groups.is_array()) return std::nullopt;
    for (const auto& group : groups) {
        if (!group.is_object() || !group.contains("family") || !group.contains("ordered_fields"))
            continue;
        const auto family = group.at("family").get<int>();
        std::optional<std::string> osu_time, sample_start, sample_length;
        for (const auto& field : group.at("ordered_fields")) {
            const auto name = field.at(0).get<std::string>();
            const auto value = field.at(1).get<std::string>();
            if (family == static_cast<int>(format_family_t::osu_mania) && name == osu_preview_time_field)
                osu_time = value;
            else if (family == static_cast<int>(format_family_t::stepmania) &&
                     text_util::ascii_upper(name) == stepmania_sample_start_field)
                sample_start = value;
            else if (family == static_cast<int>(format_family_t::stepmania) &&
                     text_util::ascii_upper(name) == stepmania_sample_length_field)
                sample_length = value;
        }
        if (osu_time) return song_preview_t::from_osu_preview_time(*osu_time);
        if (sample_start) return song_preview_t::from_stepmania_sample(*sample_start, sample_length);
    }
    return std::nullopt;
}

auto preview_json(const std::optional<song_preview_t>& preview) -> nlohmann::json {
    // Valid previews are never negative; unsigned matches how a saved project reads back.
    if (!preview) return nullptr;
    const auto unsigned_milliseconds = [](std::int64_t value) { return static_cast<std::uint64_t>(value); };
    return {{"start_milliseconds", unsigned_milliseconds(preview->start_milliseconds)},
            {"length_milliseconds", preview->length_milliseconds
                                        ? nlohmann::json(unsigned_milliseconds(*preview->length_milliseconds))
                                        : nlohmann::json(nullptr)}};
}

// Schema 20 adds the typed song preview (open question 26), lifted from the
// retained osu! and StepMania fields. A chart whose retained fields encode a
// different preview keeps it as an override.
auto migrate_v19_to_v20(nlohmann::json& document) -> std::string {
    if (!document.contains("metadata") || !document.at("metadata").is_object())
        return "document metadata must be an object";
    auto& metadata = document.at("metadata");
    const auto song = retained_preview(metadata.value("format_groups", nlohmann::json::array()));
    metadata["preview"] = preview_json(song);
    if (!document.contains("charts")) return {};
    for (auto& chart : document.at("charts")) {
        if (!chart.is_object() || !chart.contains("metadata") || !chart.at("metadata").is_object())
            return "chart metadata must be an object";
        auto& chart_metadata = chart.at("metadata");
        const auto own = retained_preview(chart_metadata.value("format_groups", nlohmann::json::array()));
        chart_metadata["preview_override"] = own && own != song ? preview_json(own) : nlohmann::json(nullptr);
    }
    return {};
}

// Schema 21 folds the former `scrolls` output (1) into `sv` (0): both always multiplied the
// chart's scroll events, but were composed as separate families until export.
void migrate_scrolls_output_recipe(nlohmann::json& recipe) {
    if (recipe.contains("output") && recipe.at("output") == 1U) recipe["output"] = 0U;
}

auto migrate_v20_to_v21(nlohmann::json& document) -> std::string {
    if (auto error = migrate_chart_recipes(document, migrate_scrolls_output_recipe); !error.empty())
        return error;
    migrate_scoped_recipes(document, migrate_scrolls_output_recipe);
    return {};
}

// Schema22 adds song-wide source-audio export cuts; existing songs start uncut.
auto migrate_v21_to_v22(nlohmann::json& document) -> std::string {
    document["export_cuts"] = nlohmann::json::array();
    return {};
}

// Schema23 adds shared SV definitions and historical generated difficulties.
// Existing SV placements remain independent; generators own no extra charts.
auto migrate_v22_to_v23(nlohmann::json& document) -> std::string {
    document["sv_recipe_definitions"] = nlohmann::json::array();
    if (!document.contains("generator_recipes")) return {};
    for (auto& recipe : document.at("generator_recipes"))
        if (!recipe.contains("generated_charts")) recipe["generated_charts"] = nlohmann::json::array();
    return {};
}

// Schema24 persists custom bookmark list order. An absent optional order keeps
// timeline order; retain orders written by the earlier unbumped schema23 build.
auto migrate_v23_to_v24(nlohmann::json&) -> std::string {
    return {};
}

// Schema25 adds an optional output-group flattening choice; absence preserves branches.
auto migrate_v24_to_v25(nlohmann::json&) -> std::string {
    return {};
}

constexpr std::array<migration_t, 24> released_migrations{{
    {1, &migrate_v1_to_v2},
    {2, &migrate_v2_to_v3},
    {3, &migrate_v3_to_v4},
    {4, &migrate_v4_to_v5},
    {5, &migrate_v5_to_v6},
    {6, &migrate_v6_to_v7},
    {7, &migrate_v7_to_v8},
    {8, &migrate_v8_to_v9},
    {9, &migrate_v9_to_v10},
    {10, &migrate_v10_to_v11},
    {11, &migrate_v11_to_v12},
    {12, &migrate_v12_to_v13},
    {13, &migrate_v13_to_v14},
    {14, &migrate_v14_to_v15},
    {15, &migrate_v15_to_v16},
    {16, &migrate_v16_to_v17},
    {17, &migrate_v17_to_v18},
    {18, &migrate_v18_to_v19},
    {19, &migrate_v19_to_v20},
    {20, &migrate_v20_to_v21},
    {21, &migrate_v21_to_v22},
    {22, &migrate_v22_to_v23},
    {23, &migrate_v23_to_v24},
    {24, &migrate_v24_to_v25},
}};

} // namespace

auto default_migrations() -> std::span<const migration_t> {
    return released_migrations;
}

namespace {

auto find_step(std::span<const migration_t> table, std::uint32_t from_version)
    -> const migration_t* {
    const auto found = std::ranges::find(table, from_version, &migration_t::from_version);
    return found == table.end() ? nullptr : &*found;
}

} // namespace

auto migrate_document(nlohmann::json& document, std::uint32_t target_version,
                      std::span<const migration_t> table)
    -> result_t<migration_phase_t, error_t<failure_code_t>> {
    if (!document.is_object() || !document.contains("schema_version") ||
        !document.at("schema_version").is_number_unsigned()) {
        return std::unexpected(error_t{failure_code_t::invalid_request,
                                     std::string{"document schema version must be an unsigned integer"}});
    }
    const auto start = document.at("schema_version").get<std::uint64_t>();
    if (start > target_version) {
        return std::unexpected(error_t{failure_code_t::unsupported,
                                     std::string{"document schema version is newer than this build supports"}});
    }
    if (start == target_version) {
        return migration_phase_t::unchanged;
    }
    // Work on a copy so a failing step never leaves a half-migrated document.
    auto working = document;
    for (auto version = static_cast<std::uint32_t>(start); version < target_version; ++version) {
        const auto* step = find_step(table, version);
        if (step == nullptr || step->migrate == nullptr) {
            return std::unexpected(error_t{failure_code_t::not_found,
                                         "no migration from document schema version " +
                                             std::to_string(version)});
        }
        if (auto message = step->migrate(working); !message.empty()) {
            return std::unexpected(error_t{failure_code_t::invalid_data, std::move(message)});
        }
        working["schema_version"] = version + 1;
    }
    document = std::move(working);
    return migration_phase_t::migrated;
}

namespace {

constexpr std::string_view legacy_lane_prefix = "BGM source #";

auto is_bms_zero_slot(const bgm_event_t& event) -> bool {
    constexpr std::string_view suffix = " object 00";
    const auto& field = event.provenance.source_field;
    return !event.sound && !event.end && event.provenance.family == format_family_t::bms &&
           field.starts_with("01 slot ") && field.ends_with(suffix);
}

auto drop_zero_slot_events(chart_document_t& chart) -> bool {
    bool changed = false;
    for (auto& measure : chart.measures) {
        changed |= std::erase_if(measure.bgm_events, is_bms_zero_slot) > 0;
    }
    return changed;
}

// Legacy lanes (one per source line) ranked by their source line.
auto ranked_legacy_lanes(const chart_document_t& chart)
    -> std::map<std::uint64_t, std::size_t> {
    std::vector<const bgm_source_lane_t*> legacy;
    for (const auto& lane : chart.bgm_source_lanes) {
        if (lane.name.starts_with(legacy_lane_prefix)) legacy.push_back(&lane);
    }
    std::ranges::stable_sort(legacy, {}, &bgm_source_lane_t::source_order);
    std::map<std::uint64_t, std::size_t> rank;
    for (std::size_t index = 0; index < legacy.size(); ++index)
        rank.emplace(legacy[index]->id.value, index);
    return rank;
}

// Column of every legacy lane in the measure that uses it: the k-th distinct
// legacy lane (by source order) of a measure becomes column k.
auto measure_columns(const measure_chunk_t& measure,
                     const std::map<std::uint64_t, std::size_t>& rank)
    -> std::map<std::uint64_t, std::size_t> {
    std::set<std::size_t> used_ranks;
    for (const auto& event : measure.bgm_events) {
        if (const auto found = rank.find(event.source_lane.value); found != rank.end())
            used_ranks.insert(found->second);
    }
    std::map<std::size_t, std::size_t> column_of_rank;
    for (const auto value : used_ranks) column_of_rank.emplace(value, column_of_rank.size());
    std::map<std::uint64_t, std::size_t> columns;
    for (const auto& [lane, lane_rank] : rank) {
        if (const auto found = column_of_rank.find(lane_rank); found != column_of_rank.end())
            columns.emplace(lane, found->second);
    }
    return columns;
}

void rebuild_bgm_columns(chart_document_t& chart) {
    const auto rank = ranked_legacy_lanes(chart);
    if (rank.empty()) return;
    std::vector<std::map<std::uint64_t, std::size_t>> per_measure;
    std::size_t column_count = 0;
    std::vector<std::uint64_t> column_lane;
    std::map<std::size_t, std::size_t> column_first_order;
    for (const auto& measure : chart.measures) {
        per_measure.push_back(measure_columns(measure, rank));
        for (const auto& [lane, column] : per_measure.back()) {
            column_count = std::max(column_count, column + 1);
            const auto order = std::ranges::find(chart.bgm_source_lanes, lane,
                                                 [](const auto& item) { return item.id.value; })
                                   ->source_order;
            const auto [slot, inserted] = column_first_order.try_emplace(column, order);
            if (!inserted) slot->second = std::min(slot->second, order);
        }
    }
    // Column k keeps the id of the k-th legacy lane, so ids stay stable.
    column_lane.resize(column_count);
    for (const auto& [lane, lane_rank] : rank) {
        if (lane_rank < column_count) column_lane[lane_rank] = lane;
    }
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        for (auto& event : chart.measures[index].bgm_events) {
            const auto found = per_measure[index].find(event.source_lane.value);
            if (found != per_measure[index].end())
                event.source_lane = object_id_t{column_lane[found->second]};
        }
    }
    std::vector<bgm_source_lane_t> lanes;
    for (const auto& lane : chart.bgm_source_lanes) {
        const auto found = rank.find(lane.id.value);
        if (found == rank.end()) {
            lanes.push_back(lane);
        } else if (found->second < column_count) {
            lanes.push_back({.id = lane.id,
                             .name = "BGM " + std::to_string(found->second + 1),
                             .source_order = column_first_order.at(found->second)});
        }
    }
    chart.bgm_source_lanes = std::move(lanes);
}

} // namespace

auto normalize_legacy_bms_bgm(song_document_t& song) -> bool {
    bool changed = false;
    for (auto& chart : song.charts) {
        const auto before = chart.bgm_source_lanes.size();
        const auto dropped = drop_zero_slot_events(chart);
        const auto legacy = !ranked_legacy_lanes(chart).empty();
        rebuild_bgm_columns(chart);
        changed |= dropped || legacy || chart.bgm_source_lanes.size() != before;
    }
    return changed;
}

} // namespace wormedit::native_project_internal
