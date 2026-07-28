# Wiring Guide

This project outputs audio from a Raspberry Pi Pico 2 W to a PCM5102A I2S DAC. PWM output and direct I2S amplifier modules are outside the scope of this guide.

## Assumed Hardware

- MCU: Raspberry Pi Pico 2 W
- DAC: PCM5102A I2S DAC module with the same jumper configuration as [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png)
- Audio output: amplifier with line input, active speaker, or headphone amplifier
- Audio format: 44.1 kHz / 16-bit / stereo PCM over I2S

The PCM5102A is not a speaker amplifier. Treat `OUTL` / `OUTR` as line-level outputs. Use a separate amplifier for passive speakers.

**Important:** This guide assumes that your PCM5102A module has the same jumper configuration as [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png). If the jumpers are unset or configured differently, this wiring alone may produce no sound.

## I2S vs I2C

This project uses the PCM5102A as an I2S DAC. I2C pins such as `SDA` / `SCL` are not used.

| Type | Signal names | Used by this project |
|---|---|---|
| I2S | `DIN`, `BCK` / `BCLK`, `LCK` / `LRCK` / `WS` | Yes |
| I2C | `SDA`, `SCL` | No |

If your PCM5102A module labels the data pin as `DATA`, `SD`, or `D`, it is likely equivalent to `DIN`. If you cannot find `BCK` / `BCLK` or `LCK` / `LRCK` / `WS`, check the module markings or product page.

## Default I2S Pins

These are the defaults in `src/config.h`.

```c
#define I2S_DATA_PIN    26
#define I2S_BCLK_PIN    27
#define I2S_LRCLK_PIN   28
```

## Pico 2 W to PCM5102A Wiring

### I2S Signals and Power

The following wiring assumes that the PCM5102A side already has the same jumper configuration as [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png).

| Pico 2 W | PCM5102A | Purpose |
|---|---|---|
| `GPIO 26` | `DIN` | I2S data |
| `GPIO 27` | `BCK` / `BCLK` | I2S bit clock |
| `GPIO 28` | `LCK` / `LRCK` / `WS` | I2S left/right clock |
| `3V3` | `VCC` / `VIN` | DAC power |
| `GND` | `GND` | Common ground |

### PCM5102A Jumper Settings

This guide requires the PCM5102A module jumpers to be configured as shown in [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png). You do not need to run separate jumper wires from the Pico 2 W to `XSMT`, `FMT`, `FLT`, or `DEMP`.

The assumed settings are:

| Setting | State | Purpose |
|---|---|---|
| `XSMT` | `H` / High | Unmute |
| `FMT` | I2S | Select I2S format |
| `FLT` | Normal / Low | Normal filter |
| `DEMP` | Off / Low | Disable de-emphasis |
| `SCK` | `GND` | 3-wire I2S / internal PLL operation |

If your module exposes an independent `SCK` pin, connect `SCK` to `GND`. If the module already ties `SCK` to GND through the same jumper configuration as the image, no additional wiring is needed.

### Audio Output

| PCM5102A | Connect to |
|---|---|
| `OUTL` | Amplifier / line input Left |
| `OUTR` | Amplifier / line input Right |
| `AGND` / `GND` | Amplifier / line input GND |

Using an amplifier or headphone amplifier is recommended instead of connecting headphones directly.

## Pico 2 W Physical Pin Reference

When looking at the Pico 2 W with the USB connector at the top, the default connections are:

| Physical pin | Pico 2 W | PCM5102A |
|---|---|---|
| `31` | `GP26` | `DIN` |
| `32` | `GP27` | `BCK` / `BCLK` |
| `34` | `GP28` | `LCK` / `LRCK` / `WS` |
| `36` | `3V3` | `VCC` / `VIN` |
| `38` | `GND` | `GND` |

`GP28` is physical pin `34`. Physical pin `35` is `ADC_VREF`, so do not connect it to `LCK` / `LRCK` / `WS`.

## Wiring Example

```text
Raspberry Pi Pico 2 W          PCM5102A

GPIO 26 ---------------------> DIN
GPIO 27 ---------------------> BCK / BCLK
GPIO 28 ---------------------> LCK / LRCK / WS
3V3     ---------------------> VCC / VIN
GND     ---------------------> GND

OUTL    ---------------------> Amplifier / line input Left
OUTR    ---------------------> Amplifier / line input Right
AGND    ---------------------> Amplifier / line input GND
```

## Power and Ground Notes

- Pico 2 W and PCM5102A must share a common GND.
- The PCM5102A can be powered from the Pico 2 W `3V3` pin.
- Adding about 0.1 uF ceramic capacitance near the PCM5102A power pins may reduce noise.
- If the amplifier uses a separate power supply, make sure the audio ground is properly referenced to the Pico 2 W / PCM5102A side.
- Keep I2S and GND wires short and solid.

## Changing I2S Pins

Edit `src/config.h`. `I2S_BCLK_PIN` and `I2S_LRCLK_PIN` are handled as consecutive side-set pins by the PIO program, so LRCLK must be the next GPIO after BCLK.

```c
#define I2S_DATA_PIN    20
#define I2S_BCLK_PIN    21
#define I2S_LRCLK_PIN   22
```

This example uses:

| Pico 2 W | PCM5102A |
|---|---|
| `GPIO 20` | `DIN` |
| `GPIO 21` | `BCK` / `BCLK` |
| `GPIO 22` | `LCK` / `LRCK` / `WS` |

## No Sound Checklist

- Confirm that `GPIO 26`, `GPIO 27`, and `GPIO 28` are connected to `DIN`, `BCK`, and `LCK/LRCK`.
- Confirm that Pico 2 W and PCM5102A share a common GND.
- Confirm that the PCM5102A jumper settings match [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png).
- If your module exposes an independent `SCK` pin, confirm that `SCK` is connected to `GND`.
- Confirm that `OUTL` / `OUTR` are connected to an amplifier or line input.
- Check the volume on the phone, amplifier, and speaker.
- Check USB serial logs for the Bluetooth connection state. To see periodic `Underruns` / `Overruns` output, set `ENABLE_DEBUG_LOG` to `1` in `src/config.h`.

## Noise Checklist

- Keep I2S wires short.
- Keep GND wiring short and solid.
- Add a 0.1 uF capacitor near the PCM5102A power pins.
- Use direct PC USB or a stable USB power supply instead of a USB hub.
- Review the amplifier power ground and PCM5102A audio ground routing.

## References

- [PCM5102A datasheet](https://www.ti.com/product/PCM5102A)
- [Raspberry Pi Pico Pinout](https://datasheets.raspberrypi.com/pico/Pico-R3-A4-Pinout.pdf)
- [I2S overview](https://en.wikipedia.org/wiki/I%C2%B2S)
