# Linux AppImage inputs

`wormedit.png` is the single, code-drawn 256×256 placeholder icon. It is original
project art under the repository's Unlicense; replace this path with owner art
before a final branded release. No generated icon is copied back into source.

The Dockerfile pins Ubuntu 22.04 (glibc 2.35), CMake, linuxdeploy, appimagetool,
the type-2 runtime and an LGPL FFmpeg. Download SHA-256 values are adjacent to
fixed version URLs. GCC 14 comes from the Ubuntu toolchain PPA because the
application uses C++23. Ubuntu/PPA security updates are intentionally permitted;
this is a pinned compatibility baseline, not a bit-reproducible package lock.

See `docs/building.md` for release commands and system requirements.

The container uses the project's `WORMEDIT_WARNINGS_AS_ERRORS=OFF` option:
older GCC at `-O3` diagnoses conditional `std::vector` copy assignment and
guarded `std::optional` reads. A type-safe, vector-only reproduction fails with
GCC 14 and passes with GCC 16. All diagnostics remain visible in the build log;
no application source or warning classes are suppressed. Native GATE retains
its usual warnings-as-errors policy. Recheck these findings when upgrading the
container compiler.
