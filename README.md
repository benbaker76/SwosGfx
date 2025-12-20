# SwosGfx
Tool for generating or extracting graphics data and palettes for SWOS (Amiga and DOS).

### Screenshots

![](images/tiled_00.png)

![](images/tiled_01.png)

---

## Amiga usage

### Explicit input/output (two positional args)

```bash
SwosGfx -amiga -map -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.map out.bmp
SwosGfx -amiga -map -output=tmx  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.map out.tmx
SwosGfx -amiga -tmx -output=map  [-bitplanes=N]    [-no-rnc]      in.tmx out.map
SwosGfx -amiga -bmp -output=map  [-bitplanes=N]    [-no-rnc]      fullPitch.bmp out.map
SwosGfx -amiga -bmp -output=raw  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.bmp out.raw
SwosGfx -amiga -raw -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.raw out.bmp
````

### Batch mode (0 or 1 positional arg)

In batch mode, input comes from the Amiga directory (`./Amiga` by default, or `-amiga-dir=<path>`).

* **0 args**: output goes to the current directory
* **1 arg**: treated as an output directory

Supported batch conversions:

```bash
SwosGfx -amiga -raw -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]
SwosGfx -amiga -map -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]
SwosGfx -amiga -map -output=tmx  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]
```

---

## DOS usage

### DOS pitch rendering (patterns from `./DOS`)

In DOS pitch mode, input is implicit from the DOS directory (`./DOS` by default, or `-dos-dir=<path>`).

This mode renders **all 6 pitches** (`pitch1..pitch6`) using the selected type.

* **0 args**: output goes to the current directory
* **1 arg**: treated as an output directory

```bash
SwosGfx -dos -output=bmp [-pitch=N] [-type=normal] [-colors=N] [outDir]
SwosGfx -dos -output=tmx [-pitch=N] [-type=normal] [-colors=N] [outDir]
```

Output filenames:

* BMP: `pitchN-<type>.bmp`
* TMX: `pitchN-<type>.tmx`

Where `<type>` is one of: `frozen, muddy, wet, soft, normal, dry, hard`.

Note: `-pitch` currently exists as an option, but the current implementation renders pitches `1..6` in a loop.

---

### DOS picture usage (`.256` → BMP)

```bash
SwosGfx -dos -picture -output=bmp in.256 out.bmp
```

---

### DOS sprite usage (SPRITE.DAT + *.DAT from `./DOS`)

#### Export all sprites to BMP

* **0 args**: output to current directory
* **1 arg**: treated as output directory

```bash
SwosGfx -dos -sprites-export [-sprite-bg=N] [-output=bmp] [outDir]
# writes sprNNNN.bmp, 0 <= N < 1334
```

#### Import sprites from a directory and update DAT/SPRITE.DAT

```bash
SwosGfx -dos -sprites-import spritesDir
# reads sprNNNN.bmp, inserts and saves changes to DAT/SPRITE.DAT
```

---

## Directories

* Amiga graphics files: `./Amiga` (default, can be changed with `-amiga-dir=<path>`)

  * Pitch files: `SWCPICH1.MAP`, `SWCPICH2.MAP`, `SWCPICH3.MAP`, `SWCPICH4.MAP`, `SWCPICH5.MAP`, `SWCPICH6.MAP`, `SWCPICH7.MAP`
  * Raw files: `CHARSET.RAW`, `CJCBENCH.RAW`, `CJCBITS.RAW`, `CJCGRAFS.RAW`, `CJCTEAM1.RAW`, `CJCTEAM2.RAW`, `CJCTEAM3.RAW`, `CJCTEAMG.RAW`, `DISK22.RAW`, `LOADER00.RAW`, `LOADER01.RAW`, `LOADER1.RAW`, `LOADER10.RAW`, `LOADER2.RAW`, `LOADER3.RAW`, `LOADER4.RAW`, `LOADER5.RAW`, `LOADER6.RAW`, `LOADER7.RAW`, `LOADER8A.RAW`, `LOADER8B.RAW`, `LOADER9.RAW`, `MENUBG.RAW`, `MENUBG2.RAW`, `MENUS.RAW`, `MENUS2.RAW`, `OLDDISK1.RAW`, `SOCCER_S.RAW`

* DOS graphics files: `./DOS` (default, can be changed with `-dos-dir=<path>`)

  * Pitch maps: `PITCH1.DAT`, `PITCH2.DAT`, `PITCH3.DAT`, `PITCH4.DAT`, `PITCH5.DAT`, `PITCH6.DAT`
  * Pitch data: `PITCH1.BLK`, `PITCH2.BLK`, `PITCH3.BLK`, `PITCH4.BLK`, `PITCH5.BLK`, `PITCH6.BLK`
  * Picture files: `TITLE.256`, `SCORES.256`, `LOADSCR.256`
  * Sprite files: `SPRITE.DAT`, `CHARSET.DAT`, `SCORE.DAT`, `TEAM1.DAT`, `TEAM3.DAT"`, `GOAL1.DAT`, `GOAL1.DAT`, `BENCH.DAT`

## File Info (Amiga)

| Name         | Size    | Size (4 bitplanes) | Size (8 bitplanes) |
| ------------ | ------- | ------------: | ------------: |
| CHARSET.RAW  | 320x256 |         40960 |         81920 |
| CJCBENCH.RAW | 320x256 |         40960 |         81920 |
| CJCBITS.RAW  | 320x256 |         40960 |         81920 |
| CJCGRAFS.RAW | 320x256 |         40960 |         81920 |
| CJCTEAM1.RAW | 320x256 |         40960 |         81920 |
| CJCTEAM2.RAW | 320x256 |         40960 |         81920 |
| CJCTEAM3.RAW | 320x256 |         40960 |         81920 |
| CJCTEAMG.RAW | 320x256 |         40960 |         81920 |
| MENUS.RAW    | 320x256 |         40960 |         81920 |
| MENUS2.RAW   | 320x256 |         40960 |         81920 |
| SOCCER_S.RAW | 320x256 |         40960 |         81920 |
| DISK22.RAW   | 352x272 |         47872 |         95744 |
| LOADER00.RAW | 352x272 |         47872 |         95744 |
| LOADER01.RAW | 352x272 |         47872 |         95744 |
| LOADER1.RAW  | 352x272 |         47872 |         95744 |
| LOADER2.RAW  | 352x272 |         47872 |         95744 |
| LOADER3.RAW  | 352x272 |         47872 |         95744 |
| LOADER4.RAW  | 352x272 |         47872 |         95744 |
| LOADER5.RAW  | 352x272 |         47872 |         95744 |
| LOADER6.RAW  | 352x272 |         47872 |         95744 |
| LOADER7.RAW  | 352x272 |         47872 |         95744 |
| LOADER8A.RAW | 352x272 |         47872 |         95744 |
| LOADER8B.RAW | 352x272 |         47872 |         95744 |
| LOADER9.RAW  | 352x272 |         47872 |         95744 |
| LOADER10.RAW | 352x272 |         47872 |         95744 |
| MENUBG.RAW   | 352x272 |         47872 |         95744 |
| MENUBG2.RAW  | 352x272 |         47872 |         95744 |
| OLDDISK1.RAW | 352x272 |         47872 |         95744 |

| Name         | Map Size WxH      | Size in Pixels  | Size (4 bitplanes) Map / Tiles / Total | Size (8 bitplanes) Map / Tiles / Total |
| ------------ | ------- | ------- | --------------------------: | -----------------------: |
| SWCPICH1.MAP | 42x55   | 672x880 |        9240 / 36352 / 45592 |     9240 / 61952 / 71192 |
| SWCPICH2.MAP | 42x55   | 672x880 |        9240 / 30208 / 39448 |     9240 / 57856 / 67096 |
| SWCPICH3.MAP | 42x55   | 672x880 |        9240 / 33792 / 43032 |     9240 / 66304 / 75544 |
| SWCPICH4.MAP | 42x55   | 672x880 |        9240 / 30592 / 39832 |     9240 / 60672 / 69912 |
| SWCPICH5.MAP | 42x55   | 672x880 |        9240 / 35072 / 44312 |     9240 / 68864 / 78104 |
| SWCPICH6.MAP | 42x55   | 672x880 |        9240 / 22144 / 31384 |     9240 / 54016 / 63256 |

## Map Data (Amiga)

The Amiga map format consists of a 42 x 55 tile map followed immediately by the tile pixel data. The tile map is an array of 32-bit values, one per tile cell (42 * 55 = 2310 entries, 9240 bytes total). Each entry stores an offset (byte address) into the tile data block for the tile that should be drawn at that cell. In other words, the map does not store "tile indices" in the conventional sense; it stores pointers/offsets to the tile’s pixel payload within the tile data that follows.

Directly after the 9240-byte tile map comes the tile data itself, stored as a contiguous stream of 16 x 16 pixel tiles in classic Amiga planar (bitplane) format using 4 bitplanes (16 colors). Each tile is encoded one scanline at a time: for each of the 16 rows, the data contains one 16-bit word per bitplane (because 16 pixels / 16 bits = 1 word). The planes are stored separately, and the final pixel color index is reconstructed by taking the corresponding bit from each plane (plane 0 is the least-significant bit), combining them into a 4-bit value in the range 0-15. This yields a per-tile storage size of 16 rows x 4 planes x 2 bytes = 128 bytes per tile, and the map entry offsets point to the start of these 128-byte tile records within the tile data stream.

## Palettes (Amiga)

### Game
![](images/amiga_pal_game.png)

### Menu
![](images/amiga_pal_menu.png)

### Pitch (Frozen)
![](images/amiga_pal_frozen.png)

### Pitch (Muddy)
![](images/amiga_pal_muddy.png)

### Pitch (Wet)
![](images/amiga_pal_wet.png)

### Pitch (Soft)
![](images/amiga_pal_soft.png)

### Pitch (Normal)
![](images/amiga_pal_normal.png)

### Pitch (Dry)
![](images/amiga_pal_dry.png)

### Pitch (Hard)
![](images/amiga_pal_hard.png)

## Palettes (DOS)

For the pitch palettes each color is written to index 0, 7, 9, 78, 79, 80, 81, 106 and 107 of the Game palette respectively.

### Game
![](images/dos_pal_game.png)

### Menu
![](images/dos_pal_menu.png)

### Pitch (Frozen)
![](images/dos_pal_frozen.png)

### Pitch (Muddy)
![](images/dos_pal_muddy.png)

### Pitch (Wet)
![](images/dos_pal_wet.png)

### Pitch (Soft)
![](images/dos_pal_soft.png)

### Pitch (Normal)
![](images/dos_pal_normal.png)

### Pitch (Dry)
![](images/dos_pal_dry.png)

### Pitch (Hard)
![](images/dos_pal_hard.png)


---

## Common options

```text
-amiga                    Amiga mode (default)
-dos                      DOS mode
-amiga-dir=<path>         Amiga directory (default: ./Amiga)
-dos-dir=<path>           DOS directory (default: ./DOS)

# Amiga input types
-map | -tmx | -bmp | -raw Input type (Amiga only)

-output=bmp|tmx|map|raw   Output type

-palette=<name>           Palette (Amiga): Soft, Muddy, Frozen, Dry, Normal, Hard, Wet
-bitplanes=N              Bitplanes 1-8 (default 4; 4=16 colors, 8=256)
-no-rnc                   Disable RNC compression for Amiga MAP/RAW outputs

# DOS pitch options
-pitch=N                  DOS pitch index (0..MaxPitch-1), default 0
-type=name                DOS pitch type: frozen, muddy, wet, soft, normal, dry, hard
-colors=N                 DOS: remap to N colors (16-256)

# DOS mode switches
-picture                  DOS: operate on a .256 picture file
-sprites-export           DOS: export all sprites to sprNNNN.bmp files
-sprites-import           DOS: import sprNNNN.bmp files into DAT/SPRITE.DAT
-sprite-bg=N              DOS sprites: menu palette index for background color (in BMP slot 16)

# General
-h, -?                    Show help
```

---

## Bitplanes (optional, default 4)

```text
4 = 16 colors
5 = 32 colors
6 = 64 colors
7 = 128 colors
8 = 256 colors
```

---

## Raw formats

```text
320x256 pixels, planar, left-to-right, top-to-bottom
352x272 pixels, planar, left-to-right, top-to-bottom
```

---

## Palette export

*(no input/output files; writes multiple files to the current directory)*

```bash
SwosGfx -palettes
    [-pal-color=amiga12|rgb32]                 # default: amiga12
    [-pal-file=asm|c|palette]                  # default: asm
    [-pal-count=16|128|256]                    # default: 16
    [-pal-full]                                # default: only pitch-affected colors for pitches
    [-pal-format=act|mspal|jasc|gimp|paintnet] # for -pal-file=palette, default: act
    [outDir]                                   # optional output directory
```

---

## Examples

```bash
# Render all DOS pitches as BMP, NORMAL type, remapped to 128 colors, output to ./dos-pitches
SwosGfx -dos -dos-dir=DOS -output=bmp -type=normal -colors=128 dos-pitches

# Convert a DOS pitch BMP into an Amiga pitch MAP with 7 bitplanes (AGA, 128 colors)
SwosGfx -amiga -bmp -output=map -bitplanes=7 dos-pitches/pitch1-normal.bmp SWCPICH1.MAP

# Convert the Amiga pitch MAP back to an AGA BMP using the selected palette
SwosGfx -amiga -map -output=bmp -palette=Normal -bitplanes=7 SWCPICH1.MAP pitch1-amiga-aga.bmp

# Export all palettes (Menu + Game + all pitch types) as 256-color ACT files into the Palettes directory
SwosGfx -palettes -pal-color=rgb32 -pal-file=palette -pal-count=256 -pal-full -pal-format=act Palettes
```

### More Amiga examples

```bash
# Convert an Amiga full-pitch BMP (16 colors) to .MAP using 4 bitplanes
SwosGfx -amiga -bmp -output=map -bitplanes=4 fullPitch.bmp SWCPICH1.MAP

# Convert a RAW 320x256 planar dump to BMP using the "Normal" pitch palette (16 colors)
SwosGfx -amiga -raw -output=bmp -palette=Normal -bitplanes=4 pitch.raw pitch-normal.bmp

# Convert a Tiled .tmx back to Amiga .MAP (7 bitplanes for AGA)
SwosGfx -amiga -tmx -output=map -bitplanes=7 pitch1-aga.tmx SWCPICH1.MAP
```

### More DOS examples

```bash
# Convert a DOS .256 picture to BMP
SwosGfx -dos -picture -output=bmp TITLE.256 title-screen.bmp

# Export all DOS sprites as BMPs using menu color index 0 as the background
SwosGfx -dos -sprites-export -dos-dir=DOS -sprite-bg=0 sprites-out

# Import edited sprNNNN.bmp files back into DAT/SPRITE.DAT
SwosGfx -dos -sprites-import sprites-out
```

### DOS TMX examples

```bash
# Render all DOS pitches, SOFT type, directly to Tiled .tmx files
SwosGfx -dos -dos-dir=DOS -output=tmx -type=soft -colors=128 pitches-soft-tmx

# Render all DOS pitches, DRY type, full 256 colors, directly to Tiled .tmx files
SwosGfx -dos -dos-dir=DOS -output=tmx -type=dry -colors=256 pitches-dry-256-tmx
```

### Amiga TMX examples

```bash
# Convert an Amiga .MAP pitch (AGA, 7 bitplanes) to a Tiled .tmx map
SwosGfx -amiga -map -output=tmx -palette=Normal -bitplanes=7 SWCPICH1.MAP pitch1-normal-aga.tmx

# Same, but OCS/ECS 16-color pitch using 4 bitplanes
SwosGfx -amiga -map -output=tmx -palette=Soft -bitplanes=4 SWCPICH2.MAP pitch2-soft-ecs.tmx

# Take a TMX edited in Tiled and convert it back into an Amiga .MAP (AGA)
SwosGfx -amiga -tmx -output=map -bitplanes=7 pitch1-normal-aga-edited.tmx SWCPICH1.MAP
```

---

## Credits

- [benbaker76](https://github.com/benbaker76/) - for writing the software, updating and maintaining it
- [starwindz](https://github.com/starwindz/) - author of [bmp-to-raw-for-amiga-swos](https://github.com/starwindz/bmp-to-raw-for-amiga-swos)
- [zlatkok](https://github.com/zlatkok) - author of [swpe](https://github.com/zlatkok/swos-port/tree/master/tools/swpe)
