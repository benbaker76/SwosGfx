# Tests

Build them with `-DSWOSGFX_TESTS=ON`; `ctest` runs each one with no arguments. Without
arguments a test checks what it can on its own data (round trips, damaged and malformed
input, fuzzing) and skips what needs the game's files, so the CI runs them all.

Every mode of swosgfx was ported from the C# [SwosGfx](https://github.com/benbaker76/SwosGfx-CSharp)
and checked against it **byte for byte**: the C# tool writes a reference folder, the test
writes the same thing, and every file must match. To repeat that, give a test the game's
files (your own copy: none ship with swosgfx) and a reference folder made by the C# tool
with the same switches:

| Test | Arguments | What it checks |
|---|---|---|
| `rnc_unpack_test` | `[DIR]` | RNC pack/unpack round trips, damaged files; with DIR, every RNC file under it unpacks with good CRCs |
| `palettes_test` | `[REFDIR [RESOLVEDIR]]` | `-palettes` for every option combination; ResolvePalette |
| `tiled_map_test` | `[DIR...]` | JSON edge cases, fuzzing; every `.tmj` under DIR reads and writes back identical |
| `dos_sprites_test` | `[DOSDIR]` | `-sprites-export` / `-sprites-import` round trip on a copy |
| `dos2amiga_test` | `DOSDIR OUTDIR [REFDIR]` | `-dos2amiga` |
| `jag2amiga_test` | `DOSDIR ROM JAGDIR OUTDIR [REFDIR [template] [sheets]]` | `-jag2amiga` (JAGDIR `-` for none) |
| `dos_picture_test` | `DOSDIR OUTDIR [REFDIR] [WxH \| scale \| batch]` | `-picture` |
| `font_extend_test` | `WORKDIR [REFDIR] [noboxes] [inplace]` | `-font-extend`, integration.txt |
| `dos_pitch_render_test` | `DOSDIR OUTDIR [REFROOT]` | PC pitches to BMP and Tiled |
| `amiga_tools_test` | `GRAFS GRAFS_AGA OUTDIR [REFDIR]` | every Amiga conversion and the batch forms (`--list` prints the cases the reference script runs) |

Each test's header comment has the details. Paths to the game's files are only ever read:
the import tests work on copies.
