# Quake for OPENSTEP 4.2

sdlquake's engine on this workspace's own SDL2 port, running
[LibreQuake](https://github.com/lavenderdotpet/LibreQuake)'s free data.
Intel i486 OPENSTEP 4.2, software renderer, 640x480.

**It runs.** The paks load, the window opens, the menus work, WASD moves,
and the ambience and torches are audible.

```
4527 frames  282.5 seconds  16.0 fps      640x480, LibreQuake demo1
```

## What is here

```
upstream/sdlquake/   the engine as cloned, unmodified
port/openstep/       the four files that had to change
build/               two scripts that run on the target
tools/               three host-side checkers
docs/Q1_PORT_PLAN.md the plan, and every measurement behind it
```

Nothing in `upstream/` is edited.  The four files in `port/openstep/` are
copies that carry a notice saying what changed and when, and the build
script prefers them.

## Building

On the target, with the SDL2 port and Mesa already built:

```
sh /ndrv/openstep-quake/build/build-openstep-quake.sh
```

It compiles the portable engine first, on its own, then the platform files,
then links.  The order is deliberate: the engine can only fail for
compiler reasons and the platform files can only fail for SDL2 reasons, so
whatever breaks says where it lives.

Needs `libSDL2.a` from
[openstep-sdl2](https://github.com/onionmixer/openstep-sdl2) (openstep.2 or
later) and a Mesa `libGL.a` -- the latter even though no GL context is ever
created, because `libSDL2.a` carries a GL backend and wants its symbols.

## Running

```
squake -basedir /usr/local/quake
```

with LibreQuake's `id1/` under that directory.  `-nomouse` leaves the
pointer alone, which is worth having while testing.

## The assembly is not used, and not for the reason you would guess

Quake ships twenty-one hand-written `.S` files, thirteen of which are the
fast software renderer.  **They assemble on OPENSTEP** -- eighteen of
eighteen that this port would ever build, checked with
`tools/check-asm-assembles.sh`.  `asm_i386.h` reduces `C(label)` to
`_label` when ELF is not defined, which is exactly Mach-O's convention.

This build uses the C twins anyway (`id386 = 0`, the path Quake shipped on
Alpha, MIPS and PowerPC), because at 640x480 the C span loop costs 1.18 ms
and putting the frame on the screen costs 41 ms.  The renderer is a few
per cent of a frame.  Making it faster buys back a few per cent.

## What limits it

The frame is 62.5 ms, and 41 of those are the presentation: AppKit moves a
window's pixels at a measured 126 ns each, whatever the size.  That puts a
ceiling of 25.8 fps at 640x480 no matter how fast the renderer gets, and
the engine is already at 16.

Hardware acceleration exists in this workspace -- the Matrox G450 driver's
Mesa can draw on the card and put the frame on screen without it crossing
the bus -- but it lives in the OpenGL path, and this renderer never calls
OpenGL.  GLQuake is the way to reach it, and the tree has all fourteen
`gl_*.c` files; what it has no SDL version of is the GL video backend.

## Licences

The engine is id Software's Quake under the **GNU General Public License,
version 2 or later**; `upstream/sdlquake/COPYING` is the text.  The four
modified files each carry a notice of what changed and when.

LibreQuake's data is **not** in this repository.  Its art is under a BSD
licence and it is 50 MB; take it from its own releases.  The `docs/` files
inside its `id1/` carry its credits, and they belong next to the data
wherever it is installed.
