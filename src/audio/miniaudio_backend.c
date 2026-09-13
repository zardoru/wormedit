/*
 * Wormedit uses SDL3 for application audio output. This translation unit keeps
 * miniaudio decoder-only and enables its bundled Ogg Vorbis backend.
 */
#define MINIAUDIO_IMPLEMENTATION

#include <extras/stb_vorbis.c>
#include <miniaudio.h>
