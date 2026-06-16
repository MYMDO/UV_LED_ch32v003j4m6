# UV LED Lamp Controller — CH32V003J4M6 (SOP-8)

> Firmware for a UV LED exposure lamp with PWM brightness control, timer auto-shutdown, and a WS2812 status indicator. Powered by the ultra-cheap RISC-V CH32V003J4M6 in an 8-pin package.

**Target:** CH32V003J4M6 (SOP-8) · **Framework:** Arduino (`.ino`) via PlatformIO · **Toolchain:** `wch-ch32v003` + `arduino` · **MCU Freq:** 48 MHz

---

## Table of Contents

- [Hardware Overview](#hardware-overview)
- [Pinout](#pinout)
- [Features](#features)
- [Schematic Notes](#schematic-notes)
- [Getting Started](#getting-started)
  - [1. Create platformio.ini](#1-create-platformioini)
  - [2. Build & Flash](#2-build--flash)
  - [3. Wiring](#3-wiring)
- [User Guide](#user-guide)
  - [Brightness Button (PC1)](#brightness-button-pc1)
  - [Timer Button (PC2)](#timer-button-pc2)
  - [WS2812 Indicator Colors](#ws2812-indicator-colors)
- [Code Architecture](#code-architecture)
  - [File Structure](#file-structure)
  - [Key Functions & Data Flow](#key-functions--data-flow)
  - [PWM Initialization (TIM1_CH4)](#pwm-initialization-tim1_ch4)
  - [WS2812 Bitbang Driver](#ws2812-bitbang-driver)
  - [Flash Settings Persistence](#flash-settings-persistence)
  - [Button Debounce & Gesture Logic](#button-debounce--gesture-logic)
- [Configuration](#configuration)
  - [MOSFET Type](#mosfet-type)
  - [Brightness Levels](#brightness-levels)
  - [Timer Durations](#timer-durations)
  - [WS2812 Brightness](#ws2812-brightness)
- [Electrical Characteristics](#electrical-characteristics)
- [Firmware Quirks & Gotchas](#firmware-quirks--gotchas)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Hardware Overview

| Component | Value |
|-----------|-------|
| MCU | WCH CH32V003J4M6 (SOP-8) |
| Core | QingKe RISC-V2A (RV32EC + Zicsr), up to 48 MHz |
| Flash | 16 KB |
| SRAM | 2 KB |
| PWM Output | PC4 (TIM1_CH4), 8-bit (0–255), ~1 kHz |
| LED Driver | N-Channel or P-Channel MOSFET, selected by `#define` |
| Status LED | WS2812 (NeoPixel) on PD6, bitbanged |
| Buttons | 2× momentary tact switches, INPUT_PULLUP |
| Flash Endurance | 100K erase/write cycles (typical) |

The total BOM can be as low as **4–6 external components**: 2 buttons, 1 MOSFET, 1 WS2812 LED, optionally a capacitor.

---

## Pinout

CH32V003J4M6 SOP-8 (top view, pin 1 marked):

```
        ┌──────┐
 PD6/PA1 │1   8│ PD1/SWIO (bonded — do not use)
      GND│2   7│ PC4 (TIM1_CH4 PWM → MOSFET gate)
      PA2│3   6│ PC2 (Timer button, INPUT_PULLUP)
      VDD│4   5│ PC1 (Brightness button, INPUT_PULLUP)
        └──────┘
```

| Pin | MCU Pin | Function | Notes |
|-----|---------|----------|-------|
| 1 | PD6 | WS2812 data | Bitbang output; also bonded to PA1 |
| 2 | VSS | GND | — |
| 3 | PA2 | — | Unused, leave floating or tie to GND |
| 4 | VDD | 3.3 V | Connect to regulated 3.3 V supply |
| 5 | PC1 | Brightness button | External pull-up not needed (INPUT_PULLUP) |
| 6 | PC2 | Timer button | External pull-up not needed (INPUT_PULLUP) |
| 7 | PC4 | PWM output | TIM1_CH4, drives MOSFET gate via resistor |
| 8 | PD1 | SWIO | Bonded to PD4/PD5; SDI debug; do not drive externally |

---

## Features

- **4 brightness modes** (0 / 75 / 150 / 255 duty cycle, 8-bit PWM)
- **3 timer durations** (10 s / 30 s / 60 s) with auto-shutdown
- **WS2812 status indicator** — color-coded by timer mode, yellow when timer disabled, blinks in last 5 seconds
- **Non-volatile settings** — timer mode and enable state saved to Flash (last page, `0x08003FC0`)
- **Dual-button gesture handling** — short-press cycles options, long-press (>1 s) toggles on/off or timer enable
- **N- and P-Channel MOSFET support** — compile-time switch via `#define USE_P_CHANNEL_MOSFET`
- **Power-failsafe PWM** — duty = 0% configures pin as GPIO with static OFF level, preventing floating gate
- **WS2812 transmit cache** — skips SPI-like re-transmission if color unchanged, saving power and reducing flicker

---

## Schematic Notes

A minimal schematic:

```
          3.3 V
            │
            ├──────────── VDD (pin 4)
            │
           ─┴─
          ═══ 100 nF + 10 µF decoupling
           ─┬─
            │
           ═══
           GND


  PC1 ────╯  ╰──── GND        (momentary switch, normally open)
  PC2 ────╯  ╰──── GND

  PC4 ────[100 Ω]──── Gate ─┐
                             MOSFET (N-ch: IRLZ44N, etc.)
  Drain ──── UV LED strip ──┐
  Source ─────────────────── GND

  PD6 ────[330 Ω]──── DIN ─┐ WS2812
                  VDD ──┐  │
                  GND ──┴──┘

  SWIO (pin 8) ──── WCH-LinkE DIO (only during programming)
```

**MOSFET selection:**
- **N-Channel** (`USE_P_CHANNEL_MOSFET 0`, default): connect source to GND, drain to UV LED cathode. PC4 HIGH = ON.
- **P-Channel** (`USE_P_CHANNEL_MOSFET 1`): connect source to VCC, drain to UV LED anode. PC4 LOW = ON.

> The resistor between PC4 and the MOSFET gate limits inrush current from the gate capacitance. 100 Ω is sufficient for <100 kHz switching. For faster edge rates, reduce to 22–47 Ω.

---

## Getting Started

### 1. Create `platformio.ini`

The firmware is an `.ino` sketch for the Arduino framework. PlatformIO is the recommended build system.

Write a `platformio.ini` in the **project root** (same directory as this README):

```ini
[env:ch32v003j4m6]
platform = wch-ch32v003
board = ch32v003j4m6
framework = arduino
build_flags = -Wno-error
build_unflags = -Werror
```

The `-Wno-error` / `-Werror` flags are **required** because the WCH SDK headers emit harmless warnings that PlatformIO elevates to errors by default.

### 2. Build & Flash

```bash
# Build only
pio run

# Build + upload via WCH-LinkE
pio run -t upload
```

### 3. Wiring

Connect WCH-LinkE to the target during programming:

| WCH-LinkE | CH32V003J4M6 |
|-----------|---------------|
| DIO (SWIO) | PD1 (pin 8) |
| 3.3 V | VDD (pin 4) |
| GND | GND (pin 2) |

> **Important:** WCH-LinkE blue LED must be OFF (= RISC-V mode). If it is ON (ARM mode), hold the mode button while plugging into USB.

Only 3 wires needed. After flashing, disconnect the programmer — PD1 is bonded to the SWIO pin and must not be driven during normal operation.

---

## User Guide

### Brightness Button (PC1)

| Gesture | Action |
|---------|--------|
| **Short press** | Cycle modes: OFF → LOW → MED → HIGH → OFF |
| **Long press (>1 s)** | If ON → OFF; if OFF → ON (LOW) |

`lastActiveMode` remembers the last non-zero mode, though currently only used for future expansion.

### Timer Button (PC2)

| Gesture | Action |
|---------|--------|
| **Short press** | Cycle timer: 10 s → 30 s → 60 s → 10 s (timer auto-enabled) |
| **Long press (>1 s)** | Toggle timer ON/OFF |

Settings are saved to Flash on every timer button action (short and long press).

### WS2812 Indicator Colors

| Condition | Color | Meaning |
|-----------|-------|---------|
| Mode = OFF | Dim white (20%) | Standby / lamp off |
| Timer ON, 10 s | Green | Standard |
| Timer ON, 30 s | Cyan | Standard |
| Timer ON, 60 s | Magenta | Standard |
| Timer disabled | Yellow | Timer bypassed, lamp stays on indefinitely |
| Last 5 s of timer | Blinking (500 ms) | Auto-shutdown imminent |
| Timer expired | Same as Mode=OFF | Lamp turned off automatically |

---

## Code Architecture

### File Structure

```
UV_LED_ch32v003j4m6/
└── UV_LED_ch32v003j4m6.ino   (single-file sketch, 308 lines)
```

The entire firmware fits in a single `.ino` file. There is no separate `platformio.ini` checked in — users create one locally (see [Getting Started](#getting-started)).

### Key Functions & Data Flow

```
                 ┌──────────────────────┐
                 │      setup()          │
                 │  - init buttons (PU)  │
                 │  - loadSettings()     │
                 │  - pwmInit()          │
                 │  - pwmSetDuty(0)      │
                 │  - updateIndicators() │
                 └──────────┬───────────┘
                            │
                 ┌──────────▼───────────┐
                 │      loop()           │
                 │  ┌────────────────┐   │
                 │  │ Button handler  │   │
                 │  │ Brightness btn  │   │
                 │  │ Timer btn       │   │
                 │  └───────┬────────┘   │
                 │          │            │
                 │  ┌───────▼────────┐   │
                 │  │ Timer expiry    │   │
                 │  │ check           │   │
                 │  └───────┬────────┘   │
                 │          │            │
                 │  ┌───────▼────────┐   │
                 │  │ updateIndicators│   │
                 │  │ → WS2812       │   │
                 │  └────────────────┘   │
                 └──────────────────────┘
```

### PWM Initialization (TIM1_CH4)

```c
void pwmInit();
```

- Enables clocks: `RCC_APB2Periph_GPIOC | RCC_APB2Periph_TIM1 | RCC_APB2Periph_AFIO`
- Configures PC4 as `GPIO_Mode_AF_PP` (alternate function push-pull)
- TIM1 prescaler calculated as `(SystemCoreClock / 256000) - 1`:
  - At 48 MHz → prescaler = 186 → timer clock ≈ 256 kHz
  - With `TIM_Period = 255` → PWM frequency ≈ 1.004 kHz
- Output channel 4 configured as PWM1 mode
- Polarity: HIGH for N-Channel, LOW for P-Channel (`#if USE_P_CHANNEL_MOSFET`)

### PWM Duty Cycle: The GPIO Trick

```c
void pwmSetDuty(uint8_t duty);
```

When `duty == 0`:
- PC4 is reconfigured as `GPIO_Mode_Out_PP` (push-pull output)
- Driven **LOW** (N-ch) or **HIGH** (P-ch) — statically OFF

When `duty > 0`:
- PC4 is reconfigured back to `GPIO_Mode_AF_PP` (PWM)
- `TIM_SetCompare4(TIM1, duty)` sets the PWM compare value

**Why:** At 0% PWM, the MOSFET gate would be weakly driven or floating in some PWM configurations. Driving it as a GPIO ensures a clean, solid OFF state and eliminates any risk of the MOSFET partially conducting.

### WS2812 Bitbang Driver

```c
void sendPixelColor(uint8_t r, uint8_t g, uint8_t b);
```

Timing (measured at 48 MHz, single-cycle NOPs):

| Bit | HIGH | LOW |
|-----|------|-----|
| 1 | ~800 ns (10 NOPs) | ~450 ns (5 NOPs) |
| 0 | ~400 ns (3 NOPs) | ~850 ns (8 NOPs) |

The driver sends 24 bits (GRB order: G16–23, R8–15, B0–7) on PD6 via direct register access (`GPIOD->BSHR`). Interrupts are disabled (`noInterrupts()`) during transmission to prevent jitter.

After each pixel, `delayMicroseconds(300)` generates the RESET pulse (>50 µs needed by WS2812).

**Brightness scaling** (per-channel):
```c
r = ((uint16_t)r * brightness) >> 8;
g = ((uint16_t)g * brightness) >> 8;
b = ((uint16_t)b * brightness) >> 8;
```

where `brightness` is derived from `getWS2812Brightness()`:
- Mode 0 (standby): 20% of `WS2812_BRIGHTNESS`, minimum 5
- Modes 1–3: `WS2812_BRIGHTNESS * currentMode / 3` (linear 1/3, 2/3, 3/3)

**Transmit cache:** `lastSentColor` stores the last transmitted color data. If no change is detected, the entire send is skipped — this eliminates unnecessary PWM interference and saves power.

### Flash Settings Persistence

**Address:** `0x08003FC0` — the last 64-byte page of the 16 KB Flash.

**Format:**
| Offset | Size | Field |
|--------|------|-------|
| +0 | 16 bit | `[timerEnabled (bit 8–15)] [currentTimerMode (bit 0–7)]` |
| +2 | 16 bit | Magic number `0xABCD` (validity check) |

**Write flow:**
1. Read current values at `0x08003FC0` and `0x08003FC2`
2. If unchanged → skip (Flash write is slow and wears the page)
3. Unlock Flash → clear error flags → erase page → program 2 half-words → lock

**Read flow:**
1. Read `w1` (magic) at `+2`
2. If `w1 == 0xABCD`, load `currentTimerMode` and `timerEnabled` from `w0`
3. Clamp values to valid range

**When saved:**
- On timer button short-press (mode changes)
- On timer button long-press (enable/disable toggle)
- NOT on brightness changes (lamp mode is intentionally volatile)

### Button Debounce & Gesture Logic

Both buttons use the same pattern:

```
                   pressed ──► wait >1 s ──► long press handled
                  │                            (once)
    button LOW ───┤
                  │
                   released after >50 ms ──► short press
                   released after <50 ms ──► debounce noise, ignored
```

- Debounce: 50 ms (`debounceDelay`)
- Long press threshold: 1000 ms
- `*IsPressing` flag prevents repeated triggers while held
- `*LongHandled` flag ensures long press fires only once

Key difference from many button libraries: **long press releases immediately after threshold is crossed**, without waiting for release. This makes the UI feel responsive.

---

## Configuration

All configuration is compile-time via `#define` and `static const` arrays at the top of the `.ino`.

### MOSFET Type

```c
#define USE_P_CHANNEL_MOSFET 0   // 0 = N-Channel, 1 = P-Channel
```

| Setting | Pin OFF (duty=0) | Pin ON (duty>0) |
|---------|------------------|-----------------|
| 0 (N-ch) | GPIO LOW | PWM HIGH |
| 1 (P-ch) | GPIO HIGH | PWM LOW |

**Why not runtime:** The polarity affects both `TIM_OCPolarity` (set at PWM init) and the GPIO output level at 0% duty. Changing this at runtime would require full PWM re-initialization.

### Brightness Levels

```c
static const uint8_t brightnessLevels[] = {0, 75, 150, 255};
```

Four modes indexed 0–3. Mode 0 is always OFF. Customize by editing this array (values 0–255).

### Timer Durations

```c
static const unsigned long timerDurations[] = {10 * 1000UL, 30 * 1000UL, 60 * 1000UL};
```

Three timer modes indexed 0–2. Values in milliseconds. Add entries by extending the array and adjusting the cycle bound check (`currentTimerMode > 2`).

### WS2812 Brightness

```c
#define WS2812_BRIGHTNESS 60  // 0–255
```

Maximum LED brightness. Actual brightness is scaled per mode (see [WS2812 Bitbang Driver](#ws2812-bitbang-driver)).

---

## Electrical Characteristics

(CH32V003 datasheet values, SOP-8 package)

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| VDD | 2.7 | 3.3 | 5.5 | V |
| IDD (active, 48 MHz) | — | 8 | 12 | mA |
| IDD (standby) | — | 0.6 | — | µA |
| IOH per pin | — | — | 20 | mA |
| IOL per pin | — | — | 25 | mA |
| GPIO input LOW | -0.3 | — | 0.3×VDD | V |
| GPIO input HIGH | 0.65×VDD | — | VDD+0.3 | V |
| Flash endurance | 10K | 100K | — | cycles |
| Flash retention (25 °C) | 20 | 100 | — | years |
| Operating temp | -40 | — | 85 | °C |

**Power budget (whole system):**
- MCU active: ~8–10 mA at 48 MHz
- WS2812 (dim, 20%): ~2–3 mA
- MOSFET gate charge: negligible at 1 kHz
- **Total:** ~10–15 mA typical, excluding UV LED strip current

UV LED strip current is supplied entirely through the MOSFET, not through the MCU.

---

## Firmware Quirks & Gotchas

### Integer Division Traps

The CH32V003 has no hardware integer divider. The compiler generates software division, and **intermediate truncation is silent**. Example from this codebase:

```c
uint8_t min_b = WS2812_BRIGHTNESS / 5;  // integer division, OK here
return (uint16_t)WS2812_BRIGHTNESS * currentMode / 3;  // multiplication first avoids truncation
```

The explicit `(uint16_t)` cast in `return (uint16_t)WS2812_BRIGHTNESS * currentMode / 3;` prevents overflow and truncation — this pattern should be preserved in similar calculations.

### `delayMicroseconds()` vs `Delay_Ms()`

- `delayMicroseconds()` is used for the WS2812 RESET pulse (300 µs) — works at this short range.
- `Delay_Ms()` from the WCH Arduino framework is preferred for longer delays. Custom SysTick-based delays are unreliable on this MCU.

### Interrupt Safety

WS2812 timing is critical. `noInterrupts()`/`interrupts()` bracket the 24-bit transmission. Any interrupt longer than ~1 µs during transmission will corrupt the WS2812 protocol.

### Flash Write Wear

Settings are saved at most a few times per session. The Flash page at `0x08003FC0` has ~100K write cycles — essentially unlimited for this use case. The read-before-write check (`saveSettings()` lines 46–48) further reduces unnecessary writes.

### WCH-LinkE Mode

When using WCH-LinkE, the blue LED must be OFF (RISC-V mode). If the programmer was used with an ARM MCU previously, hold the mode button while inserting USB to switch.

### PlatformIO Werror Quirk

The WCH SDK headers emit warnings for unused variables and implicit casts. PlatformIO adds `-Werror` by default, so `build_unflags = -Werror` is mandatory in `platformio.ini`.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Upload fails: "Read protection" | MCU locked by MounRiver Studio | Use `wchisp` to unlock: `wchisp -s config unprotect` or use USB-UART serial bootloader on PD5/PD6 |
| Upload fails: "Connection timeout" | WCH-LinkE in ARM mode | Hold mode button, re-plug USB (blue LED must be OFF) |
| WS2812 doesn't light, or wrong color | Timing mismatch | Check `WS2812_BRIGHTNESS`; verify `NOP` counts match 48 MHz core speed |
| Lamp stays on dimly at 0% | Floating gate at 0% PWM | This firmware handles this via `pwmSetDuty(0)` GPIO mode — verify your MOSFET type matches `USE_P_CHANNEL_MOSFET` |
| Timer doesn't shut down | Timer disabled | Long-press timer button to re-enable (yellow → green/cyan/magenta) |
| Build fails with `-Werror` | SDK header warnings | Add `build_unflags = -Werror` to `platformio.ini` |
| System clock wrong frequency | Incorrect prescaler | Verify `SystemCoreClock` reads 48 MHz (HSI+PLL) |
| Buttons unreliable | Missing pull-up | Code uses `INPUT_PULLUP`, no external resistor needed. Check wiring for noise. |

---

## License

This project is open hardware / open firmware. Published under the MIT License.

---

## References

- [WCH CH32V003 Datasheet](https://www.wch-ic.com/products/CH32V003.html)
- [CH32V003 Arduino Core](https://github.com/openwch/arduino_ch32v003)
- [PlatformIO WCH Platform](https://github.com/Community-PIO-CH32V/platform-wch-ch32v003)
- [WS2812 Datasheet](https://cdn-shop.adafruit.com/datasheets/WS2812.pdf)
