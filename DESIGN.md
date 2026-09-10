# SecureStream — Architecture & Design Document

A purpose-built, secure streaming compositor running inside a fast-deployable virtual machine.

> **One-line pitch:** A streaming tool built from scratch for secure broadcasting — the only things that can ever appear on stream are things the creator deliberately put there.

> **Not gaming-only.** SecureStream is a general-purpose broadcast surface for commentary channels, reaction channels, analysis, OSINT, news, tutorials, IRL — any creator who wants a clean, safe, controlled streaming environment. The explicit-source-only model is even more critical for non-gaming creators who handle browser tabs, documents, research notes, and private data on the same machine they stream from.

---

## 1. Problem Statement

Every existing streaming tool (OBS, Streamlabs, etc.) runs directly on the creator's host OS. This is why creators constantly dox themselves:

- Desktop notifications, system tray, taskbar leak on screen
- Personal folders, browser tabs, saved passwords, history exposed
- Discord / Steam / personal apps visible during Alt-Tab or window switching
- Host IP address, MAC, hostname visible to plugins and capture
- Accidental window shares of personal content

These tools were built to capture *anything on the desktop* and treat that as a feature. For a creator who cares about privacy, that default is the problem. You cannot guarantee no personal data leaks when the streaming software assumes the entire host is fair game for capture.

SecureStream takes a different starting point: **the broadcast surface is isolated from the personal machine by design, and the compositor is built to enforce that isolation.**

---

## 2. What SecureStream Actually Is

Two things, together:

1. **A secure compositor** — our own scene compositor, built from scratch, designed so that every source, every scene, and every pixel that reaches the encoder passes through code we control. Not OBS. Not a wrapper around OBS. Our own software, with the security model baked into the rendering pipeline rather than bolted on top.

2. **A fast-deployable VM** — the compositor runs inside a virtual machine that boots clean, has no personal data by construction, and can be reset to a known-good state at any time. The creator's real machine is physically incapable of leaking because it's not part of the broadcast surface.

The combination is the point. A secure compositor on a personal OS still has the host leaking through capture sources. An isolated VM running OBS still has OBS's assumptions about what a source can be. You need both: a compositor that only knows about deliberately-added sources, running on a machine that only contains deliberately-added data.

---

## 3. Goals & Non-Goals

### Goals
- A clean, isolated broadcast surface with zero personal data by default.
- A purpose-built compositor where the security model is part of the rendering pipeline, not a layer on top.
- Passthrough of the creator's real webcam and microphone into the VM.
- Customisable, persistent streaming environment (scenes, layouts, hotkeys).
- Optional ephemeral "fresh every stream" mode for maximum privacy.
- Local recording to host disk, with the same security guarantees as the live stream.
- Fast deployment — a creator goes from install to streaming in minutes, not hours of VM setup.
- Encrypted, low-latency output pipeline.
- A creator-facing UX that hides the VM layer entirely.
- Windows host as the primary target platform.

### Non-Goals (v1)
- Cloud-hosted streaming relay (host-local VM only for v1).
- Multi-VM concurrent streaming.
- OBS plugin compatibility (we build our own source/scene/filter system).
- Mobile streaming capture.
- Being a general-purpose OBS replacement for all streamers. SecureStream is specifically for creators who need the privacy and isolation guarantees.

### Honest Scope Note
Building a custom compositor is a serious undertaking. A scene compositor with capture sources, encoding, transitions, filters, and low-latency output is months of work on its own, before the VM layer. This is not a shortcut to market — it's the only way the security guarantee is real rather than hoped-for. If the compositor isn't ours, we can't guarantee what reaches the encoder.

---

## 4. High-Level Architecture

```
┌──────────────────────────── HOST ────────────────────────────┐
│  Personal OS (Windows/Linux/macOS)                           │
│  ┌─────────────┐  ┌───────────┐  ┌────────────────────────┐  │
│  │  Webcam     │  │  Mic      │  │  Game / Window Capture  │  │
│  └──────┬──────┘  └─────┬─────┘  └───────────┬────────────┘  │
│         │                │                    │               │
│         └────────┬───────┴────────────────────┘               │
│                  ▼                                            │
│         ┌──────────────────────┐    ┌──────────────────────┐  │
│         │  Peripheral Bridge    │───▶│  SecureStream Daemon │  │
│         │  (USB/v4l2/audio)     │    │  (orchestration, UI, │  │
│         └──────────────────────┘    │   key vault, save    │  │
│                                       │   states)           │  │
│                                       └──────────┬───────────┘  │
│                                                  │              │
│                                       ┌──────────▼───────────┐  │
│                                       │   VM Manager         │  │
│                                       │   (QEMU/KVM or        │  │
│                                       │    VirtualBox/VMware)│  │
│                                       └──────────┬───────────┘  │
└──────────────────────────────────────────────────┼──────────────┘
                                                    │
┌───────────────────────────────────────────────────▼────────────┐
│                       VM (Broadcast Surface)                    │
│  Minimal OS  •  SecureStream Compositor  •  Virtual GPU/Audio   │
│  No personal apps, no browser, no host integration               │
│  Encoders: NVENC/AMF/QSV/VAAPI  →  SRT / WHIP / RTMPS            │
└─────────────────────────────────────────────────────────────────┘
```

Three layers:

1. **Host** — owns real peripherals, capture, and the orchestration daemon. Never appears on stream.
2. **VM** — the only thing the audience ever sees. Minimal, curated, isolated. Boots clean every time.
3. **Compositor** — runs inside the VM. Our software. Decides what reaches the encoder. The security boundary lives here.

---

## 5. The SecureStream Compositor

This is the heart of the project. Not OBS. Not a fork of OBS. Purpose-built.

### 5.1 Why Build Our Own

OBS treats "anything on the desktop" as a valid source. Its capture model, plugin API, and scene system all assume the host is fair game. You can add warnings, redaction overlays, and privacy scans on top, but you can't change the fundamental assumption: the compositor will render whatever a source gives it, and a source can be anything on the OS.

SecureStream's compositor inverts the default: **a source must be explicitly added and explicitly permitted.** There is no "capture the whole desktop" source. There is no "capture any window" source. Sources are specific, declared, and auditable. The compositor refuses to render anything it wasn't told about.

This is only possible if we own the compositor. Patching OBS to behave this way would mean fighting its architecture at every turn.

### 5.2 Core Concepts

- **Source:** a deliberately-added input — a webcam feed, a mic feed, a host game capture feed, an image, a media clip (video file), a text source, a browser source (sandboxed, later phase). Each source has a declared type, a declared origin, and a permission scope. The compositor will not render a source that hasn't been declared and permitted.

- **Scene:** a collection of sources with layout, filters, and transitions. Scenes are the unit of what the audience sees. Switching scenes is the only way to change what's on stream.

- **Filter:** a transform applied to a source or scene — crop, scale, colour, blur, redaction. Filters are part of the rendering pipeline, not post-processing.

- **Output:** the encoded stream. Takes a scene, encodes it (NVENC/AMF/QSV/VAAPI), and sends it via a transport (SRT/WHIP/RTMPS). The output stage is the only exit point from the compositor.

### 5.3 Security Properties of the Compositor

- **No implicit capture.** The compositor has no "desktop capture" or "window capture" that scans the OS. Sources are added explicitly by the creator or fed in from the host via the daemon.

- **Source provenance.** Every source records where it came from — host webcam, host game feed, VM-local file, network. The provenance is visible in the UI and auditable.

- **No plugin arbitrary code.** v1 has no third-party plugin system. Filters and sources are built-in. (A sandboxed plugin system is a later-phase possibility, but it is not in the core.)

- **Render allowlist.** The compositor maintains a list of what it is allowed to render. If a source isn't on the list, it doesn't reach the encoder. This is enforced in the rendering code, not in the UI.

- **Single exit point.** There is one output stage. There is no "also save to disk" or "also send to a second endpoint" unless explicitly configured. The creator knows exactly where their stream goes.

### 5.4 What the Compositor Does Not Do (v1)

- No desktop capture. (Host game capture is fed in as a specific source from the daemon, not by the compositor scanning the host.)
- No browser-source arbitrary URL loading without sandboxing.
- No third-party plugins.
- No recording to VM-local disk. Recording is supported, but it goes to the host via the daemon, never to storage inside the VM (see §8.1).

### 5.5 Local Recording

Recording is a first-class feature, not a later addition. The compositor produces an encoded stream that can go to two destinations simultaneously:

- **Live output** — SRT / WHIP / RTMPS to a streaming platform.
- **Local recording** — encoded file sent to the host daemon over virtio-vsock, written to host disk. Never touches VM-local storage.

This means recordings are saved on the creator's real machine (where they actually want them), but the recording pipeline is still controlled by the compositor — the same source allowlist and security properties apply. The VM remains stateless from the recording's perspective.

Recording format: MKV or MP4 (H.264/HEVC + AAC/Opus). Multi-track audio support (mic, game, music on separate tracks) is a v1 goal.

---

## 5.6 Timing System — Software-Defined Clock & Jitter Control

Latency is a first-class concern. The target is <16ms added latency from host capture to encoder output. This is achievable, but only if time is treated as a primitive that every stage of the pipeline respects — not something each subsystem guesses at independently.

### The Clock

A single authoritative monotonic clock lives in the **host daemon**, not the VM. The VM is untrusted — it does not own time. The daemon owns:

- Capture timestamps (when a frame/audio chunk was captured on the host)
- Transport timestamps (when it was sent over vsock to the VM)
- Intended presentation time (when the compositor should render it)

The clock is `std::chrono::steady_clock` (monotonic, never jumps backwards). Wall clock is never used for timing — NTP sync can cause discontinuities.

Every frame and audio chunk carries:
```
struct TimestampedPacket {
    int64_t capture_time_us;       // when captured on host
    int64_t send_time_us;         // when sent over vsock
    int64_t presentation_time_us;  // when the compositor should present it
    // ...payload (frame data or audio samples)
};
```

### Host→VM Clock Correlation

The VM's local `steady_clock` has an offset from the host's clock. At vsock connection time, the daemon and VM exchange a few round-trip messages to measure the offset. The VM then converts host timestamps to its local timeline for jitter buffering.

This correlation is measured once at connection and re-verified periodically. If drift exceeds a threshold, the jitter buffer is flushed and re-synced.

### Circular Buffers & Jitter Control

Each source in the daemon has a circular buffer:

```
Per-source circular buffer:
  [frame 0: capture=0us, present=4000us]
  [frame 1: capture=16666us, present=20666us]
  [frame 2: capture=33333us, present=37333us]
  ...
```

The buffer holds frames for a short period (4-8ms target) and releases them on schedule. This smooths jitter from:
- Variable capture latency (webcam frame timing)
- vsock transport jitter
- OS scheduling delays

**Late frames are dropped**, not held. A frame that arrives past its presentation time is worse than a dropped frame — it causes stutter and desync. The buffer prefers to drop and let the encoder duplicate the previous frame.

**Audio gets a separate buffer** with different parameters. Audio jitter is less visible than video jitter, but audio/video sync drift is immediately noticeable. The buffers are correlated on the same timeline so the daemon can nudge one relative to the other.

### Adaptive Buffer Sizing

The 4-8ms target is a starting point. The daemon measures actual jitter at each stage and adapts:

- If measured jitter is consistently <2ms, shrink the buffer (lower latency)
- If frames are being dropped frequently, grow the buffer (more stability)
- If audio/video drift exceeds 5ms, trigger a resync

### Latency Measurement

Every stage is instrumented:

```
Stage                  Measured
─────────────────────────────────────
Host capture           capture_time_us
Daemon buffer          buffer_entry_time → buffer_exit_time
vsock send             send_time_us
vsock receive          recv_time_us (VM local, converted to host timeline)
Compositor render      render_start_us → render_end_us
Encoder                encode_start_us → encode_end_us
Network send           send_time_us
```

This data feeds a latency dashboard in the UI (creator-facing: simple "total latency" number; developer-facing: full per-stage breakdown). The dashboard is not a feature — it's a debugging tool that makes the <16ms target verifiable rather than aspirational.

### Why This Works

Professional broadcast systems have used this approach for decades: single clock, timestamped packets, jitter buffers, drop-late-frames. The difference is that SecureStream applies it across a VM boundary, which adds the clock correlation step. That step is well-understood (it's the same problem as NTP, just local and lower-latency).

The "inline atomic clock" is not a literal atomic clock — it's a software-defined monotonic timeline with the same property that matters: it never goes backwards, and every component agrees on what time it is.

---

## 5.7 Transport Layer — Localised Zero-Copy Host→VM

The host→VM transport is the lowest-level piece of Phase 2. It determines the latency floor for everything above it. The design here is adapted from a software-defined RDMA project (`C:\Users\htsou\Desktop\RDMA`) that already solved the hard problems: lock-free ring buffers, memory-mapped shared regions, sequence-numbered packets, and reordering. The RDMA project was built for cross-system network access; SecureStream uses the same ring buffer and packet structures but swaps the network transport for local shared memory + vsock notifications.

### Why Not Just vsock?

vsock alone copies data through the virtio transport. For a 1080p60 BGRA8 frame (8.3MB per frame, 500MB/s), that's a lot of copying. Shared memory + vsock notification avoids the copy:

- Frame data lives in a shared memory ring buffer (zero-copy)
- vsock carries only a tiny notification packet (slot index + timestamp)
- The VM reads the frame directly from shared memory

This is the same pattern the RDMA project uses, just with the transport channel changed from raw sockets to local shared memory.

### Architecture

```
Host daemon                              VM compositor
─────────────                            ────────────
Capture frame                            ┌─────────────────────────┐
    │                                    │ Shared memory ring      │
    ▼                                    │ (file-mapped, lock-free)│
Write frame to ring slot N ─────────────►│ slot[N] = frame data    │
    │                                    │ head = N+1 (atomic)     │
    ▼                                    └─────────────────────────┘
Send vsock notification                        │
{slot: N, pts: X, seq: S} ──────────────►    ▼
                                         Read slot N from ring
                                         (zero-copy, no vsock payload)
                                         Render frame
                                         Send vsock ACK ──────────► Release slot N
```

The frame data never crosses vsock. Only the notification does. The notification is ~32 bytes. The frame is 8.3MB. This is why it's fast.

### Shared Memory Ring Buffer

Adapted from the RDMA project's `MemoryMappedRingBuffer` (`raw_network_bypass.py` lines 111-197) and the kernel driver's `LOCKFREE_RING` (`windows_dma_driver.cpp` lines 34-41):

```
struct RingBuffer {
    // Backing store: Windows file mapping (CreateFileMapping + MapViewOfFile)
    // On Linux VM: /dev/shm or memfd_create
    void* buffer;              // mmap'd region, power-of-2 sized
    uint32_t size;             // number of slots (power of 2)
    uint32_t mask;             // size - 1, for fast modulo
    uint32_t item_size;        // bytes per slot (frame size + metadata)

    // Head/tail in separate cache lines to avoid false sharing
    // (____cacheline_aligned in the kernel driver)
    volatile uint32_t head;    // write index (daemon updates)
    volatile uint32_t tail;    // read index (VM updates)
};
```

**Lock-free push (daemon side):**
```
head = atomic_load(ring->head)
next_head = (head + 1) & ring->mask
if next_head == ring->tail: return FULL  // drop frame, don't block
memcpy(ring->buffer + head * item_size, frame, frame_size)
atomic_store(ring->head, next_head)      // memory barrier before publish
```

**Lock-free pop (VM side):**
```
tail = atomic_load(ring->tail)
if tail == ring->head: return EMPTY
frame = ring->buffer + tail * item_size  // zero-copy read
atomic_store(ring->tail, (tail + 1) & ring->tail)  // release slot
```

Key properties from the RDMA project:
- Power-of-2 sizing → mask instead of modulo (faster)
- Head/tail in separate cache lines → no false sharing between host and VM
- Memory barriers before publishing head/tail → correct visibility ordering
- Drop-on-full, never block → the daemon never stalls waiting for the VM

### Packet Structure

Adapted from the RDMA project's `MemoryPacket` (`udp_memory_bridge.py` lines 25-79) and `dma_packet` (`virtual_dma_driver.c` lines 53-58):

```
struct TransportPacket {     // sent over vsock (notification only)
    uint32_t magic;          // 0x53535452 ('SSTR')
    uint8_t  stream_id;      // 0=video, 1=audio, 2=control
    uint8_t  flags;          // bit0: keyframe, bit1: last, etc.
    uint16_t slot;           // ring buffer slot index
    uint32_t sequence;       // monotonic, for reordering
    int64_t  capture_time;   // RDTSC-derived, host timeline
    int64_t  presentation_time; // when compositor should present
    uint32_t payload_size;   // size of data in the ring slot
    uint32_t checksum;       // CRC32 of the ring slot data
};  // 40 bytes — fits in a single vsock message
```

The frame data itself is in the ring buffer slot. The vsock packet only says "go look at slot N."

### Sequence Numbers & Reordering

From the RDMA project's `udp_memory_bridge.py` — UDP packets arrive out of order, so the receiver buffers and reorders. vsock is in-order and reliable, so reordering is less critical, but the sequence numbers still serve two purposes:

1. **Drop detection.** If the VM sees sequence 100, then 104, it knows frames 101-103 were dropped by the daemon (ring was full). The compositor duplicates the previous frame.
2. **A/V sync.** Audio and video packets share the same sequence space, so the compositor can detect if audio is running ahead or behind video.

### RDTSC as the Clock Source

From the RDMA project's `windows_dma_driver.cpp` (lines 156-169):

```c
__forceinline uint64_t rdtsc() {
    return __rdtsc();  // x86 cycle counter, sub-nanosecond resolution
}
```

RDTSC is faster than `steady_clock` (nanoseconds vs microseconds) and more precise. The daemon converts RDTSC ticks to microseconds using a calibration factor measured at startup:

```
rdtsc_frequency = measure over 100ms window
microseconds = rdtsc_ticks * 1e6 / rdtsc_frequency
```

This is the "inline atomic clock" from §5.6 — RDTSC is the underlying hardware counter, and the daemon derives a monotonic microsecond timeline from it. Every packet's `capture_time` and `presentation_time` are in this timeline.

### CPU Affinity & Real-Time Priority

From the RDMA project's `windows_dma_driver.cpp` (lines 172-182):

- **Daemon capture threads:** pinned to dedicated cores, `THREAD_PRIORITY_TIME_CRITICAL`
- **Compositor render thread:** pinned to a dedicated core, real-time priority
- **vsock notification thread:** pinned to a dedicated core

This prevents OS scheduling jitter from causing frame timing variance. Without it, a background task can preempt the capture thread and add 5-10ms of latency to a single frame. With it, the capture thread runs when it needs to, every time.

### Windows Shared Memory Specifics

The RDMA project uses `/dev/shm` (Linux). On Windows, the equivalent is:

```c
// Host side (daemon)
HANDLE hMap = CreateFileMappingW(
    INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
    0, ring_size, L"SecureStream_TransportRing");

void* shared = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, ring_size);

// VM side (if Hyper-V with GPU-PV, the VM can map the same section)
// If not, fall back to vsock for frame data (slower but still works)
```

**Important limitation:** Hyper-V VMs cannot directly map host-side file mappings unless using a shared memory device (e.g., via a virtio-shared-memory-device or Hyper-V's GPU-PV shared resource). If shared memory is not available between host and VM, the transport falls back to vsock-only (frame data goes through vsock, higher latency but still functional). The ring buffer and packet structure are identical either way — only the backing store changes.

### Transport Fallback Chain

```
1. Shared memory ring + vsock notification  (target: <4ms latency)
   ↓ if shared memory not available
2. vsock with frame data in payload         (target: <8ms latency)
   ↓ if vsock not available
3. TCP loopback                             (target: <12ms latency)
```

The daemon probes the transport at startup and selects the fastest available option. The compositor doesn't know or care which transport is in use — it just receives timestamped packets.

### What We're Taking From the RDMA Project

| Component | Source | Adaptation |
|---|---|---|
| Lock-free ring buffer | `windows_dma_driver.cpp` lines 34-153 | Backing store: `/dev/shm` → Windows file mapping |
| Memory-mapped ring | `raw_network_bypass.py` lines 111-197 | Same algorithm, Windows API |
| Packet structure | `udp_memory_bridge.py` lines 25-79 | Add `slot` field, remove network headers |
| Sequence + reordering | `udp_memory_bridge.py` | Keep for drop detection + A/V sync |
| RDTSC timestamping | `windows_dma_driver.cpp` lines 156-169 | Use as the clock source for §5.6 |
| CPU affinity + RT priority | `windows_dma_driver.cpp` lines 172-182 | Apply to daemon + compositor threads |
| Cache-line alignment | `ultra_low_latency_dma.c` line 60 | Keep `____cacheline_aligned` equivalent |

### What We're NOT Taking

- The UDP transport (we use vsock + shared memory, same machine)
- The raw socket bypass (we don't need to bypass the network stack, there is no network)
- The virtual PCIe tunnel (process memory reading is for debugging, not streaming)
- The kernel driver (user-space is sufficient for streaming, no signing requirements)
- The ZeroMQ transport (adds a dependency we don't need)

The RDMA project's ring buffer and packet infrastructure is transport-agnostic. We're using the same infrastructure with a local transport channel instead of a network one.

---

## 6. Save-State System (Hybrid Three-Layer Model)

The save-state system is what makes the VM feel like a streaming PC the creator owns, rather than a disposable container they have to set up every time. It resolves a contradiction: creators need a customisable environment *and* a clean-slate guarantee. The hybrid model puts those two needs in different layers so they stop fighting.

### 6.1 The Three Layers

```
┌─────────────────────────────────────────┐
│  Session Layer  (ephemeral, auto-wiped)  │  ← runtime changes, downloads, cache
├─────────────────────────────────────────┤
│  User Layer     (persistent, customisable)│  ← scenes, layouts, hotkeys, themes
├─────────────────────────────────────────┤
│  Base Image     (immutable, signed)      │  ← OS + compositor + drivers
└─────────────────────────────────────────┘
```

- **Base image (immutable):** the OS, the SecureStream compositor, drivers, virtual devices. Read-only, versioned, signed. Never written to. Never contains personal data. This is the safe foundation.

- **User layer (persistent):** the creator's customisations — scenes, layouts, hotkeys, themes, stream profiles. This is a copy-on-write overlay on the base. `Save State` commits changes here. Portable across machines with the same base. This is the layer that makes the VM feel like *their* streaming PC.

- **Session layer (ephemeral):** created fresh on every boot. All runtime writes go here — temp files, caches, anything downloaded during a stream. Discarded on shutdown unless explicitly promoted to the User layer. This is the privacy firewall: the VM physically cannot remember anything that happened during a stream unless the creator chose to save it.

### 6.2 Why the Session Layer Is the Core Guarantee

This is the load-bearing piece. Everything else — the Base, the User layer, passthrough, encryption — exists in other systems too. The Session layer is what makes SecureStream different from "a VM with snapshots."

It's not a policy ("we delete things on exit"). It's not a scan ("we check for leftover files"). It's a **physical fact about where writes go**: the disk layout is structured so that runtime writes land in a layer that is discarded by default. The VM cannot leak data it physically cannot retain.

That's a guarantee, not a promise. The distinction matters.

### 6.3 Two Operating Modes

- **Persistent mode (default):** the User layer is the creator's streaming environment. They customise freely. `Save State` commits the session layer into the User layer. Next boot, they pick up where they left off.

- **Ephemeral mode:** the User layer is frozen. Every stream boots from a known-good state. The session layer is discarded on exit. Nothing persists. Maximum privacy — useful for guest streams, sponsored content, or creators who want a guaranteed clean slate every time.

A single toggle switches between them.

### 6.4 Save/Restore Flow

1. **Boot:** VM mounts Base (read-only) + User (read-write, copy-on-write) + fresh Session (copy-on-write).
2. **Stream:** all writes hit the Session layer.
3. **End stream:** daemon prompts — *Discard*, *Save as new Save State*, or *Promote to User Layer*.
4. **Restore a Save State:** swap the User layer pointer, discard Session, reboot. Seconds-scale with qcow2 backing files.

---

## 7. Peripheral & GPU Passthrough

### 7.1 Webcam / Mic
- **Windows host (primary):** USB redirection via Hyper-V/VirtualBox/VMware USB passthrough, or an NDI/WebRTC local loopback feed from host to VM. Virtual audio devices via VB-Cable route mic into the VM.
- **Linux host (later):** v4l2loopback + PipeWire virtual sinks → virtio devices in VM.
- Devices are bound to the VM only while streaming; released back to the host otherwise.

### 7.2 GPU
Two tiers:

- **Tier 1 — GPU passthrough (dGPU partition):** best quality, full hardware encoding (NVENC/AMF). Requires a second GPU or iGPU/dGPU split. For gaming streams. On Windows host, this is via Hyper-V GPU partitioning (GPU-PV) or VMware/VirtualBox PCI passthrough where supported.
- **Tier 2 — Virtual GPU:** sufficient for 1080p60 compositor output, slides, browser sources, IRL scenes. No hardware requirements beyond the host GPU. Default for v1.

Host game capture is fed into the VM as a declared source (via the daemon, using DXGI Desktop Duplication on Windows), so the game itself never runs inside the VM in Tier 2 — only the compositor renders it.

### 7.3 Audio
VB-Audio Cable (Windows) routes host audio → VM virtual input. Latency monitored and auto-aligned by the daemon.

---

## 8. Host Daemon

The daemon is the orchestration layer on the host. It is the only thing the creator interacts with directly (via the UI). It:

- Boots and stops the VM.
- Manages save states (Base / User / Session layer lifecycle).
- Holds stream keys in an OS-native key vault (Windows DPAPI). Keys are injected into the compositor at go-live over a sealed channel (virtio-vsock) and held in memory only. The VM image never contains a usable key.
- Bridges peripherals (webcam, mic, game capture) into the VM as declared sources.
- Receives local recordings from the compositor over virtio-vsock and writes them to host disk. Recordings never touch VM-local storage.
- Provides the UI backend.

The daemon is the trust root. The VM is the untrusted surface. Keys, recordings, and control flow from daemon → VM, never the reverse.

---

## 9. Streaming Pipeline

Inside the VM, the compositor's encoded output is sent over:

| Protocol | Use case |
|---|---|
| **SRT** | Default. Low latency, packet-loss resilient, TLS-capable. |
| **WHIP** | WebRTC-based ingest for platforms supporting it (lowest latency). |
| **RTMPS** | Legacy compatibility with Twitch/YouTube (TLS-wrapped). |

All transports enforce TLS. The stream key is injected by the host daemon at go-live and is never persisted inside the VM image.

---

## 10. Security Model

### 10.1 Zero-Trust Broadcast Surface
The VM has, by construction:
- No personal files, no saved passwords, no browser history.
- No personal accounts (Discord, Steam, email).
- No host notifications, no system tray with personal apps.
- No host IP/MAC/hostname (NAT'd behind the hypervisor).
- No clipboard/shared-folder bridge to the host.

### 10.2 Compositor-Level Enforcement
- No implicit capture — sources must be explicitly declared.
- Source provenance is recorded and visible.
- No third-party plugins in v1 (no arbitrary code in the render path).
- Single, known output path.

### 10.3 Key Hygiene
Stream keys live only in the host key vault. Injected at go-live, held in memory, never persisted in the VM image.

---

## 11. Recommended Tech Stack

| Layer | Choice | Rationale |
|---|---|---|
| Hypervisor | Hyper-V (primary, Windows-native), VirtualBox/VMware (fallback) | Windows-native, GPU partitioning support, scriptable via PowerShell/WMI |
| VM disk | VHDX with differencing disks (Windows-native), qcow2 if using QEMU fallback | Native copy-on-write, fast snapshot/restore |
| VM OS | Debian-minimal (Linux) | Small, stable, low attack surface, runs the compositor |
| Compositor | **C++** | Native AV ecosystem (ffmpeg/libavcodec, gstreamer), performance-critical render path, mature tooling |
| Host daemon | Rust | Single static binary, strong concurrency, safe FFI to C++ compositor components where needed |
| UI | Tauri (Rust + web frontend) | Lightweight, cross-platform, matches daemon language |
| IPC host↔VM | virtio-vsock (via Hyper-V vsock or VMware) | Sealed channel, no network exposure |
| Key vault | Windows DPAPI | OS-native, user-scoped |
| Transports | SRT (default), WHIP, RTMPS | Modern + legacy coverage |
| Encoding | NVENC / AMF / QSV / VAAPI | Hardware encoding via virtual GPU or passthrough |

---

## 12. Risks & Tradeoffs

| Risk | Mitigation |
|---|---|
| Building a custom compositor is a large undertaking | Accept it. The security guarantee requires owning the render path. Scope v1 tightly: core sources, scenes, transitions, one transport. No plugins. |
| GPU passthrough complexity on consumer hardware | Default to Tier 2 virtual GPU; Tier 1 opt-in with a hardware compatibility checker. |
| Latency from VM hop | Use vsock + shared memory for host→VM source feed; target <16ms added latency. |
| Audio/video sync drift | Daemon-side timestamp correlation and auto-delay compensation. |
| Users wanting "their real desktop" on stream | Explicitly out of scope — the isolation is the feature. |
| No OBS plugin compatibility | Out of scope for v1. The compositor has its own source/filter system. A sandboxed plugin system is a later possibility. |
| Large base image distribution | Delta updates against signed base; qcow2 backing files keep user deltas tiny. |

---

## 13. Roadmap

**Phase 1 — Compositor core [COMPLETE]**
- Scene/source/filter model with explicit-source-only rendering ✓
- Webcam capture via Media Foundation (NV12 → BGRA8 conversion) ✓
- Microphone capture via WASAPI (16/24/32-bit PCM → float) ✓
- Image loading via stb_image (PNG/BMP/JPEG → BGRA8) ✓
- Text rendering via GDI (lower-third style with semi-transparent bar) ✓
- Media source: video file playback via Media Foundation (MP4/MKV, H.264/HEVC, looping) ✓
- Scene presets: Commentary, Reaction, Side-by-Side, Picture-in-Picture, Full Screen, Talking Head ✓
- Basic transitions: cut + fade (crossfade) ✓
- Local recording: H.264 + AAC MP4 via Media Foundation Sink Writer ✓
- Live output: H.264 + AAC encoded via Media Foundation, UDP transport ✓
- Source provenance audit (every source declares origin) ✓
- Runs on the host first, no VM, to prove the compositor works ✓
- **Note:** Full SRT protocol (reliability, encryption) requires libsrt — Phase 1 uses UDP transport. SRT will be layered in during Phase 5.
- **Note:** Browser source (CEF sandboxed) and window capture require the daemon + VM (Phase 2+).

**Phase 2 — VM integration**
- Transport layer: shared memory ring buffer + vsock notification (adapted from RDMA project)
- Software-defined clock: RDTSC-based monotonic timeline in the daemon
- Per-source circular buffers with jitter control (4-8ms target, adaptive sizing)
- Latency instrumentation at every stage (capture → buffer → transport → compositor → encoder → network)
- CPU affinity + real-time priority for capture and render threads
- Minimal Linux VM with virtio-gpu
- Compositor running inside the VM
- Host webcam + mic passthrough (timestamped, via transport layer)
- Host game capture fed in as a declared source (timestamped, via transport layer)
- Basic daemon: boot/stop VM, inject a stream key, own the clock, own the transport

**Phase 3 — Save states**
- qcow2 backing-file Base / User / Session layer split
- Save / restore / discard flows
- Persistent vs Ephemeral mode toggle

**Phase 4 — Creator UX**
- Tauri UI: scene management, profiles, go-live flow
- Stream Profiles (separate User layers per content type)
- Scene-only save state import/export

**Phase 5 — Hardening & performance**
- GPU passthrough (Tier 1) support + compatibility checker
- WHIP transport
- Delta base-image updates
- Audio sync auto-alignment

**Later phases (not committed)**
- Sandboxed plugin system
- Privacy audit scanner
- Sensitive-data redaction overlays
- VM attestation manifest
- These are possibilities enabled by the architecture, not part of the core product.

---

## 14. Decisions

1. **Compositor language:** **C++** — native AV ecosystem (ffmpeg/libavcodec, gstreamer), performance-critical render path, mature tooling.
2. **Host OS priority:** **Windows-first** — largest creator base. Hyper-V as primary hypervisor. Linux host support is a later phase.
3. **Local recording:** **Yes, in v1** — recording goes to host disk via the daemon, never to VM-local storage. Same source allowlist and security properties apply.

## 15. Open Questions

1. **Base image distribution:** self-hosted signed images, or a public registry with delta updates?
2. **Hypervisor on Windows:** Hyper-V (native, GPU-PV capable) vs VirtualBox/VMware (broader device passthrough). Need to prototype both before committing.
3. **Compositor UI in VM vs on host:** does the creator see the compositor's UI inside the VM window, or does the host daemon's UI control the compositor remotely over vsock? The latter hides the VM more completely but adds latency to UI interactions.

---

*Status: Draft v6 — Phase 1 COMPLETE. Phase 2 transport layer designed: localised zero-copy host→VM transport adapted from software-defined RDMA project (lock-free ring buffers, RDTSC clock, vsock notifications, shared memory frame data). Next step: Phase 2 VM integration.*
