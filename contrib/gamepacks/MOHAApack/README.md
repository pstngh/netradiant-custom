# Medal of Honor / OpenMoHAA game pack

This bundled game pack adds editor profiles for:

- Medal of Honor: Allied Assault (`main`)
- Medal of Honor: Allied Assault - Spearhead (`mainta`)
- Medal of Honor: Allied Assault - Breakthrough (`maintt`)

The profiles work with original game data and with OpenMoHAA. They use
OpenMoHAA's official `launch_openmohaa_base`,
`launch_openmohaa_spearhead`, and `launch_openmohaa_breakthrough`
launchers.

## Current support

- MOHAA textual `.map` loading and saving, including extended brush-face
  metadata and patch subdivision parameters
- MOHAA `terrainDef` rendering, selection, vertex-height editing, and
  lossless metadata round-tripping
- Native `2015` version 19 BSP, VIS, and light compilation with the bundled
  `q3map2`
- Fast-test and final build-menu presets, with optional direct PK3 deployment
- PK3 archives
- TGA, JPEG, and PNG textures
- Quake 3-style shader scripts
- 96 entity definitions generated from OpenMoHAA's game source
- Correct OpenMoHAA user-data prefix on macOS

## Building and deploying

The Build menu provides:

- **MOHAA: Fast test** — BSP, fast VIS, and fast lighting
- **MOHAA: Fast test + PK3** — the fast build followed by PK3 deployment
- **MOHAA: Final + PK3** — full VIS, higher-quality lighting, and PK3 deployment

PK3 builds are written to the selected game directory as
`main/<mapname>.pk3`, `mainta/<mapname>.pk3`, or `maintt/<mapname>.pk3`.
They contain `maps/<mapname>.bsp` and also include same-name `.scr`, `.aas`,
and `.arena` files found beside the BSP. Installed stock data and assets from
other PK3s remain external, avoiding a large duplicate copy of the game data.

The compiler writes the MOHAA `2015` header and version 19 lumps accepted by
OpenMoHAA. `terrainDef` grids are converted to textured, collidable detail
geometry during compilation. This deliberately favors compatibility over the
original engine's specialized terrain LOD lump.

## Deliberate limitations

TIKI/SKD model previews and FTX texture previews are also not implemented yet.
Those formats remain usable as entity key values, but Radiant displays the
entity box instead of the game model.

The first compiler version does not emit native MOHAA terrain-LOD,
static-model, spherical-light, or compressed light-grid lumps. Terrain still
renders and collides through ordinary BSP geometry, and surface lightmaps are
written normally. MOHAA builds therefore disable light-grid generation;
dynamic entities without their own lighting are fullbright. The generated
BSPs target OpenMoHAA first; compatibility with every original EA executable
is not yet guaranteed.

Select the directory that contains the chosen OpenMoHAA launcher when Radiant
asks for the engine path. The default macOS location is
`/Applications/OpenMoHAA/`; choose the extracted OpenMoHAA directory if yours
is elsewhere.

## Entity provenance

`main/entities.def`, `mainta/entities.def`, and `maintt/entities.def` are
generated from `/*QUAKED ... */` documentation in OpenMoHAA's `code/fgame`
sources. The generated files record the exact source commit. The source
material is licensed under GPL-2.0; see `OPENMOHAA-COPYING.txt`.

To refresh the definitions from a local OpenMoHAA checkout:

```sh
python3 contrib/gamepacks/MOHAApack/tools/generate_entities.py \
  /path/to/openmohaa
```

The generator excludes three obsolete duplicate Quake 3 spawn definitions in
`g_client.cpp` in favor of OpenMoHAA's richer definitions in
`playerstart.cpp`. It also repairs the missing space before the color tuple in
five source comments so Radiant can parse their class names.
