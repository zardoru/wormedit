# StepMania warp and raindrop converter source audit

Last checked: 2026-09-13.

This note records implementation evidence used by the editor plan. It describes current sibling source behavior; it is not the desired wormedit contract.

## libotoworm timing behavior

Relevant source: [`../../../libotoworm/src/note_loader_sm.cpp`](../../../libotoworm/src/note_loader_sm.cpp).

- `reinterpret_warp_data` reads SSC `#WARPS` in beats, locates the start through the BPM/stop timing map, and stores a derived time-domain duration in `ChartTransient::warps`.
- Its comment says this is a literal jump approximation rather than StepMania's beat-integration/infinite-BPM model, and says stops inside warps are not handled fully.
- `convert_stops_to_warps` removes negative stop entries and converts them to positive-duration transient warps.
- `WarpifyTiming` finds consecutive negative-BPM sections, sums their absolute time durations, makes their BPM values positive, and creates a transient warp. Its TODO also identifies stops inside warps as an unresolved case.
- The SM loader runs the negative-stop and negative-BPM conversions before building its processed timing data.

These routines are useful reference cases, but they do not retain the exact source commands or an editor-owned beat-domain warp. Wormedit therefore needs canonical exact musical warp intervals plus source provenance, followed by target-profile-specific import/export policies and semantic verification.

Historical behavior is also described by the [ITG simfile gimmicks reference](https://itgwiki.dominick.cc/en/packs-and-simfiles/gimmicks): older StepMania content used negative BPMs or negative stops for warp-like fast-forwarding before explicit warp syntax was available. StepMania's own issue tracker records compatibility differences around [warps and negative BPM](https://github.com/stepmania/stepmania/issues/810). Exact accepted behavior must be fixture-tested per implementation profile rather than inferred from the extension.

## raindrop `rdconverter`

Relevant sources:

- [`../../../raindrop/src/converter/converter.cpp`](../../../raindrop/src/converter/converter.cpp)
- [`../../../libotoworm/include/converter.h`](../../../libotoworm/include/converter.h)

The current executable uses Boost.Program_options and accepts one positional/`--input` path, an output path, target `--format`, and `--author`. It dispatches to libotoworm functions for OM, BMS, unquantized BMS, SM timing, NPS graphs, and an accuracy-test output.

Useful behavior to retain:

- a direct headless conversion command;
- explicit input, output, and target format;
- conversion functionality callable below the executable.

Behavior wormedit's shared conversion service must tighten:

- an unknown format currently prints choices but can leave conversion mode at its default and continue;
- many parse, initialization, and conversion failures still reach process exit code 0;
- output is a directory for some modes and a file for another without a uniform target contract;
- exceptions are printed without a structured diagnostic or stable machine-readable report;
- branch choice, target implementation, warp encoding, source/output text encoding, quantization, overwrite, and accepted-loss policy are not explicit;
- there is no transactional temporary-output publication or semantic export/re-import verification.

The new UI and CLI must construct the same `conversion_request_t`, call the same staged library function, and receive the same `conversion_report_t`. The frontends differ only in how they gather policy and present diagnostics.
