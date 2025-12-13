using System;
using System.IO;

namespace SwosGfx
{
    /// <summary>
    /// DOS pitch pattern storage for all pitches (pitch1.dat/.blk .. pitch6.dat/.blk).
    /// Provides 8bpp 16x16 tiles and pointer tables for use by DosPitch.
    /// </summary>
    public sealed class DosPitchPattern : IDosPitchPatternSource
    {
        public const int MaxPitchCount = 6;   // MAX_PITCH
        public const int TileWidth = 16;
        public const int TileHeight = 16;
        public const int BytesPerTile = TileWidth * TileHeight; // 256

        /// <summary>
        /// Corresponds to S in the original C code: starting index for pitch cell pointers.
        /// </summary>
        public const int StartPitchPointerIndex = 42;

        /// <summary>
        /// SWOS maximum number of unique patterns per pitch (for FYI / validation).
        /// </summary>
        public const int SwosPatternsLimit = 296;

        /// <summary>
        /// Patterns + pointer table for a single pitch.
        /// </summary>
        public sealed class PitchPatterns
        {
            /// <summary>
            /// Pointer table as pattern indices (0..MaxPatterns-1).
            /// Length == number of pointer entries (num_ptrs in original C).
            /// </summary>
            public int[] PatternIndices { get; set; } = Array.Empty<int>();

            /// <summary>
            /// All patterns packed back-to-back as 8bpp tiles.
            /// Length must be MaxPatterns * 256.
            /// </summary>
            public byte[] PatternData { get; set; } = Array.Empty<byte>();

            /// <summary>Number of pointer entries (num_ptrs).</summary>
            public int NumPointers => PatternIndices.Length;

            /// <summary>Maximum number of patterns (maxpatterns).</summary>
            public int MaxPatterns => PatternData.Length / BytesPerTile;
        }

        private readonly PitchPatterns[] _pitches;

        /// <summary>
        /// Access raw pitch data for advanced use.
        /// </summary>
        public PitchPatterns this[int pitchIndex] => _pitches[pitchIndex];

        /// <summary>
        /// Number of pitches loaded (should be MaxPitchCount).
        /// </summary>
        public int PitchCount => _pitches.Length;

        // IDosPitchPatternSource implementation -------------------------------

        int IDosPitchPatternSource.MaxPitch => PitchCount;

        int IDosPitchPatternSource.GetPointerCount(int pitchIndex)
        {
            ValidatePitchIndex(pitchIndex);
            return _pitches[pitchIndex].NumPointers;
        }

        int IDosPitchPatternSource.GetPatternIndexForPointer(int pitchIndex, int pointerIndex)
        {
            ValidatePitchIndex(pitchIndex);

            var p = _pitches[pitchIndex];
            if (pointerIndex < 0 || pointerIndex >= p.NumPointers)
                throw new ArgumentOutOfRangeException(nameof(pointerIndex));

            return p.PatternIndices[pointerIndex];
        }

        ReadOnlySpan<byte> IDosPitchPatternSource.GetPatternBytes(int pitchIndex, int patternIndex)
        {
            ValidatePitchIndex(pitchIndex);

            var p = _pitches[pitchIndex];
            if (patternIndex < 0 || patternIndex >= p.MaxPatterns)
                throw new ArgumentOutOfRangeException(nameof(patternIndex));

            int offset = patternIndex * BytesPerTile;
            return new ReadOnlySpan<byte>(p.PatternData, offset, BytesPerTile);
        }

        // --------------------------------------------------------------------

        private DosPitchPattern(PitchPatterns[] pitches)
        {
            _pitches = pitches ?? throw new ArgumentNullException(nameof(pitches));
        }

        /// <summary>
        /// Load all DOS pitch patterns from a directory containing:
        ///   pitch1.dat / pitch1.blk
        ///   ...
        ///   pitch6.dat / pitch6.blk
        /// as in the original tools.
        /// </summary>
        /// <param name="directoryPath">Directory with pitchX.dat / pitchX.blk files.</param>
        public static DosPitchPattern LoadFromDirectory(string directoryPath)
        {
            if (directoryPath == null)
                throw new ArgumentNullException(nameof(directoryPath));

            var pitches = new PitchPatterns[MaxPitchCount];

            for (int i = 0; i < MaxPitchCount; i++)
            {
                string baseName = Path.Combine(directoryPath, $"pitch{i + 1}");
                string datPath = baseName + ".dat";
                string blkPath = baseName + ".blk";

                if (!File.Exists(datPath))
                    throw new FileNotFoundException($"Missing pattern index file: {datPath}", datPath);
                if (!File.Exists(blkPath))
                    throw new FileNotFoundException($"Missing pattern data file: {blkPath}", blkPath);

                byte[] datBytes = File.ReadAllBytes(datPath);
                byte[] blkBytes = File.ReadAllBytes(blkPath);

                if (datBytes.Length % 4 != 0)
                    throw new InvalidDataException($"{datPath} length is not a multiple of 4.");

                if (blkBytes.Length % BytesPerTile != 0)
                    throw new InvalidDataException($"{blkPath} length is not a multiple of {BytesPerTile} bytes (16x16 tiles).");

                int numPtrs = datBytes.Length / 4;
                int[] patternIndices = new int[numPtrs];

                // In original C:
                //   ptrs[j] (uint) is read from .dat
                //   After loading .blk into data, they do:
                //       (uint)ptrs[j] += (uint)data;
                // Later they compute pattern index as:
                //   (ptrs[j] - data) / 256
                // Here we just store (offset / 256) directly.
                for (int j = 0; j < numPtrs; j++)
                {
                    uint offset = BitConverter.ToUInt32(datBytes, j * 4);

                    if (offset % BytesPerTile != 0)
                    {
                        // This *should* never happen for valid SWOS data.
                        throw new InvalidDataException(
                            $"{datPath}: pointer[{j}] = {offset} is not aligned to {BytesPerTile} bytes.");
                    }

                    int patternIndex = (int)(offset / BytesPerTile);
                    patternIndices[j] = patternIndex;
                }

                pitches[i] = new PitchPatterns
                {
                    PatternIndices = patternIndices,
                    PatternData = blkBytes
                };
            }

            return new DosPitchPattern(pitches);
        }

        /// <summary>
        /// Save current patterns back to pitchX.dat / pitchX.blk files.
        /// This is the logical equivalent of SaveChangesToPatterns() from C,
        /// but simplified: it always writes out the current in-memory state.
        /// </summary>
        /// <param name="directoryPath">Destination directory.</param>
        public void SaveToDirectory(string directoryPath)
        {
            if (directoryPath == null)
                throw new ArgumentNullException(nameof(directoryPath));

            Directory.CreateDirectory(directoryPath);

            for (int i = 0; i < PitchCount; i++)
            {
                var p = _pitches[i];
                string baseName = Path.Combine(directoryPath, $"pitch{i + 1}");
                string datPath = baseName + ".dat";
                string blkPath = baseName + ".blk";

                // --- Write .dat (pointer table) ---
                // Each entry is a 4-byte offset into the data buffer: patternIndex * 256.
                byte[] datBytes = new byte[p.NumPointers * 4];
                for (int j = 0; j < p.NumPointers; j++)
                {
                    int patternIndex = p.PatternIndices[j];
                    if (patternIndex < 0 || patternIndex >= p.MaxPatterns)
                        throw new InvalidOperationException(
                            $"Pitch {i + 1}: pointer[{j}] pattern index {patternIndex} is out of range 0..{p.MaxPatterns - 1}.");

                    uint offset = (uint)(patternIndex * BytesPerTile);
                    BitConverter.GetBytes(offset).CopyTo(datBytes, j * 4);
                }

                File.WriteAllBytes(datPath, datBytes);

                // --- Write .blk (pattern data) ---
                File.WriteAllBytes(blkPath, p.PatternData);
            }
        }

        /// <summary>
        /// Convenience: returns a copy of a 16x16 pattern as a new byte[].
        /// </summary>
        public byte[] GetPatternCopy(int pitchIndex, int patternIndex)
        {
            ValidatePitchIndex(pitchIndex);

            var p = _pitches[pitchIndex];
            if (patternIndex < 0 || patternIndex >= p.MaxPatterns)
                throw new ArgumentOutOfRangeException(nameof(patternIndex));

            int offset = patternIndex * BytesPerTile;
            var result = new byte[BytesPerTile];
            Buffer.BlockCopy(p.PatternData, offset, result, 0, BytesPerTile);
            return result;
        }

        // --------------------------------------------------------------------
        // 8bpp BMP helpers
        // --------------------------------------------------------------------

        /// <summary>
        /// Save a single pattern (by pattern index) as a 16x16 8bpp BMP using the given palette.
        /// Palette entries are ARGB (0xAARRGGBB); only RGB is written to BMP.
        /// </summary>
        public void SavePatternAsBmpByIndex(int pitchIndex, int patternIndex, string path, uint[] paletteArgb)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            if (paletteArgb == null) throw new ArgumentNullException(nameof(paletteArgb));

            var span = ((IDosPitchPatternSource)this).GetPatternBytes(pitchIndex, patternIndex);
            Bitmap8Helper.Write8bppBmp(path, TileWidth, TileHeight, span, paletteArgb);
        }

        /// <summary>
        /// Save the pattern referenced by a pointer entry (from .dat) as a 16x16 8bpp BMP.
        /// </summary>
        public void SavePatternAsBmpByPointer(int pitchIndex, int pointerIndex, string path, uint[] paletteArgb)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            if (paletteArgb == null) throw new ArgumentNullException(nameof(paletteArgb));

            ValidatePitchIndex(pitchIndex);
            var p = _pitches[pitchIndex];

            if (pointerIndex < 0 || pointerIndex >= p.NumPointers)
                throw new ArgumentOutOfRangeException(nameof(pointerIndex));

            int patternIndex = p.PatternIndices[pointerIndex];
            SavePatternAsBmpByIndex(pitchIndex, patternIndex, path, paletteArgb);
        }

        /// <summary>
        /// Insert/replace the pitch at <paramref name="pitchIndex"/> by reading
        /// a full 8bpp BMP (typically pitchX.bmp, 672x848).
        ///
        /// This is the equivalent of the original C InsertPitch():
        ///  - Extracts unique 16x16 tiles.
        ///  - Builds a compact pattern stack.
        ///  - Rewrites the pointer table for the pitch grid (53x42 tiles).
        ///  - Leaves non-grid pointers intact but clamps pattern indices
        ///    beyond the new max to the last valid pattern.
        ///
        /// Returns the new pattern count (maxpatterns).
        /// </summary>
        public int InsertPitchFromBitmap(int pitchIndex, string bmpPath)
        {
            if (bmpPath == null) throw new ArgumentNullException(nameof(bmpPath));
            ValidatePitchIndex(pitchIndex);

            const int pitchW = 672; // PITCH_W
            const int pitchH = 848; // PITCH_H
            const int tilesY = 53;
            const int tilesX = 42;

            var pitchPixels = Bitmap8Helper.Read8bppBmpTopDown(bmpPath, pitchW, pitchH);

            var pitch = _pitches[pitchIndex];

            // pattern stack (like data[42*53][256] in the C code)
            // 42*53 = 2226; add some slack for animation duplicates.
            int maxPatternsStack = tilesX * tilesY + 32;
            var patternStack = new byte[maxPatternsStack][];
            var sums = new int[maxPatternsStack];
            var where = new int[tilesY, tilesX];

            int curPattern = -1;

            // Training pitch is index 5 in original code; non-training pitches
            // get a blank pattern 0 inserted at the start.
            if (pitchIndex != 5)
            {
                curPattern++;
                patternStack[curPattern] = new byte[BytesPerTile]; // all zeros
                sums[curPattern] = 0;
            }

            // For each 16x16 tile...
            for (int ty = 0; ty < tilesY; ty++)
            {
                for (int tx = 0; tx < tilesX; tx++)
                {
                    // Extract this 16x16 tile into a temporary buffer
                    var tile = new byte[BytesPerTile];
                    int dst = 0;
                    for (int row = 0; row < TileHeight; row++)
                    {
                        int srcY = ty * TileHeight + row;
                        int srcX = tx * TileWidth;
                        int srcOffset = srcY * pitchW + srcX;
                        Buffer.BlockCopy(pitchPixels, srcOffset, tile, dst, TileWidth);
                        dst += TileWidth;
                    }

                    int curSum = 0;
                    for (int k = 0; k < BytesPerTile; k++)
                        curSum += tile[k];

                    // See if we already had that pattern (sum + full compare)
                    int matchIndex = -1;
                    for (int k = curPattern; k >= 0; k--)
                    {
                        if (sums[k] != curSum)
                            continue;

                        if (PatternsEqual(patternStack[k], tile))
                        {
                            matchIndex = k;
                            break;
                        }
                    }

                    if (matchIndex >= 0)
                    {
                        // Re-use existing pattern index
                        where[ty, tx] = matchIndex;
                    }
                    else
                    {
                        // New unique pattern
                        curPattern++;
                        if (curPattern >= maxPatternsStack)
                            throw new InvalidOperationException("Pattern stack overflow; increase maxPatternsStack.");

                        patternStack[curPattern] = tile;
                        sums[curPattern] = curSum;
                        where[ty, tx] = curPattern;

                        // Training pitch doesn't have animated patterns.
                        // For non-training pitches, if curPattern in 1..24 and odd,
                        // create a duplicate pattern slot (for animated tiles).
                        if (pitchIndex != 5 &&
                            curPattern > 0 &&
                            curPattern < 25 &&
                            (curPattern & 1) == 1)
                        {
                            curPattern++;
                            if (curPattern >= maxPatternsStack)
                                throw new InvalidOperationException("Pattern stack overflow (animation dup); increase maxPatternsStack.");

                            patternStack[curPattern] = (byte[])tile.Clone();
                            // Don't match this one (change sum slightly)
                            sums[curPattern] = curSum + 1;
                        }
                    }
                }
            }

            int newPatternCount = curPattern + 1;
            if (newPatternCount <= 0)
                throw new InvalidOperationException("No patterns generated from pitch bitmap.");

            // Build contiguous pattern data buffer
            var newPatternData = new byte[newPatternCount * BytesPerTile];
            for (int i = 0; i < newPatternCount; i++)
            {
                if (patternStack[i] == null)
                    throw new InvalidOperationException($"Internal error: patternStack[{i}] is null.");

                Buffer.BlockCopy(patternStack[i], 0, newPatternData, i * BytesPerTile, BytesPerTile);
            }

            // Update pointer table:
            //  - pointers [StartPitchPointerIndex .. StartPitchPointerIndex + tilesY*tilesX)
            //    are the tile indices for this pitch and get overwritten from 'where'.
            //  - pointers outside that range are clamped to the new max pattern index
            //    if they point beyond the new pattern count.
            int pointerCount = pitch.NumPointers;
            int gridStart = StartPitchPointerIndex;
            int gridCount = tilesY * tilesX;
            int gridEnd = gridStart + gridCount; // exclusive
            int maxPatternIndex = newPatternCount - 1;

            if (gridEnd > pointerCount)
            {
                throw new InvalidOperationException(
                    $"Pointer table too small ({pointerCount}) for pitch grid ({gridEnd} required).");
            }

            // Clamp non-grid pointers
            for (int idx = 0; idx < pointerCount; idx++)
            {
                if (idx >= gridStart && idx < gridEnd)
                    continue; // will be overwritten

                int oldIndex = pitch.PatternIndices[idx];
                if (oldIndex > maxPatternIndex)
                    pitch.PatternIndices[idx] = maxPatternIndex;
            }

            // Fill grid pointers from where[] (S + 42*i + j)
            for (int i = 0; i < tilesY; i++)
            {
                for (int j = 0; j < tilesX; j++)
                {
                    int ptrIndex = gridStart + tilesX * i + j;
                    pitch.PatternIndices[ptrIndex] = where[i, j];
                }
            }

            // Commit new pattern data
            pitch.PatternData = newPatternData;

            return newPatternCount;
        }

        // --------------------------------------------------------------------
        // Internal helpers
        // --------------------------------------------------------------------

        private void ValidatePitchIndex(int pitchIndex)
        {
            if (pitchIndex < 0 || pitchIndex >= PitchCount)
                throw new ArgumentOutOfRangeException(nameof(pitchIndex));
        }

        private static bool PatternsEqual(byte[] a, byte[] b)
        {
            if (a.Length != b.Length)
                return false;

            for (int i = 0; i < a.Length; i++)
                if (a[i] != b[i])
                    return false;

            return true;
        }

        // --------------------------------------------------------------------
        // Local BMP 8bpp helper (no System.Drawing, pure header IO)
        // --------------------------------------------------------------------
        private static class Bitmap8Helper
        {
            private const ushort BmpMagic = 0x4D42; // 'BM'
            private const int FileHeaderSize = 14;
            private const int InfoHeaderSize = 40;
            private const int PaletteEntries = 256;
            private const int PaletteBytes = PaletteEntries * 4; // BGRA

            /// <summary>
            /// Write a top-down 8bpp image as a BMP with 256-color palette.
            /// </summary>
            public static void Write8bppBmp(string path, int width, int height, ReadOnlySpan<byte> pixelsTopDown, uint[] paletteArgb)
            {
                if (pixelsTopDown.Length != width * height)
                    throw new ArgumentException("Pixel buffer size does not match width*height.");

                if (paletteArgb.Length < PaletteEntries)
                    throw new ArgumentException($"Palette must have at least {PaletteEntries} entries.", nameof(paletteArgb));

                int rowSize = ((width + 3) / 4) * 4; // padded to 4 bytes
                int imageSize = rowSize * height;
                int pixelOffset = FileHeaderSize + InfoHeaderSize + PaletteBytes;
                int fileSize = pixelOffset + imageSize;

                using var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None);
                using var bw = new BinaryWriter(fs);

                // --- BITMAPFILEHEADER ---
                bw.Write(BmpMagic);               // bfType
                bw.Write(fileSize);               // bfSize
                bw.Write((ushort)0);              // bfReserved1
                bw.Write((ushort)0);              // bfReserved2
                bw.Write(pixelOffset);            // bfOffBits

                // --- BITMAPINFOHEADER ---
                bw.Write(InfoHeaderSize);         // biSize
                bw.Write(width);                  // biWidth
                bw.Write(height);                 // biHeight (positive = bottom-up)
                bw.Write((ushort)1);              // biPlanes
                bw.Write((ushort)8);              // biBitCount
                bw.Write(0u);                     // biCompression = BI_RGB
                bw.Write(imageSize);              // biSizeImage
                bw.Write(0);                      // biXPelsPerMeter
                bw.Write(0);                      // biYPelsPerMeter
                bw.Write((uint)PaletteEntries);   // biClrUsed
                bw.Write(0u);                     // biClrImportant

                // --- Palette (BGRA) ---
                for (int i = 0; i < PaletteEntries; i++)
                {
                    uint argb = paletteArgb[i];
                    byte r = (byte)((argb >> 16) & 0xFF);
                    byte g = (byte)((argb >> 8) & 0xFF);
                    byte b = (byte)(argb & 0xFF);

                    bw.Write(b);
                    bw.Write(g);
                    bw.Write(r);
                    bw.Write((byte)0); // reserved
                }

                // --- Pixels (bottom-up) ---
                Span<byte> pad = stackalloc byte[3]; // up to 3 bytes padding
                for (int y = height - 1; y >= 0; y--)
                {
                    int srcOffset = y * width;
                    bw.Write(pixelsTopDown.Slice(srcOffset, width));

                    int padding = rowSize - width;
                    if (padding > 0)
                        bw.Write(pad.Slice(0, padding));
                }
            }

            /// <summary>
            /// Read an 8bpp BMP and return its pixels as a top-down buffer (width*height).
            /// Validates width, height and 8bpp, BI_RGB.
            /// Palette is ignored; only indices are used.
            /// </summary>
            public static byte[] Read8bppBmpTopDown(string path, int expectedWidth, int expectedHeight)
            {
                using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
                using var br = new BinaryReader(fs);

                // --- BITMAPFILEHEADER ---
                ushort magic = br.ReadUInt16();
                if (magic != BmpMagic)
                    throw new InvalidDataException("Not a BMP file (missing 'BM').");

                uint fileSize = br.ReadUInt32();
                br.ReadUInt16(); // reserved1
                br.ReadUInt16(); // reserved2
                uint pixelOffset = br.ReadUInt32();

                // --- BITMAPINFOHEADER ---
                uint infoSize = br.ReadUInt32();
                if (infoSize < InfoHeaderSize)
                    throw new InvalidDataException("Unsupported BMP info header size.");

                int width = br.ReadInt32();
                int height = br.ReadInt32();
                ushort planes = br.ReadUInt16();
                ushort bitCount = br.ReadUInt16();
                uint compression = br.ReadUInt32();
                uint sizeImage = br.ReadUInt32();
                int xPels = br.ReadInt32();
                int yPels = br.ReadInt32();
                uint clrUsed = br.ReadUInt32();
                uint clrImportant = br.ReadUInt32();

                if (bitCount != 8)
                    throw new InvalidDataException("Only 8bpp BMPs are supported.");
                if (compression != 0)
                    throw new InvalidDataException("Compressed BMPs are not supported.");
                if (planes != 1)
                    throw new InvalidDataException("Invalid planes count in BMP (expected 1).");

                bool bottomUp = height > 0;
                if (!bottomUp)
                    throw new InvalidDataException("Top-down BMPs (negative height) are not supported here.");

                if (width != expectedWidth || height != expectedHeight)
                    throw new InvalidDataException($"Unexpected BMP dimensions: {width}x{height}, expected {expectedWidth}x{expectedHeight}.");

                // Palette (we just skip over 256 entries if present)
                int paletteSizeBytes = PaletteBytes;
                fs.Seek(FileHeaderSize + infoSize, SeekOrigin.Begin);
                fs.Seek(paletteSizeBytes, SeekOrigin.Current);

                // Pixels
                fs.Seek(pixelOffset, SeekOrigin.Begin);

                int rowSize = ((width + 3) / 4) * 4;
                var buffer = new byte[width * height];
                byte[] row = new byte[rowSize];

                for (int rowIndex = 0; rowIndex < height; rowIndex++)
                {
                    int destY = height - 1 - rowIndex; // convert bottom-up to top-down
                    br.Read(row, 0, rowSize);
                    Buffer.BlockCopy(row, 0, buffer, destY * width, width);
                }

                return buffer;
            }
        }
    }
}
