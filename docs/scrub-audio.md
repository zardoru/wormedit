# Scrub audio (needle-on-record scrolling)

Smooth scrolling (macOS touchpad and Magic Mouse, inertia included) plays the song like a needle
on a record: speed follows the scroll, reverses when scrolling backwards, pitch follows speed,
and the sound is held silent while fingers rest on the pad. Wheel notches stay silent.
Preference: Options > Editor > "Scrub audio while scrolling" (`editor.scrub_audio_on_scroll`,
default on).

## Pieces

| Piece | Where | Role |
| --- | --- | --- |
| `scrub_tracker_t` | `core/scrub_tracker` | Audio position per frame -> smoothed signed speed, held state, resume timing. Pure; unit tested. |
| `scrub_reader_t` | `audio/scrub_reader` | Fractional-position cubic reader with signed, slewing speed over a block cache. Allocation free; unit tested headless. |
| `decoded_source_registry_t::decode_block_into` | `audio/source_registry` | Decodes one block into a private cache without a transport session. |
| `sdl_scrub_output_t` | `sys/audio/sdl_scrub_output` | Own SDL stream, 16-block private cache, refill worker that keeps blocks around the needle (leaning along the motion). |
| `scrub_controller_t` | `sys/audio/scrub_controller` | Feeds the tracker, drives the output, mutes the song while scrubbing during playback. |
| `touchpad_fingers_t` | `sys/app/touchpad_fingers.h` | Counts `SDL_EVENT_FINGER_*` fingers (the pinned SDL sends them for the macOS trackpad). |
| `scrub_position.h` | `sys/audio` | Row -> timeline frame and the song's timeline segments (export cuts split it). |

## Behavior

- The needle is only down while the chart plays: scrolling while stopped never scrubs, and a
  scrub ends if playback stops. Scrolling with Tab held (the fast wheel) never scrubs either.
- Continuous scroll actions set `transport.scrub_scrolled`. Each frame, `application.cpp`
  (`service_scrub_audio`) resolves where the scroll has dragged the audio: the playhead scroll
  target.
- Playing: the song is muted from the first scrub until `resume_delay_seconds` (0.15 s) after the
  gesture ends. The existing playhead seeks keep running underneath, so playback simply unmutes at
  the new position. Notches keep stepping by beat.
- Fingers resting without motion hold the record (fast brake, silence). Lifting them without
  inertia ends the gesture. Platforms that report no finger events fall back to input idle time
  (0.1 s).
- Only the song audio scrubs; BGM lanes, hitsounds and the metronome do not.

## Unverified (compiled only in headless pieces and by syntax check against the pinned SDL headers)

- The SDL stream, refill worker latency (MP3 reverse seeks on album-length sources), and the
  finger count on a real trackpad need a run on macOS.
