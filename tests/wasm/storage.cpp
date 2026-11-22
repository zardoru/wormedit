#include "core/browser_file_publication.h"

#include <array>
#include <barrier>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace {
auto read(const std::filesystem::path& path) -> std::string {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

auto check_directory_publication() -> bool {
    namespace fs = std::filesystem;
    const fs::path staging{"/tmp/staged-directory"}, destination{"/tmp/published-directory"};
    fs::create_directory(staging);
    std::ofstream{staging / "payload"} << "first directory";
    if (wormedit::platform::browser_move_no_replace(staging, destination) != 0) return false;
    fs::create_directory(staging);
    std::ofstream{staging / "payload"} << "replacement directory";
    if (wormedit::platform::browser_move_no_replace(staging, destination) != -1 || errno != EEXIST)
        return false;
    if (read(destination / "payload") != "first directory") return false;
    if (!wormedit::platform::browser_exchange_directories(staging, destination)) return false;
    return read(destination / "payload") == "replacement directory" &&
        read(staging / "payload") == "first directory";
}
} // namespace

int main() {
    const std::array<std::filesystem::path, 2> sources{ "/tmp/first", "/tmp/second" };
    const std::filesystem::path destination{"/tmp/exclusive"};
    std::ofstream{sources[0]} << "complete first payload";
    std::ofstream{sources[1]} << "complete second payload";
    std::array<int, 2> results{}, errors{};
    std::barrier start{3};
    const auto publish = [&](std::size_t index) {
        start.arrive_and_wait();
        results[index] = wormedit::platform::browser_move_no_replace(sources[index], destination);
        errors[index] = errno;
    };
    std::jthread first{publish, 0};
    std::jthread second{publish, 1};
    start.arrive_and_wait();
    first.join();
    second.join();
    const auto first_won = results[0] == 0;
    const auto winner = first_won ? 0U : 1U;
    const auto loser = 1U - winner;
    const auto expected = first_won ? "complete first payload" : "complete second payload";
    const auto correct_race = first_won != (results[1] == 0) && errors[loser] == EEXIST &&
        read(destination) == expected && std::filesystem::exists(sources[loser]);
    const auto refused = wormedit::platform::browser_move_no_replace(sources[loser], destination);
    const auto preserved = refused == -1 && errno == EEXIST && read(destination) == expected &&
        std::filesystem::exists(sources[loser]);
    if (!correct_race || !preserved || !check_directory_publication()) {
        MAIN_THREAD_EM_ASM({ Module.storageCheckFailed = true; });
        std::puts("FAIL: browser no-replace publication");
        return 1;
    }
    MAIN_THREAD_EM_ASM({ Module.storageCheckPassed = true; });
    std::puts("PASS: concurrent browser publication has one complete winner and preserves the existing destination");
    return 0;
}
