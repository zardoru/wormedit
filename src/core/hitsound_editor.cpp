#include <wormedit/core/hitsound_editor.h>
#include <unordered_set>

namespace wormedit {
auto hitsound_commands() -> std::span<const hitsound_command_t> {
    static constexpr std::array commands{
        hitsound_command_t{"hitsound.whistle", "Toggle Whistle", {hitsound_field_t::whistle, {}}, shortcut_key_t::w, 0},
        hitsound_command_t{"hitsound.finish", "Toggle Finish", {hitsound_field_t::finish, {}}, shortcut_key_t::e, 0},
        hitsound_command_t{"hitsound.clap", "Toggle Clap", {hitsound_field_t::clap, {}}, shortcut_key_t::r, 0},
        hitsound_command_t{"hitsound.normal_auto", "Normal set: Auto", {hitsound_field_t::normal_set, 0}, shortcut_key_t::q, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift)},
        hitsound_command_t{"hitsound.normal_normal", "Normal set: Normal", {hitsound_field_t::normal_set, 1}, shortcut_key_t::w, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift)},
        hitsound_command_t{"hitsound.normal_soft", "Normal set: Soft", {hitsound_field_t::normal_set, 2}, shortcut_key_t::e, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift)},
        hitsound_command_t{"hitsound.normal_drum", "Normal set: Drum", {hitsound_field_t::normal_set, 3}, shortcut_key_t::r, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift)},
        hitsound_command_t{"hitsound.addition_auto", "Addition set: Auto", {hitsound_field_t::addition_set, 0}, shortcut_key_t::q, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt)},
        hitsound_command_t{"hitsound.addition_normal", "Addition set: Normal", {hitsound_field_t::addition_set, 1}, shortcut_key_t::w, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt)},
        hitsound_command_t{"hitsound.addition_soft", "Addition set: Soft", {hitsound_field_t::addition_set, 2}, shortcut_key_t::e, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt)},
        hitsound_command_t{"hitsound.addition_drum", "Addition set: Drum", {hitsound_field_t::addition_set, 3}, shortcut_key_t::r, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt)}};
    return commands;
}
namespace {
auto sample_fields(const osu_hitsample_t& sample) -> std::array<int, 5> {
    return {sample.whistle, sample.finish, sample.clap,
            static_cast<int>(sample.normal_set), static_cast<int>(sample.addition_set)};
}

auto selected_hitsound_notes(chart_document_t& chart, std::span<const object_id_t> ids)
    -> attempt_t<std::vector<note_t*>> {
    if (ids.empty() || ids.size() > 50'000)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Invalid hitsound selection")}};
    std::unordered_set<std::uint64_t> requested;
    for (const auto id : ids) requested.insert(id.value);
    std::vector<note_t*> notes;
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            if (!requested.contains(note.id.value)) continue;
            notes.push_back(&note);
        }
    }
    if (notes.size() != ids.size() || requested.size() != ids.size())
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Hitsound selection contains missing or duplicate notes")}};
    return {.value = std::move(notes)};
}

auto edit_hitsounds(chart_document_t& chart, std::span<const object_id_t> ids,
                    hitsound_change_t change) -> attempt_t<document_edit_effect_t> {
    if (!change.is_valid())
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Invalid hitsound value")}};
    auto selected = selected_hitsound_notes(chart, ids);
    if (!selected.value) return {.diagnostics = std::move(selected.diagnostics)};
    const auto& notes = *selected.value;
    std::vector<osu_hitsample_t> samples;
    for (const auto* note : notes) samples.push_back(note->osu_hitsample.value_or(osu_hitsample_t{}));
    if (!change.value) change.value = hitsound_values_t::from(samples).next_value(change.field);
    bool changed{};
    for (auto* note : notes) {
        auto sample = changed_hitsample(note->osu_hitsample.value_or(osu_hitsample_t{}), change);
        if (!sample.is_valid())
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Invalid hitsound value")}};
        if (note->osu_hitsample == sample) continue;
        note->osu_hitsample = std::move(sample);
        changed = true;
    }
    return {.value = document_edit_effect_t{.changed = changed, .command_label = "Edit hitsounds"}};
}
} // namespace

auto hitsound_values_t::from(std::span<const osu_hitsample_t> samples) -> hitsound_values_t {
    hitsound_values_t result;
    if (samples.empty()) return result;
    const auto first = sample_fields(samples.front());
    for (std::size_t field = 0; field < first.size(); ++field) {
        result.fields[field] = first[field];
        for (const auto& sample : samples)
            if (sample_fields(sample)[field] != first[field]) result.fields[field].reset();
    }
    return result;
}

auto hitsound_values_t::next_value(hitsound_field_t field) const -> int {
    const auto& value = fields[static_cast<std::size_t>(field)];
    if (field < hitsound_field_t::normal_set) return value == 1 ? 0 : 1;
    return value ? (*value + 1) % 4 : 0;
}

auto changed_hitsample(osu_hitsample_t sample, hitsound_change_t change) -> osu_hitsample_t {
    const auto value = change.value.value_or(hitsound_values_t::from(std::span{&sample, 1}).next_value(change.field));
    switch (change.field) {
    case hitsound_field_t::whistle: sample.whistle = value != 0; break;
    case hitsound_field_t::finish: sample.finish = value != 0; break;
    case hitsound_field_t::clap: sample.clap = value != 0; break;
    case hitsound_field_t::normal_set: sample.normal_set = static_cast<osu_sample_set_t>(value); break;
    case hitsound_field_t::addition_set: sample.addition_set = static_cast<osu_sample_set_t>(value); break;
    }
    return sample;
}

auto apply_hitsound_edit(document_history_t& history, chart_id_t chart,
                         std::span<const object_id_t> notes, hitsound_change_t change)
    -> document_command_result_t {
    document_command_transaction_t transaction{history};
    if (transaction.has_bms_program(chart))
        return transaction.finish_execution(transaction.stage_bms_native_objects(chart, notes,
            bms_existing_edit_kind_t::update,
            [&](chart_document_t& target) { return edit_hitsounds(target, notes, change); }),
            command_noop_policy_t::accept_without_diagnostics);
    return transaction.finish_execution(transaction.stage(chart, "Hitsound chart is unavailable",
        [&](chart_document_t& target, song_document_t&) { return edit_hitsounds(target, notes, change); }),
        command_noop_policy_t::accept_without_diagnostics);
}
} // namespace wormedit
