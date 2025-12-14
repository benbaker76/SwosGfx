using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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

    public sealed class DosPitch
    {
        public const int Width = 672;  // PITCH_W
        public const int Height = 880; // PITCH_H

        public const int PatternSize = 16;
        public const int Columns = 42; // 672 / 16
        public const int Rows = 55;    // 880 / 16

        // Tileset layout for TMX export: 16 columns of 16x16 tiles
        private const int TilesetColumns = 16;

        private readonly IDosPitchPatternSource _patterns;

        public DosPitch(IDosPitchPatternSource patterns)
        {
            _patterns = patterns ?? throw new ArgumentNullException(nameof(patterns));
        }

        /// <summary>
        /// Build the full pitch image as palette indices (top-down int[,] [y,x]) + palette (ARGB uint[]).
        /// </summary>
        public (byte[,] Indices, uint[] Palette) BuildPitchImage(int pitchIndex, DosPitchType pitchType)
        {
            if (pitchIndex < 0 || pitchIndex >= _patterns.MaxPitch)
                throw new ArgumentOutOfRangeException(nameof(pitchIndex));

            uint[] palette = BuildPitchPalette(pitchType);

            // Top-down indices: [y,x]
            var indices = new byte[Height, Width];

            int pointerCount = _patterns.GetPointerCount(pitchIndex);

            for (int tileY = 0; tileY < Rows; tileY++)
            {
                for (int tileX = 0; tileX < Columns; tileX++)
                {
                    int pointerIndex = Columns * tileY + tileX;

                    // Guard against short pointer tables
                    if (pointerIndex >= pointerCount)
                        continue;

                    int patternIndex = _patterns.GetPatternIndexForPointer(pitchIndex, pointerIndex);
                    ReadOnlySpan<byte> pattern = _patterns.GetPatternBytes(pitchIndex, patternIndex);

                    BlitPattern(pattern, indices, tileX * PatternSize, tileY * PatternSize);
                }
            }

            return (indices, palette);
        }

        public void SavePitchAsBmp(int pitchIndex, DosPitchType pitchType, string path, int colorCount = 256)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));

            var (indices, palette) = BuildPitchImage(pitchIndex, pitchType);
            Write8bppIndexedBmp(path, Width, Height, indices, palette, colorCount);
        }

        public void SavePitchAsTmx(int pitchIndex, DosPitchType pitchType, string path, int colorCount = 256)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            if (pitchIndex < 0 || pitchIndex >= _patterns.MaxPitch)
                throw new ArgumentOutOfRangeException(nameof(pitchIndex));

            string fullTmxPath = Path.GetFullPath(path);
            string tmxDir = Path.GetDirectoryName(fullTmxPath) ?? ".";
            string baseName = Path.GetFileNameWithoutExtension(fullTmxPath);
            string tilesBmpPath = Path.Combine(tmxDir, baseName + ".bmp");

            uint[] palette = BuildPitchPalette(pitchType);
            int pointerCount = _patterns.GetPointerCount(pitchIndex);

            // patternIndex -> tileId (0-based)
            var patternToTileId = new Dictionary<int, int>();
            var tilePixels = new List<byte[]>(); // each is 16*16 bytes (indices)

            // GID map for TMX (1-based tile IDs; 0 = empty)
            int[,] gidMap = new int[Rows, Columns];

            for (int tileY = 0; tileY < Rows; tileY++)
            {
                for (int tileX = 0; tileX < Columns; tileX++)
                {
                    int pointerIndex = Columns * tileY + tileX;

                    if (pointerIndex >= pointerCount)
                    {
                        gidMap[tileY, tileX] = 0;
                        continue;
                    }

                    int patternIndex = _patterns.GetPatternIndexForPointer(pitchIndex, pointerIndex);

                    if (!patternToTileId.TryGetValue(patternIndex, out int tileId))
                    {
                        ReadOnlySpan<byte> pattern = _patterns.GetPatternBytes(pitchIndex, patternIndex);
                        if (pattern.Length != PatternSize * PatternSize)
                            throw new InvalidOperationException("Pattern must be 16x16 (256 bytes).");

                        var tileData = new byte[PatternSize * PatternSize];
                        pattern.CopyTo(tileData);

                        tileId = tilePixels.Count;
                        tilePixels.Add(tileData);
                        patternToTileId[patternIndex] = tileId;
                    }

                    gidMap[tileY, tileX] = tileId + 1;
                }
            }

            int tileCount = tilePixels.Count;
            int tilesetRows = (tileCount + TilesetColumns - 1) / TilesetColumns;

            int tilesetWidth = TilesetColumns * PatternSize;
            int tilesetHeight = tilesetRows * PatternSize;

            // Top-down tileset indices: [y,x]
            var tilesetIndices = new byte[tilesetHeight, tilesetWidth];

            for (int tileId = 0; tileId < tileCount; tileId++)
            {
                byte[] tileData = tilePixels[tileId];
                int col = tileId % TilesetColumns;
                int row = tileId / TilesetColumns;

                int dstX0 = col * PatternSize;
                int dstY0 = row * PatternSize;

                for (int y = 0; y < PatternSize; y++)
                {
                    for (int x = 0; x < PatternSize; x++)
                    {
                        int srcIndex = y * PatternSize + x;
                        tilesetIndices[dstY0 + y, dstX0 + x] = tileData[srcIndex];
                    }
                }
            }

            // Write tileset BMP
            Write8bppIndexedBmp(tilesBmpPath, tilesetWidth, tilesetHeight, tilesetIndices, palette, colorCount);

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
                    if (!lastCell) writer.Write(",");
                    if (x == Columns - 1) writer.WriteLine();
                }
            }

            writer.WriteLine("  </data>");
            writer.WriteLine(" </layer>");
            writer.WriteLine("</map>");
        }

        // ---------------------------------------------------------------------
        // Internal helpers
        // ---------------------------------------------------------------------

        private static void BlitPattern(ReadOnlySpan<byte> pattern, byte[,] destIndices, int destX, int destY)
        {
            if (pattern.Length != PatternSize * PatternSize)
                throw new ArgumentException("Pattern must be 16x16 (256 bytes).", nameof(pattern));

            for (int y = 0; y < PatternSize; y++)
            {
                int srcOffset = y * PatternSize;
                int dy = destY + y;

                for (int x = 0; x < PatternSize; x++)
                {
                    int dx = destX + x;
                    destIndices[dy, dx] = pattern[srcOffset + x];
                }
            }
        }

        /// <summary>
        /// Write an 8bpp indexed BMP from top-down indices [y,x] and an ARGB palette (uint[]).
        /// </summary>
        private static void Write8bppIndexedBmp(
            string path,
            int width,
            int height,
            byte[,] indices,
            uint[] palette,
            int colorCount)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            if (indices == null) throw new ArgumentNullException(nameof(indices));
            if (palette == null) throw new ArgumentNullException(nameof(palette));
            if (indices.GetLength(0) != height || indices.GetLength(1) != width)
                throw new ArgumentException("indices dimensions do not match width/height.", nameof(indices));
            if (palette.Length < 256)
                throw new ArgumentException("Palette must have at least 256 entries (ARGB).", nameof(palette));

            AmigaPalette.QuantizeBmp(ref indices, ref palette, colorCount);

            // BMP rows are padded to 4 bytes.
            int stride = (width + 3) & ~3;
            int pixelBytes = stride * height;

            // Build bottom-up byte buffer.
            var pixelData = new byte[pixelBytes];

            for (int y = 0; y < height; y++)
            {
                int srcY = height - 1 - y;       // bottom-up
                int dstRow = y * stride;

                for (int x = 0; x < width; x++)
                {
                    int idx = indices[srcY, x];
                    if ((uint)idx > 255u)
                        throw new InvalidDataException($"Pixel index {idx} at ({x},{srcY}) is not in 0..255.");

                    pixelData[dstRow + x] = (byte)idx;
                }

                // padding bytes already zero
            }

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".");

            using var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None);
            using var bw = new BinaryWriter(fs);

            // --- BITMAPFILEHEADER (14 bytes) ---
            bw.Write((ushort)0x4D42); // 'BM'

            int headerSize = 14 + 40 + 256 * 4;
            int fileSize = headerSize + pixelBytes;

            bw.Write(fileSize);
            bw.Write((ushort)0);
            bw.Write((ushort)0);
            bw.Write(headerSize);

            // --- BITMAPINFOHEADER (40 bytes) ---
            bw.Write(40);                 // biSize
            bw.Write(width);              // biWidth
            bw.Write(height);             // biHeight (positive -> bottom-up)
            bw.Write((ushort)1);          // biPlanes
            bw.Write((ushort)8);          // biBitCount
            bw.Write(0);                  // biCompression = BI_RGB
            bw.Write(pixelBytes);         // biSizeImage (including padding)
            bw.Write(0);                  // biXPelsPerMeter
            bw.Write(0);                  // biYPelsPerMeter
            bw.Write(256);                // biClrUsed (keep 256-entry table)
            bw.Write(0);                  // biClrImportant

            // --- Color table: 256 x (B,G,R,0) ---
            const uint FallbackMagenta = 0xFFFF00FF;

            for (int i = 0; i < 256; i++)
            {
                uint argb = (i < palette.Length) ? palette[i] : FallbackMagenta;
                AmigaPalette.UnpackRgb(argb, out byte r, out byte g, out byte b);

                bw.Write(b);
                bw.Write(g);
                bw.Write(r);
                bw.Write((byte)0);
            }

            // --- Pixel data ---
            bw.Write(pixelData);
        }

        // === Pitch palette handling =========================================

        private static readonly uint[,] PatCols =
        {
            { 0x484830, 0x404830, 0x384830, 0x5E5E50, 0x4D4D42, 0x1F1F1A, 0x2D2D27, 0x443B2B, 0x504E45 }, // Frozen
            { 0x382800, 0x302800, 0x282800, 0x645D53, 0x583D00, 0x221600, 0x2F1D00, 0x2A1E04, 0x5C4E3D }, // Muddy
            { 0x184800, 0x384800, 0x184800, 0x5C6150, 0x425718, 0x1E2D00, 0x243700, 0x333E00, 0x5A5B4B }, // Wet
            { 0x183800, 0x383800, 0x183800, 0x5B614C, 0x2B4B12, 0x172300, 0x1E2D00, 0x2F2D00, 0x525240 }, // Soft
            { 0x384800, 0x304800, 0x484800, 0x5A604C, 0x405517, 0x1E2D00, 0x253700, 0x3E4001, 0x605C44 }, // Normal
            { 0x484800, 0x404800, 0x384800, 0x595C4B, 0x435918, 0x1E2D00, 0x243700, 0x433C00, 0x5E5245 }, // Dry
            { 0x483800, 0x403800, 0x383800, 0x645D4A, 0x5A4500, 0x271E00, 0x342700, 0x283200, 0x584D38 }  // Hard
        };

        public static readonly byte[] PaletteIndicesToChange = { 0, 7, 9, 78, 79, 80, 81, 106, 107 };

        /// <summary>
        /// Build a pitch palette for the given pitch type, starting from DosPalette.Game (ARGB uint[256]).
        /// </summary>
        public static uint[] BuildPitchPalette(DosPitchType pitchType)
        {
            var basePal = DosPalette.Game;
            if (basePal == null || basePal.Length != 256)
                throw new InvalidOperationException("DosPalette.Game must contain 256 entries.");

            var palette = new uint[256];
            Array.Copy(basePal, palette, 256);

            int typeIndex = (int)pitchType;
            if (typeIndex < 0 || typeIndex >= PatCols.GetLength(0))
                throw new ArgumentOutOfRangeException(nameof(pitchType));

            for (int i = 0; i < PaletteIndicesToChange.Length; i++)
            {
                int palIndex = PaletteIndicesToChange[i];
                uint packed = PatCols[typeIndex, i]; // 0xRRGGBB

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

                palette[palIndex] = AmigaPalette.PackArgb(0xFF, (byte)r, (byte)g, (byte)b);
            }

            return palette;
        }
    }
}
