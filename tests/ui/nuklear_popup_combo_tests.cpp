#define NK_IMPLEMENTATION
#include "ui/nuklear/chrome.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace {
void expect_combo(bool value, std::source_location where = std::source_location::current()) {
    INFO("combo check caller line " << where.line());
    REQUIRE(value);
}

struct combo_context_t {
    nk_user_font font{};
    nk_context context{};
    combo_context_t() {
        font.height = 14;
        font.width = [](nk_handle, float, const char*, int count) { return count * 7.0f; };
        expect_combo(nk_init_default(&context, &font));
    }
    ~combo_context_t() { nk_free(&context); }
};

void input_frame(nk_context& context, struct nk_vec2 point, bool down) {
    nk_input_begin(&context);
    nk_input_motion(&context, static_cast<int>(point.x), static_cast<int>(point.y));
    nk_input_button(&context, NK_BUTTON_LEFT, static_cast<int>(point.x),
                     static_cast<int>(point.y), down);
    nk_input_end(&context);
}

auto text_position(nk_context& context, std::string_view label) -> struct nk_vec2 {
    const nk_command* command;
    nk_foreach(command, &context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        if (std::string_view(text->string, static_cast<std::size_t>(text->length)) == label)
            return nk_vec2(text->x + text->w * 0.5f, text->y + text->h * 0.5f);
    }
    expect_combo(false);
    return nk_vec2(0, 0);
}

auto popup_combo_frame(nk_context& context, int selected) -> int {
    expect_combo(nk_begin(&context, "Host", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    expect_combo(nk_popup_begin(&context, NK_POPUP_STATIC, "Popup", NK_WINDOW_NO_SCROLLBAR,
                               nk_rect(20, 20, 250, 180)));
    expect_combo(wormedit::ui::chrome::popup_panel(&context));
    const char* choices[]{"First", "Second", "Third"};
    nk_layout_row_dynamic(&context, 24, 1);
    const auto next = wormedit::ui::chrome::safe_combo(
        &context, choices, 3, selected, 24, nk_vec2(200, 150));
    nk_popup_end(&context);
    nk_end(&context);
    return next;
}

auto ordinary_combo_frame(nk_context& context) -> bool {
    expect_combo(nk_begin(&context, "Ordinary", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    const char* choices[]{"First", "Second", "Third"};
    nk_layout_row_dynamic(&context, 24, 1);
    static_cast<void>(wormedit::ui::chrome::safe_combo(
        &context, choices, 3, 0, 24, nk_vec2(200, 150)));
    const bool open = context.current->popup.active != 0;
    nk_end(&context);
    return open;
}

auto unguarded_dropdown_files() -> std::vector<std::string> {
    const auto directory = std::filesystem::path(WORMEDIT_SOURCE_ROOT) / "src/ui/nuklear";
    const std::regex direct_call(R"(\bnk_(?:combo(?:_begin(?:_[A-Za-z0-9]+)?|_string|_separator|_callback)?|combobox(?:_[A-Za-z0-9]+)?)\s*\()");
    std::vector<std::string> unexpected;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        const auto path = entry.path();
        if (!entry.is_regular_file() || path.filename() == "chrome.h") continue;
        if (path.extension() != ".cpp" && path.extension() != ".h") continue;
        std::ifstream file(path);
        const std::string source{std::istreambuf_iterator<char>(file), {}};
        if (std::regex_search(source, direct_call)) unexpected.push_back(path.string());
    }
    return unexpected;
}
}

TEST_CASE("Popup combo guard selects inline choices without creating a nested popup", "[unit][frontend][popup-combo]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    auto selected = popup_combo_frame(context, 0);
    const auto third = text_position(context, "Third");
    nk_clear(&context);
    input_frame(context, third, true);
    selected = popup_combo_frame(context, selected);
    nk_clear(&context);
    input_frame(context, third, false);
    selected = popup_combo_frame(context, selected);
    expect_combo(selected == 2);
}

TEST_CASE("Ordinary window combo guard keeps the native dropdown", "[unit][frontend][popup-combo]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    expect_combo(!ordinary_combo_frame(context));
    const auto first = text_position(context, "First");
    nk_clear(&context);
    input_frame(context, first, true);
    static_cast<void>(ordinary_combo_frame(context));
    nk_clear(&context);
    input_frame(context, first, false);
    expect_combo(ordinary_combo_frame(context));
}

TEST_CASE("Popup label-combo guard rejects nesting safely", "[unit][frontend][popup-combo]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    expect_combo(nk_begin(&context, "Host", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    expect_combo(nk_popup_begin(&context, NK_POPUP_STATIC, "Popup", NK_WINDOW_NO_SCROLLBAR,
                               nk_rect(20, 20, 250, 180)));
    nk_layout_row_dynamic(&context, 24, 1);
    expect_combo(!wormedit::ui::chrome::safe_combo_begin_label(&context, "Retained choice", nk_vec2(200, 150)));
    nk_popup_end(&context);
    nk_end(&context);
    static_cast<void>(text_position(context, "Retained choice"));
}

TEST_CASE("Nuklear dropdown entry points are owned only by the central chrome guard", "[unit][frontend][popup-combo][architecture]") {
    const auto unexpected = unguarded_dropdown_files();
    for (const auto& path : unexpected) INFO(path);
    expect_combo(unexpected.empty());
}
