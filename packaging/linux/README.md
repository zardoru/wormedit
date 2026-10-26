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

The container enables `WORMEDIT_WARNINGS_AS_ERRORS=ON`, using the shared policy
in `cmake/warnings.cmake`. GCC's optimizer-dependent `maybe-uninitialized` and
`array-bounds` diagnostics remain visible without failing the build; other
enabled warnings are errors. Recheck the recorded GCC 14 vector/optional
findings when upgrading the container compiler.
