using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Text;
using static RncProPack.RncProcessor;

namespace SwosGfx
{
    /// <summary>
    /// Types of pitch in SWOS - controls palette tweaks.
    /// Must match original enum order: FROZEN, MUDDY, WET, SOFT, NORMAL, DRY, HARD.
    /// </summary>
    public enum DosPitchType
    {
        Frozen = 0,
        Muddy = 1,
        Wet = 2,
        Soft = 3,
        Normal = 4,
        Dry = 5,
        Hard = 6
    }

    /// <summary>
    /// Minimal abstraction for accessing pitch pattern data.
    /// Have your DosPitchPattern class implement this.
    /// </summary>
    public interface IDosPitchPatternSource
    {
        /// <summary>Maximum number of pitches (usually 6).</summary>
        int MaxPitch { get; }

        /// <summary>Number of pointer entries for the given pitch (num_ptrs in C).</summary>
        int GetPointerCount(int pitchIndex);

        /// <summary>
        /// Return the pattern index (0..maxpatterns-1) for the given pointer entry.
        /// This corresponds to (ptrs[pointerIndex] - data) / 256 in the original C.
        /// </summary>
        int GetPatternIndexForPointer(int pitchIndex, int pointerIndex);

        /// <summary>
        /// Returns a span over the 16x16 (256-byte) pattern with given pattern index.
        /// </summary>
        ReadOnlySpan<byte> GetPatternBytes(int pitchIndex, int patternIndex);
    }

    /// <summary>
    /// Offline pitch renderer for the DOS version.
    /// - Composes a full 672x848 8bpp pitch from pattern data.
    /// - Builds a pitch-type-specific palette from DosPalette.Game.
    /// - Saves as 8bpp indexed BMP.
    /// - Can optionally remap to N colors.
    /// - Can export pitch as Tiled TMX + tileset BMP (16 columns of 16x16 tiles).
    /// </summary>
    public sealed class DosPitch
    {
        public const int Width = 672;  // PITCH_W
        public const int Height = 880; // PITCH_H

        public const int PatternSize = 16;
        public const int Columns = 42; // 672 / 16
        public const int Rows = 55;    // 880 / 16

        // Tileset layout for TMX export: 16 columns of 16x16 tiles (matches Amiga tool).
        private const int TilesetColumns = 16;

        private readonly IDosPitchPatternSource _patterns;

        public DosPitch(IDosPitchPatternSource patterns)
        {
            _patterns = patterns ?? throw new ArgumentNullException(nameof(patterns));
        }

        /// <summary>
        /// Build the full pitch image (672x848) as 8bpp indexed pixels + palette.
        ///
        /// If use128Colors is true:
        ///   - Every pixel index is masked with 0x7F (so 128..255 map to 0..127).
        ///   - Palette entries 128..255 are forced to black.
        /// </summary>
        public (byte[] Pixels, Color[] Palette) BuildPitchImage(
            int pitchIndex,
            DosPitchType pitchType)
        {
            if (pitchIndex < 0 || pitchIndex >= _patterns.MaxPitch)
                throw new ArgumentOutOfRangeException(nameof(pitchIndex));

            // Build palette based on DosGamePal + pat_cols tweaks.
            var palette = BuildPitchPalette(pitchType);

            // Compose the full pitch from 16x16 tiles.
            var pixels = new byte[Width * Height];

            int pointerCount = _patterns.GetPointerCount(pitchIndex);

            for (int tileY = 0; tileY < Rows; tileY++)
            {
                for (int tileX = 0; tileX < Columns; tileX++)
                {
                    int pointerIndex = Columns * tileY + tileX;

                    // Guard against short pointer tables in case of malformed data.
                    if (pointerIndex >= pointerCount)
                        continue;

                    int patternIndex = _patterns.GetPatternIndexForPointer(pitchIndex, pointerIndex);
                    var pattern = _patterns.GetPatternBytes(pitchIndex, patternIndex);

                    BlitPattern(pattern, pixels, tileX * PatternSize, tileY * PatternSize, Width);
                }
            }

            return (pixels, palette);
        }

        /// <summary>
        /// Save the pitch as an 8bpp indexed BMP file.
        /// Filename logic (pitchN-TYPE.bmp) can be done by the caller.
        ///
        /// If use128Colors is true:
        ///   - Indices are masked with 0x7F.
        ///   - Palette entries 128..255 are black.
        /// </summary>
        public void SavePitchAsBmp(
            int pitchIndex,
            DosPitchType pitchType,
            string path,
            int colorCount = 256)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));

            var (pixelsTopDown, palette) = BuildPitchImage(pitchIndex, pitchType);
            Write8bppIndexedBmp(path, Width, Height, pixelsTopDown, palette, colorCount);
        }

        /// <summary>
        /// Export the pitch as a Tiled TMX map + tileset BMP.
        ///
        /// - Tileset BMP is stored next to the TMX file, named &lt;baseName&gt;.bmp.
        /// - Tiles are arranged in 16 columns of 16x16 pixels.
        /// - Tileset is built from unique pattern indices actually used by the map.
        /// - TMX refers to tiles by GID (pattern-based), with GID=0 meaning empty.
        ///
        /// If use128Colors is true:
        ///   - Tile pixel indices are masked with 0x7F.
        ///   - Palette entries 128..255 are black.
        /// </summary>
        public void SavePitchAsTmx(
            int pitchIndex,
            DosPitchType pitchType,
            string path,
            int colorCount = 256)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            if (pitchIndex < 0 || pitchIndex >= _patterns.MaxPitch)
                throw new ArgumentOutOfRangeException(nameof(pitchIndex));

            string fullTmxPath = Path.GetFullPath(path);
            string tmxDir = Path.GetDirectoryName(fullTmxPath) ?? ".";
            string baseName = Path.GetFileNameWithoutExtension(fullTmxPath);
            string tilesBmpPath = Path.Combine(tmxDir, baseName + ".bmp");

            // Build pitch-type-specific palette
            var palette = BuildPitchPalette(pitchType);
            int pointerCount = _patterns.GetPointerCount(pitchIndex);

            // Map: patternIndex -> tileId (0-based)
            var patternToTileId = new System.Collections.Generic.Dictionary<int, int>();
            var tilePixels = new System.Collections.Generic.List<byte[]>(); // each is 16*16

            // GID map for TMX (1-based tile IDs; 0 = empty)
            int[,] gidMap = new int[Rows, Columns];

            for (int tileY = 0; tileY < Rows; tileY++)
            {
                for (int tileX = 0; tileX < Columns; tileX++)
                {
                    int pointerIndex = Columns * tileY + tileX;

                    if (pointerIndex >= pointerCount)
                    {
                        gidMap[tileY, tileX] = 0; // empty tile
                        continue;
                    }

                    int patternIndex = _patterns.GetPatternIndexForPointer(pitchIndex, pointerIndex);

                    if (!patternToTileId.TryGetValue(patternIndex, out int tileId))
                    {
                        var pattern = _patterns.GetPatternBytes(pitchIndex, patternIndex);
                        if (pattern.Length != PatternSize * PatternSize)
                            throw new InvalidOperationException("Pattern must be 16x16 (256 bytes).");

                        var tileData = new byte[PatternSize * PatternSize];

                        for (int i = 0; i < tileData.Length; i++)
                        {
                            byte idx = pattern[i];
                            tileData[i] = idx;
                        }

                        tileId = tilePixels.Count;
                        tilePixels.Add(tileData);
                        patternToTileId[patternIndex] = tileId;
                    }

                    gidMap[tileY, tileX] = tileId + 1; // TMX GID is 1-based
                }
            }

            int tileCount = tilePixels.Count;
            int tilesetRows = (tileCount + TilesetColumns - 1) / TilesetColumns;

            int tilesetWidth = TilesetColumns * PatternSize;
            int tilesetHeight = tilesetRows * PatternSize;

            var tilesetImagePixels = new byte[tilesetWidth * tilesetHeight]; // top-down

            // Blit each tile into tileset image
            for (int tileId = 0; tileId < tileCount; tileId++)
            {
                var tileData = tilePixels[tileId];
                int col = tileId % TilesetColumns;
                int row = tileId / TilesetColumns;

                int dstX0 = col * PatternSize;
                int dstY0 = row * PatternSize;

                for (int y = 0; y < PatternSize; y++)
                {
                    for (int x = 0; x < PatternSize; x++)
                    {
                        int srcIndex = y * PatternSize + x;
                        byte idx = tileData[srcIndex];

                        int dstX = dstX0 + x;
                        int dstY = dstY0 + y;
                        int dstIndex = dstY * tilesetWidth + dstX;

                        tilesetImagePixels[dstIndex] = idx;
                    }
                }
            }

            // Write tileset BMP
            Write8bppIndexedBmp(tilesBmpPath, tilesetWidth, tilesetHeight, tilesetImagePixels, palette, colorCount);

            // Write TMX
            var encoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);

            Directory.CreateDirectory(tmxDir);

            using var writer = new StreamWriter(fullTmxPath, false, encoding);

            writer.WriteLine("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
            writer.WriteLine("<map version=\"1.10\" tiledversion=\"1.10.2\" orientation=\"orthogonal\" renderorder=\"right-down\" " +
                             $"width=\"{Columns}\" height=\"{Rows}\" tilewidth=\"{PatternSize}\" tileheight=\"{PatternSize}\" infinite=\"0\" nextlayerid=\"2\" nextobjectid=\"1\">");
            writer.WriteLine($" <tileset firstgid=\"1\" name=\"tiles\" tilewidth=\"{PatternSize}\" tileheight=\"{PatternSize}\" tilecount=\"{tileCount}\" columns=\"{TilesetColumns}\">");
            writer.WriteLine($"  <image source=\"{Path.GetFileName(tilesBmpPath)}\" width=\"{tilesetWidth}\" height=\"{tilesetHeight}\"/>");
            writer.WriteLine(" </tileset>");
            writer.WriteLine($" <layer id=\"1\" name=\"Tile Layer 1\" width=\"{Columns}\" height=\"{Rows}\">");
            writer.WriteLine("  <data encoding=\"csv\">");

            for (int y = 0; y < Rows; y++)
            {
                for (int x = 0; x < Columns; x++)
                {
                    int gid = gidMap[y, x];
                    bool lastCell = (y == Rows - 1) && (x == Columns - 1);

                    writer.Write(gid);
                    if (!lastCell)
                        writer.Write(",");
                    if (x == Columns - 1)
                        writer.WriteLine();
                }
            }

            writer.WriteLine("  </data>");
            writer.WriteLine(" </layer>");
            writer.WriteLine("</map>");
        }

        // ---------------------------------------------------------------------
        // Internal helpers
        // ---------------------------------------------------------------------

        private static void BlitPattern(
            ReadOnlySpan<byte> pattern,
            byte[] dest,
            int destX,
            int destY,
            int destPitch)
        {
            if (pattern.Length != PatternSize * PatternSize)
                throw new ArgumentException("Pattern must be 16x16 (256 bytes).", nameof(pattern));

            int destBase = destY * destPitch + destX;

            for (int y = 0; y < PatternSize; y++)
            {
                int srcOffset = y * PatternSize;
                int dstOffset = destBase + y * destPitch;

                for (int x = 0; x < PatternSize; x++)
                {
                    byte idx = pattern[srcOffset + x];
                    dest[dstOffset + x] = idx;
                }
            }
        }

        public static double GetColorDistance(Color color1, Color color2)
        {
            double minDistance = double.MaxValue;

            CIELab labColor = Lab.RGBtoLab(color1.R, color1.G, color1.B);
            CIELab paletteLabColor = Lab.RGBtoLab(color2.R, color2.G, color2.B);

            double distance = Lab.GetDeltaE_CIEDE2000(labColor, paletteLabColor);

            if (distance == 0)
                return 0;

            if (distance < minDistance)
                minDistance = distance;

            return minDistance;
        }

        public static double GetNearestColor(Color color, Color[] palette, out int nearestIndex)
        {
            double minDistance = double.MaxValue;
            nearestIndex = -1;

            for (int i = 0; i < palette.Length; i++)
            {
                Color paletteColor = palette[i];
                
                double distance = GetColorDistance(color, paletteColor);

                if (distance < minDistance)
                {
                    minDistance = distance;
                    nearestIndex = i;
                }
            }

            return minDistance;
        }

        private static void QuantizeBmp(
            ref byte[] pixelsTopDown,
            ref Color[] palette,
            int colorCount)
        {
            if (palette == null)
                throw new ArgumentNullException(nameof(palette));
            if (pixelsTopDown == null)
                throw new ArgumentNullException(nameof(pixelsTopDown));
            if (colorCount <= 0 || colorCount > palette.Length)
                throw new ArgumentOutOfRangeException(nameof(colorCount),
                    $"colorCount must be between 1 and {palette.Length}, got {colorCount}.");

            // New palette = first N colors
            Color[] newPalette = palette.Take(colorCount).ToArray();

            var colorMap = new Dictionary<byte, byte>();

            // Build mapping from "old" indices -> nearest index in newPalette
            for (int i = colorCount; i < palette.Length; i++)
            {
                Color color = palette[i];

                double distance = GetNearestColor(color, newPalette, out int nearestIndex);

                if (nearestIndex < 0)
                    throw new InvalidOperationException(
                        $"Nearest color not found for palette index {i}.");

                colorMap[(byte)i] = (byte)nearestIndex;
            }

            // Remap pixels
            for (int i = 0; i < pixelsTopDown.Length; i++)
            {
                byte idx = pixelsTopDown[i];

                if (idx >= palette.Length)
                {
                    throw new InvalidDataException(
                        $"Pixel index {idx} at position {i} is outside palette length {palette.Length}.");
                }

                if (idx < colorCount)
                    continue;

                if (!colorMap.TryGetValue(idx, out byte mapped))
                {
                    throw new InvalidDataException(
                        $"No remap entry for palette index {idx}. colorCount={colorCount}, paletteLength={palette.Length}.");
                }

                pixelsTopDown[i] = mapped;
            }

            // Shrink palette
            palette = newPalette;
        }

        /// <summary>
        /// Write an 8bpp indexed BMP from top-down pixels and a 256-entry palette.
        /// Assumes the row width is already DWORD-aligned (which it is for pitch and tileset).
        /// </summary>
        private static void Write8bppIndexedBmp(
            string path,
            int width,
            int height,
            byte[] pixelsTopDown,
            Color[] palette,
            int colorCount)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            if (pixelsTopDown == null) throw new ArgumentNullException(nameof(pixelsTopDown));
            if (palette == null) throw new ArgumentNullException(nameof(palette));
            if (palette.Length < 256) throw new ArgumentException("Palette must have at least 256 colors.", nameof(palette));
            if (pixelsTopDown.Length != width * height)
                throw new ArgumentException("pixelsTopDown length does not match width*height.", nameof(pixelsTopDown));

            QuantizeBmp(ref pixelsTopDown, ref palette, colorCount);

            // BMP is stored bottom-up; flip vertically.
            var flipped = new byte[pixelsTopDown.Length];
            for (int y = 0; y < height; y++)
            {
                int srcRow = (height - 1 - y) * width;
                int dstRow = y * width;
                Buffer.BlockCopy(pixelsTopDown, srcRow, flipped, dstRow, width);
            }

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".");

            using var fs = File.Create(path);
            using var bw = new BinaryWriter(fs);

            // --- BITMAPFILEHEADER (14 bytes) ---
            // WORD bfType = 'BM'
            bw.Write((ushort)0x4D42);

            int headerSize = 14 + 40 + 256 * 4; // file header + info header + palette
            int pixelBytes = width * height;    // 8bpp, width is multiple of 4
            int fileSize = headerSize + pixelBytes;

            // DWORD bfSize
            bw.Write(fileSize);
            // WORD bfReserved1, bfReserved2
            bw.Write((ushort)0);
            bw.Write((ushort)0);
            // DWORD bfOffBits
            bw.Write(headerSize);

            // --- BITMAPINFOHEADER (40 bytes) ---
            bw.Write(40);                 // biSize
            bw.Write(width);              // biWidth
            bw.Write(height);             // biHeight (positive -> bottom-up)
            bw.Write((ushort)1);          // biPlanes
            bw.Write((ushort)8);          // biBitCount (8bpp indexed)
            bw.Write(0);                  // biCompression = BI_RGB
            bw.Write(pixelBytes);         // biSizeImage
            bw.Write(0);                  // biXPelsPerMeter
            bw.Write(0);                  // biYPelsPerMeter
            bw.Write(0);                  // biClrUsed
            bw.Write(0);                  // biClrImportant

            // --- Color table (256 * 4 bytes, BGR0) ---
            for (int i = 0; i < 256; i++)
            {
                var c = i < palette.Length ? palette[i] : Color.Magenta;
                bw.Write(c.B);
                bw.Write(c.G);
                bw.Write(c.R);
                bw.Write((byte)0); // reserved
            }

            // --- Pixel data ---
            bw.Write(flipped);
        }

        // === Pitch palette handling (SetPitchPalette equivalent) =============

        // pat_cols[pitchType][COLOR_TABLE_SIZE]
        // 9 packed RGB values (0xRRGGBB) per pitch type.
        private static readonly uint[,] PatCols =
        {
            // Frozen
            {
                0x484830, 0x404830, 0x384830, 0x5E5E50, 0x4D4D42,
                0x1F1F1A, 0x2D2D27, 0x443B2B, 0x504E45
            },
            // Muddy
            {
                0x382800, 0x302800, 0x282800, 0x645D53, 0x583D00,
                0x221600, 0x2F1D00, 0x2A1E04, 0x5C4E3D
            },
            // Wet
            {
                0x184800, 0x384800, 0x184800, 0x5C6150, 0x425718,
                0x1E2D00, 0x243700, 0x333E00, 0x5A5B4B
            },
            // Soft
            {
                0x183800, 0x383800, 0x183800, 0x5B614C, 0x2B4B12,
                0x172300, 0x1E2D00, 0x2F2D00, 0x525240
            },
            // Normal
            {
                0x384800, 0x304800, 0x484800, 0x5A604C, 0x405517,
                0x1E2D00, 0x253700, 0x3E4001, 0x605C44
            },
            // Dry
            {
                0x484800, 0x404800, 0x384800, 0x595C4B, 0x435918,
                0x1E2D00, 0x243700, 0x433C00, 0x5E5245
            },
            // Hard
            {
                0x483800, 0x403800, 0x383800, 0x645D4A, 0x5A4500,
                0x271E00, 0x342700, 0x283200, 0x584D38
            }
        };

        // Indices of palette entries that change with pitch type.
        // Same as "where[]" table in original C.
        public static readonly byte[] PaletteIndicesToChange =
        {
            0, 7, 9, 78, 79, 80, 81, 106, 107
        };

        /// <summary>
        /// Build a pitch palette for given pitch type, starting from SwosPalettes.DosGamePal.
        /// </summary>
        public static Color[] BuildPitchPalette(
            DosPitchType pitchType)
        {
            // Start from base game palette (ARGB uints)
            var basePal = DosPalette.Game;
            if (basePal == null || basePal.Length != 256)
                throw new InvalidOperationException("SwosPalettes.DosGamePal must contain 256 entries.");

            var palette = new Color[256];
            for (int i = 0; i < 256; i++)
            {
                uint argb = basePal[i];
                byte a = (byte)((argb >> 24) & 0xFF);
                byte r = (byte)((argb >> 16) & 0xFF);
                byte g = (byte)((argb >> 8) & 0xFF);
                byte b = (byte)(argb & 0xFF);
                palette[i] = Color.FromArgb(a, r, g, b);
            }

            int typeIndex = (int)pitchType;
            if (typeIndex < 0 || typeIndex >= PatCols.GetLength(0))
                throw new ArgumentOutOfRangeException(nameof(pitchType));

            // Apply pat_cols tweaks to specific palette indices.
            for (int i = 0; i < PaletteIndicesToChange.Length; i++)
            {
                int palIndex = PaletteIndicesToChange[i];
                uint packed = PatCols[typeIndex, i]; // 0xRRGGBB in 0..100 range

                int r = (int)((packed >> 16) & 0xFF);
                int g = (int)((packed >> 8) & 0xFF);
                int b = (int)(packed & 0xFF);

                // Original DOS logic: (value & ~1) << 1
                r = (r & ~1) << 1;
                g = (g & ~1) << 1;
                b = (b & ~1) << 1;

                r = Math.Clamp(r, 0, 255);
                g = Math.Clamp(g, 0, 255);
                b = Math.Clamp(b, 0, 255);

                palette[palIndex] = Color.FromArgb(255, r, g, b);
            }

            return palette;
        }
    }
}
