/*  SPDX-License-Identifier: GPL-2.0-or-later */
// C ABI over Audacity's project, track and waveform-summary code for the
// experimental Alicorn-owned host window. No wxWidgets window is created; all
// project state stays in Audacity's C++ objects and the host only receives
// bounded display data and scalar track facts.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#   define ACETATE_BACKEND_API __declspec(dllexport)
#else
#   define ACETATE_BACKEND_API __attribute__((visibility("default")))
#endif

typedef struct AcetateBackend AcetateBackend;

// Bumped whenever the exported surface changes shape.
#define ACETATE_BACKEND_ABI_VERSION 1

ACETATE_BACKEND_API int acetate_backend_abi_version(void);

// Creates an in-process headless project. Returns NULL on failure.
ACETATE_BACKEND_API AcetateBackend *acetate_backend_create(void);
ACETATE_BACKEND_API void acetate_backend_destroy(AcetateBackend *backend);

// Imports an audio file (any format libsndfile reads) as a new track in the
// project. Returns 0 on success; on failure returns non-zero and the message
// is available from acetate_backend_last_error.
ACETATE_BACKEND_API int acetate_backend_import(
   AcetateBackend *backend, const char *utf8Path);
ACETATE_BACKEND_API const char *acetate_backend_last_error(
   const AcetateBackend *backend);

// Monotonic counter bumped after every successful mutation of the project, so
// the host can discard stale display data.
ACETATE_BACKEND_API unsigned long long acetate_backend_revision(
   const AcetateBackend *backend);

ACETATE_BACKEND_API int acetate_backend_track_count(
   const AcetateBackend *backend);
ACETATE_BACKEND_API int acetate_backend_channel_count(
   const AcetateBackend *backend, int track);
ACETATE_BACKEND_API double acetate_backend_track_rate(
   const AcetateBackend *backend, int track);
ACETATE_BACKEND_API double acetate_backend_track_start(
   const AcetateBackend *backend, int track);
ACETATE_BACKEND_API double acetate_backend_track_end(
   const AcetateBackend *backend, int track);
ACETATE_BACKEND_API double acetate_backend_project_end(
   const AcetateBackend *backend);

// Fills `minMax` with 2 * columns floats (min, max per column) for one
// channel, dividing [t0, t1) in seconds into equal columns. Columns outside
// the audio are zero. Returns the number of columns written, or -1.
ACETATE_BACKEND_API int acetate_backend_waveform(
   AcetateBackend *backend, int track, int channel, double t0, double t1,
   int columns, float *minMax);

#ifdef __cplusplus
}
#endif
