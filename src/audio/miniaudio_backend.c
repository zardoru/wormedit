/*
 * Wormedit uses SDL3 for application audio output. This translation unit keeps
 * miniaudio decoder-only and enables its bundled Ogg Vorbis backend.
 */
#define MINIAUDIO_IMPLEMENTATION

/* stb_vorbis defines single-letter macros (L, R, C) that break winnt.h, so
 * pull the Windows headers in first. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <extras/stb_vorbis.c>
#include <miniaudio.h>
