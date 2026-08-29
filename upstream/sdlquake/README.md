# sdlquake
SDL port of the rasterizer only Quake engine, original on http://www.libsdl.org/projects/quake/

The quake engines begins with software rasterizer only, and the in quake 2 begins to use OpenGL backends.. the older pc in linux does not have much support of opengl modules in Xorg, so the software rasterizer are a good choice for those machines.

So Quake and quake2 can run in machines like AsusEEPC, VIA motherboards, Intel old i810 chipsets, Older Nvidia GNUp's and matrox unsupported GPU's.

## Cuurently works in this repo

The work in this repo by me will be limited to some compability with linux level, the rest will be let to contributions:

* the support for home configuration
* the support per game loading by game base dir names
* trying to port quake2 call to sdl rasterizer (by follow the work done in quake1)

This repo contains a quake1 engine ans some quake2 engine. Its focused in the quake 1 engine for now.
