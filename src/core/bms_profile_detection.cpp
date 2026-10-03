#include <wormedit/core/bms_profile_detection.h>

#include <algorithm>
#include <array>
#include <set>
#include <string_view>
#include <utility>

namespace wormedit {
namespace {

auto is_base36(char value) -> bool {
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z');
}

auto uppercase_ascii(std::string value) -> std::string {
    for (auto& character : value) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return value;
}

auto normalize_extension(std::string extension) -> std::string {
    extension = uppercase_ascii(std::move(extension));
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    return extension;
}

auto observe_channel(std::size_t order, const std::string& raw) -> bms_raw_channel_t {
    auto result = bms_raw_channel_t{
        .source_order = order,
        .raw_spelling = raw,
    };
    if (raw.size() != 2) {
        return result;
    }

    const auto canonical = uppercase_ascii(raw);
    if (!is_base36(canonical[0]) || !is_base36(canonical[1])) {
        return result;
    }
    result.canonical_spelling = canonical;
    switch (canonical[0]) {
    case '1':
    case '2':
        result.kind = bms_raw_channel_kind_t::visible_note;
        break;
    case '3':
    case '4':
        result.kind = bms_raw_channel_kind_t::invisible_note;
        break;
    case '5':
    case '6':
        result.kind = bms_raw_channel_kind_t::long_note;
        break;
    case 'D':
    case 'E':
        if (canonical[1] >= '1' && canonical[1] <= '9') {
            result.kind = bms_raw_channel_kind_t::mine;
        }
        break;
    default:
        result.kind = bms_raw_channel_kind_t::non_playable;
        break;
    }
    return result;
}

auto is_playable_family(bms_raw_channel_kind_t kind) -> bool {
    return kind == bms_raw_channel_kind_t::visible_note ||
           kind == bms_raw_channel_kind_t::invisible_note ||
           kind == bms_raw_channel_kind_t::long_note || kind == bms_raw_channel_kind_t::mine;
}

auto profile_maps_channel(const lane_layout_t& layout, std::string_view channel) -> bool {
    return std::ranges::any_of(layout.lanes, [channel](const auto& lane) {
        const auto matches = [channel](const std::optional<std::string>& candidate) {
            return candidate && *candidate == channel;
        };
        return matches(lane.bms_channels.visible) || matches(lane.bms_channels.invisible) ||
               matches(lane.bms_channels.long_note) || matches(lane.bms_channels.mine);
    });
}

auto channel_for_kind(const lane_definition_t& lane, bms_raw_channel_kind_t kind)
    -> const std::optional<std::string>* {
    switch (kind) {
    case bms_raw_channel_kind_t::visible_note:
        return &lane.bms_channels.visible;
    case bms_raw_channel_kind_t::invisible_note:
        return &lane.bms_channels.invisible;
    case bms_raw_channel_kind_t::long_note:
        return &lane.bms_channels.long_note;
    case bms_raw_channel_kind_t::mine:
        return &lane.bms_channels.mine;
    case bms_raw_channel_kind_t::non_playable:
    case bms_raw_channel_kind_t::unknown_or_reserved:
        return nullptr;
    }
    return nullptr;
}

constexpr std::array playable_channel_families{
    bms_raw_channel_kind_t::visible_note,
    bms_raw_channel_kind_t::invisible_note,
    bms_raw_channel_kind_t::long_note,
    bms_raw_channel_kind_t::mine,
};
using channel_family_sets_t =
    std::array<std::set<std::string, std::less<>>, playable_channel_families.size()>;

auto observed_channel_families(const std::vector<const bms_raw_channel_t*>& observed)
    -> channel_family_sets_t {
    channel_family_sets_t result;
    for (const auto* channel : observed) {
        if (!channel->canonical_spelling.has_value()) {
            continue;
        }
        const auto family = std::ranges::find(playable_channel_families, channel->kind);
        if (family != playable_channel_families.end()) {
            result[static_cast<std::size_t>(family - playable_channel_families.begin())].insert(
                *channel->canonical_spelling);
        }
    }
    return result;
}

auto complete_channel_family_match(const lane_layout_t& layout,
                                   const channel_family_sets_t& observed) -> bool {
    for (std::size_t family_index = 0; family_index < playable_channel_families.size();
         ++family_index) {
        const auto& observed_channels = observed[family_index];
        if (observed_channels.empty()) {
            continue;
        }
        std::set<std::string, std::less<>> profile_channels;
        for (const auto& lane : layout.lanes) {
            const auto* channel = channel_for_kind(lane, playable_channel_families[family_index]);
            if (channel != nullptr && channel->has_value()) {
                profile_channels.insert(**channel);
            }
        }
        if (observed_channels == profile_channels) {
            return true;
        }
    }
    return false;
}

auto extension_is_known(std::string_view extension) -> bool {
    return extension == ".BMS" || extension == ".BME" || extension == ".BML" || extension == ".PMS";
}

auto profile_header_compatibility(const bms_lane_profile_descriptor_t& spec,
                                  std::string_view extension,
                                  std::optional<unsigned int> player_header, bool octave_fp_header)
    -> std::optional<bool> {
    if (!player_header || *player_header < 1 || *player_header > 4) {
        return std::nullopt;
    }
    if (spec.family == bms_lane_profile_family_t::popn && extension == ".PMS") {
        // The .pms extension is the compatibility discriminator for the
        // one-panel 9-button interpretation of #PLAYER 3.
        return *player_header == 3;
    }
    if (spec.octave_fp_header && octave_fp_header) {
        return *player_header == 3;
    }
    const auto expected_two_stage = *player_header != 1;
    return spec.player_header_two_stage == expected_two_stage;
}

auto primary_channel_families(const lane_layout_t& layout) -> channel_family_sets_t {
    channel_family_sets_t result;
    for (const auto& lane : layout.lanes) {
        if (lane.role == lane_role_t::free_zone || lane.role == lane_role_t::foot_pedal) continue;
        for (std::size_t family = 0; family < playable_channel_families.size(); ++family) {
            const auto* channel = channel_for_kind(lane, playable_channel_families[family]);
            if (channel && channel->has_value()) result[family].insert(**channel);
        }
    }
    return result;
}

auto complete_primary_family_match(const lane_layout_t& layout,
                                   const channel_family_sets_t& observed) -> bool {
    const auto primary = primary_channel_families(layout);
    bool complete = false;
    for (std::size_t family = 0; family < observed.size(); ++family) {
        for (const auto& channel : observed[family])
            if (!primary[family].contains(channel)) return false;
        complete = complete || (!observed[family].empty() && observed[family] == primary[family]);
    }
    return complete;
}

auto primary_detection_hints_match(const bms_lane_profile_descriptor_t& spec,
                                   const bms_profile_detection_request_t& request,
                                   std::string_view extension) -> bool {
    if (spec.family == bms_lane_profile_family_t::special || request.octave_fp_header) return false;
    if (spec.family == bms_lane_profile_family_t::popn)
        return request.player_header == 3 && extension != ".BME";
    if (extension == ".PMS") return false;
    if (spec.family == bms_lane_profile_family_t::bms_five_or_ten_key &&
        extension != ".BMS" && extension != ".BML") return false;
    return request.player_header &&
        profile_header_compatibility(spec, extension, request.player_header, false).value_or(false);
}

auto clamp_confidence(double value) -> double {
    return std::clamp(value, 0.0, 1.0);
}

auto extension_weight(const bms_lane_profile_descriptor_t& spec, std::string_view extension)
    -> double {
    if (extension == ".PMS") {
        return spec.family == bms_lane_profile_family_t::popn ? 0.18 : -0.18;
    }
    if (extension == ".BMS") {
        if (spec.family == bms_lane_profile_family_t::bms_five_or_ten_key) {
            return 0.12;
        }
        return spec.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key ? 0.04 : 0.0;
    }
    if (extension == ".BME") {
        if (spec.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key) {
            return 0.14;
        }
        return spec.family == bms_lane_profile_family_t::bms_five_or_ten_key ? 0.02 : 0.0;
    }
    if (extension == ".BML") {
        if (spec.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key) {
            return 0.08;
        }
        return spec.family == bms_lane_profile_family_t::bms_five_or_ten_key ? 0.05 : 0.0;
    }
    return 0.0;
}

void add_extension_evidence(const bms_lane_profile_descriptor_t& spec, std::string_view extension,
                            double& score, std::vector<bms_profile_evidence_t>& evidence) {
    if (extension_is_known(extension)) {
        const auto weight = extension_weight(spec, extension);
        score += weight;
        evidence.push_back({
            .source = bms_detection_evidence_source_t::file_extension,
            .message = std::string("Extension ") + std::string(extension) +
                       (weight > 0 ? " favors this profile family."
                                   : (weight < 0 ? " favors another profile family."
                                                 : " gives no preference among these profiles.")),
            .weight = weight,
        });
    } else if (!extension.empty()) {
        evidence.push_back({
            .source = bms_detection_evidence_source_t::file_extension,
            .message = "The filename extension is not a recognized BMS-family extension.",
            .weight = 0.0,
        });
    }
}

void add_player_evidence(const bms_lane_profile_descriptor_t& spec, std::string_view extension,
                         std::optional<unsigned int> player_header, bool octave_fp_header,
                         double& score, std::vector<bms_profile_evidence_t>& evidence) {
    if (player_header && *player_header >= 1 && *player_header <= 4) {
        const auto compatibility =
            profile_header_compatibility(spec, extension, player_header, octave_fp_header);
        const auto weight = compatibility.value_or(false) ? 0.12 : -0.14;
        score += weight;
        evidence.push_back({
            .source = bms_detection_evidence_source_t::player_header,
            .message = std::string("#PLAYER ") + std::to_string(*player_header) +
                       (weight > 0 ? " is consistent with the candidate stage count."
                                   : " does not establish the candidate stage count."),
            .weight = weight,
        });
    } else if (player_header) {
        evidence.push_back({
            .source = bms_detection_evidence_source_t::player_header,
            .message = std::string("Unrecognized #PLAYER value ") + std::to_string(*player_header) +
                       " is retained but not used as a hint.",
            .weight = 0.0,
        });
    }
}

void add_octave_evidence(const bms_lane_profile_descriptor_t& spec, bool octave_fp_header,
                         double& score, std::vector<bms_profile_evidence_t>& evidence) {
    if (!octave_fp_header) {
        return;
    }
    const auto weight = spec.octave_fp_header ? 0.30 : -0.25;
    score += weight;
    evidence.push_back({
        .source = bms_detection_evidence_source_t::octave_fp_header,
        .message = spec.octave_fp_header
                       ? "#OCT/FP explicitly identifies the octave/foot-pedal profile."
                       : "#OCT/FP conflicts with this candidate profile.",
        .weight = weight,
    });
}

void add_channel_evidence(const bms_lane_profile_descriptor_t& spec,
                          std::size_t compatible_count, std::size_t playable_channel_count,
                          bool complete_family_match, double& score,
                          std::vector<bms_profile_evidence_t>& evidence) {
    if (playable_channel_count != 0) {
        evidence.push_back({
            .source = bms_detection_evidence_source_t::used_channel,
            .message = std::to_string(playable_channel_count) +
                       " used playable-family channel(s) are mapped by this candidate.",
            .weight = 0.0,
        });
    }
    if (compatible_count == 1) {
        score += 0.14;
        evidence.push_back({
            .source = bms_detection_evidence_source_t::used_channel,
            .message = "This is the only built-in candidate that maps every recognized playable channel.",
            .weight = 0.14,
        });
    } else if (compatible_count > 1) {
        const auto weight = -std::min(0.16, 0.025 * static_cast<double>(compatible_count - 1));
        score += weight;
        evidence.push_back({
            .source = bms_detection_evidence_source_t::used_channel,
            .message = std::to_string(compatible_count) +
                       " built-in candidates map all recognized playable channels.",
            .weight = weight,
        });
    }
    if (complete_family_match) {
        const auto weight = spec.family == bms_lane_profile_family_t::special ? 0.0 : 0.20;
        score += weight;
        evidence.push_back({
            .source = bms_detection_evidence_source_t::used_channel,
            .message = spec.family == bms_lane_profile_family_t::special
                           ? "A complete channel family matches this special profile, but special "
                             "dialects still require named or explicit evidence."
                           : "A complete observed channel family exactly matches this standard "
                             "profile rather than only a subset of it.",
            .weight = weight,
        });
    }
}

auto profile_evidence(const bms_lane_profile_descriptor_t& spec, std::string_view extension,
                      std::optional<unsigned int> player_header, bool octave_fp_header,
                      std::size_t compatible_count, std::size_t playable_channel_count,
                      bool complete_family_match)
    -> std::pair<double, std::vector<bms_profile_evidence_t>> {
    auto score = 0.55;
    auto evidence = std::vector<bms_profile_evidence_t>{};
    add_extension_evidence(spec, extension, score, evidence);
    add_player_evidence(spec, extension, player_header, octave_fp_header, score, evidence);
    add_octave_evidence(spec, octave_fp_header, score, evidence);
    add_channel_evidence(spec, compatible_count, playable_channel_count, complete_family_match,
                         score, evidence);
    return {clamp_confidence(score), std::move(evidence)};
}

auto playable_channels(const bms_profile_detection_result_t& result)
    -> std::vector<const bms_raw_channel_t*> {
    auto channels = std::vector<const bms_raw_channel_t*>{};
    for (const auto& channel : result.custom_fallback.channels) {
        if (is_playable_family(channel.kind)) {
            channels.push_back(&channel);
        }
    }
    return channels;
}

auto make_candidates(std::span<const bms_lane_profile_descriptor_t> specs,
                     const std::vector<const bms_raw_channel_t*>& channels)
    -> std::vector<bms_profile_candidate_t> {
    const auto families = observed_channel_families(channels);
    auto candidates = std::vector<bms_profile_candidate_t>{};
    candidates.reserve(specs.size());
    for (const auto& spec : specs) {
        auto candidate = bms_profile_candidate_t{};
        candidate.profile = spec.profile;
        for (const auto* channel : channels) {
            const auto canonical = channel->canonical_spelling.value_or(channel->raw_spelling);
            if (!profile_maps_channel(candidate.profile.layout, canonical)) {
                candidate.unmapped_playable_channels.push_back(channel->raw_spelling);
            }
        }
        candidate.maps_all_playable_channels = candidate.unmapped_playable_channels.empty();
        candidate.complete_channel_family_match =
            complete_channel_family_match(candidate.profile.layout, families);
        candidate.automatic_selection_eligible = spec.family != bms_lane_profile_family_t::special;
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

void score_candidates(std::span<const bms_lane_profile_descriptor_t> specs,
                      const std::vector<const bms_raw_channel_t*>& channels,
                      std::string_view extension, const bms_profile_detection_request_t& request,
                      std::vector<bms_profile_candidate_t>& candidates,
                      bms_profile_detection_result_t& result) {
    const auto compatible_count = static_cast<std::size_t>(std::ranges::count_if(
        candidates, [](const auto& item) { return item.maps_all_playable_channels; }));
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        auto& candidate = candidates[index];
        if (!candidate.maps_all_playable_channels) {
            continue;
        }
        auto [confidence, evidence] = profile_evidence(
            specs[index], extension, request.player_header, request.octave_fp_header,
            compatible_count, channels.size(), candidate.complete_channel_family_match);
        candidate.confidence = confidence;
        candidate.evidence = std::move(evidence);
        result.candidates.push_back(std::move(candidate));
    }
}

auto find_candidate(std::vector<bms_profile_candidate_t>& candidates, std::string_view profile_id)
    -> bms_profile_candidate_t* {
    const auto found = std::ranges::find_if(candidates, [profile_id](const auto& item) {
        return item.profile.layout.profile_id == profile_id;
    });
    return found == candidates.end() ? nullptr : &*found;
}

void select_candidate(bms_profile_detection_result_t& result,
                      const bms_profile_candidate_t& chosen, double confidence) {
    result.status = bms_profile_detection_status_t::selected;
    result.confidence = confidence;
    result.selected_profile = chosen.profile;
    result.evidence = chosen.evidence;
}

auto select_explicit_profile(const bms_profile_detection_request_t& request,
                             bms_profile_detection_result_t& result) -> bool {
    if (!request.explicit_profile_id) {
        return false;
    }
    if (auto* chosen = find_candidate(result.candidates, *request.explicit_profile_id)) {
        chosen->confidence = 1.0;
        chosen->evidence.push_back({
            .source = bms_detection_evidence_source_t::explicit_profile,
            .message = "An explicit project/user profile choice matches all recognized playable channels.",
            .weight = 1.0,
        });
        select_candidate(result, *chosen, 1.0);
        return true;
    }
    result.evidence.push_back({
        .source = bms_detection_evidence_source_t::explicit_profile,
        .message = "The explicit profile is unknown or does not map every recognized playable "
                   "channel; raw channels remain in the custom fallback.",
        .weight = 0.0,
    });
    result.status = bms_profile_detection_status_t::custom_required;
    result.confidence = 0.0;
    return true;
}

auto select_octave_profile(const bms_profile_detection_request_t& request,
                           bms_profile_detection_result_t& result) -> bool {
    if (!request.octave_fp_header) {
        return false;
    }
    if (const auto* chosen = find_candidate(result.candidates, "bms-octave-foot-pedal")) {
        select_candidate(result, *chosen, std::max(chosen->confidence, 0.95));
        return true;
    }
    result.evidence.push_back({
        .source = bms_detection_evidence_source_t::octave_fp_header,
        .message = "#OCT/FP was present, but its named profile does not map every recognized "
                   "playable channel.",
        .weight = 0.0,
    });
    return false;
}

auto choose_primary_candidate(bms_profile_candidate_t*& chosen,
                             bms_profile_candidate_t& candidate) -> bool {
    if (!chosen) {
        chosen = &candidate;
        return true;
    }
    if (primary_channel_families(chosen->profile.layout) !=
        primary_channel_families(candidate.profile.layout)) return false;
    if (candidate.profile.layout.lanes.size() == chosen->profile.layout.lanes.size()) return false;
    // Identical primary channels with unused auxiliary lanes prefer the smaller layout.
    if (candidate.profile.layout.lanes.size() < chosen->profile.layout.lanes.size())
        chosen = &candidate;
    return true;
}

auto select_complete_primary_profile(std::span<const bms_lane_profile_descriptor_t> specs,
                                     const std::vector<const bms_raw_channel_t*>& channels,
                                     const bms_profile_detection_request_t& request,
                                     std::string_view extension,
                                     bms_profile_detection_result_t& result) -> bool {
    const auto observed = observed_channel_families(channels);
    bms_profile_candidate_t* chosen = nullptr;
    for (const auto& spec : specs) {
        if (!primary_detection_hints_match(spec, request, extension)) continue;
        // Free-zone and pedal variants are identical on their primary lanes.
        // Use the catalog's ordinary variant only while every auxiliary channel is unused.
        if (std::ranges::any_of(spec.profile.layout.lanes, [](const auto& lane) {
            return lane.role == lane_role_t::foot_pedal;
        })) continue;
        auto* candidate = find_candidate(result.candidates, spec.profile.layout.profile_id);
        if (!candidate || !complete_primary_family_match(candidate->profile.layout, observed)) continue;
        if (!choose_primary_candidate(chosen, *candidate)) return false;
    }
    if (!chosen) return false;
    chosen->confidence = std::max(chosen->confidence, 0.95);
    chosen->evidence.push_back({
        .source = bms_detection_evidence_source_t::used_channel,
        .message = "A complete standard primary channel family matches the player header; "
                   "unused auxiliary channels do not require a different interpretation.",
        .weight = 0.30,
    });
    select_candidate(result, *chosen, chosen->confidence);
    return true;
}

void append_custom_required_evidence(bms_profile_detection_result_t& result,
                                     bool no_playable_channels) {
    result.status = bms_profile_detection_status_t::custom_required;
    result.evidence.push_back({
        .source = bms_detection_evidence_source_t::used_channel,
        .message = no_playable_channels
                       ? "No recognized playable-family channels identify a built-in lane profile."
                       : "No built-in profile maps every recognized playable-family channel; the raw "
                         "custom map is retained.",
        .weight = 0.0,
    });
}

void decide_automatic_profile(bms_profile_detection_result_t& result) {
    std::ranges::sort(result.candidates, [](const auto& left, const auto& right) {
        if (left.confidence != right.confidence) {
            return left.confidence > right.confidence;
        }
        return left.profile.layout.profile_id < right.profile.layout.profile_id;
    });
    if (result.candidates.empty()) {
        append_custom_required_evidence(result, false);
        return;
    }
    const auto& best = result.candidates.front();
    const auto second = result.candidates.size() > 1 ? result.candidates[1].confidence : 0.0;
    const auto margin = best.confidence - second;
    const auto enough = result.candidates.size() == 1 ? best.confidence >= 0.72
                                                      : best.confidence >= 0.80 && margin >= 0.18;
    result.confidence = best.confidence;
    result.evidence = best.evidence;
    if (enough && best.automatic_selection_eligible) {
        select_candidate(result, best, best.confidence);
        return;
    }
    result.status = bms_profile_detection_status_t::ambiguous;
    result.evidence.push_back({
        .source = bms_detection_evidence_source_t::used_channel,
        .message = !best.automatic_selection_eligible
                       ? "The best channel match is a special dialect without named or explicit "
                         "evidence; the custom raw-channel map is retained for review."
                       : "Several profile interpretations remain plausible; the custom raw "
                         "channel map is retained for review.",
        .weight = 0.0,
    });
}

} // namespace

auto detect_bms_lane_profile(const bms_profile_detection_request_t& request)
    -> bms_profile_detection_result_t {
    auto result = bms_profile_detection_result_t{};
    result.custom_fallback.channels.reserve(request.used_channels.size());
    for (std::size_t index = 0; index < request.used_channels.size(); ++index) {
        result.custom_fallback.channels.push_back(
            observe_channel(index, request.used_channels[index]));
    }
    const auto extension = normalize_extension(request.file_extension);
    const auto channels = playable_channels(result);
    if (channels.empty() && !request.explicit_profile_id) {
        result.evidence.push_back({
            .source = bms_detection_evidence_source_t::used_channel,
            .message = "No recognized playable-family channels identify a lane layout; extension "
                       "and header hints alone do not select one.",
            .weight = 0.0,
        });
        result.status = bms_profile_detection_status_t::custom_required;
        return result;
    }
    const auto specs = built_in_bms_lane_profiles();
    auto candidates = make_candidates(specs, channels);
    score_candidates(specs, channels, extension, request, candidates, result);
    if (select_explicit_profile(request, result)) {
        return result;
    }
    if (select_octave_profile(request, result)) {
        return result;
    }
    if (select_complete_primary_profile(specs, channels, request, extension, result)) {
        return result;
    }
    decide_automatic_profile(result);
    return result;
}

} // namespace wormedit
