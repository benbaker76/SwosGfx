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

        public static readonly ushort[] Menu12 =
        {
            0x0001, 0x0AAB, 0x0FFF, 0x0102, 0x0621, 0x0A40, 0x0F71, 0x0667,
            0x0204, 0x0445, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
        };

        public static readonly ushort[] Game12 =
        {
            0x0360, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0250,
            0x0030, 0x0370, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
        };

        public static readonly ushort[][] Pitches12 =
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

        public static readonly uint[] Menu =
        {
            0xFF000010, 0xFFA0A0B0, 0xFFF0F0F0, 0xFF100020, 0xFF602010, 0xFFA04000, 0xFFF07010, 0xFF606070,
            0xFF200040, 0xFF404050, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
        };

        public static readonly uint[] Game =
        {
            0xFF306000, 0xFF909090, 0xFFF0F0F0, 0xFF000000, 0xFF702010, 0xFFA04000, 0xFFF07010, 0xFF205000,
            0xFF003000, 0xFF307000, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
        };

        public static readonly uint[][] Pitches =
        {
            // Frozen
            new uint[]
            {
                0xFF909060, 0xFF909090, 0xFFF0F0F0, 0xFF000000, 0xFF702010, 0xFFA04000, 0xFFF07010, 0xFF809060,
                0xFF003000, 0xFF709060, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
            },
            // Muddy
            new uint[]
            {
                0xFF705000, 0xFF909090, 0xFFF0F0F0, 0xFF000000, 0xFF702010, 0xFFA04000, 0xFFF07010, 0xFF605000,
                0xFF003000, 0xFF505000, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
            },
            // Wet
            new uint[]
            {
                0xFF309000, 0xFF909090, 0xFFF0F0F0, 0xFF000000, 0xFF702010, 0xFFA04000, 0xFFF07010, 0xFF709000,
                0xFF003000, 0xFF309000, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
            },
            // Soft
            new uint[]
            {
                0xFF307000, 0xFF909090, 0xFFF0F0F0, 0xFF000000, 0xFF702010, 0xFFA04000, 0xFFF07010, 0xFF707000,
                0xFF003000, 0xFF307000, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
            },
            // Normal
            new uint[]
            {
                0xFF709000, 0xFF909090, 0xFFF0F0F0, 0xFF000000, 0xFF702010, 0xFFA04000, 0xFFF07010, 0xFF609000,
                0xFF003000, 0xFF909000, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
            },
            // Dry
            new uint[]
            {
                0xFF909000, 0xFF909090, 0xFFF0F0F0, 0xFF000000, 0xFF702010, 0xFFA04000, 0xFFF07010, 0xFF809000,
                0xFF003000, 0xFF709000, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
            },
            // Hard
            new uint[]
            {
                0xFF907000, 0xFF909090, 0xFFF0F0F0, 0xFF000000, 0xFF702010, 0xFFA04000, 0xFFF07010, 0xFF807000,
                0xFF003000, 0xFF707000, 0xFFF00000, 0xFF0000F0, 0xFF700020, 0xFF8080F0, 0xFF308000, 0xFFF0F000
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
            string prefix = "$")
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
                        sb.AppendLine(",");
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
            string prefix = "$")
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
                        sb.AppendLine(",");
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
            string commentPrefix = (fileFormat == FileFormat.Asm) ? ";" : "//";
            string hexPrefix = (fileFormat == FileFormat.Asm) ? "$" : "0x";

            sb.AppendLine($"{commentPrefix} {name}");
            sb.AppendLine(PaletteToHex(argbPalette, hexPrefix));

            return sb.ToString();
        }

        public static string PaletteToCode(
            string name,
            ushort[] amigaPalette,
            FileFormat fileFormat)
        {
            StringBuilder sb = new StringBuilder();
            string commentPrefix = (fileFormat == FileFormat.Asm) ? ";" : "//";
            string hexPrefix = (fileFormat == FileFormat.Asm) ? "$" : "0x";

            sb.AppendLine($"{commentPrefix} {name}");
            sb.AppendLine(PaletteToHex(amigaPalette, hexPrefix));

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
    }
}
