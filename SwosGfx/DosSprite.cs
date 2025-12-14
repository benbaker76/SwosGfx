using System;
using System.IO;
using System.Linq;
using System.Text;

namespace SwosGfx
{
    /// <summary>
    /// DOS sprite system for SWOS.
    /// Handles loading/saving of SPRITE.DAT + *.DAT,
    /// and 8bpp BMP export/import of sprites.
    /// </summary>
    public sealed class DosSprite
    {
        public const int NumSprites = 1334;
        public const int SpriteDatSize = 5340;
        public const int NumDatFiles = 7;
        private const int SpriteStructSize = 24; // packed C struct size

        /// <summary>
        /// Sprite as used by the editor (after ChainSprite).
        /// </summary>
        public sealed class Sprite
        {
            /// <summary>Raw sprite data, as used by DrawSprite/InsertSprite (nibble-packed, 2 pixels per byte, rows bottom-up).</summary>
            public byte[] Data { get; set; } = Array.Empty<byte>();

            /// <summary>Width in pixels.</summary>
            public short Width { get; set; }

            /// <summary>Height in lines (pixels).</summary>
            public short Height { get; set; }

            /// <summary>Number of bytes / 8 in one line.</summary>
            public short WQuads { get; set; }

            /// <summary>Center X position (used in-game for centering).</summary>
            public short CenterX { get; set; }

            /// <summary>Center Y position (used in-game for centering).</summary>
            public short CenterY { get; set; }

            /// <summary>Unknown field from original struct (byte at offset 0x14).</summary>
            public byte Unknown4 { get; set; }

            /// <summary>Height / 4 (original nlines_div4).</summary>
            public byte HeightDiv4 { get; set; }

            /// <summary>Ordinal number in sprite.dat.</summary>
            public short Ordinal { get; set; }

            /// <summary>Index of the containing DAT file (0..6).</summary>
            public int DatFileIndex { get; set; }

            /// <summary>Size of Data in bytes (cached).</summary>
            public int SizeBytes { get; set; }

            /// <summary>Sprite has been modified in memory.</summary>
            public bool Changed { get; set; }
        }

        private sealed class DatFile
        {
            public string FileName = string.Empty;
            public int Entries;
            public int StartSprite;
            public int Size;       // current planned size
            public int OldSize;    // size when loaded from disk
            public byte[] Buffer = Array.Empty<byte>();
            public bool Changed;
        }

        // Static description of the 7 DAT containers (same as original C table)
        private sealed record DatSpec(string Name, int Entries, int StartSprite);

        private static readonly DatSpec[] DatSpecs =
        {
            new("CHARSET.DAT", 227,   0),
            new("SCORE.DAT",   114, 227),
            new("TEAM1.DAT",   303, 341),
            new("TEAM3.DAT",   303, 644),
            new("GOAL1.DAT",   116, 947),
            new("GOAL1.DAT",   116, 1063),
            new("BENCH.DAT",   155, 1179),
        };

        private readonly Sprite[] _sprites = new Sprite[NumSprites];
        private readonly DatFile[] _datFiles = new DatFile[NumDatFiles];

        private readonly string _rootDirectory;
        private readonly string _spriteDatPath;
        private byte[] _spriteDatOriginal = Array.Empty<byte>();

        private bool _anyChanged;

        // Optional codec hooks (ChainSprite/UnchainSprite)
        private readonly Action<Sprite>? _chainSprite;
        private readonly Action<Sprite>? _unchainSprite;

        private DosSprite(
            string rootDirectory,
            DatFile[] datFiles,
            string spriteDatPath,
            Sprite[] sprites,
            Action<Sprite>? chainSprite,
            Action<Sprite>? unchainSprite,
            byte[] spriteDatOriginal)
        {
            _rootDirectory = rootDirectory;
            _datFiles = datFiles;
            _spriteDatPath = spriteDatPath;
            _sprites = sprites;
            _chainSprite = chainSprite;
            _unchainSprite = unchainSprite;
            _spriteDatOriginal = spriteDatOriginal;
        }

        /// <summary>
        /// Access a sprite by index (0..NumSprites-1).
        /// </summary>
        public Sprite this[int index] => _sprites[index];

        /// <summary>
        /// Load SPRITE.DAT and all DAT files from a directory.
        /// Optionally provide Chain/Unchain delegates to match
        /// the original tool's encoding behaviour.
        /// </summary>
        public static DosSprite Load(
            string directoryPath,
            Action<Sprite>? chainSprite = null,
            Action<Sprite>? unchainSprite = null)
        {
            if (directoryPath == null)
                throw new ArgumentNullException(nameof(directoryPath));

            directoryPath = Path.GetFullPath(directoryPath);

            var datFiles = new DatFile[NumDatFiles];
            var sprites = new Sprite[NumSprites];

            // --- Load sprite index file (SPRITE.DAT) just for size + later rewrite ---
            string spriteDatPath = Path.Combine(directoryPath, "SPRITE.DAT");
            if (!File.Exists(spriteDatPath))
                throw new FileNotFoundException("SPRITE.DAT not found.", spriteDatPath);

            byte[] spriteIndexBytes = File.ReadAllBytes(spriteDatPath);
            if (spriteIndexBytes.Length != SpriteDatSize)
            {
                throw new InvalidDataException(
                    $"SPRITE.DAT has invalid size. Expected {SpriteDatSize}, got {spriteIndexBytes.Length}.");
            }

            // --- Load all DAT files and build in-memory sprite array ---
            for (int i = 0; i < NumDatFiles; i++)
            {
                var spec = DatSpecs[i];
                string path = Path.Combine(directoryPath, spec.Name);
                if (!File.Exists(path))
                    throw new FileNotFoundException($"DAT file not found: {spec.Name}", path);

                byte[] data = File.ReadAllBytes(path);

                var dat = new DatFile
                {
                    FileName = spec.Name,
                    Entries = spec.Entries,
                    StartSprite = spec.StartSprite,
                    Size = data.Length,
                    OldSize = data.Length,
                    Buffer = data,
                    Changed = false
                };
                datFiles[i] = dat;

                int offset = 0;
                int sprIndex = spec.StartSprite;

                for (int j = 0; j < spec.Entries; j++, sprIndex++)
                {
                    sprites[sprIndex] = ReadSpriteFromDatBuffer(
                        data,
                        ref offset,
                        globalSpriteIndex: sprIndex,
                        datFileIndex: i);
                }

                if (offset != data.Length)
                {
                    // Warn but continue; file may have some padding we don't care about
                    // (we simply ignore extra bytes).
                }
            }

            // --- Optionally "chain" (decode) all sprites as original tool does ---
            if (chainSprite != null)
            {
                foreach (var spr in sprites)
                {
                    chainSprite(spr);
                }
            }

            return new DosSprite(directoryPath, datFiles, spriteDatPath, sprites, chainSprite, unchainSprite, spriteIndexBytes);
        }

        private static Sprite ReadSpriteFromDatBuffer(
            byte[] buffer,
            ref int offset,
            int globalSpriteIndex,
            int datFileIndex)
        {
            // On-disk packed C struct:
            //  uint32 spr_data;
            //  int16  size;        // always 0 on disk
            //  int16  dat_file;    // 0 on disk
            //  uint8  changed;     // 0 on disk
            //  int8   unk1;
            //  int16  width;
            //  int16  nlines;
            //  int16  wquads;
            //  int16  center_x;
            //  int16  center_y;
            //  uint8  unk4;
            //  uint8  nlines_div4;
            //  int16  ordinal;

            if (offset + SpriteStructSize > buffer.Length)
                throw new InvalidDataException("Unexpected end of DAT file while reading sprite header.");

            // Ignore spr_data, size, dat_file, changed, unk1 here; we only need geometry + meta fields
            uint sprDataPtr = BitConverter.ToUInt32(buffer, offset);
            offset += 4;

            short sizeField = BitConverter.ToInt16(buffer, offset);
            offset += 2;

            short datFileField = BitConverter.ToInt16(buffer, offset);
            offset += 2;

            byte changedField = buffer[offset++];
            sbyte unk1 = unchecked((sbyte)buffer[offset++]);

            short width = BitConverter.ToInt16(buffer, offset);
            offset += 2;

            short nlines = BitConverter.ToInt16(buffer, offset);
            offset += 2;

            short wquads = BitConverter.ToInt16(buffer, offset);
            offset += 2;

            short centerX = BitConverter.ToInt16(buffer, offset);
            offset += 2;

            short centerY = BitConverter.ToInt16(buffer, offset);
            offset += 2;

            byte unk4 = buffer[offset++];
            byte nlinesDiv4 = buffer[offset++];

            short ordinal = BitConverter.ToInt16(buffer, offset);
            offset += 2;

            if (nlines < 0 || wquads < 0)
                throw new InvalidDataException("Negative nlines or wquads in sprite header.");

            int sizeBytes = nlines * wquads * 8;
            if (sizeBytes < 0 || offset + sizeBytes > buffer.Length)
                throw new InvalidDataException("Invalid sprite size in DAT file.");

            var data = new byte[sizeBytes];
            Buffer.BlockCopy(buffer, offset, data, 0, sizeBytes);
            offset += sizeBytes;

            return new Sprite
            {
                Data = data,
                Width = width,
                Height = nlines,
                WQuads = wquads,
                CenterX = centerX,
                CenterY = centerY,
                Unknown4 = unk4,
                HeightDiv4 = nlinesDiv4,
                Ordinal = ordinal,
                DatFileIndex = datFileIndex,
                SizeBytes = sizeBytes,
                Changed = false
            };
        }

        /// <summary>
        /// Export a single sprite as 8bpp indexed BMP.
        /// Uses 0..15 as sprite colors, 16 as "transparent" background index.
        /// The palette entries are taken from menuPal/gamePal according to
        /// the original tool's rules.
        /// </summary>
        /// <param name="spriteIndex">0..NumSprites-1</param>
        /// <param name="backgroundIndex">Menu palette index to use as color 16 in BMP.</param>
        /// <param name="menuPal">256-entry ARGB menu palette (DosPal).</param>
        /// <param name="gamePal">256-entry ARGB game palette (DosGamePal).</param>
        /// <param name="filePath">Destination BMP path.</param>
        public bool SaveSpriteToBmp(
            int spriteIndex,
            byte backgroundIndex,
            string filePath)
        {
            if (filePath == null) throw new ArgumentNullException(nameof(filePath));

            if (spriteIndex < 0 || spriteIndex >= NumSprites)
                return false;

            var spr = _sprites[spriteIndex];
            int width = spr.Width;
            int height = spr.Height;

            if (width <= 0 || height <= 0)
                return false;

            // Build top-down 8bpp pixel buffer for just this sprite (no clipping).
            var pixels = new byte[width * height];

            // Background index in the BMP is 16.
            for (int i = 0; i < pixels.Length; i++)
                pixels[i] = 16;

            int bytesPerLine = spr.WQuads * 8;

            // spr.Data rows are stored bottom-up; convert to top-down.
            for (int y = 0; y < height; y++)
            {
                int srcRow = height - 1 - y;
                int srcBase = srcRow * bytesPerLine;
                int dstBase = y * width;

                for (int x = 0; x < width; x++)
                {
                    int byteIndex = srcBase + (x >> 1);
                    byte packed = spr.Data[byteIndex];
                    byte index = (byte)((x & 1) == 0 ? (packed >> 4) : (packed & 0x0F));

                    // 0 = transparent (leave as 16)
                    if (index != 0)
                        pixels[dstBase + x] = index;
                }
            }

            // Build 256-color palette for BMP (only first 17 meaningful)
            var bmpPalette = new uint[256];

            bool isRS =
                spriteIndex > 1208 && spriteIndex < 1273;

            if (!isRS)
            {
                // First 16 colors from menu palette indices 0..15
                for (int i = 0; i < 16; i++)
                    bmpPalette[i] = DosPalette.Menu[i];
            }
            else
            {
                // Big R/S sprites: use game palette indices i | 0x70 for 0..15
                for (int i = 0; i < 16; i++)
                {
                    int idx = i | 0x70;
                    bmpPalette[i] = DosPalette.Game[idx];
                }
            }

            // Transparent/background color (slot 16) from menu palette bk index
            bmpPalette[16] = DosPalette.Menu[backgroundIndex];

            // The rest: just copy from menu palette to keep BMP "full" 256 colors.
            for (int i = 17; i < 256; i++)
                bmpPalette[i] = DosPalette.Menu[i];

            // Ensure directory exists
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(filePath)) ?? ".");

            Bitmap8Helper.Write8bppBmp(filePath, width, height, pixels, bmpPalette);
            return true;
        }

        /// <summary>
        /// Save all sprites as spr####.bmp into a directory.
        /// backgroundIndex is the menu-pal index used for color 16 (transparent).
        /// </summary>
        public void SaveAllSpritesToDirectory(
            string directoryPath,
            byte backgroundIndex)
        {
            if (directoryPath == null) throw new ArgumentNullException(nameof(directoryPath));

            Directory.CreateDirectory(directoryPath);

            for (int i = 0; i < NumSprites; i++)
            {
                string name = $"spr{i:D4}.bmp";
                string path = Path.Combine(directoryPath, name);
                SaveSpriteToBmp(i, backgroundIndex, path);
            }
        }

        /// <summary>
        /// Insert/replace a sprite from an 8bpp indexed BMP (spr####.bmp format).
        /// Transparent index is 16; it will be mapped to index 0 in packed data.
        /// This matches the original InsertSprite() logic.
        /// </summary>
        public bool InsertSpriteFromBmp(int spriteIndex, string filePath)
        {
            if (filePath == null) throw new ArgumentNullException(nameof(filePath));

            if (spriteIndex < 0 || spriteIndex >= NumSprites)
                return false;

            if (!File.Exists(filePath))
                return false;

            var spr = _sprites[spriteIndex];

            // Read 8bpp BMP as top-down indices
            int width, height;
            var pixels = Bitmap8Helper.Read8bppBmpTopDown(filePath, out width, out height);

            if (width <= 0 || height <= 0)
                return false;

            // Compute new wquads & size as original C:
            // new_width_aligned = (bmih.biWidth + 15) & ~15;
            // new_size = new_width_aligned * bmih.biHeight / 2;
            int newWidthAligned = (width + 15) & ~15;
            int newSize = newWidthAligned * height / 2; // bytes (2 pix per byte)

            // Reallocate spr.Data if size changed.
            if (height != spr.Height || spr.WQuads != (newWidthAligned / 16))
            {
                spr.Data = new byte[newSize];

                var dat = _datFiles[spr.DatFileIndex];

                int oldSize = spr.SizeBytes;
                spr.WQuads = (short)(newWidthAligned / 16);
                spr.SizeBytes = newSize;

                // Adjust DAT file planned size
                dat.OldSize = dat.Size;
                dat.Size += newSize - oldSize;
                dat.Changed = true;
            }
            else
            {
                // Same geometry as before; just overwrite data.
                Array.Clear(spr.Data, 0, spr.Data.Length);
                spr.SizeBytes = spr.Data.Length;
                _datFiles[spr.DatFileIndex].Changed = true;
            }

            spr.Width = (short)width;
            spr.Height = (short)height;
            spr.HeightDiv4 = (byte)(height / 4);
            spr.Changed = true;
            _anyChanged = true;

            int bytesPerLine = spr.WQuads * 8;

            // Fill with 0 to eliminate padding garbage
            Array.Clear(spr.Data, 0, spr.Data.Length);

            // Encode: 2 pixels per byte, rows written bottom-up
            for (int y = 0; y < height; y++)
            {
                int srcBase = y * width;
                int dstRow = height - 1 - y;                // bottom-up
                int dstBase = dstRow * bytesPerLine;

                for (int x = 0; x < width; x += 2)
                {
                    byte c1 = pixels[srcBase + x];
                    byte c2 = (x + 1 < width) ? pixels[srcBase + x + 1] : (byte)16;

                    // Transparent index 16 -> 0 in packed data
                    c1 = (byte)(c1 == 16 ? 0 : (c1 & 0x0F));
                    c2 = (byte)(c2 == 16 ? 0 : (c2 & 0x0F));

                    byte packed = (byte)((c1 << 4) | (c2 & 0x0F));
                    int dstByteIndex = dstBase + (x >> 1);
                    spr.Data[dstByteIndex] = packed;
                }
            }

            return true;
        }

        /// <summary>
        /// Insert all sprites in a directory named spr####.bmp (like original tool).
        /// Filenames must match sprNNNN.bmp (NNNN numeric).
        /// </summary>
        public void InsertAllSpritesFromDirectory(string directoryPath)
        {
            if (directoryPath == null) throw new ArgumentNullException(nameof(directoryPath));
            if (!Directory.Exists(directoryPath))
                return;

            foreach (var file in Directory.EnumerateFiles(directoryPath, "spr????.bmp"))
            {
                string name = Path.GetFileNameWithoutExtension(file);
                if (name.Length != 7 || !name.StartsWith("spr", StringComparison.OrdinalIgnoreCase))
                    continue;

                string digits = name.Substring(3, 4);
                if (!digits.All(char.IsDigit))
                    continue;

                int spriteIndex = int.Parse(digits);
                if (spriteIndex < 0 || spriteIndex >= NumSprites)
                    continue;

                InsertSpriteFromBmp(spriteIndex, file);
            }
        }

        /// <summary>
        /// Save any changed sprites back to their DAT files and update SPRITE.DAT offsets.
        /// Requires UnchainSprite/ChainSprite delegates if your data is stored
        /// in unchained form on disk, as per the original C code.
        /// </summary>
        public bool SaveChangesToSprites()
        {
            if (!_anyChanged)
                return true;

            if (_unchainSprite != null)
            {
                // Convert to on-disk representation
                foreach (var spr in _sprites)
                    _unchainSprite(spr);
            }

            // Build sprite offsets array (for SPRITE.DAT)
            // spritesOffsets[i] = byte offset of sprite i within its logical stream.
            var offsets = new uint[NumSprites + 1];

            // charset.dat (dat_files[0]) is special in original code
            {
                uint ofs = 0;
                int entries = _datFiles[0].Entries;
                for (int i = 0; i < entries; i++)
                {
                    offsets[i] = ofs;
                    ofs += (uint)(_sprites[i].SizeBytes + SpriteStructSize);
                }
            }

            // remaining dat files
            {
                uint ofs = 0;
                for (int di = 1; di < NumDatFiles; di++)
                {
                    var dat = _datFiles[di];
                    int cnt = dat.StartSprite;
                    for (int j = 0; j < dat.Entries; j++, cnt++)
                    {
                        offsets[cnt] = ofs;
                        ofs += (uint)(_sprites[cnt].SizeBytes + SpriteStructSize);
                    }
                }
            }

            // Update DAT file buffers
            BuildDatFile(0, 0);
            uint runningDataOffset = 0;
            for (int di = 1; di < NumDatFiles - 1; di++)
            {
                runningDataOffset += BuildDatFile(di, 0);
            }
            // Last DAT file gets data_ofs = sum of previous ones (as in original)
            BuildDatFile(NumDatFiles - 1, runningDataOffset);

            // Re-chain in-memory sprites for further usage
            if (_chainSprite != null)
            {
                foreach (var spr in _sprites)
                    _chainSprite(spr);
            }

            // Save DAT files
            for (int di = 0; di < NumDatFiles; di++)
            {
                var dat = _datFiles[di];
                if (!dat.Changed)
                    continue;

                string path = Path.Combine(_rootDirectory, dat.FileName);
                File.WriteAllBytes(path, dat.Buffer);
                dat.Changed = false;
                dat.OldSize = dat.Size;
            }

            // Terminate offsets array with 0 as in original
            offsets[NumSprites] = 0;

            // Save SPRITE.DAT
            using (var fs = new FileStream(_spriteDatPath, FileMode.Create, FileAccess.Write, FileShare.None))
            using (var bw = new BinaryWriter(fs, Encoding.ASCII, leaveOpen: false))
            {
                for (int i = 0; i < offsets.Length; i++)
                    bw.Write(offsets[i]);
            }

            _anyChanged = false;
            return true;
        }

        /// <summary>
        /// Revert sprites to what is currently on disk (re-load DAT files).
        /// ChainSprite delegate (if provided) is applied again.
        /// </summary>
        public void RevertSpritesToSaved()
        {
            for (int di = 0; di < NumDatFiles; di++)
            {
                var spec = DatSpecs[di];
                string path = Path.Combine(_rootDirectory, spec.Name);
                byte[] data = File.ReadAllBytes(path);

                var dat = _datFiles[di];
                dat.Buffer = data;
                dat.Size = data.Length;
                dat.OldSize = data.Length;
                dat.Changed = false;

                int offset = 0;
                int sprIndex = spec.StartSprite;
                for (int j = 0; j < spec.Entries; j++, sprIndex++)
                {
                    _sprites[sprIndex] = ReadSpriteFromDatBuffer(
                        data,
                        ref offset,
                        globalSpriteIndex: sprIndex,
                        datFileIndex: di);
                }
            }

            if (_chainSprite != null)
            {
                foreach (var spr in _sprites)
                    _chainSprite(spr);
            }

            _anyChanged = false;
        }

        // --------------------------------------------------------------------
        // Internal helpers
        // --------------------------------------------------------------------

        private uint BuildDatFile(int datIndex, uint dataOfs)
        {
            var dat = _datFiles[datIndex];

            // no need to rebuild if not changed
            if (!dat.Changed)
                return (uint)dat.Size;

            if (dat.Size != dat.OldSize)
            {
                dat.Buffer = new byte[dat.Size];
                dat.OldSize = dat.Size;
            }

            byte[] buf = dat.Buffer;
            int p = 0;
            int cnt = dat.StartSprite;

            for (int i = 0; i < dat.Entries; i++, cnt++)
            {
                var spr = _sprites[cnt];
                int size = spr.SizeBytes;

                int structOffset = p;
                int sprDataPtr = structOffset + (int)dataOfs + SpriteStructSize;

                // struct Sprite on disk:

                // spr_data (uint32)
                WriteUInt32LE(buf, ref p, (uint)sprDataPtr);
                // size (int16) -> always 0
                WriteInt16LE(buf, ref p, 0);
                // dat_file (int16) -> always 0
                WriteInt16LE(buf, ref p, 0);
                // changed (uint8) -> 0
                buf[p++] = 0;
                // unk1 (int8) -> keep 0
                buf[p++] = 0;

                // width
                WriteInt16LE(buf, ref p, spr.Width);
                // nlines
                WriteInt16LE(buf, ref p, spr.Height);
                // wquads
                WriteInt16LE(buf, ref p, spr.WQuads);
                // center_x
                WriteInt16LE(buf, ref p, spr.CenterX);
                // center_y
                WriteInt16LE(buf, ref p, spr.CenterY);
                // unk4
                buf[p++] = spr.Unknown4;
                // nlines_div4
                buf[p++] = spr.HeightDiv4;
                // ordinal
                WriteInt16LE(buf, ref p, spr.Ordinal);

                // Copy sprite pixels
                Buffer.BlockCopy(spr.Data, 0, buf, p, size);
                p += size;
            }

            if (p != dat.Size)
            {
                // If the computed size doesn't match, adjust and trim buffer
                dat.Size = p;
                if (dat.Buffer.Length != p)
                    Array.Resize(ref dat.Buffer, p);
            }

            return (uint)dat.Size;
        }

        private static void WriteUInt32LE(byte[] buffer, ref int offset, uint value)
        {
            buffer[offset++] = (byte)(value & 0xFF);
            buffer[offset++] = (byte)((value >> 8) & 0xFF);
            buffer[offset++] = (byte)((value >> 16) & 0xFF);
            buffer[offset++] = (byte)((value >> 24) & 0xFF);
        }

        private static void WriteInt16LE(byte[] buffer, ref int offset, short value)
        {
            buffer[offset++] = (byte)(value & 0xFF);
            buffer[offset++] = (byte)((value >> 8) & 0xFF);
        }

        // --------------------------------------------------------------------
        // 8bpp BMP helper (no System.Drawing; pure header IO)
        // --------------------------------------------------------------------
        private static class Bitmap8Helper
        {
            private const ushort BmpMagic = 0x4D42; // 'BM'
            private const int FileHeaderSize = 14;
            private const int InfoHeaderSize = 40;
            private const int PaletteEntries = 256;
            private const int PaletteBytes = PaletteEntries * 4; // BGRA

            /// <summary>
            /// Write a top-down 8bpp image as a BMP with a 256-color palette.
            /// </summary>
            public static void Write8bppBmp(
                string path,
                int width,
                int height,
                ReadOnlySpan<byte> pixelsTopDown,
                uint[] paletteArgb)
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
                    uint argb = paletteArgb[i];
                    byte r = (byte)((argb >> 16) & 0xFF);
                    byte g = (byte)((argb >> 8) & 0xFF);
                    byte b = (byte)(argb & 0xFF);

                    bw.Write(b);
                    bw.Write(g);
                    bw.Write(r);
                    bw.Write((byte)0); // reserved
                }

                // Pixels (bottom-up)
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
            /// Width & height are returned via out parameters.
            /// </summary>
            public static byte[] Read8bppBmpTopDown(string path, out int width, out int height)
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

                width = br.ReadInt32();
                height = br.ReadInt32();
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

                int heightAbs = height;

                // Seek to pixel data using bfOffBits
                fs.Seek(pixelOffset, SeekOrigin.Begin);

                int rowSize = ((width + 3) / 4) * 4;
                byte[] row = new byte[rowSize];
                byte[] buffer = new byte[width * heightAbs];

                for (int rowIndex = 0; rowIndex < heightAbs; rowIndex++)
                {
                    int read = br.Read(row, 0, rowSize);
                    if (read != rowSize)
                        throw new EndOfStreamException("Unexpected end of BMP file reading pixel data.");

                    int destY = heightAbs - 1 - rowIndex; // convert bottom-up to top-down
                    Buffer.BlockCopy(row, 0, buffer, destY * width, width);
                }

                height = heightAbs;
                return buffer;
            }
        }
    }
}
