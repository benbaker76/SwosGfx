using System;
using System.Drawing;
using System.Net.NetworkInformation;
using System.Text;
using static System.Runtime.InteropServices.JavaScript.JSType;

namespace SwosGfx
{
    public static class AmigaPalette
    {
        public enum ColorFormat
        {
            Amiga12,
            RGB32
        };

        public enum ColorCount
        {
            Colors16 = 16,
            Colors128 = 128,
            Colors256 = 256
        };

        public enum FileFormat
        {
            Asm,
            C,
            Palette
        };

        public static string[] PitchPaletteNames = new string[]
        {
            "Frozen",
            "Muddy",
            "Wet",
            "Soft",
            "Normal",
            "Dry",
            "Hard"
        };

        public static readonly ushort[] Menu =
        {
            0x0001, 0x0AAB, 0x0FFF, 0x0102, 0x0621, 0x0A40, 0x0F71, 0x0667,
            0x0204, 0x0445, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
        };

        public static readonly ushort[] Game =
        {
            0x0360, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0250,
            0x0030, 0x0370, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
        };

        public static readonly ushort[] TitleLogo =
        {
            0x0002, 0x0A02, 0x0FB2, 0x0333, 0x043A, 0x0082, 0x0CCC, 0x0888,
            0x0555, 0x0FF9, 0x0DD8, 0x0BB7, 0x0996, 0x0774, 0x0553, 0x0332
        };

        public static readonly ushort[] TitleText =
        {
            0x0002, 0x0F04, 0x0F04, 0x0004, 0x0F04, 0x0F04, 0x0F04, 0x0F04,
            0x0006, 0x0FF9, 0x0DD8, 0x0BB7, 0x0996, 0x0774, 0x0553, 0x0332
        };

        public static readonly ushort[] TitleScreen =
        {
            0x0002 ,0x0002, 0x0222, 0x0444, 0x0777, 0x0999, 0x0CCC, 0x0EEE,
            0x003A, 0x008C, 0x0EC3, 0x0802, 0x0A02, 0x0D02, 0x0062, 0x0FFF
        };

        public static readonly ushort[] Panic =
{
            0x0000 ,0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
            0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00FF
        };

        public static readonly ushort[][] Pitches =
        {
            // Frozen
            new ushort[]
            {
                0x0996, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0896,
                0x0030, 0x0796, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
            },
            // Muddy
            new ushort[]
            {
                0x0750, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0650,
                0x0030, 0x0550, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
            },
            // Wet
            new ushort[]
            {
                0x0390, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0790,
                0x0030, 0x0390, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
            },
            // Soft
            new ushort[]
            {
                0x0370, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0770,
                0x0030, 0x0370, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
            },
            // Normal
            new ushort[]
            {
                0x0790, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0690,
                0x0030, 0x0990, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
            },
            // Dry
            new ushort[]
            {
                0x0990, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0890,
                0x0030, 0x0790, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
            },
            // Hard
            new ushort[]
            {
                0x0970, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0870,
                0x0030, 0x0770, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
            }
        };

        // 0xAARRGGBB -> Amiga 12-bit 0RGB
        public static ushort ToAmiga12(uint argb)
        {
            int r8 = (int)((argb >> 16) & 0xFF);
            int g8 = (int)((argb >> 8) & 0xFF);
            int b8 = (int)(argb & 0xFF);

            // Truncate to upper nibble
            int r4 = r8 >> 4;
            int g4 = g8 >> 4;
            int b4 = b8 >> 4;

            return (ushort)((r4 << 8) | (g4 << 4) | b4);
        }

        // Amiga 12-bit 0RGB -> 0xAARRGGBB (alpha forced to 0xFF)
        public static uint FromAmiga12(ushort amiga)
        {
            int r4 = (amiga >> 8) & 0xF;
            int g4 = (amiga >> 4) & 0xF;
            int b4 = (amiga >> 0) & 0xF;

            // Use << 4 to match your existing PC palettes / screenshots
            int r8 = r4 << 4;
            int g8 = g4 << 4;
            int b8 = b4 << 4;

            return 0xFF000000u
                 | (uint)(r8 << 16)
                 | (uint)(g8 << 8)
                 | (uint)b8;
        }

        // ARGB (0xAARRGGBB) -> Amiga 12-bit 0RGB
        public static ushort[] PaletteToAmiga12(uint[] argbPalette)
        {
            if (argbPalette == null)
                throw new ArgumentNullException(nameof(argbPalette));

            var result = new ushort[argbPalette.Length];

            for (int i = 0; i < argbPalette.Length; i++)
                result[i] = ToAmiga12(argbPalette[i]);

            return result;
        }

        // Amiga 12-bit 0RGB -> ARGB (0xAARRGGBB)
        public static uint[] PaletteFromAmiga12(ushort[] amigaPalette)
        {
            if (amigaPalette == null)
                throw new ArgumentNullException(nameof(amigaPalette));

            var result = new uint[amigaPalette.Length];

            for (int i = 0; i < amigaPalette.Length; i++)
                result[i] = FromAmiga12(amigaPalette[i]);

            return result;
        }

        // Amiga 12-bit palette -> comma-delimited hex, newline every 8 entries,
        // comma comes *before* the newline.
        public static string PaletteToHex(
            ushort[] amigaPalette,
            string prefix = "$",
            bool eolComma = true)
        {
            if (amigaPalette == null)
                throw new ArgumentNullException(nameof(amigaPalette));

            var sb = new StringBuilder();

            for (int i = 0; i < amigaPalette.Length; i++)
            {
                sb.Append(prefix);
                sb.Append(amigaPalette[i].ToString("X4"));

                bool isLast = (i == amigaPalette.Length - 1);

                if (!isLast)
                {
                    // End of an 8-entry block → comma then newline
                    if (i % 8 == 7)
                        sb.AppendLine(eolComma ? ",": "");
                    else
                        sb.Append(",");
                }
            }

            sb.AppendLine();

            return sb.ToString();
        }

        // ARGB palette -> comma-delimited hex, newline every 8 entries,
        // comma comes *before* the newline.
        public static string PaletteToHex(
            uint[] argbPalette,
            string prefix = "$",
            bool eolComma = true)
        {
            if (argbPalette == null)
                throw new ArgumentNullException(nameof(argbPalette));

            var sb = new StringBuilder();

            for (int i = 0; i < argbPalette.Length; i++)
            {
                sb.Append(prefix);
                sb.Append(argbPalette[i].ToString("X8"));

                bool isLast = (i == argbPalette.Length - 1);

                if (!isLast)
                {
                    if (i % 8 == 7)
                        sb.AppendLine(eolComma ? ",": "");
                    else
                        sb.Append(",");
                }
            }

            sb.AppendLine();

            return sb.ToString();
        }

        public static void PaletteToPalette(
            string name,
            uint[] argbPalette,
            PaletteFormat paletteFormat)
        {
            if (argbPalette == null)
                throw new ArgumentNullException(nameof(argbPalette));

            string fileName = "";

            switch(paletteFormat)
            {
                case PaletteFormat.Act:
                    fileName = $"{name}.act";
                    break;
                case PaletteFormat.MSPal:
                    fileName = $"{name}.pal";
                    break;
                case PaletteFormat.JASC:
                    fileName = $"{name}.pal";
                    break;
                case PaletteFormat.GIMP:
                    fileName = $"{name}.gpl";
                    break;
                case PaletteFormat.PaintNET:
                    fileName = $"{name}.txt";
                    break;
                default:
                    throw new ArgumentOutOfRangeException(nameof(paletteFormat), "Unsupported palette format.");
            }

            Color[] colors = new Color[argbPalette.Length];

            for (int i = 0; i < argbPalette.Length; i++)
                colors[i] = Color.FromArgb((int)argbPalette[i]);

            PalFile.WritePalette(fileName, colors, -1, paletteFormat);
        }

        public static string PaletteToCode(
            string name,
            uint[] argbPalette,
            FileFormat fileFormat)
        {
            StringBuilder sb = new StringBuilder();
            string cHeader = $"unsigned int {name}[] = {{";
            string cFooter = "};";
            string commentPrefix = (fileFormat == FileFormat.Asm) ? ";" : "//";
            string hexPrefix = (fileFormat == FileFormat.Asm) ? "$" : "0x";
            string linePrefix = (fileFormat == FileFormat.Asm) ? "    dc.w " : "";
            bool eolComma = (fileFormat != FileFormat.Asm);

            sb.AppendLine($"{commentPrefix} {name}");

            if (fileFormat == FileFormat.C)
                sb.AppendLine(cHeader);

            sb.AppendLine($"{linePrefix}{PaletteToHex(argbPalette, hexPrefix, eolComma)}");

            if (fileFormat == FileFormat.C)
                sb.AppendLine(cFooter);

            return sb.ToString();
        }

        public static string PaletteToCode(
            string name,
            ushort[] amigaPalette,
            FileFormat fileFormat)
        {
            StringBuilder sb = new StringBuilder();
            string cHeader = $"unsigned short {name}[] = {{";
            string cFooter = "};";
            string commentPrefix = (fileFormat == FileFormat.Asm) ? ";" : "//";
            string hexPrefix = (fileFormat == FileFormat.Asm) ? "$" : "0x";
            string linePrefix = (fileFormat == FileFormat.Asm) ? "dc.w " : "";
            bool eolComma = (fileFormat != FileFormat.Asm);

            sb.AppendLine($"{commentPrefix} {name}");

            if (fileFormat == FileFormat.C)
                sb.AppendLine(cHeader);

            sb.AppendLine($"    {linePrefix}{PaletteToHex(amigaPalette, hexPrefix, eolComma)}");

            if (fileFormat == FileFormat.C)
                sb.AppendLine(cFooter);

            return sb.ToString();
        }

        public static void OutputAllPalettes(ColorFormat colorFormat, FileFormat fileFormat, ColorCount colorCount, bool fullPalettes = false, PaletteFormat paletteFormat = PaletteFormat.Act)
        {
            string extension = (fileFormat == FileFormat.Asm) ? ".s" : ".c";

            if (colorFormat == ColorFormat.Amiga12)
            {
                if (colorCount == ColorCount.Colors16)
                {
                    File.WriteAllText($"Menu{extension}", PaletteToCode("Menu", AmigaPalette.Menu, fileFormat));
                    File.WriteAllText($"Game{extension}", PaletteToCode("Game", AmigaPalette.Game, fileFormat));

                    for (int i = 0; i < PitchPaletteNames.Length; i++)
                        File.WriteAllText($"{PitchPaletteNames[i]}{extension}", PaletteToCode(PitchPaletteNames[i], AmigaPalette.Pitches[i], fileFormat));
                }
                else
                {
                    File.WriteAllText($"Menu{extension}", PaletteToCode("Menu", PaletteToAmiga12(DosPalette.Menu.Take((int)colorCount).ToArray()), fileFormat));
                    File.WriteAllText($"Game{extension}", PaletteToCode("Game", PaletteToAmiga12(DosPalette.Game.Take((int)colorCount).ToArray()), fileFormat));

                    for (int i = 0; i < PitchPaletteNames.Length; i++)
                    {
                        if (fullPalettes)
                            File.WriteAllText($"{PitchPaletteNames[i]}{extension}", PaletteToCode(PitchPaletteNames[i], PaletteToAmiga12(DosPalette.Pitches[i].Take((int)colorCount).ToArray()), fileFormat));
                        else
                            File.WriteAllText($"{PitchPaletteNames[i]}{extension}", PaletteToCode(PitchPaletteNames[i], PaletteToAmiga12(DosPalette.Pitches[i].Skip(DosPitch.PaletteIndicesToChange[i]).Take(9).ToArray()), fileFormat));
                    }
                }
            }
            else // RGB32
            {
                if (fileFormat == FileFormat.Palette)
                {
                    PaletteToPalette("Menu", DosPalette.Menu.Take((int)colorCount).ToArray(), paletteFormat);
                    PaletteToPalette("Game", DosPalette.Game.Take((int)colorCount).ToArray(), paletteFormat);

                    for (int i = 0; i < PitchPaletteNames.Length; i++)
                    {
                        if (fullPalettes)
                            PaletteToPalette(PitchPaletteNames[i], DosPalette.Pitches[i].Take((int)colorCount).ToArray(), paletteFormat);
                        else
                            PaletteToPalette(PitchPaletteNames[i], DosPalette.Pitches[i].Skip(DosPitch.PaletteIndicesToChange[i]).Take(9).ToArray(), paletteFormat);
                    }
                }
                else
                {
                    File.WriteAllText($"Menu{extension}", PaletteToCode("Menu", DosPalette.Menu.Take((int)colorCount).ToArray(), fileFormat));
                    File.WriteAllText($"Game{extension}", PaletteToCode("Game", DosPalette.Game.Take((int)colorCount).ToArray(), fileFormat));

                    for (int i = 0; i < PitchPaletteNames.Length; i++)
                    {
                        if (fullPalettes)
                            File.WriteAllText($"{PitchPaletteNames[i]}{extension}", PaletteToCode(PitchPaletteNames[i], DosPalette.Pitches[i].Take((int)colorCount).ToArray(), fileFormat));
                        else
                            File.WriteAllText($"{PitchPaletteNames[i]}{extension}", PaletteToCode(PitchPaletteNames[i], DosPalette.Pitches[i].Skip(DosPitch.PaletteIndicesToChange[i]).Take(9).ToArray(), fileFormat));
                    }
                }
            }
        }

        public static void UnpackRgb(uint argb, out byte r, out byte g, out byte b)
        {
            r = (byte)((argb >> 16) & 0xFF);
            g = (byte)((argb >> 8) & 0xFF);
            b = (byte)(argb & 0xFF);
        }

        public static uint PackArgb(byte a, byte r, byte g, byte b)
        {
            return ((uint)a << 24) | ((uint)r << 16) | ((uint)g << 8) | b;
        }

        public static int BitplanesToColors(int bitplanes)
        {
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");
            return 1 << bitplanes;
        }

        public static double GetColorDistance(uint argb1, uint argb2)
        {
            UnpackRgb(argb1, out byte r1, out byte g1, out byte b1);
            UnpackRgb(argb2, out byte r2, out byte g2, out byte b2);

            CIELab lab1 = Lab.RGBtoLab(r1, g1, b1);
            CIELab lab2 = Lab.RGBtoLab(r2, g2, b2);

            double d = Lab.GetDeltaE_CIEDE2000(lab1, lab2);
            return d;
        }

        public static double GetNearestColor(uint argb, uint[] palette, out int nearestIndex)
        {
            double minDistance = double.MaxValue;
            nearestIndex = -1;

            for (int i = 0; i < palette.Length; i++)
            {
                double distance = GetColorDistance(argb, palette[i]);
                if (distance < minDistance)
                {
                    minDistance = distance;
                    nearestIndex = i;
                }
            }

            return minDistance;
        }

        public static void QuantizeBmp(ref byte[,] indices, ref uint[] palette, uint[] newPalette)
        {
            if (palette == null) throw new ArgumentNullException(nameof(palette));
            if (newPalette == null) throw new ArgumentNullException(nameof(newPalette));
            if (indices == null) throw new ArgumentNullException(nameof(indices));
            // Map old palette index -> nearest index in newPalette
            var colorMap = new Dictionary<int, int>(capacity: palette.Length);

            for (int i = 0; i < palette.Length; i++)
            {
                uint c = palette[i];
                _ = GetNearestColor(c, newPalette, out int nearestIndex);
                if (nearestIndex < 0)
                    throw new InvalidOperationException($"Nearest color not found for palette index {i}.");
                colorMap[i] = nearestIndex;
            }
            int h = indices.GetLength(0);
            int w = indices.GetLength(1);
            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    int idx = indices[y, x];
                    // Skip transparent
                    if (idx == 0)
                        continue;
                    if ((uint)idx >= (uint)palette.Length)
                        throw new InvalidDataException(
                            $"Pixel index {idx} at ({x},{y}) is outside palette length {palette.Length}.");
                    if (!colorMap.TryGetValue(idx, out int mapped))
                        throw new InvalidDataException(
                            $"No remap entry for palette index {idx}. oldPaletteLength={palette.Length}, newPaletteLength={newPalette.Length}.");
                    indices[y, x] = (byte)mapped;
                }
            }
            palette = newPalette;
        }

        public static void QuantizeBmp(ref byte[,] indices, ref uint[] palette, int colorCount)
        {
            if (palette == null) throw new ArgumentNullException(nameof(palette));
            if (indices == null) throw new ArgumentNullException(nameof(indices));
            if (colorCount <= 0 || colorCount > palette.Length)
                throw new ArgumentOutOfRangeException(nameof(colorCount),
                    $"colorCount must be between 1 and {palette.Length}, got {colorCount}.");

            uint[] newPalette = palette.Take(colorCount).ToArray();

            // Map old palette index -> nearest index in newPalette
            var colorMap = new Dictionary<int, int>(capacity: palette.Length - colorCount);

            for (int i = colorCount; i < palette.Length; i++)
            {
                uint c = palette[i];
                _ = GetNearestColor(c, newPalette, out int nearestIndex);
                if (nearestIndex < 0)
                    throw new InvalidOperationException($"Nearest color not found for palette index {i}.");

                colorMap[i] = nearestIndex;
            }

            int h = indices.GetLength(0);
            int w = indices.GetLength(1);

            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    int idx = indices[y, x];

                    // Skip transparent
                    if (idx == 0)
                        continue;

                    if ((uint)idx >= (uint)palette.Length)
                        throw new InvalidDataException(
                            $"Pixel index {idx} at ({x},{y}) is outside palette length {palette.Length}.");

                    if (idx < colorCount)
                        continue;

                    if (!colorMap.TryGetValue(idx, out int mapped))
                        throw new InvalidDataException(
                            $"No remap entry for palette index {idx}. colorCount={colorCount}, paletteLength={palette.Length}.");

                    indices[y, x] = (byte)mapped;
                }
            }

            palette = newPalette;
        }
    }
}
