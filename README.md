# Sector8

**An open fantasy console you plug into a TV — real two-chip hardware, PICO-8-style creative constraints, fully open spec.**

A project of **ParticleSector**.

> **Status: v0.1 framework spec — published and feature-complete.** This repo currently contains the spec. The PC simulator, toolchain, MCU firmware, and FPGA cores are in active development and will be published as they reach usable milestones. See [Roadmap](#roadmap).

---

## What is this?

Sector8 is a fantasy console in the tradition of PICO-8 and TIC-80 — tight, deliberate constraints as a creative feature; write Lua, make a whole game in a sitting — with one difference that changes everything:

**It's real hardware.** A microcontroller (the CPU) talks to an FPGA (the video/sound chip) over a documented bus, and the FPGA drives your TV over HDMI. This is the classic console architecture — 6502 + PPU, 68000 + VDP — built from ~$35 of modern, open silicon.

That makes Sector8 two things at once:

1. **A console for homebrew.** Sub-$100 kit, HDMI out at 720p60, USB gamepads, games on SD cards. Write a cart in Lua, plug it into a television, hand a friend a controller.
2. **An open teaching machine.** Every layer is inspectable: the Lua game, the MCU firmware running it, the command protocol on the wire, and (eventually) the FPGA design itself. It's a ladder from "my first game" down to "how a real console actually works."

Nobody else ships this. PICO-8 is closed and hardware-less. TIC-80 is open but PC-only. Sector8 is the open FPGA console on a real TV.

## The core idea: a spec, not a board

The most important design decision in this project:

**Sector8 is a *logical machine* — a stable contract. Physical boards are *implementations* of it.**

A cart targets the contract, never the board. Any conforming implementation runs it unchanged:

- the **PC simulator** (no hardware needed — the free on-ramp),
- the **v0.1 kit** (RP2350 + Tang Nano 20K),
- **future, beefier boards** — no porting, ever.

The contract is defined as **minimum guarantees**. Implementations may exceed the baseline (more sprites, bigger overlay, higher output resolution), but a cart that stays inside it runs everywhere — and **the baseline never shrinks across spec versions.**

📄 **Read the full spec: [`fantasy-console-spec-v0_1-revised.md`](./fantasy-console-spec-v0_1-revised.md)**

## Architecture

```
  USB gamepad ─► [ RP2350 MCU ]  ──command/register stream──►  [ FPGA GPU/APU ]  ──HDMI 720p60─► TV
                  Lua runtime,      ◄──── vsync / ready ────    2 BG layers, sprites,
       SD card ─► game logic,                                   overlay rasterizer,
                  USB host                                      4-ch wavetable audio
```

- **The MCU runs the game.** Lua VM, game logic, render-command generation, USB gamepad host, SD carts.
- **The FPGA is a sealed GPU/APU coprocessor.** No CPU inside — it composites graphics and synthesizes sound from the register/command state the MCU sends, and drives HDMI (video *and* audio, one cable).
- **The bus between them is a documented, versioned command/register protocol** (spec Appendix A). The protocol is the contract; the physical transport is implementation-defined.

## The machine at a glance

| | |
|---|---|
| **Resolution** | 320×180 logical, integer-scaled ×4 to 720p60 over HDMI |
| **Color** | 4bpp indexed, four 16-color sub-palettes from a 12-bit RGB gamut — up to 64 colors on screen |
| **Backgrounds** | **Two** independently scrolling tilemap layers (true parallax, baseline-guaranteed) |
| **Sprites** | 8×8, 64 on screen, 16 per scanline, H/V flip, per-sprite palette |
| **Tiles** | 8×8, 512-tile active bank shared by BG + sprites, bank-swappable per scene |
| **Overlay** | Free-draw layer (pixels, lines, circles, text, bulk blit) rasterized *in the FPGA* from compact commands; 320×180 or doubled 160×90 mode |
| **Audio** | 4-channel wavetable synth + noise, autonomous FPGA sequencer, output embedded in HDMI |
| **Input** | 8-button USB gamepads, 2 players baseline |
| **Code** | Lua, `_init` / `_update` / `_draw` at 60 fps |
| **Carts** | Precompiled bytecode + assets on SD, ~1 MB baseline, bank-swappable |

## Three drawing paths, one API

The heart of the programming model. You write the same friendly calls everywhere — *where* you call them determines the cost:

| Tier | Calls | Cost | Mechanism |
|---|---|---|---|
| **Baked** | anything inside `_bake()` | free at runtime | runs at *build* time, auto-tiled into the tile bank |
| **PPU** | `sprite` `draw_map` `camera` `use_palette` | cheap | register writes; the FPGA composites |
| **Overlay** | `set_pixel` `line` `circle` `text` `blit` | moderate | compact commands; the FPGA rasterizes |

The same `circle()` is a free baked tile in `_bake()`, or a live overlay command in your game loop. ~90% of real game drawing is sprite/tile-shaped and rides the cheap path; the overlay is the expressive escape hatch for HUDs, text, and effects — including full-screen software rendering via a packed blit.

PICO-8 muscle memory works: terse aliases (`spr`, `btn`, `rnd`, `pset`…) ship alongside the canonical verbose names.

## Roadmap

The spec is done; the implementations are being built. Roughly in order:

- [x] **v0.1 framework spec** — feature-complete, including the MCU↔FPGA wire protocol (Appendix A)
- [ ] **PC simulator** — in active development (SDL3 host, shared C++ runtime, Verilator-ready FPGA boundary). Palette + background + sprite compositing already rendering.
- [ ] **Lua runtime + API bindings** on the shared runtime
- [ ] **Simulator in the browser (WASM)** — try Sector8 with zero install
- [ ] **FPGA cores** on the Tang Nano 20K, gated by the validation checklist in spec §5.5
- [ ] **v0.1 kit** — carrier PCB, case, gamepad; committed only after the silicon gates pass

A note on honesty: the spec's §5.5 lists every claim the v0.1 kit makes that hasn't been proven in silicon yet — FPGA fit/timing, 720p TMDS, audio-over-HDMI, and more. Each is a named validation gate, not hand-waving. If a gate fails on the target part, the fix is bigger silicon — **the contract never shrinks.**

## Openness

Everything is open; the brand is the moat (the Arduino model):

- **Spec, SDK, simulator, MCU firmware:** MIT / Apache-2.0
- **Hardware design:** CERN-OHL
- Clone it, port it, teach with it, build your own board against the contract — every implementation and every cart grows the platform. The official ParticleSector kit competes on convenience and trust, not secrets.

## The teaching ladder

1. **Lua games** — pure software, runs in the simulator or on the console. The beginner's whole world.
2. **MCU firmware (C++)** — the Lua runtime *is* the console runtime. Hacking it extends the real machine: controllers, SD, the render layer, the bus.
3. **The wire protocol** — a documented, versioned command/register contract you can put a logic analyzer on.
4. **FPGA** — sealed by default, reconfigurable by nature; an "open the hood" tier for later.

Even gamepad support follows this ladder: common pads work out of the box, custom pads are a plain-text mapping file on the SD card, and exotic hardware is a firmware patch.

## Repository contents

```
fantasy-console-spec-v0_1-revised.md   The v0.1 framework spec (start here)
README.md                               This file
```

Simulator, toolchain, firmware, and RTL will land here (or in sibling repos) as they mature.

## FAQ

**Is this a PICO-8 / TIC-80 clone?**
No — and deliberately not compatible with either. Software fantasy consoles already exist and are excellent. Sector8's point is the part they can't do: a real two-chip console architecture you can hold, probe, and plug into a TV. The API is *familiar* to PICO-8 developers (aliases included), but carts target Sector8's own contract.

**Why no `peek`/`poke` into video memory?**
There is no MCU-visible VRAM — that's the architecture, not an omission. The MCU and FPGA are separate chips joined by a command protocol, exactly like the consoles this machine teaches. Full-screen software rendering goes through the overlay blit path instead.

**Can I program the FPGA?**
Not in normal use — it ships sealed and acts as a fixed video/sound chip, which is the point of the teaching model. But it's an FPGA, not an ASIC, and the design will be open: reflashing it is a future tier, not a locked door.

**When can I buy one?**
Not yet. The kit ships only after the FPGA validation gates pass on real hardware. The simulator will arrive first — same contract, no purchase required.

## License

- Code & spec text: MIT / Apache-2.0 (final per-file licensing to be confirmed at first code publish)
- Hardware: CERN-OHL

*Sector8 and ParticleSector are working names, pending trademark clearance.*
