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
- PK3 archives
- TGA, JPEG, and PNG textures
- Quake 3-style shader scripts
- 96 entity definitions generated from OpenMoHAA's game source
- Correct OpenMoHAA user-data prefix on macOS

## Deliberate limitations

MOHAA BSP compilation is not enabled in the default build menu. MOHAA uses the
`2015` BSP identifier and versions 17 through 21, with MOH-specific terrain,
static-model, and lighting lumps. NetRadiant Custom's bundled `q3map2` writes
Quake 3 `IBSP` files and must not be presented as a compatible compiler.

MOHAA `terrainDef` primitives are preserved when loading and saving maps, but
they are not rendered or editable yet. Brushes, entities, and `patchDef2`
primitives in the same map remain available for normal editing.

TIKI/SKD model previews and FTX texture previews are also not implemented yet.
Those formats remain usable as entity key values, but Radiant displays the
entity box instead of the game model.

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
