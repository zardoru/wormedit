#include <wormedit/core/sound_group_proposal.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace wormedit;

TEST_CASE("sound_group_proposal collect numbered stems across sources") {
    const auto result = propose_sound_groups(
        {"/one/kick_001.wav", "snare-01.ogg", "C:\\two\\kick.002.wav", "snare-02.ogg",
         "hihat 01.wav", "hihat 02.wav", "loose.wav"}, {});

    REQUIRE(result.groups.size() == 3);
    CHECK(result.groups[0].name == "hihat");
    CHECK(result.groups[1].name == "kick");
    CHECK(result.groups[2].name == "snare");
    CHECK(result.groups[1].members == std::vector<std::string>{
        "C:\\two\\kick.002.wav", "/one/kick_001.wav"});
    CHECK(result.ungrouped_names == std::vector<std::string>{"loose.wav"});
}

TEST_CASE("sound_group_proposal use natural ordering and leave lone numbered names loose") {
    const auto result = propose_sound_groups(
        {"snd12.wav", "snd2.wav", "snd10.wav", "snd1.wav", "snd_fx.wav", "solo_001.wav"}, {});

    REQUIRE(result.groups.size() == 1);
    CHECK(result.groups.front().name == "snd");
    CHECK(result.groups.front().members == std::vector<std::string>{
        "snd1.wav", "snd2.wav", "snd10.wav", "snd12.wav"});
    CHECK(result.ungrouped_names == std::vector<std::string>{"snd_fx.wav", "solo_001.wav"});
}

TEST_CASE("sound_group_proposal join an existing group and fold case") {
    const auto groups = std::vector<sound_group_t>{{.id = sound_group_id_t{7}, .name = "Drums"}};
    const auto result = propose_sound_groups(
        {"DRUMS_02.wav", "drums_01.wav", "Drums.wav", "KiCk_01.wav", "kick_02.wav"}, groups);

    REQUIRE(result.groups.size() == 2);
    CHECK(result.groups[0].name == "Drums");
    CHECK(result.groups[0].existing_group == sound_group_id_t{7});
    CHECK(result.groups[0].members == std::vector<std::string>{
        "Drums.wav", "drums_01.wav", "DRUMS_02.wav"});
    CHECK(result.groups[1].name == "KiCk");
    CHECK_FALSE(result.groups[1].existing_group.has_value());
}

TEST_CASE("sound_group_proposal handles 1300 names efficiently") {
    auto names = std::vector<std::string>{};
    names.reserve(1300);
    for (auto index = 0; index < 1300; ++index) {
        names.push_back("stem_" + std::to_string(index + 1) + ".wav");
    }

#ifdef NDEBUG
    const auto start = std::chrono::steady_clock::now();
#endif
    const auto result = propose_sound_groups(names, {});
#ifdef NDEBUG
    const auto elapsed = std::chrono::steady_clock::now() - start;
#endif

    REQUIRE(result.groups.size() == 1);
    CHECK(result.groups.front().members.size() == 1300);
#ifdef NDEBUG
    CHECK(elapsed < std::chrono::milliseconds{10});
#endif
}

TEST_CASE("sound_group_proposal are empty and permutation deterministic") {
    const auto empty = propose_sound_groups({}, {});
    CHECK(empty.groups.empty());
    CHECK(empty.ungrouped_names.empty());

    const auto first = propose_sound_groups(
        {"snd10.wav", "SND2.wav", "snd1.wav", "single_001.wav", "001.wav"}, {});
    const auto second = propose_sound_groups(
        {"001.wav", "single_001.wav", "snd1.wav", "SND2.wav", "snd10.wav"}, {});
    REQUIRE(first.groups.size() == 1);
    CHECK(first.groups == second.groups);
    CHECK(first.ungrouped_names == second.ungrouped_names);
    CHECK(first.ungrouped_names == std::vector<std::string>{"001.wav", "single_001.wav"});
}

TEST_CASE("sound_group_proposal members use natural filenames before directory names") {
    const auto result = propose_sound_groups(
        {"/z/stem2.wav", "/a/stem10.wav", "/m/stem1.wav",
         "/a/zeta.wav", "/z/alpha.wav"}, {});

    REQUIRE(result.groups.size() == 1);
    CHECK(result.groups.front().members == std::vector<std::string>{
        "/m/stem1.wav", "/z/stem2.wav", "/a/stem10.wav"});
    CHECK(result.ungrouped_names == std::vector<std::string>{"/z/alpha.wav", "/a/zeta.wav"});
}
