# Experimental Odin backend and Caliber transport

This optional Audacity 3.7.9 fork change sends the snapping toolbar toggle and
Snap-To mode menu commands through Caliber's versioned C ABI and an Odin
application backend. The normal Audacity path remains unchanged unless
`audacity_use_odin_caliber_backend` is enabled.

## What the proof covers

Each project toolbar owns a persistent `SnapSession`. Odin owns that session,
encodes and validates the application-owned command, consumes it, reads the
result, and publishes a versioned state snapshot through Caliber. The narrow
C++ project adapter invokes the existing `ProjectSnap::SetSnapMode` operation
and reads Audacity's project state on the UI thread. Existing project observers
still run. The published snapshot contains project identity, snap mode,
nonlocalized snap-unit identifier, and resulting time selection. Revisions
increase across edits in one project; separate projects use separate sessions.

The opt-in also routes the built-in Fade effect's offline block math through
Odin. Its sample index remains owned by the effect instance and advances across
blocks; Odin receives borrowed buffers synchronously. Most effects and the
remaining Audacity project and application logic still use their existing C++
implementations.

The Normalize effect's streaming DC accumulator and per-block offset/gain
transform also run in Odin. Audacity continues to own track traversal, peak
analysis, cancellation, progress reporting, and project mutation.

The schema and command belong to Audacity. Caliber only transports bounded
bytes, owns the command queue and publication, checks revisions, and exposes
the C ABI. Session lifetime is bounded by the project toolbar. The adapter
holds the project and session alive across synchronous observers, and rejects
calls from a non-UI thread.

This is an incremental migration, not a complete Audacity backend rewrite or an
asynchronous backend. The project adapter still calls Audacity's C++ model
synchronously on the UI thread. The Fade path runs on the offline effect worker,
not the realtime audio callback. Do not send audio buffers through Caliber's
general command queue; realtime DSP needs a separate realtime-safe ABI.

## Build

Use a clean Caliber checkout at the exact revision in
`cmake-proxies/cmake-modules/CaliberSnapPin.cmake`:

```sh
git clone https://github.com/samanshaiza004/caliber.git /path/to/caliber
git -C /path/to/caliber checkout <pinned-revision>
cmake -S . -B build-caliber \
  -Daudacity_use_odin_caliber_backend=ON \
  -DAUDACITY_CALIBER_SOURCE=/path/to/caliber
cmake --build build-caliber --target Audacity audacity-caliber-snap-test audacity-caliber-snap-bridge-test audacity-caliber-odin-effect-test
ctest --test-dir build-caliber -R audacity-caliber --output-on-failure
```

The feature is disabled by default. Configuration verifies the Caliber Git
revision and relevant source cleanliness, builds the pinned Rust static
library offline, compiles the Odin backend as a static library, and links both
into Audacity. Rust 1.85 or newer, Odin on `PATH`, and an already populated
Cargo dependency cache are required. The first experimental build supports
native macOS and Linux configurations; Windows, cross-compilation, and
universal macOS binaries remain unsupported by this opt-in module.

The Odin protocol test covers command versions, identity, and malformed input.
The project adapter test uses real `AudacityProject`, `ProjectSnap`,
`ProjectSelectionManager`, and `ViewInfo` state. It checks observer behavior,
selection snapping, per-project isolation, monotonic session revisions, and
off-thread rejection. The bridge test covers ABI rejection, full queues,
failed and corrupted state operations, and repeated session use. The Odin
effect test checks Fade and Normalize block math, chunk continuity, in-place
processing, and empty/invalid input. CTest runs all four tests.
