#include "nuklear_event_harness.h"
#include "ui/nuklear/audio/audio_state.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <source_location>

namespace {
template<class T>
void expect(const T& condition, std::source_location at = std::source_location::current()) {
    INFO("Sounds expectation at " << at.line());
    REQUIRE(static_cast<bool>(condition));
}

auto key(SDL_Keycode code, SDL_Scancode scan, bool down) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; event.key.down = down;
    event.key.key = code; event.key.scancode = scan;
    return event;
}
auto pointer(float x, float y, bool down) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP; event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down; event.button.x = x; event.button.y = y;
    return event;
}
struct fixture_t {
    nuklear_event_harness_t harness;
    std::array<wormedit::ui::sound_row_view_t, 3> rows{{
        {.resource_id=wormedit::resource_id_t{101}, .asset_id=wormedit::asset_id_t{11}, .name="Kick", .kind=static_cast<wormedit::sound_kind_t>(2)},
        {.resource_id=wormedit::resource_id_t{102}, .asset_id=wormedit::asset_id_t{22}, .name="Snare", .kind=static_cast<wormedit::sound_kind_t>(2)},
        {.resource_id=wormedit::resource_id_t{103}, .asset_id=wormedit::asset_id_t{33}, .name="Missing", .kind=static_cast<wormedit::sound_kind_t>(2), .missing=true}}};
    std::array<wormedit::ui::audio_waveform_column_view_t, 8> columns{};
    std::size_t release_shortcuts{}, release_compose_inputs{};
    explicit fixture_t(int width = 1280, int height = 900) : harness{width, height} {
        auto& workspace = harness.view();
        workspace.active_tab = WORM_UI_WORKSPACE_TAB_AUDIO_SLICES;
        workspace.compose.visible = false;
        auto& view = workspace.audio_slices;
        view.visible = true; view.usage_song_id=workspace.project_id;
        view.document_revision=workspace.document_revision;
        view.has_chart=true; view.chart_id=workspace.selected_chart_id; view.offset_denominator=1;
        view.sound_rows.assign(rows.data(), rows.data() + rows.size());
        view.has_selected_asset=true; view.selected_asset_id=wormedit::asset_id_t{11}; view.selected_resource_id=wormedit::resource_id_t{101};
        view.waveform_available=true; view.total_frames=480000; view.sample_rate=48000;
        view.waveform_columns.assign(columns.data(), columns.data() + columns.size());
        view.waveform_end_frame=view.total_frames;
        (void)harness.frame(); (void)harness.frame();
    }
    auto click(float x, float y) -> workspace_action_snapshot_t {
        SDL_Event motion{}; motion.type=SDL_EVENT_MOUSE_MOTION;
        motion.motion.x=x; motion.motion.y=y;
        (void)harness.frame({&motion,1});
        const auto down=pointer(x,y,true); auto result=harness.frame({&down,1});
        const auto up=pointer(x,y,false); auto released=harness.frame({&up,1});
        return released.get().kind != WORM_UI_WORKSPACE_ACTION_NONE || released.get().sound_properties_apply
            ? released : result;
    }
    auto press(SDL_Keycode code, SDL_Scancode scan) -> workspace_action_snapshot_t {
        const auto down=key(code,scan,true); auto result=harness.frame({&down,1});
        const auto up=key(code,scan,false); const auto released=harness.frame({&up,1});
        release_shortcuts=released.get().shortcut_events.size();
        release_compose_inputs=released.get().compose_inputs.size();
        return result;
    }
    void focus_list() {
        const auto bounds=harness.drawn_text_center("Kick  ·  Keysound");
        expect(bounds); (void)click(bounds->first,bounds->second);
        expect(worm_ui_nuklear_audio_state().sound_list_focused);
    }
};
}

TEST_CASE("Sounds keyboard selection and audition use resource identity and capture releases",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture;
    fixture.focus_list();
    auto result=fixture.press(SDLK_DOWN,SDL_SCANCODE_DOWN);
    expect(result.get().audio_resource_id.value==102);
    expect(worm_ui_nuklear_audio_state().selected_resource_id==102);
    result=fixture.press(SDLK_SPACE,SDL_SCANCODE_SPACE);
    expect(result.get().compose_action==WORM_UI_COMPOSE_ACTION_AUDITION_SOUND);
    expect(result.get().compose_resource_id.value==102);
    expect(fixture.release_shortcuts==0);
    expect(fixture.release_compose_inputs==0);
    expect(result.get().compose_input_document_revision==fixture.harness.view().document_revision);
    result=fixture.press(SDLK_DOWN,SDL_SCANCODE_DOWN);
    expect(result.get().audio_resource_id.value==103);
    expect(fixture.press(SDLK_SPACE,SDL_SCANCODE_SPACE).get().compose_action==WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Sounds waveform zoom retains pointer anchor and asset change resets view",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture;
    auto& state=worm_ui_nuklear_audio_state();
    const auto before=state.waveform_viewport;
    const auto plus=fixture.harness.drawn_text_center("+"); expect(plus);
    (void)fixture.click(plus->first,plus->second);
    expect(state.waveform_viewport.frames_per_pixel<before.frames_per_pixel);
    expect(state.waveform_viewport.begin_frame>0);
    wormedit::ui::audio_waveform_viewport_request_t request{};
    worm_ui_nuklear_audio_waveform_viewport_request(request);
    expect(request.valid); expect(request.asset_id==wormedit::asset_id_t{11});
    expect(request.begin_frame==state.waveform_viewport.begin_frame);
    fixture.harness.view().audio_slices.selected_asset_id=wormedit::asset_id_t{22};
    (void)fixture.harness.frame();
    expect(state.waveform_viewport.begin_frame==0);
    expect(state.waveform_asset_id==22);
}

TEST_CASE("Sounds properties are staged, revertible and stamped before Apply",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture;
    auto& state=worm_ui_nuklear_audio_state();
    std::strcpy(state.properties_name,"New kick"); state.properties_kind=3;
    (void)fixture.harness.frame();
    const auto revert=fixture.harness.drawn_text_center("Revert"); expect(revert);
    (void)fixture.click(revert->first,revert->second);
    expect(std::string_view{state.properties_name}=="Kick"); expect(state.properties_kind==2);
    std::strcpy(state.properties_name,"New kick"); state.properties_kind=3;
    (void)fixture.harness.frame();
    const auto apply=fixture.harness.drawn_text_center("Apply"); expect(apply);
    const auto result=fixture.click(apply->first,apply->second);
    expect(result.get().sound_properties_apply);
    expect(result.get().sound_properties_resource_id.value==101);
    expect(result.get().sound_properties_document_revision==fixture.harness.view().document_revision);
    expect(std::string_view{result.get().sound_properties_name}=="New kick");
    ++fixture.harness.view().audio_slices.document_revision;
    (void)fixture.harness.frame();
    expect(std::string_view{state.properties_name}=="Kick");
}

TEST_CASE("Sounds tag chips emit one stamped add or remove without applying name drafts",
          "[event-harness][sounds-workflow][sound-tags]") {
    fixture_t fixture;
    const std::array<std::string_view, 2> tags{"Drum", "Kick"};
    fixture.rows[0].tags.assign(tags.begin(), tags.end());
    fixture.harness.view().audio_slices.sound_rows[0].tags = fixture.rows[0].tags;
    auto& state = worm_ui_nuklear_audio_state();
    std::strcpy(state.properties_name, "Unapplied name");
    std::strcpy(state.new_tag, "Low");
    (void)fixture.harness.frame();
    SECTION("add") {
        const auto button = fixture.harness.drawn_text_center("Add tag");
        expect(button);
        const auto action = fixture.click(button->first, button->second);
        expect(action.get().sound_properties_tags ==
              std::optional{std::vector<std::string>{"Drum", "Kick", "Low"}});
        expect(std::string_view{action.get().sound_properties_name} == "Kick");
    }
    SECTION("remove") {
        const auto button = fixture.harness.drawn_text_center("Drum  ×");
        expect(button);
        const auto action = fixture.click(button->first, button->second);
        expect(action.get().sound_properties_tags ==
              std::optional{std::vector<std::string>{"Kick"}});
        expect(action.get().sound_properties_resource_id.value == 101);
    }
}

TEST_CASE("Sounds pan and alignment drag are separate zoom-aware gestures",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture;
    auto& state=worm_ui_nuklear_audio_state();
    const auto plus=fixture.harness.drawn_text_center("+"); expect(plus);
    (void)fixture.click(plus->first,plus->second);
    const auto before=state.waveform_viewport;
    const auto x=plus->first, y=plus->second+100;
    const auto down=pointer(x,y,true);
    expect(fixture.harness.frame({&down,1}).get().audio_offset_drag_action==WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    SDL_Event move{}; move.type=SDL_EVENT_MOUSE_MOTION;
    move.motion.x=x-30; move.motion.y=y;
    (void)fixture.harness.frame({&move,1});
    expect(state.waveform_viewport.begin_frame>before.begin_frame);
    const auto up=pointer(x-30,y,false); (void)fixture.harness.frame({&up,1});
    move.motion.x=x;
    (void)fixture.harness.frame({&move,1});
    const std::array alignment_events{key(SDLK_LSHIFT,SDL_SCANCODE_LSHIFT,true), down};
    const auto alignment=fixture.harness.frame(alignment_events,SDL_KMOD_SHIFT);
    expect(alignment.get().audio_offset_drag_action==WORM_UI_AUDIO_OFFSET_DRAG_BEGIN);
    const auto scale=state.offset_drag_frames_per_pixel;
    auto& view=fixture.harness.view().audio_slices;
    view.offset_drag_active=true; view.proposed_offset_denominator=1;
    move.motion.x=x+10;
    const auto preview=fixture.harness.frame({&move,1},SDL_KMOD_SHIFT);
    expect(preview.get().audio_offset_drag_action==WORM_UI_AUDIO_OFFSET_DRAG_UPDATE);
    expect(preview.get().audio_offset_drag_delta_frames==static_cast<std::int64_t>(std::round(10*scale)));
    SDL_Event lost{}; lost.type=SDL_EVENT_WINDOW_FOCUS_LOST;
    const auto cancel=fixture.harness.frame({&lost,1});
    expect(cancel.get().audio_offset_drag_action==WORM_UI_AUDIO_OFFSET_DRAG_CANCEL);
    expect(!state.offset_drag_active);
}

TEST_CASE("Sounds list yields to text and modal ownership and clears stale pending commands",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture;
    fixture.focus_list();
    fixture.harness.view().options.visible=true;
    expect(fixture.press(SDLK_DOWN,SDL_SCANCODE_DOWN).get().audio_resource_id.value==0);
    expect(!worm_ui_nuklear_audio_state().sound_list_focused);
    fixture.harness.view().options.visible=false;
    (void)fixture.harness.frame();
    fixture.focus_list();
    auto& state=worm_ui_nuklear_audio_state();
    expect(worm_ui_nuklear_audio_list_key_command(1,false));
    ++fixture.harness.view().audio_slices.document_revision;
    expect(fixture.harness.frame().get().audio_resource_id.value==0);
    expect(state.list_move_pending==0);
    expect(!state.sound_list_focused);
}

TEST_CASE("Sounds double click selects and auditions the clicked resource with its context",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture;
    const auto snare=fixture.harness.drawn_text_center("Snare  ·  Keysound"); expect(snare);
    SDL_Event motion{}; motion.type=SDL_EVENT_MOUSE_MOTION;
    motion.motion.x=snare->first; motion.motion.y=snare->second;
    (void)fixture.harness.frame({&motion,1});
    auto down=pointer(snare->first,snare->second,true); down.button.clicks=2;
    const auto result=fixture.harness.frame({&down,1});
    expect(result.get().audio_resource_id.value==102);
    expect(result.get().compose_action==WORM_UI_COMPOSE_ACTION_AUDITION_SOUND);
    expect(result.get().compose_resource_id.value==102);
    expect(result.get().compose_input_project_id.value==fixture.harness.view().project_id.value);
    expect(result.get().compose_input_document_revision==fixture.harness.view().document_revision);
}

TEST_CASE("Sounds audition yields to properties text and IME editing",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture;
    fixture.focus_list();
    const auto name=fixture.harness.drawn_text_center("Kick"); expect(name);
    (void)fixture.click(name->first,name->second);
    expect(!worm_ui_nuklear_audio_state().sound_list_focused);
    expect(fixture.press(SDLK_SPACE,SDL_SCANCODE_SPACE).get().compose_action==WORM_UI_COMPOSE_ACTION_NONE);
    fixture.focus_list();
    SDL_Event composition{}; composition.type=SDL_EVENT_TEXT_EDITING;
    composition.edit.text="composing"; composition.edit.start=0; composition.edit.length=9;
    (void)fixture.harness.frame({&composition,1});
    expect(fixture.press(SDLK_SPACE,SDL_SCANCODE_SPACE).get().compose_action==WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Sounds double click cannot audition through Options",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture;
    const auto snare=fixture.harness.drawn_text_center("Snare  ·  Keysound"); expect(snare);
    fixture.harness.view().options.visible=true;
    (void)fixture.harness.frame();
    SDL_Event motion{}; motion.type=SDL_EVENT_MOUSE_MOTION;
    motion.motion.x=snare->first; motion.motion.y=snare->second;
    (void)fixture.harness.frame({&motion,1});
    auto down=pointer(snare->first,snare->second,true); down.button.clicks=2;
    const auto result=fixture.harness.frame({&down,1});
    expect(result.get().compose_action==WORM_UI_COMPOSE_ACTION_NONE);
    expect(result.get().audio_resource_id.value==0);
}

TEST_CASE("Sounds waveform instructions remain visible at narrow workspace size",
          "[event-harness][sounds-workflow]") {
    fixture_t fixture{800,720};
    const auto pan=fixture.harness.drawn_text_center("Wheel: zoom · Drag: pan");
    const auto align=fixture.harness.drawn_text_center("Shift-drag: align");
    expect(pan); expect(align);
    expect(pan->second<680); expect(align->second<680);
    expect(align->second>pan->second);
    expect(worm_ui_nuklear_audio_state().waveform_viewport.pixel_width>0);
}

TEST_CASE("Sounds alignment release commits without a movement threshold",
          "[event-harness][sounds-workflow][capture-continuation]") {
    fixture_t fixture;
    const auto plus = fixture.harness.drawn_text_center("+"); expect(plus);
    const auto down = pointer(plus->first, plus->second + 100, true);
    const std::array press{key(SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, true), down};
    const auto started = fixture.harness.frame(press, SDL_KMOD_SHIFT);
    expect(started.get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_BEGIN);
    fixture.harness.view().audio_slices.offset_drag_active = true;
    fixture.harness.view().audio_slices.proposed_offset_denominator = 1;
    const auto up = pointer(plus->first, plus->second + 100, false);
    const auto released = fixture.harness.frame(std::span{&up, 1}, SDL_KMOD_SHIFT);
    expect(released.get().audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_COMMIT);
    expect(released.get().audio_offset_drag_delta_frames == 0);
    expect(!worm_ui_nuklear_audio_state().offset_drag_active);
}

TEST_CASE("Sounds Slice emits the current source stamp and exact text parameters",
          "[event-harness][sounds-workflow][slicer-ui]") {
    fixture_t fixture{1280, 720};
    auto& view = fixture.harness.view().audio_slices;
    view.slicer.can_slice = true;
    view.slicer.message = "8 slices";
    auto& state = worm_ui_nuklear_audio_state();
    std::strcpy(state.slicer_bpm, "123.456");
    std::strcpy(state.slicer_division, "4");
    std::strcpy(state.slicer_offset, "-0.125");
    (void)fixture.harness.frame();
    const auto button = fixture.harness.drawn_text_center("Slice");
    expect(button);
    const auto frames = wormedit::test::click_at(fixture.harness, button->first, button->second);
    const auto found = std::ranges::find_if(frames, [](const auto& frame) { return frame.get().audio_slicer.has_value(); });
    expect(found != frames.end());
    const auto& action = *found->get().audio_slicer;
    expect(action.project == view.usage_song_id);
    expect(action.chart == view.chart_id);
    expect(action.resource == view.selected_resource_id);
    expect(action.asset == view.selected_asset_id);
    expect(action.revision == view.document_revision);
    expect(action.settings.bpm == "123.456");
    expect(action.settings.division == "4");
    expect(action.settings.offset == "-0.125");
}
