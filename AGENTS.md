# UV LED Lamp — CH32V003J4M6 (SOP-8)

## Quick Reference

```bash
# Build & flash via PlatformIO (create platformio.ini first)
pio run -t upload
```

## Pinout (SOP-8)

| Pin | Function |
|-----|----------|
| 1 PD6 | WS2812 data (bitbang) |
| 2 GND | — |
| 3 PA2 | unused |
| 4 VDD | — |
| 5 PC1 | Brightness button (INPUT_PULLUP) |
| 6 PC2 | Timer button (INPUT_PULLUP) |
| 7 PC4 | TIM1_CH4 PWM → MOSFET gate |
| 8 PD1 | SWIO (bonded, don't use) |

## Architecture

- **MCU:** CH32V003J4M6 — SOP-8, 16KB flash, 2KB RAM, 48MHz
- **Framework:** Arduino (.ino) via PlatformIO `wch-ch32v003` + `arduino`
- **PWM:** TIM1_CH4 on PC4, 8-bit (0–255), ~1kHz
- **MOSFET:** N-Channel by default (`USE_P_CHANNEL_MOSFET 0`); set to 1 for P-Channel
- **WS2812:** Bitbang on PD6 with inline-asm timing; brightness scales per mode

## Key Logic

- **Brightness button (PC1):** short-press cycles modes (0→1→2→3→0); long-press (>1s) toggles on/off. Mode 0 = off.
- **Timer button (PC2):** short-press cycles 10s→30s→60s; long-press (>1s) toggles timer enable.
- **Timer auto-shutdown:** blinks WS2812 in last 5s (500ms toggle).
- **Flash save:** settings at `0x08003FC0` (2 half-words: `timerEnabled|timerMode + magic 0xABCD`). Saved on timer mode change.

## Code Quirks

- `pwmSetDuty(0)` switches PC4 to GPIO mode (LOW for N-ch, HIGH for P-ch), not PWM — avoids floating gate at 0%.
- `getWS2812Brightness()`: standby (mode 0) = 20% of `WS2812_BRIGHTNESS`; active modes scale linearly per 1/3, 2/3, 3/3.
- WS2812 `lastSentColor` cache — skips send if color unchanged (saves power, avoids flicker).
- Integer division traps apply (see parent `../AGENTS.md`).

## Build

No `platformio.ini` checked in. Before building, create:

```ini
[env:ch32v003j4m6]
platform = wch-ch32v003
board = ch32v003j4m6
framework = arduino
build_flags = -Wno-error
build_unflags = -Werror
```

Flash via WCH-LinkE (blue LED OFF = RISC-V mode): DIO→PD1, VCC→3.3V, GND→GND.
