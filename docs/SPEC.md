\# Sector8 — Open FPGA Fantasy Console (v0.1 Framework Spec)

&#x20;

> \*\*Name: Sector8\*\* — the console, a product of the \*\*ParticleSector\*\* studio/org. The "8" nods to 8-bit heritage, 8×8 tiles/sprites, and the 8-button pad. \*(Working name; pending final trademark/domain clearance.)\*

> Status: v0.1 draft. This is a \*framework spec\*, not a board datasheet. Decisions marked \*\*\[OPEN]\*\* are still to be made.

> \*\*Resolved:\*\* all framework decisions. \*\*v0.1 is feature-complete\*\* — the MCU↔FPGA wire protocol is in Appendix A; what remains is implementation.

&#x20;

\---

&#x20;

\## 1. Core principle: a spec, not a board

&#x20;

The most important idea in this document:

&#x20;

\*\*We are specifying a \*logical machine\* (a stable contract). Physical boards are \*implementations\* of that contract.\*\*

&#x20;

A game (cart) targets the logical machine — never the board. As long as an implementation provides \*at least\* the logical machine's guarantees, the same cart runs on it unchanged. Three payoffs from one design:

&#x20;

\- A cart written today runs on the \*\*PC simulator\*\* (no hardware needed).

\- The same cart runs on the \*\*v0.1 kit\*\* (Tang Nano 20K + RP2350).

\- The same cart runs on \*\*future, beefier boards\*\* with no porting — they're just new implementations of the same spec.

PICO-8 carts run anywhere because they target the virtual machine. We inherit that, but our virtual machine can also be instantiated in real silicon and plugged into a TV.

&#x20;

\*\*Design rule that makes this work:\*\* the logical machine is defined in terms of \*\*minimum guarantees\*\*. A conforming implementation must provide \*at least\* these. It may provide more (bigger overlay, more sprites, higher output resolution), but a cart that stays within the baseline runs everywhere. \*\*The baseline never shrinks across versions.\*\*

&#x20;

\---

&#x20;

\## 2. What it is, and what inspires it

&#x20;

An \*\*open, teaching-focused fantasy console\*\* delivered as a low-cost two-board kit you plug into an HDMI TV with a USB gamepad.

&#x20;

\- \*\*From PICO-8:\*\* the design language — tight, deliberate constraints as a creative feature; the cart-as-data model; Lua; all-in-one friendly tooling; "make a whole game in one sitting."

\- \*\*From TIC-80:\*\* the posture — fully open, MIT-spirited, reproducible, school-friendly.

\- \*\*The white space neither fills:\*\* a \*real hardware\* fantasy console — open, cheap, on a TV — built for teaching. PICO-8 is closed/paid and hardware-less; TIC-80 is open but PC-only. Nobody ships an open FPGA console you plug into a television.

\### Goals

\- Approachable for \*\*extreme beginners\*\* (write Lua, plug in a pad, play).

\- \*\*Open\*\* (open spec, open SDK, open simulator, open MCU firmware).

\- \*\*Cheap\*\* — sub-$100 kit.

\- \*\*Real hardware\*\* on a real TV.

\- \*\*Forward-compatible\*\* by construction (§1).

\- A \*\*teaching ladder\*\* from pure software up to embedded hardware.

\### Non-goals

\- Not chasing fidelity. Intentionally below NES-class on raw specs, richer on color/parallax.

\- The \*\*FPGA is not user-programmed\*\* in normal use. It ships sealed.

\- Not a general-purpose computer.

\### Openness \& brand model

\*\*Sector8 is fully open; the brand is the moat.\*\* The spec, SDK, simulator, cart format, MCU firmware, and hardware design are published under permissive licenses (MIT/Apache-2.0 for code, CERN-OHL for hardware) so the community can build, port, teach, and remix — every contributor and every game grows the platform. The defensible asset is the \*\*ParticleSector / Sector8 brand (trademark) and the official kit\*\* — convenience, trust, all-parts-in-a-box — not secret code. This is the Arduino model: openly cloneable, but the official thing wins on brand and ease. (Firmware-openness is reversible and could be deferred — ship locked, open later — but the default and the intent is open.)

&#x20;

\---

&#x20;

\## 3. System architecture — a CPU + a video/sound chip

&#x20;

The machine is two cooperating parts, which is the \*\*classic console design\*\* (a CPU chip talking to a video/sound chip — like 6502 + PPU, or 68000 + VDP):

&#x20;

```

&#x20; ┌─────────────────────────┐     command/register stream      ┌──────────────────────────┐

&#x20; │  MCU  (the "CPU chip")   │ ───────────────────────────────► │  FPGA ("video/sound chip")│

&#x20; │  • Lua runtime (game)    │ ◄───── vsync / ready (GPIO) ───── │  • PPU: 2 BG + sprites    │

&#x20; │  • render-command gen    │                                   │  • overlay + rasterizer   │

&#x20; │  • USB host (gamepad)    │                                   │  • APU: wavetable + seq   │

&#x20; │  • SD (carts)            │                                   │  • compositor → HDMI A/V  │

&#x20; └─────────────────────────┘                                   └──────────────────────────┘

```

&#x20;

\- \*\*The MCU runs the game.\*\* The Lua VM, game logic, and the code that turns game state into render commands all live on the MCU.

\- \*\*The FPGA is a sealed GPU/APU coprocessor.\*\* It holds no game logic and no CPU — it composites graphics and synthesizes sound from the register/command state the MCU sends, and drives HDMI.

\- \*\*The bus between them is a documented command/register protocol\*\* (§5.3) — the \*protocol\* (opcodes + semantics) is the contract; the \*physical transport\* is implementation-defined (SPI in v0.1; a faster link permitted later). This protocol + the Lua runtime/API together \*are\* the stable contract.

\- \*\*Input is MCU-local.\*\* The gamepad is on the MCU's USB host and the game runs on the MCU, so `btn()` reads local state — no SPI round-trip for input.

Everything in §3 is \*\*board-independent\*\* and is what carts target.

&#x20;

\### 3.1 Display \& palette

\- \*\*Logical resolution: 320×180\*\*, fixed. 16:9.

\- \*\*Indexed color\*\*, 4 bits/pixel (a pixel stores a 0–15 index \*within\* a sub-palette).

\- \*\*Four 16-color sub-palettes\*\*, each color from a \*\*12-bit RGB\*\* gamut (4096). Each tile/sprite selects its sub-palette (2-bit field). Up to \*\*64 colors on screen\*\*.

\- \*\*Palette 0 is the default.\*\* A cart that never touches sub-palette selection just uses palette 0 — a plain flat-16 experience. Richness is opt-in; simplicity is the default.

\- Ships with a sensible default palette set; remappable at runtime.

\- \*(Per-scanline palette swaps for gradients: future effect tier.)\*

\### 3.2 Rendering: layered compositor

Per frame, front to back:

&#x20;

```

&#x20; ┌─────────────────────────────────┐  front

&#x20; │ OVERLAY   (free-draw, rasterized)│  pset/line/circ/print → FPGA draws from commands

&#x20; ├─────────────────────────────────┤

&#x20; │ SPRITES   (OAM, 64 / 16-line)    │  spr()  → register write

&#x20; ├─────────────────────────────────┤

&#x20; │ BG1 tilemap (scrollable)         │  parallax — independent scroll

&#x20; ├─────────────────────────────────┤

&#x20; │ BG0 tilemap (scrollable)         │  map()  → register write

&#x20; └─────────────────────────────────┘  back

&#x20;       │ per pixel: front-most non-transparent index wins

&#x20;       ▼

&#x20;  (sub-palette, index) → PALETTE LUT → scale ×4 → HDMI 720p

```

&#x20;

BG and sprite layers are \*\*scanline-composited\*\* — no full framebuffer needed. Only the overlay owns a real pixel buffer.

&#x20;

\*\*Two BG layers\*\* (BG0 + BG1) give true independent-scroll parallax, and \*\*both are a committed baseline guarantee\*\* — every conforming implementation provides them. This deliberately binds the contract to a feature the v0.1 FPGA build must deliver; the accepted build risk and its fallback are owned in the kit section, not the contract (see §5.1).

&#x20;

\### 3.3 The three drawing paths (the heart of the model)

Same friendly API; \*\*where\*\* you call determines cost:

&#x20;

| Tier | Example calls | Cost | Dynamic? | Mechanism |

|---|---|---|---|---|

| \*\*Baked\*\* | any draw inside `\_bake()` | free render — \*spends tile bank\* | no — frozen at build | run at build time, auto-tiled into BG tiles |

| \*\*PPU\*\* | `spr` `map` `mset` `camera` `pal` | cheap | yes | MCU writes registers; FPGA composites |

| \*\*Overlay\*\* | `pset` `line` `rect` `circ` `print` | moderate | yes | MCU sends draw commands; FPGA rasterizes |

&#x20;

The \*same\* `circ()` is a free baked tile if called in `\_bake()`, or an overlay-cost command in the game loop. The programmer writes one API; placement picks the cost. \~90% of real drawing is sprite/tile-shaped and rides the cheap PPU path; the overlay is the expressive escape hatch for HUD, text, and effects.

&#x20;

\*\*Baked isn't unconditionally free:\*\* baking trades \*render\* cost for \*tile-bank\* space, and it only pays off when the output is tile-redundant. Near-unique imagery dedups poorly — a full 320×180 screen is 900 raw 8×8 tiles before dedup, already past the 512-tile bank. Such art belongs in a \*\*scene-specific bank\*\* (bank-swap, §3.4) rather than the active baseline bank; the baker is what enforces this (see §7).

&#x20;

\### 3.4 Tiles \& maps

\- \*\*8×8 tiles\*\*, indexed, \*\*4bpp = 32 bytes/tile\*\*.

\- \*\*Unified bank shared by BG layers and sprites\*\* (any tile usable anywhere, one index space).

\- \*\*512 unique tiles in the active bank.\*\* SDRAM-resident; the compositor fetches per upcoming line (\~33 MB/s — trivial). \*\*Bank-swappable per scene\*\* (CHR-bank style) → total art across a game is effectively unlimited. Index field sized for 1024 (doubling = free capability tier).

\- Visible tilemap at 320×180: \*\*40 columns × 22.5 rows.\*\* The half-row (180 not ÷8) is handled by clipping the partial bottom row or a 4px letterbox.

\- \*\*Tilemap cell / OAM entry:\*\* tile index (10b) · sub-palette (2b) · H/V flip (2b) · priority (1b) → fits a 16-bit entry.

\### 3.5 Sprites

\- \*\*8×8 sprites\*\*, indexed, transparency color key, H/V flip, per-sprite sub-palette select.

\- \*\*64 total (OAM), 16 per scanline.\*\* The per-scanline cap (double the NES) is the real hardware limit / "flicker" threshold and is a contract minimum. Heavier density is a capability tier or moves to the overlay.

\### 3.6 Overlay layer

\- Indexed framebuffer in the FPGA, physically \*\*full 320×180×4bpp\*\* always.

\- \*\*Two readout modes\*\* via a mode register:

&#x20; - \*\*Mode 0 (default):\*\* 1:1 native 320×180 — crisp accents. Coords 0–319 / 0–179.

&#x20; - \*\*Mode 1 (pure-draw):\*\* treated as \*\*160×90\*\*, hardware nearest-neighbor doubles it to fill the screen — a PICO-8-style free-draw-the-whole-screen mode at a cheaper pixel count. Coords 0–159 / 0–89. Fixed at 160×90 for cart portability.

\- Cart selects the mode (at init, switchable). Scale-up is trivial fabric + one register.

\- \*\*Primitives are rasterized in the FPGA from compact MCU commands\*\* (e.g. "circle x,y,r,c"), so the link carries commands, not pixels. \*\*For full-screen software rendering, `OVL\_BLIT` pushes a \*packed\* pixel region\*\* — a whole 160×90 Mode-1 framebuffer is \~7.2 KB (well inside one frame); a full 320×180 Mode-0 push is \~28.8 KB. Only naive per-pixel command spam (thousands of `OVL\_PIXEL`s) is bandwidth-bound; primitives and blits are not, and bulk paths scale directly with transport speed.

\- \*\*Hardware auto-clears to transparent each vblank.\*\* `clip()` bounds the redraw region.

\### 3.7 Sound (APU)

\- \*\*4 channels, wavetable DDS\*\* (phase accumulator → 32-sample wavetable → envelope → volume). One channel can be LFSR noise.

\- \*\*Classic waveforms preloaded as defaults\*\* (square/duty, triangle, saw, sine, noise) so beginners get chiptune with no setup; \*\*custom wavetables\*\* are opt-in (mirrors the palette's presets-as-default pattern).

\- \*\*Optional FPGA-side sequencer:\*\* the FPGA can read song data and step note changes itself, so the MCU just says "play track 3" and audio runs autonomously — it can't cause a game-loop hitch.

\- \*\*Audio output over HDMI\*\* (embedded in the TMDS stream → TV speakers) is the \*\*committed baseline experience\*\* — one cable, the TV plays the sound. The onboard I²S amp + 3.5mm out is the \*\*fallback\*\* if the audio-island work slips (a UX change, not a contract change — see the gate in §5.5).

\### 3.8 Input

\- \*\*8-button gamepad\*\*: d-pad (4) + A/B/X/Y + Start/Select. Read locally on the MCU (USB host) — no SPI involvement.

\- \*\*2 players baseline\*\* — a \*\*single USB host with a hub\*\* (TinyUSB hub support) enumerates both pads, so Core 1 runs one USB host stack, not two. The carrier can still expose two physical USB-A ports, but behind the hub rather than as independent host controllers. `button(b, player)` carries the player index. \*\*4-player is a capability tier\*\* (a bigger hub, or a multi-host RP2350 board).

\### 3.9 The contract surface

Two stable surfaces define the machine:

1\. \*\*The Lua runtime + API\*\* the cart is written against (§4).

2\. \*\*The command/register model\*\* the FPGA exposes (transport-agnostic; SPI is v0.1's wire): tile bank · BG tilemaps + scroll · OAM · palette LUT · overlay (mode + draw commands + blit) · APU regs/sequencer. Double-buffered and swapped at vblank.

\*\*Working RAM:\*\* \~256 KB guaranteed Lua heap (of the RP2350's 520 KB SRAM, after firmware) — very generous for 2D. \*\*Cart size:\*\* \~1 MB baseline, \*\*bank-swappable per scene\*\* from SD → effectively unlimited total. Assets live in the FPGA's 8 MB SDRAM; the overlay buffer in FPGA block RAM.

&#x20;

\### 3.10 Code / language

\- \*\*Lua\*\*, carts shipped as precompiled bytecode, run by the MCU runtime.

\- \*\*Lifecycle:\*\* `\_init` / `\_update` / `\_draw` (logic vs render split), both at \*\*60 fps\*\*; plus `\_bake()` (build-time static).

\- \*\*Naming:\*\* verbose, readable names (`sprite`, `button`, `random`) are the \*\*canonical documented API\*\*; terse PICO-8 names (`spr`, `btn`, `rnd`) ship as \*\*compatibility aliases\*\* so PICO-8 habits and pasted snippets still work. \*\*One exception:\*\* PICO-8 code that pokes/peeks raw framebuffer addresses (e.g. `0x6000`) has no aliased equivalent — there is no MCU-visible VRAM to address; full-screen software rendering goes through the overlay blit path (§3.6) instead. Tutorials show only the verbose form.

\---

&#x20;

\## 4. The Lua API surface (representative)

&#x20;

The API \*is\* part of the contract. Verbose names are canonical; terse PICO-8 names in parens are aliases.

&#x20;

```text

LIFECYCLE

&#x20; \_init()                              once at start

&#x20; \_update()                            game logic, 60 fps

&#x20; \_draw()                              rendering, 60 fps

&#x20; \_bake()                              static-only; runs at BUILD time, auto-tiled

&#x20;

PPU (cheap — register writes)

&#x20; sprite(id, x, y, \[flip])    (spr)    draw sprite

&#x20; draw\_map(...)               (map)    draw tilemap region

&#x20; set\_tile(cx, cy, tile)      (mset)   set a map cell

&#x20; camera(x, y)                         scroll the active layer

&#x20; layer(n)                             target BG0 / BG1

&#x20; use\_palette(n)                       select sub-palette (0–3)

&#x20; remap(from, to)             (pal)    remap a palette index

&#x20;

OVERLAY (moderate — FPGA-rasterized commands)

&#x20; set\_pixel(x, y, c)          (pset)

&#x20; line(x0, y0, x1, y1, c)

&#x20; rect / rect\_fill(...)       (rect/rectfill)

&#x20; circle / circle\_fill(...)   (circ/circfill)

&#x20; text(str, x, y, c)          (print)

&#x20; overlay\_mode(0 | 1)                  0 = full 320×180, 1 = pure-draw 160×90

&#x20; blit(x, y, w, h, src)                bulk-push a packed pixel region (full-screen sw render)

&#x20; clip(...)                            bound overlay redraw region

&#x20;

INPUT  (local on MCU)

&#x20; button(b, \[player])         (btn)    held

&#x20; button\_pressed(b, \[player]) (btnp)   pressed this frame

&#x20;

AUDIO

&#x20; sound(id, ...)              (sfx)    play a sound effect

&#x20; music(track, ...)                    play / control a track

```

&#x20;

\---

&#x20;

\## 5. The Kit — v0.1 reference implementation

&#x20;

The \*\*first implementation\*\* of the logical machine. Board-specific limits live here, not in the contract.

&#x20;

\### 5.1 FPGA — Sipeed Tang Nano 20K (sealed GPU/APU coprocessor)

\- GW2AR-18: 20,736 LUT4, \*\*828 Kbit (\~103 KB) block RAM\*\*, 8 MB SDRAM, HDMI, onboard I²S amp.

\- Holds: PPU (2 BG + sprites), overlay + command rasterizer, APU (wavetable DDS + sequencer), SPI slave, HDMI A/V out. \*\*No CPU, no Lua.\*\*

\- \*\*Pre-flashed at production and sealed.\*\* The user never touches Verilog or the Gowin toolchain.

\- \*\*Output: 720p60.\*\* 320×180 integer-scales ×4 to 1280×720 — both 16:9, fills the screen with no pillarbox. (1080p is not a 60 Hz option here — these Gowin parts hit 1080p only at 24–30 Hz; ×6→1080p is a \*future-board\* perk.)

\- Block-RAM budget (of \~103 KB): overlay 320×180×4bpp ≈ \*\*28.8 KB\*\*, PPU line buffers/OAM/tile cache ≈ 10 KB → \~39 KB used — leaving \~64 KB free for BG1 line buffers and a larger tile cache.

\- \*\*Accepted build risk — 2 BG layers (§3.2):\*\* the contract commits to two independent-scroll BG layers. This is a deliberate bet that both fit the GW2AR-18 and close 720p60 timing. If they don't, the fix is to re-spin the \*\*kit\*\* onto larger silicon — the contract never drops a layer (the baseline never shrinks, §1). Validate two-layer fit/timing in early synthesis, before committing the carrier PCB.

\### 5.2 MCU — one RP2350 (Raspberry Pi Pico 2-class), dual-core

\- Dual Cortex-M33 @ 150 MHz, 520 KB SRAM. Runs \*\*the Lua VM = the console runtime\*\*.

\- \*\*Core 0:\*\* Lua game logic + render-command generation + owns SPI to the FPGA.

\- \*\*Core 1:\*\* USB HID host (PIO-USB, single hub-backed controller for 1–2 pads), audio kick, SD cart streaming.

\- \*\*SD over SPI\*\*, \*\*USB host via PIO-USB\*\* — both demonstrated together on shipping RP2350 boards.

\- \*Sourcing:\* a bare Pico 2 has no SD slot / USB-A host, so either (a) prototype on an RP2350 board that has both (e.g. Waveshare RP2350-PiZero; Olimex BB48 has 4 USB hosts for multiplayer), or (b) integrate the RP2350 + microSD + USB-A host onto the kit's carrier PCB.

\### 5.3 The MCU ↔ FPGA link — the central contract

A \*\*documented command/register protocol\*\*, the "bus" of this two-chip console (carried over SPI in v0.1; the protocol itself is transport-agnostic — see Appendix A.1):

\- \*\*Cart load:\*\* push bytecode + tile/sound assets into FPGA SDRAM.

\- \*\*Per-frame state:\*\* OAM, scroll, changed tilemap cells, palette, overlay commands, audio triggers — written into a \*\*back register set\*\*, swapped to active at vblank (no tearing).

\- \*\*Timing:\*\* an FPGA→MCU \*\*vsync/ready GPIO\*\* drives the frame cadence and buffer swap.

\- Per-frame state delta is only a few KB → well within SPI bandwidth.

This protocol is also what the SD config layer and firmware hackers build on — design it deliberately, version it, don't break it casually.

\### 5.4 Kit block diagram

```

&#x20; USB gamepad ─► \[ RP2350 ]  ──SPI commands──►  \[ Tang Nano 20K ]  ──HDMI A/V 720p60─► TV

&#x20;                 Lua, 2 cores  ◄──vsync GPIO───   sealed GPU/APU

&#x20;      SD card ─►  (carts)

```

Silicon ≈ \*\*$35\*\*, leaving room under $100 for the carrier PCB, case, and gamepad.

&#x20;

\### 5.5 Validation gates — what's unproven until synthesis

The contract (§1–§4) is fixed, but this \*implementation\* asserts fit and timing that haven't been built yet. None of these are believed hard; all are \*\*gating, and must be validated before committing the carrier PCB.\*\* If a gate fails, the fix is a different/larger board (the contract never shrinks, §1) — not a weakened spec.

&#x20;

\- \*\*Overall fit/timing on the GW2AR-18.\*\* The full design — 2 BG + sprites, overlay rasterizer, APU, SPI slave, HDMI out — must place, route, and close timing in \~20K LUT4. \*Gate: synthesize the integrated design, not just isolated cores.\*

\- \*\*2 BG layers (§3.2).\*\* Both independent-scroll layers must fit and close timing together (the committed-baseline bet from §3.2).

\- \*\*720p TMDS serialization.\*\* 1280×720p60 needs a \~371 MHz DDR serializer (OSER/ELVDS); it's demonstrated on these Gowin parts but sits near the device ceiling. \*Gate: a clean 720p test pattern out of the real part.\*

\- \*\*Worst-case sprite-per-line SDRAM fetch.\*\* 16 sprites/line + 2 BG fetches must stay within the SDRAM line budget at the tightest scanline. \*Gate: a stress scene at max sprite density.\*

\- \*\*Overlay rasterizer throughput.\*\* The command rasterizer and `OVL\_BLIT` must keep up with a busy overlay frame within the per-frame window. \*Gate: a full-screen blit + heavy-primitive frame.\*

\- \*\*Audio-over-HDMI (§3.7) — committed-baseline bet.\*\* Embedding I²S into TMDS data islands is the least-proven block, and the kit commits it as the day-one audio experience anyway (one cable to the TV). \*Gate: audio islands verified on a real TV.\* If it slips, the fallback is the onboard I²S amp + 3.5mm out — a UX change (external speaker/headphones), not a contract change.

\- \*\*Core 1 firmware budget (§5.2).\*\* The single hub-backed PIO-USB host (1–2 pads) + bursty SD streaming + audio triggers must all fit one Cortex-M33 at 60 fps without starving input. \*Gate: a max-load profile — 2 pads polled, a scene-boundary cart stream, and active audio triggers, all in one frame.\*

\---

&#x20;

\## 6. Teaching ladder \& openness

&#x20;

1\. \*\*Lua games\*\* — pure software; runs in the simulator or on the console. The beginner's whole world.

2\. \*\*MCU firmware (C / MicroPython)\*\* — the "real hardware" tier. Because the Lua runtime lives here, this \*is\* the console runtime: controllers, SD, the render/SPI layer. Hacking it extends the real machine.

3\. \*\*FPGA\*\* — sealed by default. Reconfigurable (not ASIC), so an "open the hood" path can come later without being in the normal kit flow.

\### The "my own gamepad" story (beginner-safe, 3 rungs)

\- \*\*Plug-and-play:\*\* firmware ships with HID + common-pad support (standard HID, Xbox/XInput). Plug in, it works.

\- \*\*SD config file:\*\* a plain-text mapping keyed by USB ID, editable in Notepad — covers nearly all custom-pad cases, no flashing.

\- \*\*Firmware:\*\* full open MCU source for exotic peripherals / protocol changes.

Because the common case is a \*data file\*, firmware stays untouched for almost everyone — hacker-tier openness with appliance-tier support load.

&#x20;

\---

&#x20;

\## 7. Build \& simulation pipeline

&#x20;

```

VSCode  (Lua + sprite/tile/sound assets; API autocomplete)

&#x20;  │  build

&#x20;  ▼

┌─ toolchain (PC) ───────────────────────────────────────┐

│  luac                   → Lua bytecode                  │

│  run \_bake() in sim     → TILER: framebuffer → dedupe   │

│                           8×8 → tile bank + map + palette│

│  pack                   → CART                           │

└─────────────────────────────────────────────────────────┘

&#x20;  │

&#x20;  ├─► PC SIMULATOR  =  Lua runtime (host process)  +  Verilator FPGA model

&#x20;  │   This mirrors real hardware exactly: MCU-runtime ↔ FPGA.

&#x20;  │   Free, no hardware — the on-ramp and the fast dev loop.

&#x20;  │

&#x20;  └─► KIT:  RP2350 loads cart from SD → streams to FPGA over SPI → run

```

&#x20;

\- The simulator pairing (Lua host + Verilator FPGA) is a \*\*faithful implementation of the spec\*\*, not a toy — same split as the kit.

\- The \*\*tiler\*\* only bakes deterministic output, so static content must live in `\_bake()`. Everything else runs live.

\- \*\*Tile-budget guardrail:\*\* the tiler reports the post-dedup unique-tile count per bank and \*\*errors on overflow\*\* (baked output exceeding the active bank's tile capacity), with an actionable message — split into a scene bank (§3.4), reduce unique tiles, or move the art to the overlay. A beginner who bakes a photo gets a clear diagnostic, never silent corruption or a mystery overflow.

\---

&#x20;

\## 8. Forward-compatibility strategy

&#x20;

\- \*\*Carts target the contract (Lua runtime/API + command/register model), not the board.\*\* Any conforming implementation runs them.

\- \*\*Versioned spec.\*\* v0.1 = baseline. Future implementations provide \*\*≥ baseline\*\*.

\- \*\*Additive capability tiers.\*\* New capabilities (custom wavetables, more sprites/palettes, bigger overlay, 1080p, more players) are \*opt-in\* via `requires spec ≥ X`. The baseline never shrinks.

\- \*\*Every target is "an implementation":\*\* the simulator, the v0.1 kit, and a future board (faster MCU + bigger FPGA, or a single SoC) are interchangeable from the cart's view.

\---

&#x20;

\## 9. Open decisions

&#x20;

\*\*None — v0.1 is feature-complete.\*\* Every framework decision is resolved and folded into §2/§3/§5, and the MCU↔FPGA wire protocol is specified in \*\*Appendix A\*\*. What remains is implementation: building the FPGA cores, the MCU runtime, the Verilator simulator, the toolchain, and the kit PCB. The implementation risks that gate the v0.1 kit (fit/timing, 720p TMDS, audio-over-HDMI, Core 1 budget) are catalogued in \*\*§5.5\*\* — each one is a build-time validation gate, not an open contract question.

&#x20;

\---

&#x20;

\## Appendix A — SPI Protocol (MCU ↔ FPGA)

&#x20;

The wire contract between the MCU (master) and the FPGA (slave). Versioned; a `HELLO` handshake negotiates the protocol version at boot.

&#x20;

\### A.1 Physical

\- \*\*Transport (v0.1 = SPI):\*\* MCU = master, FPGA = slave, target clock \~32 MHz (raise once the slave closes timing). The opcode/packet layer (A.2–A.4) is \*\*transport-agnostic\*\* — a faster link (QSPI, parallel, LVDS) is a conforming implementation if it carries the same packets; only the framing (A.2) is transport-specific. Bulk paths (`OVL\_BLIT`, asset loads) scale directly with transport bandwidth.

\- \*\*VSYNC\*\* (FPGA→MCU GPIO): asserted at vblank; drives the frame clock / MCU interrupt.

\- \*\*READY\*\* (FPGA→MCU GPIO): flow control during asset loads (deasserted while the FPGA is busy writing SDRAM).

\### A.2 Framing

Each SPI transaction (CS low→high) is exactly one packet — on SPI, chip-select delimits frames, so no COBS is needed. \*(A non-SPI transport defines its own delimiting — e.g. a length prefix or COBS over a stream.)\*

```

\[ OPCODE : 1 ] \[ ARGS : n ] \[ CRC8 : 1 ]

```

&#x20;

\### A.3 Integrity (path-matched)

\- \*\*Per-frame packets:\*\* CRC8 always on (covers OPCODE+ARGS). On mismatch → FPGA drops the packet and bumps an error counter (readable via `STATUS`). Recovery is by \*\*periodic resend\*\*, not retransmit-on-error, so transients converge with no ACK round-trip:

&#x20; - \*Small full-state channels\* (OAM, scroll, palette) are resent \*\*in full every frame\*\* → a dropped packet self-heals on the next frame.

&#x20; - \*Overlay\* is redrawn from scratch each frame (auto-cleared at vblank, §3.6) → a dropped overlay command self-heals on the next frame.

&#x20; - \*Tilemap\* uses a delta fast path (changed cells only) \*\*plus a rolling full resync\*\*: the MCU continuously re-sends the entire visible tilemap spread across a window of N frames, so any dropped tilemap delta self-corrects within ≤ N frames. N is a firmware constant trading resync latency against background bandwidth (e.g. N≈16 → ≤\~¼ s worst case; the full visible map is only \~3.7 KB/layer, so a small N — even N=1 on a faster transport — is affordable).

\- \*\*Asset loads:\*\* a whole-blob CRC accompanies `LOAD\_END` and is verified after the DMA. On mismatch → the FPGA flags it and the MCU re-runs the load (not time-critical).

\### A.4 Opcodes (IDs illustrative)

\*\*Per-frame state → back buffer\*\*

\- `WRITE\_OAM(data\[256])` — full sprite table (small, resent each frame)

\- `SET\_SCROLL(layer, x, y)` — resent each frame

\- `WRITE\_TILEMAP(layer, addr, count, cells…)` — changed cells only (delta fast path); the full visible map is also resent on a rolling N-frame cycle as a resync safety net (§A.3)

\- `SET\_PALETTE(index, entries…)` — active palette resent each frame

\- `SET\_OVERLAY\_MODE(0|1)`

\*\*Overlay draw → back overlay\*\*

\- `OVL\_PIXEL / OVL\_LINE / OVL\_RECT / OVL\_FILLRECT / OVL\_CIRCLE / OVL\_FILLCIRCLE / OVL\_TEXT / OVL\_CLIP`

\- `OVL\_BLIT(x, y, w, h, packed\[…])` — bulk-write a packed 4bpp pixel region into the back overlay (full-screen software rendering; payload ≈ w·h/2 bytes, scales with transport speed)

\*\*Audio\*\*

\- `SOUND(id, params…)` · `MUSIC(track, cmd…)` — FPGA sequencer runs autonomously

\*\*Assets (bursty, paced by READY)\*\*

\- `LOAD\_BEGIN(dest\_addr, length)` · `LOAD\_DATA(chunk…)` · `LOAD\_END(blob\_crc)` · `BANK\_ACTIVATE(bank\_id)`

\*\*Control\*\*

\- `SWAP` — commit back→active at next vblank

\- `HELLO(version)` / `VERSION` — handshake · `STATUS` — error counters / ready · `PING` · `RESET`

\### A.5 Per-frame sequence

```

FPGA: ─ VSYNC↑ ─────────────────────────────── VSYNC↑ ─

MCU:       │ \_update + \_draw                        │

&#x20;          │  → WRITE\_OAM / SET\_SCROLL /            │  (into

&#x20;          │    WRITE\_TILEMAP / OVL\_\* / SOUND       │   back buffer)

&#x20;          │  → SWAP                                │

FPGA:      │ scans out ACTIVE (frame N) ─────────► │ commit back→active (N+1)

```

One frame of latency; fully pipelined — the MCU builds N+1 while the FPGA displays N.

&#x20;

\### A.6 Double-buffering

The per-frame display state — OAM, scroll, tilemap, palette, \*\*and\*\* the overlay — lives in a back buffer and swaps to active atomically on `SWAP` at vblank, so no frame is ever shown half-updated. Asset SDRAM (tile/sound banks) is \*\*not\*\* double-buffered; it's loaded explicitly and made live via `BANK\_ACTIVATE` at scene boundaries.

&#x20;

\*\*Copy-on-swap (delta coherence).\*\* Channels resent in full every frame (OAM, scroll, palette) and the overlay (redrawn each frame, §3.6) are self-contained per frame, so naive ping-pong is safe for them. The \*\*tilemap is the exception\*\*: it's updated by deltas (changed cells only), so the back set must begin each frame as a copy of what was just displayed — otherwise deltas would land on two-frame-old state and the map would corrupt every frame. The FPGA therefore \*\*copies the active tilemap into the back set on each `SWAP`\*\* (equivalently: the tilemap is one persistent read-modify-write set whose \*scanout snapshot\* is what double-buffers). This makes per-frame deltas apply to current state; the rolling resync (§A.3) then only has to cover genuinely dropped packets, not structural divergence.

&#x20;

\---

&#x20;

\*End of v0.1 spec.\*



