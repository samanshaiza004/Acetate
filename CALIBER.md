# Experimental Caliber Snap boundary

This optional Audacity 3.7.9 fork change sends the Snapping toolbar checkbox
through Caliber's versioned C ABI. The normal Audacity path remains unchanged
unless `audacity_use_caliber_snap` is enabled.

## What the proof covers

Each project toolbar owns a persistent `SnapSession`. A checkbox change encodes
an application-owned command, dispatches it through Caliber, consumes it on the
UI thread, and applies the existing `ProjectSnap::SetSnapMode` operation. The
existing project observers still run. After they finish, Audacity publishes a
versioned snapshot containing the project identity, snap mode, nonlocalized
snap-unit identifier, and resulting time selection. The frontend reads that
snapshot and updates the controls. Revisions increase across edits in one
project; separate projects use separate sessions.

The schema and command belong to Audacity. Caliber only transports bounded
bytes, owns the command queue and publication, checks revisions, and exposes
the C ABI. Session lifetime is bounded by the project toolbar. The adapter
holds the project and session alive across synchronous observers, and rejects
calls from a non-UI thread.

This is a boundary dogfood, not an asynchronous backend or an audio-thread
feature. The C++ application operation currently runs synchronously on the UI
thread. Do not call this path from an audio callback. The eventual Odin backend
can replace the command consumer and state publisher while preserving the
application-owned schema and frontend contract.

## Build

Use a clean Caliber checkout at the exact revision in
`cmake-proxies/cmake-modules/CaliberSnapPin.cmake`:

```sh
git clone https://github.com/samanshaiza004/caliber.git /path/to/caliber
git -C /path/to/caliber checkout <pinned-revision>
cmake -S . -B build-caliber \
  -Daudacity_use_caliber_snap=ON \
  -DAUDACITY_CALIBER_SOURCE=/path/to/caliber
cmake --build build-caliber --target Audacity audacity-caliber-snap-test audacity-caliber-snap-bridge-test
ctest --test-dir build-caliber -R audacity-caliber-snap --output-on-failure
```

The feature is disabled by default. Configuration verifies the Caliber Git
revision and relevant source cleanliness, builds the pinned Rust static
library offline, and links it into Audacity. Rust 1.85 or newer and an already
populated Cargo dependency cache are required. The first experimental build
supports native macOS and Linux configurations; Windows, cross-compilation,
and universal macOS binaries remain unsupported by this opt-in module.

The project adapter test uses real `AudacityProject`, `ProjectSnap`,
`ProjectSelectionManager`, and `ViewInfo` state. It checks observer behavior,
selection snapping, per-project isolation, monotonic session revisions, and
off-thread rejection. The bridge test also covers ABI rejection, full queues,
failed and corrupted state operations, and repeated session use. Both targets
are part of the documented root CTest command.
