#include <wormedit/core/bms_profile_detection.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace wormedit;

namespace {

auto has_evidence(const std::vector<bms_profile_evidence_t>& evidence,
                  bms_detection_evidence_source_t source) -> bool {
    return std::ranges::any_of(evidence,
                               [source](const auto& item) { return item.source == source; });
}

auto find_candidate(const bms_profile_detection_result_t& result, std::string_view profile_id)
    -> const bms_profile_candidate_t* {
    const auto iterator = std::ranges::find_if(result.candidates, [profile_id](const auto& item) {
        return item.profile.layout.profile_id == profile_id;
    });
    return iterator == result.candidates.end() ? nullptr : &*iterator;
}

} // namespace

TEST_CASE("BMS profile detection keeps sparse lane layouts ambiguous",
          "[unit][core][bms-detection]") {
    const auto result = detect_bms_lane_profile({
        .file_extension = ".bms",
        .player_header = 1,
        .used_channels = {"11", "12", "16"},
    });

    CHECK(result.status == bms_profile_detection_status_t::ambiguous);
    CHECK_FALSE(result.selected_profile);
    CHECK(result.candidates.size() > 1);
    CHECK(has_evidence(result.evidence, bms_detection_evidence_source_t::used_channel));
    CHECK(result.custom_fallback.channels.size() == 3);
    CHECK(result.custom_fallback.channels[0].raw_spelling == "11");
    CHECK(result.custom_fallback.channels[1].raw_spelling == "12");
    CHECK(result.custom_fallback.channels[2].raw_spelling == "16");
}

TEST_CASE(
    "extension and player-header evidence changes candidate confidence without hiding ambiguity",
    "[unit][core][bms-detection]") {
    const auto bms = detect_bms_lane_profile({
        .file_extension = ".bms",
        .player_header = 1,
        .used_channels = {"18"},
    });
    const auto bme = detect_bms_lane_profile({
        .file_extension = ".bme",
        .player_header = 1,
        .used_channels = {"18"},
    });
    const auto double_play = detect_bms_lane_profile({
        .file_extension = ".bme",
        .player_header = 3,
        .used_channels = {"18"},
    });

    const auto* bms_candidate = find_candidate(bms, "bme-7k-outside-scratch");
    const auto* bme_candidate = find_candidate(bme, "bme-7k-outside-scratch");
    const auto* player_candidate = find_candidate(double_play, "bme-7k-outside-scratch");
    REQUIRE(bms_candidate);
    REQUIRE(bme_candidate);
    REQUIRE(player_candidate);
    CHECK(bme_candidate->confidence > bms_candidate->confidence);
    CHECK(bme_candidate->confidence > player_candidate->confidence);
    CHECK(bme.status == bms_profile_detection_status_t::ambiguous);
    CHECK(double_play.status == bms_profile_detection_status_t::ambiguous);
    CHECK(has_evidence(bme_candidate->evidence, bms_detection_evidence_source_t::file_extension));
    CHECK(has_evidence(player_candidate->evidence, bms_detection_evidence_source_t::player_header));
}

TEST_CASE("explicit profile choice resolves a supported mapping without discarding raw spelling",
          "[unit][core][bms-detection]") {
    const auto result = detect_bms_lane_profile({
        .file_extension = "bme",
        .player_header = 1,
        .used_channels = {"11", "31", "56", "d9"},
        .explicit_profile_id = "bme-7k-outside-scratch",
    });

    REQUIRE(result.status == bms_profile_detection_status_t::selected);
    REQUIRE(result.selected_profile);
    CHECK(result.selected_profile->layout.profile_id == "bme-7k-outside-scratch");
    CHECK(result.confidence == 1.0);
    CHECK(has_evidence(result.evidence, bms_detection_evidence_source_t::explicit_profile));
    REQUIRE(result.custom_fallback.channels.size() == 4);
    CHECK(result.custom_fallback.channels[3].raw_spelling == "d9");
    CHECK(result.custom_fallback.channels[3].canonical_spelling == "D9");
    CHECK(result.custom_fallback.channels[3].kind == bms_raw_channel_kind_t::mine);
}

TEST_CASE("every catalogued BMS profile is selectable through the shared detector registry",
          "[unit][core][bms-detection][registry]") {
    for (const auto& descriptor : built_in_bms_lane_profiles()) {
        std::vector<std::string> channels;
        for (const auto& lane : descriptor.profile.layout.lanes) {
            for (const auto* channel : {&lane.bms_channels.visible, &lane.bms_channels.invisible,
                                        &lane.bms_channels.long_note, &lane.bms_channels.mine}) {
                if (*channel)
                    channels.push_back(**channel);
            }
        }
        CAPTURE(descriptor.profile.layout.profile_id);
        const auto result = detect_bms_lane_profile({
            .used_channels = std::move(channels),
            .explicit_profile_id = descriptor.profile.layout.profile_id,
        });
        REQUIRE(result.status == bms_profile_detection_status_t::selected);
        REQUIRE(result.selected_profile);
        CHECK(result.selected_profile->layout.profile_id == descriptor.profile.layout.profile_id);
        CHECK(result.confidence == 1.0);
        CHECK(has_evidence(result.evidence, bms_detection_evidence_source_t::explicit_profile));
    }
}

TEST_CASE("octave foot-pedal header is strong evidence but channel conflicts retain custom data",
          "[unit][core][bms-detection]") {
    const auto identified = detect_bms_lane_profile({
        .file_extension = ".bms",
        .player_header = 3,
        .octave_fp_header = true,
        .used_channels = {"16", "11", "18", "22", "26", "21"},
    });
    REQUIRE(identified.status == bms_profile_detection_status_t::selected);
    REQUIRE(identified.selected_profile);
    CHECK(identified.selected_profile->layout.profile_id == "bms-octave-foot-pedal");
    CHECK(identified.confidence >= 0.95);
    CHECK(has_evidence(identified.evidence, bms_detection_evidence_source_t::octave_fp_header));

    const auto conflict = detect_bms_lane_profile({
        .file_extension = ".bms",
        .octave_fp_header = true,
        .used_channels = {"1Z"},
    });
    CHECK(conflict.status == bms_profile_detection_status_t::custom_required);
    CHECK_FALSE(conflict.selected_profile);
    REQUIRE(conflict.custom_fallback.channels.size() == 1);
    CHECK(conflict.custom_fallback.channels[0].raw_spelling == "1Z");
}

TEST_CASE("unknown, reserved, malformed, and repeated channel spellings stay lossless",
          "[unit][core][bms-detection][negative]") {
    const auto input = std::vector<std::string>{"01", "d1", "D1", "A?", "XYZ", "3Z"};
    const auto result = detect_bms_lane_profile({.used_channels = input});

    CHECK(result.status == bms_profile_detection_status_t::custom_required);
    CHECK_FALSE(result.selected_profile);
    REQUIRE(result.custom_fallback.channels.size() == input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        CHECK(result.custom_fallback.channels[index].source_order == index);
        CHECK(result.custom_fallback.channels[index].raw_spelling == input[index]);
    }
    CHECK(result.custom_fallback.channels[0].kind == bms_raw_channel_kind_t::non_playable);
    CHECK(result.custom_fallback.channels[1].canonical_spelling == "D1");
    CHECK(result.custom_fallback.channels[1].kind == bms_raw_channel_kind_t::mine);
    CHECK(result.custom_fallback.channels[2].raw_spelling == "D1");
    CHECK(result.custom_fallback.channels[3].kind == bms_raw_channel_kind_t::unknown_or_reserved);
    CHECK_FALSE(result.custom_fallback.channels[3].canonical_spelling);
    CHECK(result.custom_fallback.channels[4].kind == bms_raw_channel_kind_t::unknown_or_reserved);
    CHECK(result.custom_fallback.channels[5].kind == bms_raw_channel_kind_t::invisible_note);
}

TEST_CASE("extension and player headers alone do not invent a lane layout",
          "[unit][core][bms-detection][negative]") {
    const auto result = detect_bms_lane_profile({
        .file_extension = ".pms",
        .player_header = 3,
        .used_channels = {"01", "A?", "XYZ"},
    });

    CHECK(result.status == bms_profile_detection_status_t::custom_required);
    CHECK_FALSE(result.selected_profile);
    CHECK(result.candidates.empty());
    CHECK(result.custom_fallback.channels.size() == 3);
    CHECK(has_evidence(result.evidence, bms_detection_evidence_source_t::used_channel));
}

TEST_CASE("a contradictory explicit profile blocks automatic reinterpretation",
          "[unit][core][bms-detection][negative]") {
    const auto result = detect_bms_lane_profile({
        .file_extension = ".bms",
        .player_header = 1,
        .used_channels = {"11", "12", "16", "18", "19"},
        .explicit_profile_id = "bms-5k-outside-scratch-free-zone",
    });

    CHECK(result.status == bms_profile_detection_status_t::custom_required);
    CHECK_FALSE(result.selected_profile);
    CHECK(has_evidence(result.evidence, bms_detection_evidence_source_t::explicit_profile));
    CHECK(result.custom_fallback.channels.size() == 5);
}
