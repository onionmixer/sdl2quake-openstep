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
| Mesa port | [onionmixer/opennstep-mesa342](https://github.com/onionmixer/opennstep-mesa342) -- Mesa 3.4.2 on OPENSTEP |

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

## Building from source

On the target, with the SDL2 port and Mesa built:

```
sh /ndrv/openstep-quake/build/build-openstep-quake.sh    # squake
sh /ndrv/openstep-quake/build/build-glquake.sh           # glquake
```

Both scripts take the SDL2 build directory and the Mesa/driver trees as
arguments; the defaults name the paths this workspace uses.  `glquake`
links `libGL_mga.a` and gets the card; the same script also links a
`glquake_sw` control against stock Mesa, which draws the same picture the
slow way and exists to be compared against.

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
