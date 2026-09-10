# SecureStream

A purpose-built, secure streaming compositor that prevents creators from accidentally exposing personal information while recording or streaming.

> **The only things that can ever appear on stream are things the creator deliberately put there.**

SecureStream is **not** a wrapper around OBS. It is a standalone C++ compositor built from scratch with a security-first model: no implicit desktop capture, no arbitrary host visibility, explicit sources only, every source carries provenance metadata.

---

## Why SecureStream?

Every existing streaming tool (OBS, Streamlabs, etc.) runs directly on the creator's host OS. This is why creators constantly dox themselves:

- Desktop notifications, system tray, taskbar leak on screen
- Personal folders, browser tabs, saved passwords, history exposed
- Discord / Steam / personal apps visible during Alt-Tab or window switching
- Host IP address, MAC, hostname visible to plugins and capture
- Accidental window shares of personal content

These tools were built to capture *anything on the desktop* and treat that as a feature. For a creator who cares about privacy, that default is the problem.

SecureStream inverts this: **nothing is captured unless explicitly added as a source.**

---

## Not Gaming-Only

SecureStream is a general-purpose broadcast surface. The explicit-source-only model is even more critical for non-gaming creators who handle browser tabs, documents, research notes, and private data on the same machine they stream from.

**Supported creator workflows:**

| Type | Examples |
|---|---|
| Commentary | Analysis, breakdowns, news, OSINT, geopolitics |
| Reaction | Clip reactions, video breakdowns |
| Tutorials | Walkthroughs, tech demos, educational content |
| IRL | Direct-to-camera, vlogs |
| Investigation | Research, document review, data analysis |

**Scene presets built in:**
- **Commentary** — webcam bottom-right, background, lower-third text
- **Reaction** — large clip center, webcam small bottom-right
- **Side-by-Side** — two equal halves for comparisons
- **Picture-in-Picture** — full-screen content with small webcam overlay
- **Full Screen** — single source fills frame (BRB, intermission, slides)
- **Talking Head** — webcam centered, dark background, lower-third text

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  Host (Windows)                                          │
│  ┌────────────┐    ┌────────────┐    ┌────────────────┐  │
│  │  Daemon    │───►│ Transport │───►│  VM            │  │
│  │  (captures │    │ (shared   │    │  (compositor   │  │
│  │   webcam,  │    │  memory + │    │   runs here,   │  │
│  │   mic,     │    │  vsock)   │    │   isolated     │  │
│  │   game)    │    │           │    │   from host)   │  │
│  └────────────┘    └────────────┘    └────────────────┘  │
│                                            │             │
│                                       ┌────┴─────┐       │
│                                       │ Outputs  │       │
│                                       │ (record │       │
│                                       │  + live) │       │
│                                       └──────────┘       │
└──────────────────────────────────────────────────────────┘
```

**Three-layer save-state model:**

```
┌─────────────────────────────────────────┐
│  Session Layer  (ephemeral, auto-wiped)  │  ← runtime changes, downloads, cache
├─────────────────────────────────────────┤
│  User Layer     (persistent, customisable)│  ← scenes, layouts, hotkeys, themes
├─────────────────────────────────────────┤
│  Base Image     (immutable, signed)      │  ← OS + compositor + drivers
└─────────────────────────────────────────┘
```

- **Base image** — immutable, signed, never contains personal data
- **User layer** — persistent creator customizations (scenes, profiles, themes)
- **Session layer** — created fresh on every boot, deleted after streaming. This is the privacy firewall: the VM physically cannot remember anything that happened during a stream unless the creator chose to save it.

---

## Current Status

### Phase 1 — Compositor Core [COMPLETE]

The compositor runs on the host (no VM yet) and proves the full pipeline works:

- **Scene/source/filter model** with explicit-source-only rendering
- **Source provenance audit** — every source declares its origin (HostWebcam, HostMicrophone, VMLocal, Generated, etc.)
- **Webcam capture** via Media Foundation (NV12/RGB32/YUY2 → BGRA8)
- **Microphone capture** via WASAPI (16/24/32-bit PCM → float)
- **Image loading** via stb_image (PNG/BMP/JPEG → BGRA8)
- **Text rendering** via GDI (lower-third with semi-transparent bar)
- **Media source** — video file playback via Media Foundation (MP4, looping)
- **Scene presets** — Commentary, Reaction, Side-by-Side, PiP, Full Screen, Talking Head
- **Transitions** — cut + fade (crossfade)
- **Local recording** — H.264 + AAC MP4 via Media Foundation Sink Writer
- **Live output** — H.264 + AAC encoded via Media Foundation, UDP transport (full SRT protocol in Phase 5)

### Phase 2 — VM Integration [NEXT]

- Transport layer: shared memory ring buffer + vsock notification (zero-copy, <4ms target)
- Software-defined clock: RDTSC-based monotonic timeline in the daemon
- Per-source circular buffers with jitter control (4-8ms, adaptive sizing)
- Minimal VM with compositor running inside, isolated from host
- Host webcam + mic + game capture passthrough via transport layer
- Basic daemon: boot/stop VM, inject stream key, own the clock

### Phase 3 — Save States
### Phase 4 — Creator UX (Tauri UI)
### Phase 5 — Hardening & Performance (GPU passthrough, WHIP, full SRT, delta updates)

See [DESIGN.md](DESIGN.md) for the full architecture and design document.

---

## Build (Windows)

**Prerequisites:**
- Visual Studio 2026 (or 2022 with C++ build tools)
- CMake 4.x
- Ninja

```bash
# From the repo root:
.\build.bat     # initializes MSVC dev environment + configures CMake
.\compile.bat   # builds
```

**Output:**
```
build\compositor\securestream_test.exe
```

## Run

```bash
.\build\compositor\securestream_test.exe
```

The test executable:
1. Lists available scene presets
2. Prints a source provenance audit (security check)
3. Renders a test frame
4. Starts the render loop at 1920x1080@60fps
5. Switches scenes with fade transitions (Commentary → Reaction → BRB)
6. Writes a real MP4 recording to `recordings/`
7. Sends encoded H.264+AAC over UDP to `127.0.0.1:8888`
8. Stops cleanly after 8 seconds

---

## Project Structure

```
SecureStream/
├── CMakeLists.txt              # Root CMake config
├── build.bat                   # Initializes MSVC environment + CMake configure
├── compile.bat                 # Builds the project
├── DESIGN.md                   # Full architecture & design document
├── compositor/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── types.hpp           # Frame, AudioChunk, Timestamp types
│   │   ├── source.hpp          # Source base class + provenance
│   │   ├── scene.hpp           # Scene + scene items
│   │   ├── filter.hpp          # Filter base class
│   │   ├── output.hpp          # Output base class
│   │   ├── compositor.hpp      # Compositor render loop
│   │   ├── transition.hpp      # Cut + fade transitions
│   │   ├── scene_presets.hpp   # Pre-built scene layouts
│   │   ├── sources/
│   │   │   ├── image_source.hpp
│   │   │   ├── text_source.hpp
│   │   │   ├── webcam_source.hpp
│   │   │   ├── microphone_source.hpp
│   │   │   └── media_source.hpp
│   │   └── output/
│   │       ├── file_output.hpp
│   │       └── srt_output.hpp
│   ├── src/
│   │   ├── compositor.cpp
│   │   ├── scene.cpp
│   │   ├── source.cpp
│   │   ├── filter.cpp
│   │   ├── output.cpp
│   │   ├── transition.cpp
│   │   ├── scene_presets.cpp
│   │   ├── main.cpp            # Test executable
│   │   ├── sources/
│   │   │   ├── image_source.cpp
│   │   │   ├── text_source.cpp
│   │   │   ├── webcam_source.cpp
│   │   │   ├── microphone_source.cpp
│   │   │   └── media_source.cpp
│   │   └── output/
│   │       ├── file_output.cpp
│   │       └── srt_output.cpp
│   └── third_party/
│       └── stb_image.h         # Single-header image loading
├── daemon/                     # Phase 2
├── ui/                         # Phase 4 (Tauri)
└── vm/                         # Phase 2
```

---

## Security Model

- **No implicit capture.** The compositor has no desktop capture or window scanning. Sources are added explicitly.
- **Source provenance.** Every source records where it came from. The provenance is auditable.
- **No plugin arbitrary code.** v1 has no third-party plugin system.
- **Render allowlist.** The compositor maintains a list of what it is allowed to render. If a source isn't on the list, it doesn't reach the encoder.
- **Single exit point.** There is one output stage. The creator knows exactly where their stream goes.
- **Session layer isolation.** The VM cannot remember anything that happened during a stream unless the creator explicitly saved it.

---

## License

MIT
