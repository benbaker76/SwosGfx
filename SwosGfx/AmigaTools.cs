using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Xml.Linq;

namespace SwosGfx
{
    public enum RawFormat
    {
        Raw320x256,   // CJCBITS.RAW / CJCGRAFS.RAW etc.
        Raw352x272    // LOADER1.RAW-style (DC_MakeAmigaRaw_New)
    }

    public sealed class AmigaTools
    {
        // Pitch constants
        private const int PitchTilesX = 42;
        private const int PitchTilesY = 55;
        private const int TileSize = 16;
        private const int PitchWidth = PitchTilesX * TileSize;   // 672
        private const int PitchHeight = PitchTilesY * TileSize;  // 880

        // MAP layout constants
        private const int MapHeaderSize = PitchTilesX * PitchTilesY * 4; // 42*55*4 = 9240
        private const int SpriteRows = 16;
        private const int BytesPerPlanePerRow16 = TileSize / 8; // 16px / 8 = 2 bytes per plane per row

        // Default number of columns for tileset image in pitch-map-to-tiled
        private const int TileSheetColumns = 16;

        // Tile data structures (equivalent to TMapping / TPitchBitmapItem)
        private struct Mapping
        {
            public byte B1, B2, B3, B4;
        }

        private sealed class PitchBitmapItem
        {
            public readonly byte[,] Bits = new byte[TileSize, TileSize]; // [y,x]
        }

        // ------------------------------------------------------------------
        // Planar helpers for 16-pixel rows (1-8 bitplanes)
        // ------------------------------------------------------------------

        // Encode 16 pixels (indices 0..255) into planar bytes for N bitplanes.
        // For 16 pixels, each plane = 2 bytes, so total = bitplanes * 2.
        private static void EncodeAmigaRow16(byte[] inputPixels16, int bitplanes, byte[] outputRowBytes)
        {
            if (inputPixels16 == null) throw new ArgumentNullException(nameof(inputPixels16));
            if (outputRowBytes == null) throw new ArgumentNullException(nameof(outputRowBytes));
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");

            const int width = TileSize; // 16
            int bytesPerPlanePerRow = BytesPerPlanePerRow16; // 2

            if (inputPixels16.Length != width)
                throw new ArgumentException($"inputPixels16 must be length {width}.");
            if (outputRowBytes.Length != bytesPerPlanePerRow * bitplanes)
                throw new ArgumentException($"outputRowBytes must be length {bytesPerPlanePerRow * bitplanes} for {bitplanes} bitplanes.");

            int maxIndex = (1 << bitplanes) - 1;
            int outIndex = 0;

            for (int plane = 0; plane < bitplanes; plane++)
            {
                for (int block = 0; block < bytesPerPlanePerRow; block++)
                {
                    byte value = 0;

                    // Each byte packs 8 pixels for this plane
                    for (int bit = 0; bit < 8; bit++)
                    {
                        int x = block * 8 + bit;     // pixel index 0..15
                        int pix = inputPixels16[x];  // full index (0..255)

                        if (pix > maxIndex)
                            throw new InvalidOperationException(
                                $"Pixel index {pix} exceeds maximum {maxIndex} for {bitplanes} bitplanes.");

                        int bitVal = (pix >> plane) & 1; // plane 0 = LSB

                        // MSB first: left-shift then OR in the new bit
                        value = (byte)((value << 1) | bitVal);
                    }

                    outputRowBytes[outIndex++] = value;
                }
            }
        }

        // Decode planar bytes for a 16-pixel row (1-8 bitplanes) to indices 0..255.
        private static void DecodeAmigaRow16(byte[] inputRowBytes, int bitplanes, byte[] outputPixels16)
        {
            if (inputRowBytes == null) throw new ArgumentNullException(nameof(inputRowBytes));
            if (outputPixels16 == null) throw new ArgumentNullException(nameof(outputPixels16));
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");

            const int width = TileSize; // 16
            int bytesPerPlanePerRow = BytesPerPlanePerRow16; // 2

            if (inputRowBytes.Length != bytesPerPlanePerRow * bitplanes)
                throw new ArgumentException($"inputRowBytes must be length {bytesPerPlanePerRow * bitplanes} for {bitplanes} bitplanes.");
            if (outputPixels16.Length != width)
                throw new ArgumentException($"outputPixels16 must be length {width}.");

            for (int x = 0; x < width; x++)
            {
                int pixelIndex = 0;

                int byteIndexInPlane = x / 8;      // 0 or 1
                int bitIndexInByte = 7 - (x % 8);  // MSB first

                for (int plane = 0; plane < bitplanes; plane++)
                {
                    int planeBase = plane * bytesPerPlanePerRow;
                    byte b = inputRowBytes[planeBase + byteIndexInPlane];
                    int bit = (b >> bitIndexInByte) & 1;
                    pixelIndex |= (bit << plane);
                }

                outputPixels16[x] = (byte)pixelIndex;
            }
        }

        // ------------------------------------------------------------------
        // BMP helper: 4/8bpp indexed, no System.Drawing
        // ------------------------------------------------------------------

        private static class Bmp8Helper
        {
            private const ushort BmpMagic = 0x4D42; // 'BM'
            private const int FileHeaderSize = 14;
            private const int InfoHeaderSize = 40;
            private const int PaletteEntries = 256;
            private const int PaletteBytes = PaletteEntries * 4; // BGRA

            /// <summary>
            /// Read an indexed BMP (4bpp or 8bpp) and return indices[y,x] + ARGB palette[256].
            /// For 4bpp, only the first 16 entries are typically used; the rest are zeroed.
            /// </summary>
            public static byte[,] Read8bppBmpToIndices(
                string path,
                out uint[] paletteArgb,
                int? expectedWidth = null,
                int? expectedHeight = null)
            {
                using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
                using var br = new BinaryReader(fs);

                // BITMAPFILEHEADER
                ushort magic = br.ReadUInt16();
                if (magic != BmpMagic)
                    throw new InvalidDataException("Not a BMP file (missing 'BM').");

                uint fileSize = br.ReadUInt32();
                br.ReadUInt16(); // reserved1
                br.ReadUInt16(); // reserved2
                uint pixelOffset = br.ReadUInt32();

                // BITMAPINFOHEADER
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

                if (bitCount != 4 && bitCount != 8)
                    throw new InvalidDataException("Only 4bpp or 8bpp indexed BMPs are supported.");
                if (compression != 0)
                    throw new InvalidDataException("Compressed BMPs are not supported.");
                if (planes != 1)
                    throw new InvalidDataException("Invalid planes count in BMP (expected 1).");

                bool bottomUp = height > 0;
                if (!bottomUp)
                    throw new InvalidDataException("Top-down BMPs (negative height) are not supported here.");

                if (expectedWidth.HasValue && width != expectedWidth.Value)
                    throw new InvalidDataException($"BMP width must be {expectedWidth.Value}, got {width}.");
                if (expectedHeight.HasValue && height != expectedHeight.Value)
                    throw new InvalidDataException($"BMP height must be {expectedHeight.Value}, got {height}.");

                int heightAbs = height;
                int bitsPerPixel = bitCount;

                // Determine how many palette entries are actually stored
                int paletteCount;
                if (clrUsed != 0)
                    paletteCount = (int)Math.Min(clrUsed, (uint)PaletteEntries);
                else
                    paletteCount = (bitCount == 4) ? 16 : 256;

                // Palette
                paletteArgb = new uint[PaletteEntries];

                // Seek to palette (immediately after BITMAPFILEHEADER + INFOHEADER)
                int paletteOffset = FileHeaderSize + (int)infoSize;
                fs.Seek(paletteOffset, SeekOrigin.Begin);

                for (int i = 0; i < paletteCount; i++)
                {
                    byte b = br.ReadByte();
                    byte g = br.ReadByte();
                    byte r = br.ReadByte();
                    byte a = br.ReadByte(); // reserved (ignored)

                    paletteArgb[i] = (uint)((r << 16) | (g << 8) | b);
                }

                // Any unused palette slots are left as 0
                for (int i = paletteCount; i < PaletteEntries; i++)
                    paletteArgb[i] = 0;

                // Seek to pixel data
                fs.Seek(pixelOffset, SeekOrigin.Begin);

                // General formula: rows are DWORD-aligned
                int rowSize = ((width * bitsPerPixel + 31) / 32) * 4;
                byte[] row = new byte[rowSize];
                byte[,] indices = new byte[heightAbs, width];

                for (int rowIndex = 0; rowIndex < heightAbs; rowIndex++)
                {
                    int read = br.Read(row, 0, rowSize);
                    if (read != rowSize)
                        throw new EndOfStreamException("Unexpected end of BMP file reading pixel data.");

                    int destY = heightAbs - 1 - rowIndex; // convert bottom-up to top-down

                    if (bitsPerPixel == 8)
                    {
                        // one byte per pixel
                        for (int x = 0; x < width; x++)
                            indices[destY, x] = row[x];
                    }
                    else // 4bpp
                    {
                        // two pixels per byte (high nibble = left pixel)
                        for (int x = 0; x < width; x++)
                        {
                            int byteIndex = x / 2;
                            byte packed = row[byteIndex];
                            byte idx = (x % 2 == 0)
                                ? (byte)((packed >> 4) & 0x0F)
                                : (byte)(packed & 0x0F);

                            indices[destY, x] = idx;
                        }
                    }
                }

                return indices;
            }

            /// <summary>
            /// Write indices[y,x] + ARGB palette[256] as an 8bpp BMP.
            /// </summary>
            public static void Write8bppBmpFromIndices(
                string path,
                byte[,] indices,
                uint[] paletteArgb)
            {
                if (indices == null) throw new ArgumentNullException(nameof(indices));
                if (paletteArgb == null) throw new ArgumentNullException(nameof(paletteArgb));

                int height = indices.GetLength(0);
                int width = indices.GetLength(1);

                const int bitsPerPixel = 8;
                int rowSize = ((width * bitsPerPixel + 31) / 32) * 4; // 8bpp rows, DWORD-aligned
                int imageSize = rowSize * height;
                int pixelOffset = FileHeaderSize + InfoHeaderSize + PaletteBytes;
                int fileSize = pixelOffset + imageSize;

                Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".");

                using var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None);
                using var bw = new BinaryWriter(fs);

                // BITMAPFILEHEADER
                bw.Write(BmpMagic);              // bfType
                bw.Write(fileSize);              // bfSize
                bw.Write((ushort)0);             // bfReserved1
                bw.Write((ushort)0);             // bfReserved2
                bw.Write(pixelOffset);           // bfOffBits

                // BITMAPINFOHEADER
                bw.Write(InfoHeaderSize);        // biSize
                bw.Write(width);                 // biWidth
                bw.Write(height);                // biHeight (positive = bottom-up)
                bw.Write((ushort)1);             // biPlanes
                bw.Write((ushort)8);             // biBitCount
                bw.Write(0u);                    // biCompression = BI_RGB
                bw.Write(imageSize);             // biSizeImage
                bw.Write(0);                     // biXPelsPerMeter
                bw.Write(0);                     // biYPelsPerMeter
                bw.Write((uint)PaletteEntries);  // biClrUsed
                bw.Write(0u);                    // biClrImportant

                // Palette (BGRA)
                for (int i = 0; i < PaletteEntries; i++)
                {
                    uint argb = i < paletteArgb.Length ? paletteArgb[i] : 0;
                    byte r = (byte)((argb >> 16) & 0xFF);
                    byte g = (byte)((argb >> 8) & 0xFF);
                    byte b = (byte)(argb & 0xFF);

                    bw.Write(b);
                    bw.Write(g);
                    bw.Write(r);
                    bw.Write((byte)0); // reserved
                }

                // Pixels (bottom-up)
                byte[] rowBuf = new byte[rowSize];

                for (int y = height - 1; y >= 0; y--)
                {
                    for (int x = 0; x < width; x++)
                        rowBuf[x] = indices[y, x];

                    // zero padding
                    for (int p = width; p < rowSize; p++)
                        rowBuf[p] = 0;

                    bw.Write(rowBuf);
                }
            }
        }

        /// <summary>
        /// Load an indexed BMP and return its index buffer [y,x] and palette.
        /// </summary>
        private static byte[,] LoadIndexedImageIndices(
            string path,
            out uint[] palette,
            int? expectedWidth = null,
            int? expectedHeight = null)
        {
            return Bmp8Helper.Read8bppBmpToIndices(path, out palette, expectedWidth, expectedHeight);
        }

        // ------------------------------------------------------------------
        // Helpers for MAP <-> sprites+mapping (variable bitplanes)
        // ------------------------------------------------------------------

        private static void ReadPitchMap(
            string mapFilePath,
            int bitplanes,
            out Mapping[,] mapping,
            out List<PitchBitmapItem> sprites)
        {
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");

            byte[] data = AmigaRncHelper.ReadAllBytes(mapFilePath);

            if (data.Length < MapHeaderSize)
                throw new InvalidOperationException($"MAP file too small. Length={data.Length}, expected at least {MapHeaderSize}.");

            using var ms = new MemoryStream(data);
            using var br = new BinaryReader(ms);

            // 1) Header = mapping
            mapping = new Mapping[PitchTilesY, PitchTilesX];
            for (int y = 0; y < PitchTilesY; y++)
            {
                for (int x = 0; x < PitchTilesX; x++)
                {
                    mapping[y, x].B1 = br.ReadByte();
                    mapping[y, x].B2 = br.ReadByte();
                    mapping[y, x].B3 = br.ReadByte();
                    mapping[y, x].B4 = br.ReadByte();
                }
            }

            long bytesRead = ms.Position;
            int remaining = data.Length - (int)bytesRead;
            if (remaining < 0)
                throw new InvalidOperationException("MAP parsing error: negative remaining bytes.");

            int spriteRowBytes = BytesPerPlanePerRow16 * bitplanes; // 2 * bitplanes
            int spriteSizeBytes = SpriteRows * spriteRowBytes;      // 16 rows

            if (remaining % spriteSizeBytes != 0)
                throw new InvalidOperationException(
                    $"MAP sprite data size ({remaining} bytes) is not a multiple of spriteSizeBytes={spriteSizeBytes} for {bitplanes} bitplanes.");

            int spriteCount = remaining / spriteSizeBytes;
            sprites = new List<PitchBitmapItem>(spriteCount);

            var rowBytes = new byte[spriteRowBytes];
            var rowPixels = new byte[TileSize];

            for (int sIndex = 0; sIndex < spriteCount; sIndex++)
            {
                var tile = new PitchBitmapItem();

                for (int row = 0; row < SpriteRows; row++)
                {
                    int read = br.Read(rowBytes, 0, spriteRowBytes);
                    if (read != spriteRowBytes)
                        throw new EndOfStreamException("Unexpected end of sprite data in MAP.");

                    DecodeAmigaRow16(rowBytes, bitplanes, rowPixels);

                    for (int col = 0; col < TileSize; col++)
                        tile.Bits[row, col] = rowPixels[col];
                }

                sprites.Add(tile);
            }
        }

        private static bool SameTile(PitchBitmapItem a, PitchBitmapItem b)
        {
            for (int j = 0; j < TileSize; j++)
                for (int i = 0; i < TileSize; i++)
                    if (a.Bits[j, i] != b.Bits[j, i])
                        return false;
            return true;
        }

        private static void BuildSpritesAndMapping(
            PitchBitmapItem[,] tileMatrix,
            int maxTiles,
            out List<PitchBitmapItem> sprites,
            out int[,] tileIndices,
            out Mapping[,] mapping)
        {
            int rows = tileMatrix.GetLength(0);
            int cols = tileMatrix.GetLength(1);

            sprites = new List<PitchBitmapItem>();
            tileIndices = new int[rows, cols];

            // Deduplicate tiles into sprite list
            for (int ty = 0; ty < rows; ty++)
            {
                for (int tx = 0; tx < cols; tx++)
                {
                    var tile = tileMatrix[ty, tx];
                    int foundIndex = -1;

                    for (int i = 0; i < sprites.Count; i++)
                    {
                        if (SameTile(tile, sprites[i]))
                        {
                            foundIndex = i;
                            break;
                        }
                    }

                    if (foundIndex == -1)
                    {
                        foundIndex = sprites.Count;
                        sprites.Add(tile);
                    }

                    tileIndices[ty, tx] = foundIndex;
                }
            }

            int spriteCount = sprites.Count;
            if (maxTiles > 0 && spriteCount > maxTiles)
            {
                Console.WriteLine($"WARNING: Tile count {spriteCount} exceeds limit {maxTiles}.");
            }

            // Build Mapping matrix from tile indices (Amigarize pitch matrix)
            mapping = new Mapping[rows, cols];
            for (int ty = 0; ty < rows; ty++)
            {
                for (int tx = 0; tx < cols; tx++)
                {
                    int p = tileIndices[ty, tx];
                    int p2 = p / 2;
                    int pr = p % 2;
                    byte b4 = (byte)(pr == 0 ? 0 : 128);

                    mapping[ty, tx].B1 = 0;
                    mapping[ty, tx].B2 = 0;
                    mapping[ty, tx].B3 = (byte)p2;
                    mapping[ty, tx].B4 = b4;
                }
            }
        }

        private static void WritePitchMap(
            string outputMapPath,
            Mapping[,] mapping,
            List<PitchBitmapItem> sprites,
            int bitplanes)
        {
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");

            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms);

            int rows = mapping.GetLength(0);
            int cols = mapping.GetLength(1);

            // mapping header
            for (int ty = 0; ty < rows; ty++)
            {
                for (int tx = 0; tx < cols; tx++)
                {
                    var m = mapping[ty, tx];
                    bw.Write(m.B1);
                    bw.Write(m.B2);
                    bw.Write(m.B3);
                    bw.Write(m.B4);
                }
            }

            int spriteRowBytes = BytesPerPlanePerRow16 * bitplanes;
            var rowPixels = new byte[TileSize];
            var rowBytes = new byte[spriteRowBytes];

            // sprites: 16 rows * (bitplanes * 2 bytes) each
            for (int k = 0; k < sprites.Count; k++)
            {
                var tile = sprites[k];

                for (int row = 0; row < TileSize; row++)
                {
                    for (int col = 0; col < TileSize; col++)
                        rowPixels[col] = tile.Bits[row, col];

                    EncodeAmigaRow16(rowPixels, bitplanes, rowBytes);
                    bw.Write(rowBytes);
                }
            }

            bw.Flush();
            byte[] mapBytes = ms.ToArray();

            // RNC-aware write (uses global setting inside AmigaRncHelper)
            AmigaRncHelper.WriteAllBytes(outputMapPath, mapBytes);
        }

        // ------------------------------------------------------------------
        // 1. PITCH MAP → FULL PITCH BMP (MAP-only, variable bitplanes)
        // ------------------------------------------------------------------

        /// <summary>
        /// Rebuild a full 42x55-tile pitch bitmap from a SWOS pitch MAP file.
        ///
        /// Tiles are read directly from the MAP's planar sprite data:
        /// - Header: 42x55x4 bytes (mapping)
        /// - Sprites: N * 16 * (bitplanes * 2) bytes (16x16 pixels each)
        ///
        /// overridePalette:
        ///   - if null, a default SWOS palette is used (SwosPalettes.Base).
        ///   - otherwise, the given palette is used for the output BMP.
        ///
        /// bitplanes:
        ///   - number of bitplanes used in the MAP sprite data (1-8).
        /// </summary>
        public void ConvertPitchMapToBmp(
            string mapFilePath,
            uint[]? overridePalette,
            string outputBmpPath,
            int bitplanes = 4)
        {
            // 1) Read mapping + sprites from MAP (RNC-aware, variable bitplanes)
            ReadPitchMap(mapFilePath, bitplanes, out var mapping, out var sprites);

            // 2) Palette: override or default
            uint[] palette = overridePalette ?? (bitplanes > 4 ? DosPalette.Game : AmigaPalette.Game);

            // 3) Build full pitch index buffer
            var fullIndices = new byte[PitchHeight, PitchWidth];

            for (int ty = 0; ty < PitchTilesY; ty++)
            {
                for (int tx = 0; tx < PitchTilesX; tx++)
                {
                    var m = mapping[ty, tx];
                    int a = m.B3 * 2;
                    int b = (m.B4 == 128) ? 1 : 0;
                    int spriteIndex = a + b;

                    if (spriteIndex < 0 || spriteIndex >= sprites.Count)
                        throw new InvalidOperationException($"Mapping refers to sprite index {spriteIndex} but sprites.Count={sprites.Count}.");

                    var tile = sprites[spriteIndex];

                    int dstX0 = tx * TileSize;
                    int dstY0 = ty * TileSize;

                    for (int j = 0; j < TileSize; j++)
                    {
                        for (int i = 0; i < TileSize; i++)
                        {
                            byte idx = tile.Bits[j, i];
                            fullIndices[dstY0 + j, dstX0 + i] = idx;
                        }
                    }
                }
            }

            // 4) Write indexed BMP from indices + palette
            Bmp8Helper.Write8bppBmpFromIndices(outputBmpPath, fullIndices, palette);
        }

        // ------------------------------------------------------------------
        // 2. PITCH MAP → Tiled TMX + tiles BMP (MAP-only, variable bitplanes)
        // ------------------------------------------------------------------

        /// <summary>
        /// Convert a SWOS pitch MAP file into:
        ///   - a tileset BMP (MyPitch.bmp, same directory as TMX)
        ///   - a Tiled TMX file with one layer that references those tiles.
        ///
        /// Tiles are laid out in a tileset image with 16 columns of 16x16 tiles.
        /// Tile IDs in Tiled are 1-based and directly correspond to sprite indices + 1.
        ///
        /// bitplanes:
        ///   - number of bitplanes used in the MAP sprite data (1-8).
        /// </summary>
        public void ConvertPitchMapToTiled(
            string mapFilePath,
            uint[] palette,
            string dstTmxPath,
            int bitplanes = 4)
        {
            // 1) Read mapping + sprites from MAP
            ReadPitchMap(mapFilePath, bitplanes, out var mapping, out var sprites);

            // 2) Build tileset image (sprites -> grid 16x16 tiles)
            int tileCount = sprites.Count;
            int destCols = TileSheetColumns;
            int destRows = (tileCount + destCols - 1) / destCols;

            int imgWidth = destCols * TileSize;
            int imgHeight = destRows * TileSize;

            var tilesetIndices = new byte[imgHeight, imgWidth];

            for (int sIndex = 0; sIndex < tileCount; sIndex++)
            {
                int col = sIndex % destCols;
                int row = sIndex / destCols;

                var tile = sprites[sIndex];
                int dstX0 = col * TileSize;
                int dstY0 = row * TileSize;

                for (int y = 0; y < TileSize; y++)
                {
                    for (int x = 0; x < TileSize; x++)
                    {
                        tilesetIndices[dstY0 + y, dstX0 + x] = tile.Bits[y, x];
                    }
                }
            }

            string tmxFullPath = Path.GetFullPath(dstTmxPath);
            string tmxDir = Path.GetDirectoryName(tmxFullPath) ?? ".";
            string baseName = Path.GetFileNameWithoutExtension(tmxFullPath);
            string tilesBmpPath = Path.Combine(tmxDir, baseName + ".bmp");

            // 3) Write tiles BMP
            Bmp8Helper.Write8bppBmpFromIndices(tilesBmpPath, tilesetIndices, palette);

            // 4) Write TMX
            int tileCols = PitchTilesX;
            int tileRows = PitchTilesY;

            var encoding = new UTF8Encoding(false);
            using var writer = new StreamWriter(tmxFullPath, false, encoding);

            writer.WriteLine("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
            writer.WriteLine("<map version=\"1.10\" tiledversion=\"1.10.2\" orientation=\"orthogonal\" renderorder=\"right-down\" " +
                             $"width=\"{tileCols}\" height=\"{tileRows}\" tilewidth=\"{TileSize}\" tileheight=\"{TileSize}\" infinite=\"0\" nextlayerid=\"2\" nextobjectid=\"1\">");
            writer.WriteLine($" <tileset firstgid=\"1\" name=\"tiles\" tilewidth=\"{TileSize}\" tileheight=\"{TileSize}\" tilecount=\"{tileCount}\" columns=\"{destCols}\">");
            writer.WriteLine($"  <image source=\"{Path.GetFileName(tilesBmpPath)}\" width=\"{imgWidth}\" height=\"{imgHeight}\"/>");
            writer.WriteLine(" </tileset>");
            writer.WriteLine($" <layer id=\"1\" name=\"Tile Layer 1\" width=\"{tileCols}\" height=\"{tileRows}\">");
            writer.WriteLine("  <data encoding=\"csv\">");

            for (int y = 0; y < tileRows; y++)
            {
                for (int x = 0; x < tileCols; x++)
                {
                    var m = mapping[y, x];
                    int a = m.B3 * 2;
                    int b = (m.B4 == 128) ? 1 : 0;
                    int spriteIndex = a + b;
                    int gid = spriteIndex + 1; // 1-based tile ID for Tiled

                    bool lastCell = (y == tileRows - 1) && (x == tileCols - 1);
                    writer.Write(gid);
                    if (!lastCell)
                        writer.Write(",");
                    if (x == tileCols - 1)
                        writer.WriteLine();
                }
            }

            writer.WriteLine("  </data>");
            writer.WriteLine(" </layer>");
            writer.WriteLine("</map>");
        }

        // ------------------------------------------------------------------
        // 3. Tiled TMX + tiles BMP → PITCH MAP (variable bitplanes)
        // ------------------------------------------------------------------

        /// <summary>
        /// Convert a Tiled TMX file (plus its associated tiles BMP) back into
        /// a SWOS MAP file.
        ///
        /// Requirements:
        /// - TMX has a single tileset with a single image.
        /// - TMX tilewidth/tileheight are 16x16.
        /// - TMX width/height is 42x55.
        /// - Tile IDs (gid) are >0 (no empty tiles) and reference a tileset image
        ///   that contains all tile graphics.
        ///
        /// bitplanes:
        ///   - number of bitplanes to use when encoding the MAP (1-8).
        ///     4 = original ECS/OCS 16-color; 8 = full 256-color AGA.
        /// </summary>
        public void ConvertTiledToPitchMap(
            string tmxPath,
            string outputMapPath,
            int maxTiles = 284,
            int bitplanes = 4)
        {
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");

            string fullTmxPath = Path.GetFullPath(tmxPath);
            var doc = XDocument.Load(fullTmxPath);

            var mapElem = doc.Element("map")
                ?? throw new InvalidDataException("TMX: missing <map> root element.");

            int tileCols = (int)mapElem.Attribute("width");
            int tileRows = (int)mapElem.Attribute("height");
            int tileW = (int)mapElem.Attribute("tilewidth");
            int tileH = (int)mapElem.Attribute("tileheight");

            if (tileW != TileSize || tileH != TileSize)
                throw new InvalidOperationException($"TMX tile size must be {TileSize}x{TileSize}.");
            if (tileCols != PitchTilesX || tileRows != PitchTilesY)
                throw new InvalidOperationException($"TMX map dimensions must be {PitchTilesX}x{PitchTilesY}.");

            var tilesetElem = mapElem.Element("tileset")
                ?? throw new InvalidDataException("TMX: missing <tileset> element.");
            int tileCount = (int)tilesetElem.Attribute("tilecount");
            int columns = (int)tilesetElem.Attribute("columns");

            var imageElem = tilesetElem.Element("image")
                ?? throw new InvalidDataException("TMX: missing <image> inside <tileset>.");
            string imageSource = (string)imageElem.Attribute("source")
                ?? throw new InvalidDataException("TMX: <image> missing 'source' attribute.");

            int imageWidth = (int)imageElem.Attribute("width");
            int imageHeight = (int)imageElem.Attribute("height");

            string tmxDir = Path.GetDirectoryName(fullTmxPath) ?? ".";
            string tilesBmpPath = Path.Combine(tmxDir, imageSource);

            var layerElem = mapElem.Element("layer")
                ?? throw new InvalidDataException("TMX: missing <layer> element.");
            var dataElem = layerElem.Element("data")
                ?? throw new InvalidDataException("TMX: missing <data> inside <layer>.");

            string encoding = (string)dataElem.Attribute("encoding") ?? "";
            if (!encoding.Equals("csv", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("TMX <data> encoding must be 'csv'.");

            string csv = dataElem.Value ?? "";
            var tokens = csv.Split(new[] { ',', ' ', '\r', '\n', '\t' },
                                   StringSplitOptions.RemoveEmptyEntries);

            if (tokens.Length != tileCols * tileRows)
                throw new InvalidOperationException($"TMX: expected {tileCols * tileRows} tile IDs, got {tokens.Length}.");

            int[,] gids = new int[tileRows, tileCols];
            for (int i = 0; i < tokens.Length; i++)
            {
                int gid = int.Parse(tokens[i]);
                int y = i / tileCols;
                int x = i % tileCols;
                gids[y, x] = gid;
            }

            // 1) Read tileset image
            uint[] tsPal;
            byte[,] tsIndices = LoadIndexedImageIndices(
                tilesBmpPath,
                out tsPal,
                expectedWidth: imageWidth,
                expectedHeight: imageHeight);

            int tileSheetCols = columns;
            int tileSheetRows = imageHeight / TileSize;

            // 2) Build tile library: one PitchBitmapItem per tile in tileset
            var tileLibrary = new PitchBitmapItem[tileCount];

            for (int tileIndex = 0; tileIndex < tileCount; tileIndex++)
            {
                int col = tileIndex % tileSheetCols;
                int row = tileIndex / tileSheetCols;
                if (row >= tileSheetRows)
                    throw new InvalidOperationException("TMX: tileset image is too small for declared tilecount/columns.");

                var tile = new PitchBitmapItem();
                int srcX0 = col * TileSize;
                int srcY0 = row * TileSize;

                for (int y = 0; y < TileSize; y++)
                {
                    for (int x = 0; x < TileSize; x++)
                    {
                        tile.Bits[y, x] = tsIndices[srcY0 + y, srcX0 + x];
                    }
                }

                tileLibrary[tileIndex] = tile;
            }

            // 3) Build tile matrix for the pitch (using tileLibrary + gids)
            var tileMatrix = new PitchBitmapItem[PitchTilesY, PitchTilesX];

            for (int ty = 0; ty < PitchTilesY; ty++)
            {
                for (int tx = 0; tx < PitchTilesX; tx++)
                {
                    int gid = gids[ty, tx];
                    if (gid <= 0)
                        throw new InvalidOperationException("TMX: tile GID 0 (empty) encountered; SWOS MAP requires all tiles present.");

                    int tileIndex = gid - 1; // 1-based gid → 0-based
                    if (tileIndex < 0 || tileIndex >= tileCount)
                        throw new InvalidOperationException($"TMX: tile GID {gid} references out-of-range tile index {tileIndex}.");

                    tileMatrix[ty, tx] = tileLibrary[tileIndex];
                }
            }

            // 4) Deduplicate tiles + build mapping, then write MAP
            BuildSpritesAndMapping(tileMatrix, maxTiles, out var sprites, out var tileIndices, out var mapping);
            WritePitchMap(outputMapPath, mapping, sprites, bitplanes);
        }

        // ------------------------------------------------------------------
        // 4. FULL PITCH BMP → PITCH MAP RAW (variable bitplanes)
        // ------------------------------------------------------------------

        /// <summary>
        /// Convert a full pitch indexed BMP (42x55 tiles = 672x880) to a SWOS MAP file.
        /// This builds both the mapping and the planar sprite data.
        ///
        /// fullPitchBmpPath MUST be an indexed BMP whose indices are already correct.
        /// Palette is read but not used for the planar encoding.
        ///
        /// bitplanes:
        ///   - number of bitplanes to use in the MAP sprite data (1-8).
        /// </summary>
        public void ConvertFullPitchBmpToMap(
            string fullPitchBmpPath,
            string outputMapPath,
            int maxTiles = 284,
            int bitplanes = 4)
        {
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");

            // 1) Load full pitch indices (ignore image palette; trust indices)
            uint[] imgPal;
            byte[,] indexBuffer = LoadIndexedImageIndices(
                fullPitchBmpPath,
                out imgPal,
                expectedWidth: PitchWidth,
                expectedHeight: PitchHeight);

            // 2) Slice into tile matrix (55 x 42 tiles of 16x16)
            var tileMatrix = new PitchBitmapItem[PitchTilesY, PitchTilesX];

            for (int ty = 0; ty < PitchTilesY; ty++)
            {
                for (int tx = 0; tx < PitchTilesX; tx++)
                {
                    var tile = new PitchBitmapItem();
                    for (int j = 0; j < TileSize; j++)
                    {
                        for (int i = 0; i < TileSize; i++)
                        {
                            tile.Bits[j, i] = indexBuffer[ty * TileSize + j, tx * TileSize + i];
                        }
                    }
                    tileMatrix[ty, tx] = tile;
                }
            }

            // 3) Deduplicate tiles and build mapping
            BuildSpritesAndMapping(tileMatrix, maxTiles, out var sprites, out var tileIndices, out var mapping);

            // 4) Write MAP with the requested bitplane depth
            WritePitchMap(outputMapPath, mapping, sprites, bitplanes);
        }

        // ------------------------------------------------------------------
        // 5. GENERIC RAW ↔ BMP (variable bitplanes)
        // ------------------------------------------------------------------

        /// <summary>
        /// Convert an indexed BMP into planar Amiga RAW (1-8 bitplanes).
        /// Equivalent to DC_MakeAmigaRaw / DC_MakeAmigaRaw_New, generalized.
        ///
        /// The BMP MUST be indexed; palette[] is not used for extraction
        /// (indices are taken directly) but is kept for API symmetry.
        /// </summary>
        public void ConvertBmpToRaw(
            string inputBmpPath,
            uint[] palette,
            string outputRawPath,
            RawFormat format,
            int bitplanes = 4)
        {
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");

            (int width, int height) = format switch
            {
                RawFormat.Raw320x256 => (320, 256),
                RawFormat.Raw352x272 => (352, 272),
                _ => throw new ArgumentOutOfRangeException(nameof(format))
            };

            uint[] imgPal;
            byte[,] buffer = LoadIndexedImageIndices(
                inputBmpPath,
                out imgPal,
                expectedWidth: width,
                expectedHeight: height);

            int bytesPerPlanePerRow = width / 8;
            int maxIndex = (1 << bitplanes) - 1;

            using var ms = new MemoryStream();
            using var bw = new BinaryWriter(ms);

            for (int y = 0; y < height; y++)
            {
                for (int plane = 0; plane < bitplanes; plane++)
                {
                    for (int block = 0; block < bytesPerPlanePerRow; block++)
                    {
                        byte value = 0;

                        for (int bit = 0; bit < 8; bit++)
                        {
                            int x = block * 8 + bit;     // pixel x in [0..width-1]
                            int pix = buffer[y, x];      // full index (0..255)

                            if (pix > maxIndex)
                                throw new InvalidOperationException(
                                    $"Pixel index {pix} exceeds maximum {maxIndex} for {bitplanes} bitplanes.");

                            int bitVal = (pix >> plane) & 1; // plane 0 = LSB

                            value = (byte)((value << 1) | bitVal);
                        }

                        bw.Write(value);
                    }
                }
            }

            bw.Flush();
            byte[] rawBytes = ms.ToArray();

            // RNC-aware write
            AmigaRncHelper.WriteAllBytes(outputRawPath, rawBytes);
        }

        /// <summary>
        /// Convert planar Amiga RAW (1-8 bitplanes) back to an indexed BMP using the given palette.
        /// This is the inverse of ConvertBmpToRaw.
        ///
        /// RAW is read via AmigaRncHelper, so RNC-packed RAW is supported transparently.
        /// </summary>
        public void ConvertRawToBmp(
            string inputRawPath,
            uint[] palette,
            string outputBmpPath,
            RawFormat format,
            int bitplanes = 4)
        {
            if (bitplanes < 1 || bitplanes > 8)
                throw new ArgumentOutOfRangeException(nameof(bitplanes), "Bitplanes must be between 1 and 8.");

            (int width, int height) = format switch
            {
                RawFormat.Raw320x256 => (320, 256),
                RawFormat.Raw352x272 => (352, 272),
                _ => throw new ArgumentOutOfRangeException(nameof(format))
            };

            int bytesPerPlanePerRow = width / 8;
            int bytesPerRow = bytesPerPlanePerRow * bitplanes;
            int expectedLength = bytesPerRow * height;

            byte[] raw = AmigaRncHelper.ReadAllBytes(inputRawPath);
            if (raw.Length < expectedLength)
                throw new InvalidOperationException(
                    $"RAW file too small. Expected at least {expectedLength} bytes, got {raw.Length}.");

            var buffer = new byte[height, width];

            // Decode bitplanes -> index buffer
            for (int y = 0; y < height; y++)
            {
                int rowBase = y * bytesPerRow;

                for (int x = 0; x < width; x++)
                {
                    int pixelIndex = 0;

                    int byteIndexInPlane = x / 8;
                    int bitIndexInByte = 7 - (x % 8); // MSB = first pixel

                    for (int plane = 0; plane < bitplanes; plane++)
                    {
                        int planeBase = rowBase + plane * bytesPerPlanePerRow;
                        byte b = raw[planeBase + byteIndexInPlane];
                        int bit = (b >> bitIndexInByte) & 1;

                        pixelIndex |= (bit << plane); // plane0=LSB
                    }

                    buffer[y, x] = (byte)pixelIndex;
                }
            }

            Bmp8Helper.Write8bppBmpFromIndices(outputBmpPath, buffer, palette);
        }
    }
}
