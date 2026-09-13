# wormedit

Wormedit is a planned cross-platform rhythm chart editor for BMS-family,
StepMania, osu!mania, and BMSON authoring. The reviewed design is in
[`docs/chart-editor-plan.md`](docs/chart-editor-plan.md).

The repository currently contains the stage-zero headless core/test
environment and the original SDL3 + Nuklear application scaffold.

The SDL/OpenGL3 Nuklear backend in `src/vendor/nuklear_sdl_gl3_sdl3.h` is a
minimal SDL3 port of Nuklear's upstream `demo/sdl_opengl3/nuklear_sdl_gl3.h`
and retains the original public domain attribution header.

```sh
cmake --preset headless \
  -DWORMEDIT_NLOHMANN_JSON_SOURCE_DIR=/path/to/nlohmann-json-3.12.0 \
  -DWORMEDIT_CATCH2_SOURCE_DIR=/path/to/Catch2 \
  -DWORMEDIT_LUA_SOURCE_DIR=/path/to/lua-5.4.8
cmake --build --preset headless
ctest --preset headless
```

See [`docs/building.md`](docs/building.md) for native builds and generic
Linux/Windows cross-compilation presets. `config.h.in` emits
`WORMEDIT_GIT_HASH`.
