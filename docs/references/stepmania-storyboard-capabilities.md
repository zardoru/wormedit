# StepMania-family storyboard capabilities (SMX-1)

Research retrieved **2026-11-01 (UTC)**. This is an export design survey, not a
runtime compatibility certificate: no engine binary was executed. Source-confirmed
facts, recommendations and unverified behavior are distinguished below.
Replaces the route assumptions in [T-292](../chart-editor-plan/tasks/scripting-storyboard.md#t-292-carry-storyboards-into-smssc-exports).

## Source scope and versions

| Profile | Inspected revision | Evidence boundary |
| --- | --- | --- |
| StepMania 5.1 | `5_1-new`, `825467bcd81c812b33ad684dc04dd151b2d5dec3` | Same pinned 5.1 tree as the [keysound survey](stepmania-keysounds.md); not all SM5 forks. |
| ITGmania | `v1.3.0`, `8a3bf8884acdcb69363f7f0ea592138e398c71e4` | Release source, independently checked rather than assumed identical to SM5. |
| Etterna | `v0.75.1`, `2d706147cc4bcf95a8ceb24dc4f4129be207f803` | Concrete 0.75.x target; earlier/later patch releases need a delta check. |
| NotITG | v4.x, API documentation target **4.2.0** | No public v4 engine source located. Primary evidence is the author's [Mirin template][mirin-sm] and [XML entry point][mirin-main]; [OpenITG source][oitg-actor] establishes ancestry only. V4-only API notes below are explicitly provisional, from an unofficial reference. |

All engine-source links below are immutable commit links. The NotITG API reference
is a live, unofficial documentation site and is not an engine-source pin. Neither
OpenITG nor a working template proves all current NotITG behavior. SMX-4 must pin
and exercise an actual v4 build before advertising that profile as verified.

## Route decision

| Engine | Recommended native route | Video route | Owning follow-up |
| --- | --- | --- | --- |
| SM5.1 / ITGmania | Generated Lua actor trees with an absolute-song-time evaluator; song-local assets and no runtime OSB parser. | Offer the VID renderer/encoder fallback for fidelity or resource limits. | SMX-2 native adapter; VID tasks supply video. |
| Etterna 0.75.1 | Explicit SSC Actors mode uses the same generated Lua 5.1 actor runtime as SM5/ITGmania, with folder `default.lua` hooks. Target build/theme execution remains unverified. | Explicit Video mode remains unavailable; no video fallback is silently selected. | SMX-3 native route; VID output remains separate. |
| NotITG v4.x | Separate XML/Lua 5.0 dialect, generated actors and absolute song time. Do not reuse an SM5 `Def.ActorFrame` file. | Optional fallback, with codec/build and foreground-alpha verification. | SMX-4 profile/dialect; VID output. |

SMX-3's native actor decision supersedes the original conservative video-first
deployment recommendation; that recommendation was never evidence of a song-Lua ban.
An unsupported storyboard must produce classified losses while chart export
continues, as T-292 requires.

## Song hooks, restrictions and packaging

### SM5.1 and ITGmania

Both accept song-folder actor Lua through `#BGCHANGES` and `#FGCHANGES`.
`Foreground::LoadFromSong` first tries `<song>/<entry>/default.lua`, then makes an
actor from the referenced path. `ActorUtil` loads Lua actor tables and resolves a
directory's `default.lua`; this is a song asset hook, requiring no installed theme
modification. Direct XML/INI names are rejected by `LoadFromBGChangesVector` unless
`QuirksMode` is enabled. Prefer a folder with `default.lua`, not legacy XML.
Sources: SM5 [Foreground.cpp][sm-Foreground-cpp], [ActorUtil.cpp][sm-ActorUtil-cpp],
[NotesLoaderSM.cpp][sm-NotesLoaderSM-cpp]; ITGmania [Foreground.cpp][itg-Foreground-cpp],
[ActorUtil.cpp][itg-ActorUtil-cpp], [NotesLoaderSM.cpp][itg-NotesLoaderSM-cpp].

Both bundle **Lua 5.1** ([lua.h][sm-lua-h],
[lua.h][itg-lua-h]). Their Lua managers remove dangerous OS/IO
entry points; song actors still run in the engine Lua environment, not wormedit's
bounded generator sandbox. Emit self-contained declarations and math; do not
assume arbitrary filesystem/process access, a native module, or Lua 5.3+ syntax.
Sources: [LuaManager.cpp][sm-LuaManager-cpp], [LuaManager.cpp][itg-LuaManager-cpp].

Background visibility is user-controlled: `SongBackgrounds`, static/random
background options and brightness affect presentation. Foregrounds sit outside
that background brightness policy. A theme can alter presentation, and a loader
existing does not guarantee the user's screen shows it.
Sources: [Background.cpp][sm-Background-cpp], [Background.cpp][itg-Background-cpp].

### Etterna 0.75.1

`SMLoader::LoadFromBGChangesVector` rejects file1/file2 containing `.ini` or `.xml`
without SM5's QuirksMode exception; **it does not reject `.lua`**.
`Foreground::LoadFromSong` explicitly prefers `<folder>/default.lua`, and
`BackgroundImpl::Layer::CreateBackground` handles `FT_Lua` and directories with
`default.lua`. `ScreenGameplay::UseSongBackgroundAndForeground()` returns true in
the inspected base class, and gameplay loads both actors. Thus the old T-292
parenthesis “Etterna restricts it” needs this narrower interpretation.
Sources: [NotesLoaderSM.cpp][et-NotesLoaderSM-cpp], [Foreground.cpp][et-Foreground-cpp],
[Background.cpp][et-Background-cpp], [ScreenGameplay.h][et-ScreenGameplay-h], [ScreenGameplay.cpp][et-ScreenGameplay-cpp].

Etterna bundles **LuaJIT 2.1 rolling, Lua 5.1 API/language compatibility**
([luajit.h][et-luajit-h], [lua.h][et-lua-h]).
Its Lua manager removes OS process operations, IO file opening and package dynamic
loading. Actor Lua is not a per-song security sandbox. `Song:HasLua()` checks whether stored filenames end in `.lua` or `.xml`;
it does not inspect folder contents. This detector is not a playback prohibition.
Sources: [LuaManager.cpp][et-LuaManager-cpp], [Song.cpp][et-Song-cpp].

Use the same song-local folder layout as SM5. Backgrounds can be bypassed by
`SongBackgrounds=false`, StaticBackground or RandomBGOnly; brightness can hide
them. Video is subject to these settings too. Theme, decoder, practice/seek and
rate behavior remain SMX-3 integration checks, not verified here.
Source: [Background.cpp][et-Background-cpp].

### NotITG v4.x

The primary [Mirin simfile][mirin-sm] references `template/main.xml` via
`#FGCHANGES`; its [entry point][mirin-main] creates an XML ActorFrame, uses
song-relative `loadfile` for Lua and keeps the foreground alive with a long sleep.
This demonstrates the XML-to-Lua route; it does not demonstrate SSC loading.
Choose a **`.sm` companion/dialect** for SMX-4. Do not promise that NotITG reads
SSC 0.83 or direct SM5 Lua tables: neither is established by the evidence.

The [OpenITG Lua header][oitg-lua] identifies Lua 5.0; no NotITG v4 Lua runtime
source was retrieved. Target Lua 5.0 conservatively and confirm `_VERSION` on the
chosen binary. Lua/IO restrictions in that binary remain unverified; generated
files should need only song-local loads and engine actor calls.

The unofficial [hook guide][nitg-guide] documents folder `default.xml`, BG behind
and FG above the playfield, plus `#BETTERBGCHANGES` to fix legacy BG Init/message/model
limitations. Treat that tag as a v4 integration candidate, not an SM/SSC portable
hook. The primary template's FG route is the safer initial target.

### Concrete file layout and timing fields

Proposed generated SM5/ITGmania package (Etterna can use the same layout):

```text
Songs/Pack/Song/
  chart.ssc
  music.ogg
  wormedit-bg/default.lua
  wormedit-fg/default.lua
  wormedit-assets/image-0001.png
  wormedit-background.mp4          # video alternative, not loaded with actor BG
```

A minimal **native** song-level reference, with a start at beat zero:

```text
#BGCHANGES:0=wormedit-bg=1=0=0=1;
#FGCHANGES:0=wormedit-fg=1=0=0=1;
```

A **video alternative** is `#BGCHANGES:0=wormedit-background.mp4=1=0=0=0;`.
The six legacy fields are start beat, file, update/play rate, crossfade,
rewind and loop. `loop=0` selects `StretchNoLoop` when no explicit effect overrides
it; longer syntax adds effect, file2, transition and colors. Do not mix BG movie
stretch effects into the native actor example. File lookup is song-relative;
load textures through explicit song-relative paths rather than the process cwd.
Sources: [NotesLoaderSM.cpp][sm-NotesLoaderSM-cpp], [BackgroundUtil.cpp][sm-BackgroundUtil-cpp],
[NotesLoaderSSC.cpp][et-NotesLoaderSSC-cpp].

**Starts are beats, not seconds.** SM/SSC loaders store field zero as
`m_fStartBeat`; SSC dispatches the same background-change loader. Convert the
storyboard's desired millisecond start through song timing, including offset,
stops, delays and warps. Beat zero need not mean music second zero. A stop/warp
can make inversion ambiguous; do not silently round a second start to a beat.
For native output, activate at an early representable beat and evaluate the
original absolute timestamps inside Lua. For video, render explicit leading
padding/trim relative to the chosen activation, or report the alignment loss.
Sources: [NotesLoaderSM.cpp][sm-NotesLoaderSM-cpp], [NotesLoaderSSC.cpp][sm-NotesLoaderSSC-cpp],
[Foreground.cpp][sm-Foreground-cpp], [Foreground.cpp][et-Foreground-cpp].

For NotITG, mirror the primary template with `chart.sm`,
`wormedit-fg/main.xml`, Lua files and assets; reference the XML using `#FGCHANGES`.
Its template starts at beat -10. This is an example, not a universal preroll:
SMX-4 must select an activation before the first storyboard event using the
exported song's timing. [Primary simfile][mirin-sm].

## Actors, transforms and layering

SM5, ITGmania and Etterna register the requested actor setters. The corresponding
sources are [Actor.cpp][sm-Actor-cpp], [Actor.cpp][itg-Actor-cpp], [Actor.cpp][et-Actor-cpp];
their parent/child draw paths are [ActorFrame.cpp][sm-ActorFrame-cpp],
[ActorFrame.cpp][itg-ActorFrame-cpp], [ActorFrame.cpp][et-ActorFrame-cpp].

| Requirement | SM5 / ITGmania / Etterna | NotITG v4 target |
| --- | --- | --- |
| Nested groups | ActorFrames draw children in parent coordinates; parent diffuse/alpha propagates. | XML ActorFrames in the primary template; inherited-transform fidelity needs a v4 fixture. |
| Position | `x`, `y`; engine screen coordinates. | `x`, `y`. |
| Rotation | `rotationz`, degrees; OSBX already authors degrees. Convert radians only when consuming legacy OSB rotation values. | `rotationz`; verify the v4 convention. |
| Scale | `zoomx`, `zoomy`; signed scales permit flips. | `zoomx`, `zoomy`. |
| Shear | `skewx`, `skewy` take shear factors; convert OSBX authored degrees with `tan(angle * pi / 180)`, then verify axis/sign/order. | `skewx`; `skewy` documented as added in v4; verify units and composition. |
| Tint/opacity | `diffuse`, `diffusealpha`; parent multiplication. | `diffuse`, `diffusealpha`. |
| Additive blending | Sprite `blend('BlendMode_Add')`; normal uses `BlendMode_Normal`. Set on leaves: an ActorFrame does not itself draw geometry/set leaf render state. | `blend('add')`; use dialect-specific spelling. |
| Order | Stable actor insertion/draw order; no Z sorting unless requested. | Explicit XML child order/draworder; validate against the target build. |

NotITG setters are provisional v4 documentation evidence
([Actor reference][nitg-actor]); `blend('add')` also appears in the
[OpenITG implementation][oitg-actor]. Its [ActorFrame reference][nitg-frame]
describes nested actors and update callbacks.

An engine actor has a fixed internal transform order. **One actor per OSBX group
is not sufficient for every ordered transform stack.** Use nested wrapper
ActorFrames per operation, with explicit origins and a root viewport transform;
verify shear signs/order, negative scales, alpha multiplication and clipping
against the offline renderer. Do not flatten a general affine matrix into
rotation plus independent zoom and discard shear. These are adapter requirements,
not claims of already tested pixel equivalence.

Units follow [OSBX's authored-degree contract](../osbx-draft-spec.md#matrices-and-compound-transforms)
and the existing evaluator, not the radians used by the shared affine helper.
SM5's [skew matrices][sm-RageMath-cpp] store the supplied factor directly.
An OSBX skew with both axes has unit diagonal; composing the engine's X and Y
shears introduces a cross term. Setting both fields on one actor is therefore
not a proven equivalent of that OSBX op. The adapter must verify an affine
factorization with additional wrappers, or report an unsupported object while
continuing chart export.

SM5/ITGmania/Etterna gameplay sets SongBackground to
`DRAW_ORDER_BEFORE_EVERYTHING`, and SongForeground to `DRAW_ORDER_OVERLAY+1`
(above the notefield and normal overlay, below transitions). Therefore FG can
cover gameplay/HUD, not merely sit just above notes. A child draworder cannot
move between those separate screen layers. Sources: [ScreenGameplay.cpp][sm-ScreenGameplay-cpp],
[ScreenGameplay.cpp][itg-ScreenGameplay-cpp], [ScreenGameplay.cpp][et-ScreenGameplay-cpp].
[OpenITG gameplay][oitg-gameplay] has that order too; NotITG v4 exact HUD/theme
placement needs confirmation.

Export uses the shared **Pass / Fail / Neither** choice from VID-0 #7 / SB-13.
Engines can expose live gameplay state, but this adapter will not implement OSB
pass/fail triggers or bind them to engine life meters. Keep unconditional
Background/Foreground objects plus only the selected conditional content, in
source stacking order. Diagnose other interactive triggers. No target gets a
second independent conditional-layer option.

## Clocks, lifetime, seeking and playback rate

The SM5/ITGmania/Etterna foreground runners activate at start beat, run `On`,
compute a stop beat from **remaining tween duration**, and hide the actor after
that. An update callback alone does not keep the FG alive. Generate a bounded
lifetime extending beyond the final drawable frame (including preroll/tail),
without using visual tweens as the evaluator's clock. The Mirin primary entry
point independently demonstrates the same keep-alive need for NotITG.
Sources: [Foreground.cpp][sm-Foreground-cpp], [Foreground.cpp][itg-Foreground-cpp],
[Foreground.cpp][et-Foreground-cpp], [Mirin entry point][mirin-main].

Queued tweens consume update deltas, not absolute timestamps. The inspected
SM5/ITGmania foreground code uses song-second differences, divides by music rate
unless `RateModsAffectTweens` is enabled, and clamps negative deltas. Etterna also
divides by music rate and clamps. Backward seeks therefore do not rewind queued
tweens; a finished FG has additional lifecycle state. Background runners reset on
segment changes and catch up from segment start, but an arbitrary seek inside a
segment is not a general replay of all commands. Sources:
[Background.cpp][sm-Background-cpp], [Background.cpp][itg-Background-cpp], [Background.cpp][et-Background-cpp] and the
foreground sources above.

**Adapter contract:** evaluate all visible properties from the current absolute
music second on every update; do not accumulate callback delta or multiply song
time by rate a second time. The inspected SM5, ITGmania and Etterna `GameState`/`SongPosition` bindings
expose `GAMESTATE:GetSongPosition():GetMusicSeconds()`
([SM5 state][sm-GameState-cpp], [position][sm-SongPosition-cpp];
[ITGmania state][itg-GameState-cpp], [position][itg-SongPosition-cpp];
[Etterna state][et-GameState-cpp], [position][et-SongPosition-cpp]).
NotITG's documented interface differs (`GAMESTATE:GetSongTime()`;
[provisional API reference][nitg-state]); confirm its offset/timing semantics in
SMX-4 rather than copying SM5 calls. An evaluator fixes value drift while it runs; the host must
also recreate/reactivate it after seek/restart if the FG runner has stopped it.
Check 0.75x/1x/1.5x, pause, forward/backward seeks, restart, stops and warps.

## Easing compatibility

SM5/ITGmania/Etterna implement `linear`, `accelerate` (t²), `decelerate`
(1−(1−t)²), `spring` and typed `tween`. Their inspected native enum has Linear,
Accelerate, Decelerate, Spring and Bezier: **no portable native `smooth`,
`bouncebegin` or `bounceend`**. A theme helper with such a name is not an export
runtime dependency. Sources: [Tween.cpp][sm-Tween-cpp], [Tween.cpp][itg-Tween-cpp],
[Tween.cpp][et-Tween-cpp] and each engine's Actor bindings above.

`TweenType_Bezier` accepts four scalar controls (1D polynomial) or eight values
(four XY control points; solves Y from X). OSBX's `(x1,y1,x2,y2)` must expand to
`(0,0,x1,y1,x2,y2,1,1)` for the 2D form; it is **not** the four-number 1D form.
Validate monotone X/root selection and overshoot before claiming equivalence.
No arbitrary Lua easing callback is accepted by these `CreateFromStack` paths.
Use the exported absolute-time evaluator for general custom easing.

The [OpenITG tween formulas][oitg-actor] show that `bouncebegin/end` are sinusoidal
overshoot curves, not OSB piecewise bounces; `spring` is not OSB elastic.
NotITG's unofficial [Actor API][nitg-actor] documents expression tweens and,
from 4.2.0, function tweens. Those can express custom formulas, but are not a
portable SM5 interface and still use tween clocks. `smooth` has no verified
cross-engine built-in contract. Prefer the same evaluator for all profiles.

The following map covers **every OSB easing ID 0–34**. Names/formulas are grounded
in wormedit's existing `evaluate_storyboard_easing` in
[src/core/storyboard.cpp](../../src/core/storyboard.cpp), not guessed from similar
engine names. “Evaluator” means preserve that formula in generated Lua and sample
at absolute song time; use it even for simple curves in the first native adapter.

| OSB IDs | OSB meaning | Native equivalence / export treatment |
| --- | --- | --- |
| 0 | Linear | `linear`. |
| 1, 4 | Out / OutQuad | `decelerate`. |
| 2, 3 | In / InQuad | `accelerate`. |
| 5 | InOutQuad | Evaluator; two quadratic halves, not spring/smooth. |
| 6, 7, 8 | In / Out / InOutCubic | Evaluator; single cubic halves can also use a proven Bézier conversion. |
| 9, 10, 11 | In / Out / InOutQuart | Evaluator. |
| 12, 13, 14 | In / Out / InOutQuint | Evaluator. |
| 15, 16, 17 | In / Out / InOutSine | Evaluator. |
| 18, 19, 20 | In / Out / InOutExpo | Evaluator, retaining endpoint correction. |
| 21, 22, 23 | In / Out / InOutCirc | Evaluator. |
| 24, 25 | InElastic / OutElastic | Evaluator; engine `spring` is different. |
| 26, 27 | OutElasticHalf / OutElasticQuarter | Evaluator with the actual period/endpoint behavior. |
| 28 | InOutElastic | Evaluator. |
| 29, 30, 31 | In / Out / InOutBack | Evaluator; no assumed bouncebegin/end equivalence. |
| 32, 33, 34 | In / Out / InOutBounce | Evaluator; piecewise bounces, not engine bounce tweens. |
| OSBX cubic Bézier | Solve X for parameter, then evaluate Y | Evaluator; optional verified 2D native Bézier on SM5/ITGmania/Etterna only. |

NotITG can receive the same scalar formulas in Lua 5.0-compatible syntax; do not
require the provisional 4.2 function-tween API. Preserve overshooting progress
for transforms, and apply opacity/color clamping at the same stage as the offline
renderer. This avoids silently substituting a visually similar curve.

## Video boundaries and existing wormedit retention

Video avoids target Lua dialect/easing differences and preserves rasterized shear
and matrices, subject to codec, resolution and decoder fidelity. `#BGCHANGES`
puts it behind notes. `#FGCHANGES` can load movie actors through `MakeActor`, but
ordinary opaque MP4 cannot preserve a transparent foreground: flattening it over
a background can obscure the notefield. Do not promise alpha WebM without a
per-engine decoder test. Keep a background-only video default; report any
foreground omission/flattening and retain the shared conditional choice. Video
rate/seek correctness still needs tests; a playable file is not proof of sync.
Sources for movie actor loading: [ActorUtil.cpp][sm-ActorUtil-cpp], [ActorUtil.cpp][itg-ActorUtil-cpp],
[ActorUtil.cpp][et-ActorUtil-cpp]; VID tasks own rendering/encoding, not new engine hooks.

Current wormedit does **not** model these tags as executable storyboards:

- [stepmania.cpp](../../src/formats/stepmania.cpp): `make_stepmania_song` stores
  song fields through `as_metadata_group`; `append_unknown_fields` re-emits
  unmodeled fields when `preserve_unknown_fields` is true. BGCHANGES/FGCHANGES are
  absent from `modeled_song`, so existing values survive this retained-field path.
- This is retention of source fields/order, not parsing their animation timeline,
  converting beats, copying referenced folders/videos, or executing Lua. The
  modeled `BACKGROUND` still-image resource is a separate concept.
- SMX-2/3/4 must deliberately reconcile generated tags with retained source tags;
  blindly append would produce duplicates/conflicting layers. Preserve unrelated
  metadata and report replacement/conflicts. Turning off unknown-field retention
  is not an asset-export implementation.

## Follow-up acceptance boundaries

1. **SMX-2:** generate actors/data from typed OSBX (no runtime OSB parser); test
   screen/origin transforms, nested ordered operations, alpha/add blending,
   foreground lifetime, song-second evaluation and retained-tag reconciliation on
   both pinned SM5 and ITGmania targets. Keep video fallback available.
2. **SMX-3:** the source implementation selects the shared native actor route for
   explicit Etterna 0.75.1 SSC Actors mode. Its distinct SOURCE48 GATE passed
   all 6,332 tests and 215 changed-C++ comparisons, including fixed-state advisories,
   prepared scenes and atomic publication (`/tmp/smx3-source48-acceptance.json`).
   Document user background settings, timing alignment, foreground lifetime and the
   fixed Pass/Fail/Neither choice. Actual 0.75.1/theme/seek/rate execution is still
   required before declaring engine parity. Video is a separate explicit mode.
3. **SMX-4:** pin a NotITG v4 binary/version and confirm Lua version, XML lifecycle,
   song clock, v4 shear, blend spelling, draw order, codecs and restrictions.
   Export a verified SM/XML dialect; SSC input support and BETTERBGCHANGES remain
   unverified until exercised. Do not infer them from SM5 or OpenITG.

This survey makes no engine-runtime pass claim. Missing v4 source and unexecuted
engine fixtures are explicit limits; they do not block writing the per-profile
adapters with the conservative defaults above.

## SMX-2 adapter boundary (post-1.0 branch)

The native SSC adapter follows the pinned SM5.1/ITGmania route above. This records
the implementation boundary, not an engine-runtime acceptance result. SMX-3 reuses
this route for Etterna; NotITG uses its separate conservative SM/XML dialect.
The saved Actors/Video/None policy was introduced in native
schema 31: newly created policies select Actors, existing schema-30 policies migrate
to None (including absent/null legacy policies). Video currently warns that its
route is unavailable; it does not silently claim a video artifact.

The emitted Lua uses absolute music seconds for every binding, with no accumulated
callback delta or queued property tween. Each sprite receives its ancestor/op
wrappers in global canonical draw order, preserving interleaved groups. The
rotation convention follows pinned [Actor][sm-Actor-cpp]/[RageMath][sm-RageMath-cpp]
and [ITGmania Actor][itg-Actor-cpp] sources. For a linear matrix `[a,b,c,d]`, let
`p=hypot(a+d,b-c)`, `q=hypot(a-d,b+c)`, `u=atan2(b-c,a+d)` and
`v=atan2(b+c,a-d)`. Then the wrapper product is
`R((u+v)/2) * diag((p+q)/2,(p-q)/2) * R((u-v)/2)`.
Expanding the product recovers all four entries without determinant division;
signed scale includes reflection and rank-zero/rank-one cases. A simultaneous
OSBX shear supplies `[1,tan(y),tan(x),1]` to this factorization, avoiding the cross
term introduced by sequential engine shear setters. Engine setters take degrees
for rotations. Mock-actor fixtures compare composed corners with the canonical
fractional evaluator; they do not execute the engine's float renderer.

Sprite initialization disables frame animation, restores the decoded full image
size and sets full-image texture coordinates. This avoids filename-inferred frame
grids in the pinned [SM5 Sprite][sm-Sprite-cpp]/[RageTexture][sm-RageTexture-cpp] and
[ITGmania Sprite][itg-Sprite-cpp]/[RageTexture][itg-RageTexture-cpp] loaders. Source
retrieval for these additional files: **2026-11-01**. Images use the existing
source receipts and atomic package ownership; generated Lua is supplemental data,
not an input to chart reimport.

Clipping is deliberately conservative: direct native actors are accepted only
when an outward-rounded interval proof contains all image corners in the logical
canvas. Uncertain/outside objects report unsupported; no unverified AFT/CopySrc
compositing is emitted. Linear/step tracks use native property evaluation;
nonlinear tracks use the canonical evaluator and shared normalized full-scene
metric/simplifier. Their reports name the inclusive quarter-ms verification grid,
integer-ms keys, once-rounded nine decimals and four-key/half-open-100ms density.
Such samples do **not** certify unsampled extrema or continuous-time fidelity.
Failure to meet the sampled metric, geometry, work or storage limits refuses the
object rather than labeling an approximation exact.

Default native limits are 8 MiB emitted Lua, 64 MiB estimated detached scene
payload, 100,000 actor frames including reserved roots, depth 128 and 8,000,000
shared work units; shared source baking also bounds input keys at 65,536.
Activation/playback/authored-key/explicit-lifetime clocks are limited to ±one day.
Root keep-alives cover the accepted positive music-clock tail, and leaf visibility
still gates the requested playback span. Rewinds/restarts/rate changes are sampled
from the new absolute clock. Seeking before hook activation, beyond the domain,
or after an engine destroys a foreground actor remains outside the claimed
boundary until actual pinned-engine lifecycle fixtures run.

SSC emits one hook per accepted BG/FG layer, bundling multiple scenes. Its
song-level timing map supports the beat-based hook while retaining chart timing.
Retained BGCHANGES/FGCHANGES (including background aliases/additional layers)
produce storyboard-only losses and keep the chart. Unsupported objects never
reject an otherwise valid chart, even under reject loss policy. Unrelated native
Lua/image ownership or source/publication I/O failures still refuse an unsafe
package. Shared Pass/Fail/Neither selects one export outcome; it does not infer
gameplay state or alter ordinary `.osb` include-all conditional export.

## Source links

Every link in this section was retrieved on **2026-11-01**. Engine files were read
from raw GitHub content at the commit in each URL. NotITG's unavailable engine
source is not replaced by an assumed OpenITG version.

[sm-Foreground-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/Foreground.cpp
[sm-ActorUtil-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/ActorUtil.cpp
[sm-NotesLoaderSM-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/NotesLoaderSM.cpp
[itg-Foreground-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/Foreground.cpp
[itg-ActorUtil-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/ActorUtil.cpp
[itg-NotesLoaderSM-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/NotesLoaderSM.cpp
[sm-lua-h]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/extern/lua-5.1/src/lua.h
[itg-lua-h]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/extern/lua-5.1/src/lua.h
[sm-LuaManager-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/LuaManager.cpp
[itg-LuaManager-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/LuaManager.cpp
[sm-Background-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/Background.cpp
[itg-Background-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/Background.cpp
[et-NotesLoaderSM-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Models/NoteLoaders/NotesLoaderSM.cpp
[et-Foreground-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Actor/Gameplay/Foreground.cpp
[et-Background-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Actor/Gameplay/Background.cpp
[et-ScreenGameplay-h]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Screen/Gameplay/ScreenGameplay.h
[et-ScreenGameplay-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Screen/Gameplay/ScreenGameplay.cpp
[et-luajit-h]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/extern/luajit/src/luajit.h
[et-lua-h]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/extern/luajit/src/lua.h
[et-LuaManager-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Singletons/LuaManager.cpp
[et-Song-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Models/Songs/Song.cpp
[sm-BackgroundUtil-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/BackgroundUtil.cpp
[et-NotesLoaderSSC-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Models/NoteLoaders/NotesLoaderSSC.cpp
[sm-NotesLoaderSSC-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/NotesLoaderSSC.cpp
[sm-Actor-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/Actor.cpp
[sm-RageMath-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/RageMath.cpp
[itg-Actor-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/Actor.cpp
[et-Actor-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Actor/Base/Actor.cpp
[sm-ActorFrame-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/ActorFrame.cpp
[itg-ActorFrame-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/ActorFrame.cpp
[et-ActorFrame-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Actor/Base/ActorFrame.cpp
[sm-ScreenGameplay-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/ScreenGameplay.cpp
[itg-ScreenGameplay-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/ScreenGameplay.cpp
[sm-Tween-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/Tween.cpp
[itg-Tween-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/Tween.cpp
[et-Tween-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Actor/Base/Tween.cpp
[et-ActorUtil-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Actor/Base/ActorUtil.cpp
[mirin-sm]: https://github.com/XeroOl/mirin-template/blob/b2769570bbdbbe21fe5546e9ea1992558e0a8308/Song.sm
[mirin-main]: https://github.com/XeroOl/mirin-template/blob/b2769570bbdbbe21fe5546e9ea1992558e0a8308/template/main.xml
[oitg-actor]: https://github.com/openitg/openitg/blob/f2c129fe65c65e4a9b3a691ff35e7717b4e8de51/src/Actor.cpp
[oitg-gameplay]: https://github.com/openitg/openitg/blob/f2c129fe65c65e4a9b3a691ff35e7717b4e8de51/src/ScreenGameplay.cpp
[oitg-lua]: https://github.com/openitg/openitg/blob/f2c129fe65c65e4a9b3a691ff35e7717b4e8de51/src/lua-5.0/include/lua.h
[nitg-guide]: https://sm.heysora.net/doc/
[nitg-actor]: https://craftedcart.gitlab.io/notitg_docs/lua_api/actor.html
[nitg-frame]: https://craftedcart.gitlab.io/notitg_docs/lua_api/actor_frame.html
[sm-GameState-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/GameState.cpp
[sm-SongPosition-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/SongPosition.cpp
[itg-GameState-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/GameState.cpp
[itg-SongPosition-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/SongPosition.cpp
[et-GameState-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Singletons/GameState.cpp
[et-SongPosition-cpp]: https://github.com/etternagame/etterna/blob/2d706147cc4bcf95a8ceb24dc4f4129be207f803/src/Etterna/Models/Songs/SongPosition.cpp
[nitg-state]: https://craftedcart.gitlab.io/notitg_docs/lua_api/game_state.html
[sm-Sprite-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/Sprite.cpp
[sm-RageTexture-cpp]: https://github.com/stepmania/stepmania/blob/825467bcd81c812b33ad684dc04dd151b2d5dec3/src/RageTexture.cpp
[itg-Sprite-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/Sprite.cpp
[itg-RageTexture-cpp]: https://github.com/itgmania/itgmania/blob/8a3bf8884acdcb69363f7f0ea592138e398c71e4/src/RageTexture.cpp
