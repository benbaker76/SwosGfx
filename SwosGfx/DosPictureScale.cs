using System;
using System.Drawing;
using System.IO;

namespace SwosGfx
{
    /// <summary>
    /// Bilinear enlarge of a DOS 320x200 .256 picture to the Amiga full-screen
    /// geometry (default 352x272) for the two full-screen backdrops that come from
    /// a DOS picture rather than a sprite bank:  SWTITLE -> MENUBG, STAD -> MENUBG2.
    ///
    /// The DOS pictures are 320x200; the Amiga full-screen RAW is 352x272, so they
    /// must be ENLARGED. Bilinear smooths the enlargement; because the output has to
    /// stay 8bpp indexed (that's what the -bmp -output=raw AGA encoder consumes), the
    /// interpolated RGB is re-quantised back to the picture's own 256-colour palette
    /// via nearest-colour. Sprite/menu/charset banks are NOT handled here (DosToAmiga
    /// already draws those into the Amiga bank layout at native size).
    /// </summary>
    public static class DosPictureScale
    {
        /// Amiga full-screen picture geometry (PLANE_STRIDE 44 * 8 px = 352 wide).
        public const int AmigaWidth = 352;
        public const int AmigaHeight = 272;

        /// <summary>
        /// Bilinear-enlarge indexed pixels (srcW x srcH, index -> palette) to
        /// dstW x dstH, re-quantised to the SAME palette. Returns new indices.
        /// </summary>
        public static byte[] ScaleBilinear(byte[] src, int srcW, int srcH, Color[] pal,
                                           int dstW, int dstH)
        {
            if (src == null) throw new ArgumentNullException(nameof(src));
            if (pal == null || pal.Length < 256) throw new ArgumentException("palette must have 256 entries", nameof(pal));

            // Palette RGB as flat ints for a fast nearest-colour search.
            int[] pr = new int[256], pg = new int[256], pb = new int[256];
            for (int i = 0; i < 256; i++) { pr[i] = pal[i].R; pg[i] = pal[i].G; pb[i] = pal[i].B; }

            byte[] dst = new byte[dstW * dstH];

            for (int y = 0; y < dstH; y++)
            {
                // Pixel-centre-aligned source coordinate (standard bilinear map).
                double syf = (y + 0.5) * srcH / dstH - 0.5;
                int y0 = (int)Math.Floor(syf);
                double fy = syf - y0;
                int y0c = Clamp(y0, 0, srcH - 1);
                int y1c = Clamp(y0 + 1, 0, srcH - 1);

                for (int x = 0; x < dstW; x++)
                {
                    double sxf = (x + 0.5) * srcW / dstW - 0.5;
                    int x0 = (int)Math.Floor(sxf);
                    double fx = sxf - x0;
                    int x0c = Clamp(x0, 0, srcW - 1);
                    int x1c = Clamp(x0 + 1, 0, srcW - 1);

                    Color c00 = pal[src[y0c * srcW + x0c]];
                    Color c01 = pal[src[y0c * srcW + x1c]];
                    Color c10 = pal[src[y1c * srcW + x0c]];
                    Color c11 = pal[src[y1c * srcW + x1c]];

                    int r = (int)Math.Round(Bilerp(c00.R, c01.R, c10.R, c11.R, fx, fy));
                    int g = (int)Math.Round(Bilerp(c00.G, c01.G, c10.G, c11.G, fx, fy));
                    int b = (int)Math.Round(Bilerp(c00.B, c01.B, c10.B, c11.B, fx, fy));

                    dst[y * dstW + x] = (byte)NearestIndex(r, g, b, pr, pg, pb);
                }
            }

            return dst;
        }

        /// <summary>Write an 8bpp indexed BMP (bottom-up), the format the RAW encoder reads.</summary>
        public static void SaveIndexedBmp(string path, byte[] indices, int w, int h, Color[] pal)
        {
            int rowSize = ((w + 3) / 4) * 4;            // 8bpp rows are DWORD-aligned
            int imageSize = rowSize * h;
            int headerSize = 14 + 40 + 256 * 4;
            int fileSize = headerSize + imageSize;

            string? parent = Path.GetDirectoryName(Path.GetFullPath(path));
            if (!string.IsNullOrEmpty(parent)) Directory.CreateDirectory(parent);

            using var fs = File.Create(path);
            using var bw = new BinaryWriter(fs);

            bw.Write((ushort)0x4D42);   // 'BM'
            bw.Write(fileSize);
            bw.Write((ushort)0);
            bw.Write((ushort)0);
            bw.Write(headerSize);

            bw.Write(40);               // biSize
            bw.Write(w);                // biWidth
            bw.Write(h);                // biHeight (positive -> bottom-up)
            bw.Write((ushort)1);        // biPlanes
            bw.Write((ushort)8);        // biBitCount
            bw.Write(0);                // BI_RGB
            bw.Write(imageSize);
            bw.Write(0);
            bw.Write(0);
            bw.Write(0);
            bw.Write(0);

            for (int i = 0; i < 256; i++)
            {
                Color c = pal[i];
                bw.Write(c.B);
                bw.Write(c.G);
                bw.Write(c.R);
                bw.Write((byte)0);
            }

            byte[] row = new byte[rowSize];
            for (int y = h - 1; y >= 0; y--)
            {
                Buffer.BlockCopy(indices, y * w, row, 0, w);
                for (int p = w; p < rowSize; p++) row[p] = 0;
                bw.Write(row);
            }
        }

        private static int Clamp(int v, int lo, int hi) => v < lo ? lo : (v > hi ? hi : v);

        private static double Bilerp(int c00, int c01, int c10, int c11, double fx, double fy)
        {
            double top = c00 + (c01 - c00) * fx;
            double bot = c10 + (c11 - c10) * fx;
            return top + (bot - top) * fy;
        }

        private static int NearestIndex(int r, int g, int b, int[] pr, int[] pg, int[] pb)
        {
            int best = 0;
            long bestD = long.MaxValue;
            for (int i = 0; i < 256; i++)
            {
                long dr = r - pr[i], dg = g - pg[i], db = b - pb[i];
                long d = dr * dr + dg * dg + db * db;
                if (d < bestD) { bestD = d; best = i; if (d == 0) break; }
            }
            return best;
        }
    }
}
