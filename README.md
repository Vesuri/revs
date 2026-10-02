# Revs — Amiga

An unofficial Amiga port of Geoff Crammond’s BBC Micro Formula 3 racing
simulation, originally published by Acornsoft in 1985. Race against a full grid,
practise laps and tune your car’s wings.

The port reconstructs the original engine in C and 68000 assembly, with native
Amiga graphics, sound and input. It targets *Revs Plus Revs 4 Tracks* and includes
Silverstone, Brands Hatch, Donington Park, Oulton Park and Snetterton, plus
Nürburgring.

## Requirements and installation

- PAL Amiga with a 68000 or better and Kickstart 1.3 or better.
- Standalone: 512 KB Chip RAM and 512 KB other RAM.
- WHDLoad: version 17+, at least 2 MB RAM, and an A500 Kickstart 1.3 image
  (`kick34005.A500`) with its matching RTB file in `Devs:Kickstarts`.
- A supported BBC Micro disc image: *Revs Plus Revs 4 Tracks* or *Revs+ [hack]*.

The release is `Revs-0.91.lha`. Open its **Revs Install** drawer, run **Install**
and select your extracted `.ssd` disc image. Either supported image provides
all six circuits. To run without WHDLoad, place the image beside the executable
as `revs.ssd` and start the game from Workbench or a Shell.

The original disc image is not included; the game reads its engine at startup.
See [the release ReadMe](whdload/Revs%20Install/ReadMe) for supported images and
full installation instructions.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Steer, accelerate and brake |
| L / ; | Steer left / right |
| S / A | Accelerate / brake |
| Q / Tab | Change up / down; hold to disengage the clutch |
| T | Starter motor |
| Space | Fine steering |
| Shift + F1 / F2 | Select keyboard / mouse steering |
| Shift + Help / Backspace | Pause / resume |
| Ctrl + Q | Quit the game |

Hold **T**, **S** and **Q** together to start the engine in gear, then release
**Q** to engage the clutch. With mouse steering, the right button accelerates,
the left brakes and the middle changes gear. See [the controls guide](docs/controls.md)
for pit stops, steering assistance and other commands.

## Building

A fresh checkout needs the original disc, generated engine sources and a local
Ghidra setup. The Amiga build uses `m68k-amiga-elf-gcc`, vasm and `elf2hunk`;
release packaging also needs the WHDLoad development files and archive tools.
Follow [the toolchain guide](docs/toolchain.md) to prepare these prerequisites, then:

```sh
. amiga/env.sh
make dist
```

See [release packaging](docs/whdload.md) and the
[documentation index](docs/README.md) for development and validation details.

## Credits

Revs was written by Geoff Crammond and published by Acornsoft. Amiga port by
Vesuri. This is an unofficial fan port; the original game and assets belong to
their respective copyright holders. See [reference sources and provenance](docs/reference-sources.md)
and [framework credits](src/platform/amiga/framework/UPSTREAM.md).
