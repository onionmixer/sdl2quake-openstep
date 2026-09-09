# SDL2 Quake for OPENSTEP 4.2

Quake on OPENSTEP 4.2/Intel: the sdlquake engine on this workspace's own
SDL2 port, running LibreQuake's free data -- as `squake`, the software
renderer in an AppKit window, and as `glquake`, drawn end to end by a
Matrox G450's WARP engine through an accelerated Mesa 3.4.2, mipmaps
included.  The frame never crosses the bus.

## Where everything comes from

This tree is a PORT.  The things it ports are their own projects:

| | |
| --- | --- |
| Engine | [mckayemu/sdlquake](https://github.com/mckayemu/sdlquake) -- id Software's Quake, GPL-2.0, as cloned into `upstream/sdlquake/`, unmodified |
| Game data | [LibreQuake](https://github.com/lavenderdotpet/LibreQuake) -- free data, not in this repository; the `sdl2quake-libre` package carries it |
| SDL2 | [onionmixer/openstep-sdl2](https://github.com/onionmixer/openstep-sdl2) -- SDL 2.32.10 for OPENSTEP (openstep.2 or later) |
| Display driver + GL | [onionmixer/openstep-matrox-remade](https://github.com/onionmixer/openstep-matrox-remade) -- the G450 driver and `libGL_mga.a` (1.3 or later for mipmapping) |
| Mesa port | [onionmixer/openstep-mesa342](https://github.com/onionmixer/openstep-mesa342) -- Mesa 3.4.2 on OPENSTEP |

Nothing in `upstream/` is edited.  The files in `port/openstep/` are copies
that carry a notice saying what changed and when, and the build scripts
prefer them.

## What is here

```
upstream/sdlquake/   the engine as cloned, unmodified
port/openstep/       the files that had to change
build/               build-openstep-quake.sh (squake), build-glquake.sh (glquake)
pkg/                 the two OPENSTEP Installer packages
test/                probes and measured runs
tools/               host-side checkers
docs/                the plans, and every measurement behind them
```

## The two packages

* **sdl2quake** -- the engine binaries, statically linked: `squake`
  (software renderer) and `glquake` (hardware, needs the Matrox driver
  active with Mesa acceleration on).
* **sdl2quake-libre** -- LibreQuake's `id1/` data with its own credits,
  installed beside them.

Install both at the same place (default `/usr/local/quake`), then:

```
cd /usr/local/quake
./squake            # any machine
./glquake           # G450 + accel driver
```

The mouse is grabbed while a level is up; **Shift+Ctrl+G** hands it back,
and the title bar says so.

## Tested configuration

Everything here was developed and measured on one machine, and the
numbers assume its shape:

| | |
| --- | --- |
| OS | OPENSTEP 4.2 (Intel), desktop at **1024x768, RGB:888/32, 60 Hz** |
| Card | Matrox G450, 32 MB, primary head |
| Driver | OSMGADisplay 1.3 with `VRAM Mmap` and `Mesa Acceleration` = Yes |
| glquake | windowed **640x480** (the measured configuration); **1024x768** verified working |
| squake | windowed **640x480** |

The desktop depth matters: the accelerated path draws into a 32bpp
surface, and the driver's GL is qualified at RGB:888/32.  A 1600x1200
desktop also worked during development, but 1024x768 is where every
number in this README and the driver's docs was taken.

## Building from source

On the target, with the SDL2 and Mesa **packages installed**:

```
sh /ndrv/openstep-quake/build/build-openstep-quake.sh    # squake
sh /ndrv/openstep-quake/build/build-glquake.sh           # glquake
```

Both scripts default to `/LocalDeveloper`, the prefix the Installer
packages write to, and each prints the archives it chose before it links
anything.  Given a build tree instead -- `libSDL2.a` and `include/` side
by side rather than `Libraries/` and `Headers/` -- they take that shape
too, so a development tree still works; the point of printing is that
"what was installed" and "what the binary contains" can no longer differ
without saying so.

`glquake` links `libGL_mga.a` and gets the card; the same script also
links a `glquake_sw` control against stock Mesa, which draws the same
picture the slow way and exists to be compared against.

`glquake` still needs the driver's source tree for three headers --
`OpenStepMGAMesaTexture.h`, `OpenStepMGAMesaTriangle.h` and
`OpenStepMGAMesaWarp.h` -- which the Matrox Headers package does not
carry.  Everything else, including `libGL_mga.a` itself, comes from the
installed prefix.

## What the hardware path does

Every triangle GLQuake draws goes through the G450's WARP pipeline --
geometry, texturing with `GL_LINEAR_MIPMAP_NEAREST` mipmapping, blending,
depth.  States the engine cannot express fall back per triangle to Mesa's
software rasterizer and land in the same frame.  The measurements, and
the qualification that decided every admitted state, live in the driver
repository's `docs/`.

The software renderer is the same engine at `id386 = 0` (the C path Quake
shipped on every non-x86 Unix).  Its ceiling is the presentation: AppKit
moves a window's pixels at a measured 126 ns each, which caps 640x480 at
25.8 fps before the renderer draws anything.  That ceiling is what the GL
path removes.

## Licences

The engine is id Software's Quake under the **GNU General Public License,
version 2 or later** -- `LICENSE` here is that text, byte for byte
`upstream/sdlquake/COPYING`.  The ported files carry their change notices.

LibreQuake's data is under its own licences (BSD for the art); its
credits ship inside the `sdl2quake-libre` package next to the data.
