# Update check

The native editor can tell you when a newer release exists. It never installs anything: the
notice names the version and *Help → Download the new version…* opens the release page.

## What is sent and when

- One HTTPS `GET` of `https://zardoru.github.io/wormedit/latest.json`, at most once every 24 hours
  (the time of the last successful check is kept in `update-check.txt` beside `preferences.json`).
- The request carries a generic `User-Agent` (`wormedit-update-check`). No version, platform, project
  or identifier is sent. Redirects stay on https, the body is capped at 16 KiB, the whole request
  times out after 10 seconds, and any failure of an automatic check is silent.
- The browser build never checks (it always serves the latest release).

## Controls

- First launch asks once whether to check (default on). The answer is stored in the preferences
  (`updates.check_on_startup`; `first_run.completed` lists the answered questions).
- *Options → General → Check for updates at startup* changes it later.
- *Help → Check for updates…* asks on demand and always reports the result.

## Manifest

`scripts/write-update-manifest.py` writes `latest.json`; `release-pages.yml` publishes it to
gh-pages whenever a release is published (release candidates included):

```json
{"schema": 1,
 "channels": {"stable": {"version": "1.0.0-rc5", "url": "https://…", "notes_url": "https://…", "published": "2026-11-22"}}}
```

Unknown keys are ignored. `url` and `notes_url` must be https. A new channel (for example nightly)
is a new key under `channels` and a new `update_channel_t` value.

Versions compare as SemVer with release candidates numbered naturally (`rc9 < rc10 < final`).
Nightly builds (`-nightly.*`) are never offered an update.

## Build

The check links libcurl. `-DWORMEDIT_UPDATE_CHECK=AUTO` (default) uses it when found,
`ON` requires it, `OFF` compiles the check out; without libcurl the Help entry, the Options
toggle and the first-run question do not appear. On Linux install `libcurl4-openssl-dev`; macOS
ships libcurl in the SDK.

## First-run questions

`include/wormedit/core/first_run.h` holds the ordered table of questions. The frontend presents
the first unanswered one as a modal (`render_first_run` in `frontend.cpp`) and answers with
`updates.first_run_answer` (step id and choice). To add one, such as choosing the interface
language, append a row to the table, add its presentation, and handle its answer in
`update_service_t::handle` (or a sibling service). Questions the build cannot present are listed in
`next_first_run_step`'s `unavailable` argument.
