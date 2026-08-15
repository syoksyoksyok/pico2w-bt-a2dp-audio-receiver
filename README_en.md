# Pico 2 W Bluetooth A2DP Audio Receiver

This firmware turns a Raspberry Pi Pico 2 W into a Bluetooth audio receiver (A2DP Sink: the Bluetooth audio receiving/playback side) and outputs audio received from a smartphone through a PCM5102A I2S DAC.

## Features

- Connects from iPhone / Android as a Bluetooth speaker
- Decodes Bluetooth A2DP SBC audio to 16-bit stereo PCM
- Outputs I2S audio to a PCM5102A using PIO + DMA
- Allows another smartphone to take over the audio connection while one phone is playing
- Provides USB serial logs for connection status

## Requirements

| Type | Item |
|---|---|
| MCU | Raspberry Pi Pico 2 W |
| DAC | PCM5102A I2S DAC module |
| Audio output | Amplifier with line input, active speaker, or headphone amplifier |
| Other | USB cable, jumper wires, and optionally a breadboard |

The PCM5102A is not a speaker amplifier. If you use passive speakers, connect an amplifier after the PCM5102A.

## Important Assumption

This project's wiring assumes that your PCM5102A module has the same jumper configuration as [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png).

If the jumper configuration is different, Bluetooth may connect but no sound may be output. Check [WIRING_en.md](WIRING_en.md) before wiring.

## Wiring Summary

The default I2S pins are:

| Pico 2 W | PCM5102A | Purpose |
|---|---|---|
| GPIO 26 | DIN | I2S data |
| GPIO 27 | BCK / BCLK | I2S bit clock |
| GPIO 28 | LCK / LRCK / WS | I2S left/right clock |
| 3V3 | VCC / VIN | DAC power |
| GND | GND | Common ground |

See [WIRING_en.md](WIRING_en.md) for detailed wiring, PCM5102A jumper settings, and no-sound checks.

## Build Environment

- Pico SDK 2.x
- CMake 3.13 or later
- Arm GNU Toolchain
- Git

Set `PICO_SDK_PATH` to your Pico SDK path before building.

```bash
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$PWD
```

PowerShell example on Windows:

```powershell
$env:PICO_SDK_PATH = "C:\pico\pico-sdk"
```

## Build

```bash
git clone https://github.com/syoksyoksyok/pico2w-bt-a2dp-audio-receiver.git
cd pico2w-bt-a2dp-audio-receiver
mkdir build
cd build
cmake ..
cmake --build .
```

A successful build creates:

```text
build/pico2w_bt_a2dp_receiver.uf2
```

## Flashing to Pico 2 W

1. Hold the `BOOTSEL` button while connecting the Pico 2 W over USB.
2. The board appears as an `RPI-RP2` drive.
3. Copy `pico2w_bt_a2dp_receiver.uf2` to the `RPI-RP2` drive.
4. The Pico 2 W reboots automatically.

## Usage

1. Power on the Pico 2 W.
2. Open Bluetooth settings on your phone.
3. Select `Pico2W Audio Receiver` and connect.
4. Play music from your phone.

Open the USB serial log at 115200 bps if needed.

```bash
screen /dev/ttyACM0 115200
```

When ready, the log shows something like:

```text
Device name: Pico2W Audio Receiver
Ready! Waiting for Bluetooth connection...
```

## Configuration

Main user settings are in [src/config.h](src/config.h).

| Setting | Default | Description |
|---|---:|---|
| `BT_DEVICE_NAME` | `Pico2W Audio Receiver` | Bluetooth display name |
| `AUDIO_SAMPLE_RATE` | `44100` | Sample rate |
| `I2S_DATA_PIN` | `26` | I2S DATA |
| `I2S_BCLK_PIN` | `27` | I2S BCLK |
| `I2S_LRCLK_PIN` | `28` | I2S LRCLK |
| `AUDIO_BUFFER_SIZE` | 1 second | Ring buffer for Bluetooth receive jitter |
| `AUTO_START_THRESHOLD` | about 33% | Buffer level required before playback starts |
| `REBUFFER_THRESHOLD` | `AUTO_START_THRESHOLD` | Buffer level required before resuming after underrun |
| `PICO_A2DP_ENABLE_DEBUG_LOG` | `OFF` | Enables SBC parameter and buffer debug logs |
| `SOFTWARE_VOLUME_PERCENT` | `70` | Software volume used to reduce downstream clipping |

The current A2DP / I2S configuration assumes 44.1 kHz. To change to 48 kHz, update not only `config.h` but also the SBC capability in `src/bt_audio.c` and the I2S-related calculations.

The SBC capability advertises bitpool min 2 and max 53 for standard SBC at 44.1 kHz Stereo / Joint Stereo. Bitpool is not volume; it relates to the amount of information used by SBC compression. The actual bitpool is selected by negotiation with the source device, so advertising max 53 does not guarantee that every phone or PC will transmit at 53.

`SOFTWARE_VOLUME_PERCENT` remains 70% to reduce clipping in downstream audio equipment when the source device volume is high. The scaling uses 32-bit integer math with nearest rounding that treats positive and negative samples symmetrically.

The I2S output still uses one DMA channel with two DMA buffers. The DMA IRQ only switches to the next prepared buffer or silence; refilling 512 stereo samples is handled outside the IRQ by the main loop. After an underrun, playback stays silent until `REBUFFER_THRESHOLD` stereo samples have accumulated, which avoids rapid alternation between silence and short bursts of audio. Larger buffer thresholds improve dropout tolerance but increase restart latency.

When `PICO_A2DP_ENABLE_DEBUG_LOG=ON`, USB serial logs include actual SBC bitpool, sample rate, channel mode, blocks, subbands, allocation method, SBC parse errors, ring buffer min/max/current usage, underruns, overruns, dropped stereo samples, rebuffer count, and DMA buffer states. With the default OFF setting, display-only SBC parsing, debug counters, state comparisons, strings, timers, and debug-only memory are excluded by the preprocessor.

## Troubleshooting

### The phone cannot find the device

- Check that the Pico 2 W is running.
- Turn Bluetooth off and on again on the phone.
- Reboot the Pico 2 W.

### Bluetooth connects but there is no sound

- Check the wiring in [WIRING_en.md](WIRING_en.md).
- Confirm that the PCM5102A jumper configuration matches [PCM5102A_jumper connection.png](PCM5102A_jumper%20connection.png).
- Confirm that Pico 2 W and PCM5102A share a common GND.
- Check the volume on the phone, amplifier, and speaker.
- On Android, remove the pairing information once and reconnect.

### Audio cuts out or has noise

- Use direct PC USB or a stable USB power supply instead of a USB hub.
- Keep I2S wires short.
- Keep GND wiring short and solid.
- Add about 0.1 uF decoupling capacitance near the PCM5102A power pins.

## File Layout

| Path | Description |
|---|---|
| `README.md` | Japanese overview, build, and usage guide |
| `README_en.md` | English overview, build, and usage guide |
| `WIRING.md` | Japanese wiring guide |
| `WIRING_en.md` | English wiring guide |
| `PCM5102A_jumper connection.png` | Photo of the required PCM5102A jumper configuration |
| `src/main.c` | Main loop and initialization |
| `src/bt_audio.c` | Bluetooth A2DP Sink handling |
| `src/audio_out_i2s.c` | I2S / DMA audio output |
| `src/audio_out_pwm.c` / `src/audio_out_pwm.h` | Legacy PWM output implementation, not built by the current firmware |
| `src/config.h` | User settings |
| `src/i2s.pio` | PIO program for I2S output |
| `CMakeLists.txt` | Build configuration |

## Technical Notes

```text
Phone
  -> Bluetooth A2DP SBC
  -> BTstack A2DP Sink
  -> SBC Decoder
  -> 16-bit stereo PCM ring buffer
  -> DMA ping-pong buffer
  -> PIO I2S output
  -> PCM5102A I2S DAC
  -> Amplifier / speaker
```

Current main settings:

- Bluetooth: Classic A2DP Sink
- SBC: 44.1 kHz, Stereo / Joint Stereo, bitpool min 2 / max 53
- PCM: 16-bit stereo
- I2S BCLK: 1.4112 MHz at 44.1 kHz / 16-bit / stereo
- I2S PIO: 64 cycles / stereo sample
- Ring buffer: 44100 stereo samples
- DMA buffer: 512 stereo samples x 2
- A2DP signaling connection slots: 2, used for connection takeover

## License

MIT License. See [LICENSE](LICENSE) for details.

## References

- [Raspberry Pi Pico SDK Documentation](https://www.raspberrypi.com/documentation/pico-sdk/)
- [BTstack Documentation](https://bluekitchen-gmbh.com/btstack/)
- [PCM5102A datasheet](https://www.ti.com/product/PCM5102A)
