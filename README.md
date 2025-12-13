# SwosGfx
Tool for generating or extracting graphics data and palettes for SWOS

![](images/tiled.png)

### Amiga usage

```bash
SwosGfx -amiga -map    -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.map out.bmp
SwosGfx -amiga -map    -output=tmx  -palette=<name>   [-bitplanes=N] [-no-rnc] in.map out.tmx
SwosGfx -amiga -tmx    -output=map  [-bitplanes=N]    [-no-rnc]      in.tmx out.map
SwosGfx -amiga -bmp    -output=map  [-bitplanes=N]    [-no-rnc]      fullPitch.bmp out.map
SwosGfx -amiga -bmp    -output=raw  -raw320 -palette=<name> [-bitplanes=N] [-no-rnc] in.bmp out.raw
SwosGfx -amiga -bmp    -output=raw  -raw352 -palette=<name> [-bitplanes=N] [-no-rnc] in.bmp out.raw
SwosGfx -amiga -raw320 -output=bmp  -palette=<name> [-bitplanes=N] [-no-rnc] in.raw out.bmp
SwosGfx -amiga -raw352 -output=bmp  -palette=<name> [-bitplanes=N] [-no-rnc] in.raw out.bmp
```

---

### DOS pitch usage (patterns from `./DOS`)

```bash
SwosGfx -dos -output=bmp -pitch=N -type=normal [-colors=N] out.bmp
SwosGfx -dos -output=tmx -pitch=N -type=normal [-colors=N] out.tmx
# pitch defaults: -pitch=0 -type=normal
```

---

### DOS picture usage (`.256` → BMP)

```bash
SwosGfx -dos -picture -output=bmp in.256 out.bmp
```

---

### DOS sprite usage (SPRITE.DAT + *.DAT from `./DOS`)

**Export all sprites to BMP:**

```bash
SwosGfx -dos -sprites-export [-sprite-bg=N] [-output=bmp] outDir
# writes sprNNNN.bmp, 0 <= N < 1334
```

**Import sprites from BMP and update DAT/SPRITE.DAT:**

```bash
SwosGfx -dos -sprites-import spritesDir
# reads sprNNNN.bmp, inserts and saves changes
```

---

### Common options

```text
-amiga                    Amiga mode (default)
-dos                      DOS mode
-dos-dir=<path>           DOS directory (default: ./DOS)

-map | -tmx | -bmp        Input type (Amiga only)
-raw320 | -raw352         RAW input or RAW format (Amiga only)
-output=bmp|tmx|map|raw   Output type

-palette=<name>           Palette (Amiga): Soft, Muddy, Frozen, Dry, Normal, Hard, Wet
-bitplanes=N              Bitplanes 1-8 (default 4; 4=16 colors, 8=256)
-no-rnc                   Disable RNC compression for Amiga MAP/RAW outputs

-pitch=N                  DOS pitch index (0..MaxPitch-1), default 0
-type=name                DOS pitch type: frozen, muddy, wet, soft, normal, dry, hard
-colors=N                 DOS: remap to N colors

-picture                  DOS: operate on a .256 picture file
-sprites-export           DOS: export all sprites to sprNNNN.bmp files
-sprites-import           DOS: import sprNNNN.bmp files into DAT/SPRITE.DAT
-sprite-bg=N              DOS sprites: menu palette index for background color (in BMP slot 16)

-h, -?                    Show help
```

---

### Bitplanes (optional, default 4)

```text
4 = 16 colors
5 = 32 colors
6 = 64 colors
7 = 128 colors
8 = 256 colors
```

---

### Palette export

*(no input/output files, writes multiple files in the current directory)*

```bash
SwosGfx -palettes
    [-pal-color=amiga12|rgb32]                # default: amiga12
    [-pal-file=asm|c|palette]                 # default: asm
    [-pal-count=16|128|256]                   # default: 16
    [-pal-full]                               # default: only pitch-affected colors for pitches
    [-pal-format=act|mspal|jasc|gimp|paintnet]  # for -pal-file=palette, default: act
```

### Examples

```bash
# Render DOS pitch 0 (first pitch), NORMAL type, remapped to 128 colors
# Uses DOS patterns + DOS game palette from the ./DOS directory
SwosGfx -dos -dos-dir=DOS -output=bmp -pitch=0 -type=normal -colors=128 dos-pitch1-normal.bmp

# Convert that DOS BMP into an Amiga pitch MAP with 7 bitplanes (AGA, 128 colors)
SwosGfx -amiga -bmp -output=map -bitplanes=7 dos-pitch1-normal.bmp SWCPICH1.MAP

# Convert the Amiga pitch MAP back to an AGA BMP using the selected palette
SwosGfx -amiga -map -output=bmp -bitplanes=7 SWCPICH1.MAP pitch1-amiga-aga.bmp

# Export all palettes (Menu + Game + all pitch types) as 256-color ACT files
SwosGfx -palettes -pal-color=rgb32 -pal-file=palette -pal-count=256 -pal-full -pal-format=act
````

#### More Amiga examples

```bash
# Convert an Amiga full-pitch BMP (16 colors) to .MAP using 4 bitplanes
SwosGfx -amiga -bmp -output=map -bitplanes=4 fullPitch.bmp SWCPICH1.MAP

# Convert a RAW 320x256 planar dump to BMP using the "Normal" pitch palette (16 colors)
SwosGfx -amiga -raw320 -output=bmp -palette=Normal -bitplanes=4 pitch.raw pitch-normal.bmp

# Convert a Tiled .tmx back to Amiga .MAP (7 bitplanes for AGA)
SwosGfx -amiga -tmx -output=map -bitplanes=7 pitch1-aga.tmx SWCPICH1.MAP
```

#### More DOS examples

```bash
# Convert a DOS .256 picture to BMP
SwosGfx -dos -picture -output=bmp TITLE.256 title-screen.bmp

# Export all DOS sprites as BMPs using menu color index 0 as the background
SwosGfx -dos -sprites-export -dos-dir=DOS -sprite-bg=0 sprites-out

# Import edited sprNNNN.bmp files back into DAT/SPRITE.DAT
SwosGfx -dos -sprites-import sprites-out
```

#### Palette export variants

```bash
# Export 16-color Amiga-style palettes (Menu + Game + pitch variants) as .s (ASM) files
SwosGfx -palettes -pal-color=amiga12 -pal-file=asm -pal-count=16

# Export 256-color DOS palettes as GIMP .gpl files
SwosGfx -palettes -pal-color=rgb32 -pal-file=palette -pal-count=256 -pal-full -pal-format=gimp
```

Nice, TMX buddies coming right up 😄
Here are some extra examples you can drop into your **Examples** section, focused specifically on **DOS → TMX** and **Amiga →/from TMX**.

#### DOS ↔ TMX examples

```bash
# Render DOS pitch 3, SOFT type, directly to a Tiled .tmx map
# Uses DOS pitch patterns + DOS palette from ./DOS
SwosGfx -dos -dos-dir=DOS -output=tmx -pitch=3 -type=soft -colors=128 pitch3-soft.tmx

# Render DOS pitch 5, DRY type, full 256 colors into TMX
SwosGfx -dos -dos-dir=DOS -output=tmx -pitch=5 -type=dry -colors=256 pitch5-dry-256.tmx
````
#### Amiga ↔ TMX examples

```bash
# Convert an Amiga .MAP pitch (AGA, 7 bitplanes) to a Tiled .tmx map
# Uses a named pitch palette (Normal)
SwosGfx -amiga -map -output=tmx -palette=Normal -bitplanes=7 SWCPICH1.MAP pitch1-normal-aga.tmx

# Same, but OCS/ECS 16-color pitch using 4 bitplanes
SwosGfx -amiga -map -output=tmx -palette=Soft -bitplanes=4 SWCPICH2.MAP pitch2-soft-ecs.tmx

# Take a TMX edited in Tiled and convert it back into an Amiga .MAP (AGA)
SwosGfx -amiga -tmx -output=map -bitplanes=7 pitch1-normal-aga-edited.tmx SWCPICH1.MAP

# DOS → BMP → Amiga TMX roundtrip:
# 1) Generate DOS pitch BMP
SwosGfx -dos -dos-dir=DOS -output=bmp -pitch=2 -type=wet -colors=128 dos-pitch3-wet.bmp

# 2) Convert that BMP to Amiga .MAP (AGA, 7 bitplanes)
SwosGfx -amiga -bmp -output=map -bitplanes=7 dos-pitch3-wet.bmp SWCPICH3.MAP

# 3) Export Amiga .MAP to TMX for editing in Tiled
SwosGfx -amiga -map -output=tmx -palette=Wet -bitplanes=7 SWCPICH3.MAP pitch3-wet-aga.tmx
```

## Credits

- [benbaker76](https://github.com/benbaker76/) - for writing the software, updating and maintaining it
- [starwindz](https://github.com/starwindz/) - author of [bmp-to-raw-for-amiga-swos](https://github.com/starwindz/bmp-to-raw-for-amiga-swos)
- [zlatkok](https://github.com/zlatkok) - author of [swpe](https://github.com/zlatkok/swos-port/tree/master/tools/swpe)
