# ayumi-juce: JUCE plugin for ayumi YM2149 PSG emulator

ayumi-juce is a JUCE port of [ayumi-lv2](https://github.com/atsushieno/ayumi-lv2/) to support VST3. It is built on top of [true-grue/ayumi](https://github.com/true-grue/ayumi) YM2149 emulator.

It is primarily build for [augene-ng](https://github.com/atsushieno/augene-ng) project that requires VST3 on Linux (which is the only common plugin format among OSS AudioPluginHost build and Tracktion Waveform).

The plugin can be controlled either via plugin parameters or MIDI messages such as program changes and control changes (explained below).

## Software envelope

ayumi-juce supports primitive-ish software envelope beyond what YM2149 supports (it used to be called "hardware envelope" when it was hardware. it's kind of awkward to call it hardware on the emulator, but we'd call it so for consistency and identification).

The software envelope can be specified per channel, and an envelope consists of a sequence of "stop points" which is a pair of "point (location)" and "volume ratio". There are at most 6 points in a sequence, but we keep **the last item for "preserved" and it should be kept to 0.0f/0.0f so far**. Usually you only need 3 points for Attach, Decay and Sustain (Release is not supported, which is also kind of why the last item is preserved).

## MIDI mappings

ayumi parameters are controlled via MIDI messages.

ayumi is an SSG (PSG) emulator for AY-3-8910 or YM2149, and therefore handles at most 3 monophonic channels. Since each channel can be configured with mixer, volume, pan etc., every PSG channel is assigned an entire MIDI channel that are 0, 1, or 2. Other channels are ignored, and you cannot have any polyphonic outputs for one single channel.

The accepted MIDI messages are:

| MIDI message | Ayumi message | MIDI to ayumi mappings |
|-|-|-|
| Note Off (8xh) | note off (set_mixer) | - |
| Note On (9xh) | note on (set mixer,_set tone) | tone: 0-127 -> 0-4095 (indirect) |
| Program Change (Cxh) | set noise, set mixer | noise: 0-31 + mixer |
| CC - Bank MSB (Bxh-00h) | set mixer | 1: tone off, 2: noise off, 4: envelope on |
| CC - Volume (Bxh-07h) | set volume | 0-127 -> 0-14 |
| CC - Pan MSB (Bxh-0Ah) | set pan | 0-127 -> 0.0-1.0 |
| CC - 10h | envelope High bits | 0-65535 with 11h/12h (only lower 2 bits are used) |
| CC - 11h | envelope Middle bits | 0-65535 with 10h/12h (7 bits) |
| CC - 12h | envelope Low bits | 0-65535 with 10h/11h (7 bits) |
| CC - 13h | envelope shape | 0-15 |
| CC - 20h | software envelope: number of stops | 0-6 |
| CC - 21h | software envelope: stop 0 at | s: 0-127 -> 0.001 * s^2 seconds |
| CC - 22h | software envelope: stop 0 volume ratio | 0-127 -> 0.0-1.0 |
| CC - 23h | software envelope: stop 1 at | (same as stop 0 at) |
| CC - 24h | software envelope: stop 1 volume ratio | (same as stop 0 volume ratio) |
| .. | .. | .. |
| CC - 2Ch | software envelope: stop 5 volume ratio | (same as stop 0 volume ratio) |
| CC - 50h | remove dc | |

On Program Change messages, partial mixer settings can be added to noise as follows:

- tone off: +32
- noise off: +64

The same can be specified by CC Bank Change, which also supports envelope switch.

For some reason, ayumi does not process volume 15 as expected. Therefore it is rounded to 14.

## Licenses

ayumi-juce sources are distributed under the MIT license.

When built with JUCE, it the resulting programs (binaries etc.) can be distributed only under the GPL v3 license or whatever does not violate it.

ayumi is distributed under the MIT license.

The project is based on [tomoyanonymous/juce_cmake_vscode_example](https://github.com/tomoyanonymous/juce_cmake_vscode_example) template, which is distributed under the MIT license.
