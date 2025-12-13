using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;

namespace SwosGfx
{
    public enum DosPictureError
    {
        None,           // PIC_NO_ERROR
        File,           // PIC_ERROR_FILE
        Reading,        // PIC_ERROR_READING
        NoMemory,       // PIC_ERROR_NO_MEMORY
        InvalidSize     // PIC_ERROR_INV_SIZE
    }

    /// <summary>
    /// Represents a SWOS DOS picture (.256):
    ///  - 320x200 8bpp indexed pixels (64000 bytes)
    ///  - 256-color VGA palette (768 bytes, originally 0..63 then scaled to 0..255)
    /// </summary>
    public sealed class DosPicture
    {
        public const int Width = 320;
        public const int Height = 200;
        public const int PixelDataSize = Width * Height; // 64000
        public const int PaletteByteSize = 256 * 3;      // 768
        public const int TotalFileSize = PixelDataSize + PaletteByteSize; // 64768

        /// <summary>Source filename (just name or full path, up to you).</summary>
        public string FileName { get; }

        /// <summary>
        /// Raw 8bpp index data, length = 320x200 = 64000.
        /// Stored row-major, top-to-bottom, left-to-right.
        /// </summary>
        public byte[] Pixels { get; private set; }

        /// <summary>
        /// Palette entries in 0..255 RGB, length = 256.
        /// </summary>
        public Color[] Palette { get; private set; }

        /// <summary>
        /// Index (0..255) of brightest color used for text overlays.
        /// </summary>
        public int TextColorIndex { get; private set; } = -1;

        /// <summary>Error status (if any) during load.</summary>
        public DosPictureError Error { get; private set; } = DosPictureError.None;

        public bool IsLoaded =>
            Error == DosPictureError.None &&
            Pixels != null &&
            Palette != null;

        private DosPicture(string fileName)
        {
            FileName = fileName ?? throw new ArgumentNullException(nameof(fileName));
        }

        /// <summary>
        /// Load a SWOS .256 picture from disk.
        /// On success, Error == None and Pixels/Palette are populated.
        /// On failure, Error is set and an object is still returned.
        /// </summary>
        public static DosPicture Load(string path)
        {
            if (path == null) throw new ArgumentNullException(nameof(path));

            var pic = new DosPicture(Path.GetFileName(path));

            if (!File.Exists(path))
            {
                pic.Error = DosPictureError.File;
                return pic;
            }

            byte[] buffer;
            try
            {
                buffer = File.ReadAllBytes(path);
            }
            catch
            {
                pic.Error = DosPictureError.Reading;
                return pic;
            }

            if (buffer.Length != TotalFileSize)
            {
                pic.Error = DosPictureError.InvalidSize;
                return pic;
            }

            // Split pixels + palette
            var pixels = new byte[PixelDataSize];
            Buffer.BlockCopy(buffer, 0, pixels, 0, PixelDataSize);

            // Palette in file is 0..63, we scale to 0..255 (×4)
            var palette = new Color[256];
            int paletteOffset = PixelDataSize;
            for (int i = 0; i < 256; i++)
            {
                byte r6 = buffer[paletteOffset + i * 3 + 0];
                byte g6 = buffer[paletteOffset + i * 3 + 1];
                byte b6 = buffer[paletteOffset + i * 3 + 2];

                int r = r6 * 4;
                int g = g6 * 4;
                int b = b6 * 4;

                palette[i] = Color.FromArgb(255, r, g, b);
            }

            pic.Pixels = pixels;
            pic.Palette = palette;

            // Determine text color (brightest), or replace least-used color with white
            pic.TextColorIndex = pic.ComputeTextColorIndex();

            pic.Error = DosPictureError.None;
            return pic;
        }

        /// <summary>
        /// Save this picture as an 8bpp indexed BMP file
        /// using its internal palette.
        /// </summary>
        public void SaveAsBmp(string path)
        {
            if (!IsLoaded)
                throw new InvalidOperationException("Cannot save; picture not loaded or has an error.");

            if (Palette == null || Palette.Length != 256)
                throw new InvalidOperationException("Invalid or missing palette.");

            // Prepare vertically flipped pixel data (BMP is bottom-up)
            byte[] flipped = new byte[PixelDataSize];
            for (int y = 0; y < Height; y++)
            {
                int srcRow = (Height - 1 - y) * Width;
                int dstRow = y * Width;
                Buffer.BlockCopy(Pixels, srcRow, flipped, dstRow, Width);
            }

            using var fs = File.Create(path);
            using var bw = new BinaryWriter(fs);

            // --- BITMAPFILEHEADER (14 bytes) ---
            // WORD bfType = 'BM'
            bw.Write((ushort)0x4D42);

            int headerSize = 14 + 40 + 256 * 4;
            int pixelBytes = PixelDataSize; // 320 * 200, already 4-byte aligned per row
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
            bw.Write(Width);              // biWidth
            bw.Write(Height);             // biHeight (positive -> bottom-up)
            bw.Write((ushort)1);          // biPlanes
            bw.Write((ushort)8);          // biBitCount
            bw.Write(0);                  // biCompression = BI_RGB
            bw.Write(pixelBytes);         // biSizeImage
            bw.Write(0);                  // biXPelsPerMeter
            bw.Write(0);                  // biYPelsPerMeter
            bw.Write(0);                  // biClrUsed
            bw.Write(0);                  // biClrImportant

            // --- Color table (256 * 4 bytes, BGR0) ---
            for (int i = 0; i < 256; i++)
            {
                var c = Palette[i];
                bw.Write(c.B);
                bw.Write(c.G);
                bw.Write(c.R);
                bw.Write((byte)0); // reserved
            }

            // --- Pixel data ---
            bw.Write(flipped);
        }

        // ---------------------------------------------------------------------
        // Internal helpers
        // ---------------------------------------------------------------------

        private int ComputeTextColorIndex()
        {
            if (Palette == null || Pixels == null)
                return -1;

            const int DeltaThreshold = 381;

            // 1) Find brightest color using Euclidean length sqrt(r^2+g^2+b^2)
            int maxColor = 0;
            int maxDelta = 0;

            for (int i = 0; i < 256; i++)
            {
                var c = Palette[i];
                int r = c.R;
                int g = c.G;
                int b = c.B;

                int delta = (int)Math.Sqrt(r * r + g * g + b * b);
                if (delta >= maxDelta)
                {
                    maxDelta = delta;
                    maxColor = i;
                }
            }

            // 2) If not bright enough, replace least-used color with white and use that
            if (maxDelta < DeltaThreshold)
            {
                var usage = new int[256];
                for (int i = 0; i < Pixels.Length; i++)
                    usage[Pixels[i]]++;

                int minUsedIndex = 0;
                for (int i = 1; i < 256; i++)
                {
                    if (usage[i] < usage[minUsedIndex])
                        minUsedIndex = i;
                }

                Palette[minUsedIndex] = Color.FromArgb(255, 255, 255, 255);
                maxColor = minUsedIndex;
            }

            return maxColor;
        }
    }
}
