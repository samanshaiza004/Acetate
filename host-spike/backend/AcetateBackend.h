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
#define ACETATE_BACKEND_ABI_VERSION 6

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

// Transport. All calls must come from the host's main thread; Audacity's audio
// thread keeps running the stream and never calls back into the host.
//
// Starts playback of every wave track over [t0, t1) seconds. Returns 0 on
// success; otherwise non-zero with a message in acetate_backend_last_error.
ACETATE_BACKEND_API int acetate_backend_play(
   AcetateBackend *backend, double t0, double t1);
ACETATE_BACKEND_API void acetate_backend_stop(AcetateBackend *backend);
// Main-thread tick: finalises a stream that has reached its end (the audio
// thread only stops producing; Audacity expects the main thread to call
// StopStream). Call it regularly, e.g. from the host's frame callback.
ACETATE_BACKEND_API void acetate_backend_update(AcetateBackend *backend);
// Non-zero while the audio stream for the last play call is running.
ACETATE_BACKEND_API int acetate_backend_is_playing(
   const AcetateBackend *backend);
// Current playback position in seconds, or a negative value when idle.
ACETATE_BACKEND_API double acetate_backend_play_position(
   const AcetateBackend *backend);

// Selection and edits. The time selection lives in Audacity's ViewInfo, edits
// run Audacity's own Clear on the wave tracks and push an undo state, and
// undo/redo use its UndoManager, so history matches the wx application. Edits
// are refused while the audio engine is busy, as the app's menu does.
// Each successful edit, undo or redo bumps acetate_backend_revision.
ACETATE_BACKEND_API void acetate_backend_set_selection(
   AcetateBackend *backend, double t0, double t1);
ACETATE_BACKEND_API void acetate_backend_get_selection(
   const AcetateBackend *backend, double *t0, double *t1);
// Deletes the selected range from every wave track (ripple, like Edit >
// Delete) and collapses the selection to its start. Returns 0 on success.
ACETATE_BACKEND_API int acetate_backend_delete_selection(
   AcetateBackend *backend);
ACETATE_BACKEND_API int acetate_backend_can_undo(const AcetateBackend *backend);
ACETATE_BACKEND_API int acetate_backend_can_redo(const AcetateBackend *backend);
ACETATE_BACKEND_API int acetate_backend_undo(AcetateBackend *backend);
ACETATE_BACKEND_API int acetate_backend_redo(AcetateBackend *backend);

// Project files and export. All three block the calling thread until done and
// must be called from the host's main thread with playback stopped.
//
// Saves the project as an .aup3 at utf8Path using ProjectFileIO::SaveProject
// (an existing file is backed up and restored on failure, as in the app) and
// marks the current undo state as saved. Returns 0 on success.
ACETATE_BACKEND_API int acetate_backend_save(
   AcetateBackend *backend, const char *utf8Path);
// Replaces the current project with the .aup3 at utf8Path, loaded through
// ProjectFileIO::LoadProject. On failure the current project is kept.
ACETATE_BACKEND_API int acetate_backend_open(
   AcetateBackend *backend, const char *utf8Path);
// Mixes every wave track over the whole project and writes a 16-bit PCM WAV
// at utf8Path with Audacity's own PCM export plug-in. Returns 0 on success.
ACETATE_BACKEND_API int acetate_backend_export_wav(
   AcetateBackend *backend, const char *utf8Path);
// Non-zero when undo history has changes since the last save or open.
ACETATE_BACKEND_API int acetate_backend_has_unsaved_changes(
   const AcetateBackend *backend);
// Path of the saved project, or an empty string for an unsaved project. The
// pointer is valid until the next call that changes the project.
ACETATE_BACKEND_API const char *acetate_backend_project_path(
   AcetateBackend *backend);

// Horizontal view, stored in Audacity's ViewInfo like the wx application:
// `left` is the time at the left edge in seconds and `pixelsPerSecond` the
// zoom. The zoom is clamped by Audacity's own limits (ZoomInfo::SetZoom), and
// both values are saved in the .aup3. Changing the view is not an undoable
// edit and does not mark the project modified. Scroll limits are a host
// decision; the backend only stores the value.
ACETATE_BACKEND_API void acetate_backend_get_view(
   const AcetateBackend *backend, double *left, double *pixelsPerSecond);
// Returns the zoom actually applied after clamping.
ACETATE_BACKEND_API double acetate_backend_set_view(
   AcetateBackend *backend, double left, double pixelsPerSecond);
ACETATE_BACKEND_API void acetate_backend_zoom_limits(
   double *minPixelsPerSecond, double *maxPixelsPerSecond);

#ifdef __cplusplus
}
#endif
