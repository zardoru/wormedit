#include <wormedit/core/bms_profile_detection.h>

#include <algorithm>
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

auto clamp_confidence(double value) -> double {
    return std::clamp(value, 0.0, 1.0);
}

auto profile_evidence(const bms_lane_profile_descriptor_t& spec, std::string_view extension,
                      std::optional<unsigned int> player_header, bool octave_fp_header,
                      std::size_t compatible_count, std::size_t playable_channel_count)
    -> std::pair<double, std::vector<bms_profile_evidence_t>> {
    auto score = 0.55;
    auto evidence = std::vector<bms_profile_evidence_t>{};

    if (extension_is_known(extension)) {
        auto weight = 0.0;
        if (extension == ".PMS") {
            weight = spec.family == bms_lane_profile_family_t::popn ? 0.18 : -0.18;
        } else if (extension == ".BMS") {
            if (spec.family == bms_lane_profile_family_t::bms_five_or_ten_key) {
                weight = 0.12;
            } else if (spec.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key) {
                weight = 0.04;
            }
        } else if (extension == ".BME") {
            if (spec.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key) {
                weight = 0.14;
            } else if (spec.family == bms_lane_profile_family_t::bms_five_or_ten_key) {
                weight = 0.02;
            }
        } else if (extension == ".BML") {
            if (spec.family == bms_lane_profile_family_t::bme_seven_or_fourteen_key) {
                weight = 0.08;
            } else if (spec.family == bms_lane_profile_family_t::bms_five_or_ten_key) {
                weight = 0.05;
            }
        }
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

    if (octave_fp_header) {
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
            .message =
                "This is the only built-in candidate that maps every recognized playable channel.",
            .weight = 0.14,
        });
    } else if (compatible_count > 1) {
        const auto ambiguity_weight =
            -std::min(0.16, 0.025 * static_cast<double>(compatible_count - 1));
        score += ambiguity_weight;
        evidence.push_back({
            .source = bms_detection_evidence_source_t::used_channel,
            .message = std::to_string(compatible_count) +
                       " built-in candidates map all recognized playable channels.",
            .weight = ambiguity_weight,
        });
    }
    return {clamp_confidence(score), std::move(evidence)};
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
    const auto raw_playable_channels = [&] {
        std::vector<const bms_raw_channel_t*> channels;
        for (const auto& channel : result.custom_fallback.channels) {
            if (is_playable_family(channel.kind)) {
                channels.push_back(&channel);
            }
        }
        return channels;
    }();

    if (raw_playable_channels.empty() && !request.explicit_profile_id) {
        result.status = bms_profile_detection_status_t::custom_required;
        result.evidence.push_back({
            .source = bms_detection_evidence_source_t::used_channel,
            .message = "No recognized playable-family channels identify a lane layout; extension "
                       "and header hints alone do not select one.",
            .weight = 0.0,
        });
        return result;
    }

    const auto specs = built_in_bms_lane_profiles();
    auto all_candidates = std::vector<bms_profile_candidate_t>{};
    all_candidates.reserve(specs.size());
    for (const auto& spec : specs) {
        auto candidate = bms_profile_candidate_t{.profile = spec.profile};
        for (const auto* channel : raw_playable_channels) {
            const auto canonical = channel->canonical_spelling.value_or(channel->raw_spelling);
            if (!profile_maps_channel(candidate.profile.layout, canonical)) {
                candidate.unmapped_playable_channels.push_back(channel->raw_spelling);
            }
        }
        candidate.maps_all_playable_channels = candidate.unmapped_playable_channels.empty();
        all_candidates.push_back(std::move(candidate));
    }

    const auto compatible_count =
        static_cast<std::size_t>(std::ranges::count_if(all_candidates, [](const auto& candidate) {
            return candidate.maps_all_playable_channels;
        }));
    for (std::size_t index = 0; index < all_candidates.size(); ++index) {
        auto& candidate = all_candidates[index];
        if (!candidate.maps_all_playable_channels) {
            continue;
        }
        auto& spec = specs[index];
        auto [confidence, evidence] =
            profile_evidence(spec, extension, request.player_header, request.octave_fp_header,
                             compatible_count, raw_playable_channels.size());
        candidate.confidence = confidence;
        candidate.evidence = std::move(evidence);
        result.candidates.push_back(std::move(candidate));
    }

    const auto choose_candidate =
        [&result](std::string_view profile_id) -> bms_profile_candidate_t* {
        const auto iterator =
            std::ranges::find_if(result.candidates, [profile_id](const auto& item) {
                return item.profile.layout.profile_id == profile_id;
            });
        return iterator == result.candidates.end() ? nullptr : &*iterator;
    };

    if (request.explicit_profile_id) {
        if (auto* chosen = choose_candidate(*request.explicit_profile_id)) {
            chosen->confidence = 1.0;
            chosen->evidence.push_back({
                .source = bms_detection_evidence_source_t::explicit_profile,
                .message = "An explicit project/user profile choice matches all recognized "
                           "playable channels.",
                .weight = 1.0,
            });
            result.status = bms_profile_detection_status_t::selected;
            result.confidence = 1.0;
            result.selected_profile = chosen->profile;
            result.evidence = chosen->evidence;
            return result;
        }
        result.evidence.push_back({
            .source = bms_detection_evidence_source_t::explicit_profile,
            .message = "The explicit profile is unknown or does not map every recognized playable "
                       "channel; raw channels remain in the custom fallback.",
            .weight = 0.0,
        });
        result.status = bms_profile_detection_status_t::custom_required;
        result.confidence = 0.0;
        return result;
    }

    if (request.octave_fp_header) {
        if (auto* chosen = choose_candidate("bms-octave-foot-pedal")) {
            result.status = bms_profile_detection_status_t::selected;
            result.confidence = std::max(chosen->confidence, 0.95);
            result.selected_profile = chosen->profile;
            result.evidence = chosen->evidence;
            return result;
        }
        result.evidence.push_back({
            .source = bms_detection_evidence_source_t::octave_fp_header,
            .message = "#OCT/FP was present, but its named profile does not map every recognized "
                       "playable channel.",
            .weight = 0.0,
        });
    }

    std::ranges::sort(result.candidates, [](const auto& left, const auto& right) {
        if (left.confidence != right.confidence) {
            return left.confidence > right.confidence;
        }
        return left.profile.layout.profile_id < right.profile.layout.profile_id;
    });

    if (!result.candidates.empty()) {
        const auto& best = result.candidates.front();
        const auto second_confidence =
            result.candidates.size() > 1 ? result.candidates[1].confidence : 0.0;
        const auto margin = best.confidence - second_confidence;
        const auto enough_evidence = result.candidates.size() == 1
                                         ? best.confidence >= 0.72
                                         : best.confidence >= 0.80 && margin >= 0.18;
        result.confidence = best.confidence;
        result.evidence = best.evidence;
        if (enough_evidence) {
            result.status = bms_profile_detection_status_t::selected;
            result.selected_profile = best.profile;
        } else {
            result.status = bms_profile_detection_status_t::ambiguous;
            result.evidence.push_back({
                .source = bms_detection_evidence_source_t::used_channel,
                .message = "Several profile interpretations remain plausible; the custom "
                           "raw-channel map is retained for review.",
                .weight = 0.0,
            });
        }
    } else {
        result.status = bms_profile_detection_status_t::custom_required;
        result.evidence.push_back({
            .source = bms_detection_evidence_source_t::used_channel,
            .message =
                raw_playable_channels.empty()
                    ? "No recognized playable-family channels identify a built-in lane profile."
                    : "No built-in profile maps every recognized playable-family channel; the raw "
                      "custom map is retained.",
            .weight = 0.0,
        });
    }
    return result;
}

} // namespace wormedit
