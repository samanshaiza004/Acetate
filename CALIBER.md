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

The compressor gain-reduction envelope kernel also runs in Odin. Its direct
in-process call is synchronous and allocation-free; the C++ processor retains
settings, cross-block envelope state, peak-meter atomics, lookahead storage and
configuration, and scheduling. The same processor serves offline effects and
realtime-effect processing, so its sample buffers never enter Caliber's command
queue.

The compressor's gain-reduction lookahead ring processing also runs in Odin.
C++ retains and allocates the ring buffer and owns delay configuration, push,
read, and processor lifetime; Odin traverses and shapes the borrowed ring
synchronously without allocation.

Odin also applies each channel's gain-reduction envelope to the delayed samples
and computes that channel's peak sample and index. C++ retains the delayed
channel buffers, shifts their remainder, aggregates frame statistics, and
owns processor settings and scheduling.

The Select > At Zero Crossings command keeps its existing Audacity checks,
sample-window reads, and selection/history updates. Odin scores the fetched
channels and selects the candidate offset synchronously. PCM buffers bypass
Caliber's general command queue; Caliber remains the bounded control-plane
transport for the Snap commands.

Select > At Time also delegates its six relative-offset formulas to a
synchronous Odin C ABI function. Project and selection values remain owned and
read by Audacity; the Odin function returns only the two calculated endpoints.
This pure calculation does not create a Caliber context or queue a command.

The scriptable Select Tracks command delegates its per-track range/mode decision
to Odin. C++ retains TrackList traversal and calls `Track::SetSelected`,
including the original Set/Add/Remove side-effect pattern and fractional range
comparisons.

Shift-click track selection delegates the anchor fallback and inclusive range
decision to Odin. The C++ selection adapter continues to resolve weak Track
references, apply `SetSelected` operations, and retain the project-local anchor
for rollback by `SelectionStateChanger`.

Track-list click selection also delegates the modifier/anchor routing decision
to Odin. C++ retains selection mutation and selected-time-range updates.

Keyboard selection extension/contraction delegates track/screen boundary
clamping and endpoint crossing constraints to Odin after C++ resolves the
snapped or pixel-based candidate time. Selection mutation, viewport scrolling,
and history remain in C++.

The schema and command belong to Audacity. Caliber only transports bounded
bytes, owns the command queue and publication, checks revisions, and exposes
the C ABI. Session lifetime is bounded by the project toolbar. The adapter
holds the project and session alive across synchronous observers, and rejects
calls from a non-UI thread.

This is an incremental migration, not a complete Audacity backend rewrite or an
asynchronous backend. The project adapter still calls Audacity's C++ model
synchronously on the UI thread. Fade runs on the offline effect worker; the
compressor kernel also runs in realtime-effect processing. Neither sends audio
buffers through Caliber's general command queue.

## Build

Use a clean Caliber checkout at the exact revision in
`cmake-proxies/cmake-modules/CaliberSnapPin.cmake`:

```sh
git clone https://github.com/samanshaiza004/caliber.git /path/to/caliber
git -C /path/to/caliber checkout <pinned-revision>
cmake -S . -B build-caliber \
  -Daudacity_use_odin_caliber_backend=ON \
  -DAUDACITY_CALIBER_SOURCE=/path/to/caliber
cmake --build build-caliber --target Audacity audacity-caliber-snap-test audacity-caliber-snap-bridge-test audacity-caliber-odin-backend-test lib-dynamic-range-processor-test
ctest --test-dir build-caliber -R 'audacity-caliber|lib-dynamic-range-processor' --output-on-failure
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
backend test checks Fade, Normalize, and compressor block math plus zero-crossing
and compressor parity with the C++ calculations. It covers attack and release,
soft-knee processing, sample-state continuity across blocks, in-place
processing, and empty input. The dynamic-range processor test also exercises
the C++ integration path with Odin enabled. The focused CTest command runs
these alongside the four Caliber/Odin-specific tests.
