using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace SwosGfx
{
    /// <summary>
    /// SWOS charset font extender - C# port of SWOS_Font/swos_font.py.
    ///
    /// Adds full-ASCII + accented glyphs (en/de/it/fr) INTO the free grid cells of
    /// a CHARSET sheet. The glyph shapes come from an editable glyphs.txt
    /// ('*'=ink, '-'=blank; the 1px down-right drop shadow at palette index 8 is
    /// added automatically). New glyphs occupy the free 16x8 cells of the sheet
    /// (small font in the top half, big font in the bottom half), starting at
    /// sprite slot 1220 - identical placement to the game's font_ext_script.inc.
    ///
    /// Works on any CHARSET.bmp: feed it the DOS-sourced CHARSET.bmp from -dos2amiga
    /// for the AGA build, or an Amiga CHARSET.bmp for the OCS build. The input
    /// palette is preserved (the font uses palette indices 2=white, 8=shade in both).
    /// An integration.txt (image-script + conversionTable wiring) is also emitted so
    /// the game side can be kept in sync when glyphs.txt changes.
    /// </summary>
    public static class SwosFont
    {
        private const byte White = 2;      // font ink palette index
        private const byte Shade = 8;      // font drop-shadow palette index
        private const int BigOffset = 57;  // bigCharsTable spriteIndexOffset
        private const int SplitRow = 14;   // big font starts at row 14 (y=112)
        private const int FontExtStart = 1220; // first font-extension sprite slot
        private const int BoxFillRows = 28;    // rows 0-27 get the "missing glyph" box; 28-31 stay blank

        // The "missing glyph" marker (a box with an X inside + drop shadow) that the
        // stock Amiga charset draws in every unused cell. Extracted from CHARSET.RAW;
        // indices 2=white, 8=shade. Small = 6x6 (small font), Big = 8x8 (big font).
        private static readonly byte[][] SmallBox =
        {
            new byte[] { 2, 2, 2, 2, 2, 0 },
            new byte[] { 2, 0, 2, 0, 2, 8 },
            new byte[] { 2, 2, 0, 2, 2, 8 },
            new byte[] { 2, 0, 2, 0, 2, 8 },
            new byte[] { 2, 2, 2, 2, 2, 8 },
            new byte[] { 0, 8, 8, 8, 8, 8 },
        };
        private static readonly byte[][] BigBox =
        {
            new byte[] { 2, 2, 2, 2, 2, 2, 2, 0 },
            new byte[] { 2, 0, 2, 2, 2, 0, 2, 8 },
            new byte[] { 2, 2, 0, 2, 0, 2, 2, 8 },
            new byte[] { 2, 2, 2, 0, 2, 2, 2, 8 },
            new byte[] { 2, 2, 0, 2, 0, 2, 2, 8 },
            new byte[] { 2, 0, 2, 2, 2, 0, 2, 8 },
            new byte[] { 2, 2, 2, 2, 2, 2, 2, 8 },
            new byte[] { 0, 8, 8, 8, 8, 8, 8, 8 },
        };

        // Glyph order (verbatim from swos_font.py): punctuation, inverted !? , $, euro,
        // OE, then accents A-grave..Y-diaeresis. Non-ASCII as \u escapes so the source
        // is encoding-independent.
        private const string Order =
            "!\"#&<=>@[\\]^_`{|}~\u00A1\u00BF$\u20AC\u0152" +          // punct + inverted !? + $ euro OE
            "\u00C0\u00C1\u00C2\u00C7\u00C9\u00C8\u00CA\u00CB\u00CC\u00CD\u00CE\u00CF" + // A-grave .. I-diaeresis
            "\u00D1\u00D2\u00D3\u00D4\u00D9\u00DA\u00DB\u0178";  // N-tilde .. Y-diaeresis        // N-tilde..Y-diaer

        // Code points > 0xFF map to spare conversionTable bytes.
        private static readonly Dictionary<char, int> CpOverride = new()
        {
            ['\u0152'] = 0x8C, // OE ligature
            ['\u0178'] = 0x9E, // Y-diaeresis
            ['\u20AC'] = 0x9D, // euro
        };

        // Accented capitals already present in the stock sheet (Ae Oe Ue).
        private static readonly Dictionary<int, int> Existing = new()
        {
            [0xC4] = 53, [0xD6] = 55, [0xDC] = 54,
        };

        private sealed class Glyph
        {
            public char Char;
            public int Cp;
            public byte[][] Small = Array.Empty<byte[]>();
            public byte[][] Big = Array.Empty<byte[]>();
        }

        private struct Entry
        {
            public int Idx;
            public string Font;
            public int Cp;
            public char Char;
            public int X, Y, W, H;
        }

        // ------------------------------------------------------------------
        // Entry point
        // ------------------------------------------------------------------

        /// <summary>
        /// Read <paramref name="inBmp"/>, draw the glyphs from
        /// <paramref name="glyphsPath"/> into its free cells, and write
        /// <paramref name="outBmp"/> (palette preserved). When
        /// <paramref name="emitIntegration"/> is true, an integration.txt with the
        /// image-script + conversionTable wiring is written next to the glyphs file.
        /// </summary>
        public static int Extend(string inBmp, string outBmp, string glyphsPath, bool emitIntegration, bool fillMissing = true)
        {
            if (!File.Exists(inBmp))
                throw new FileNotFoundException("CHARSET bmp not found.", inBmp);
            if (!File.Exists(glyphsPath))
                throw new FileNotFoundException($"glyphs file not found: {glyphsPath}", glyphsPath);

            byte[,] sheet = ReadIndexedBmp(inBmp, out uint[] palette, out int width, out int height);

            var core = ParseCoreGlyphs();
            var smallRows = FreeRows(core, 0, SplitRow);
            var bigRows = FreeRows(core, SplitRow, 32);

            var (blocks, fileOrder) = ReadGlyphsTxt(glyphsPath);

            // Glyph sequence: the fixed Order first (for the glyphs present), then any
            // extra glyphs found in glyphs.txt, in file order. Missing small/big art
            // drops the glyph (that is how you "remove" one).
            var seq = new List<char>();
            foreach (char ch in Order)
                if (blocks.ContainsKey((ch, "small")) && blocks.ContainsKey((ch, "big")))
                    seq.Add(ch);
            foreach (char ch in fileOrder)
                if (Order.IndexOf(ch) < 0 && !seq.Contains(ch) &&
                    blocks.ContainsKey((ch, "small")) && blocks.ContainsKey((ch, "big")))
                    seq.Add(ch);

            var glyphs = seq.Select(ch => new Glyph
            {
                Char = ch,
                Cp = CpOverride.TryGetValue(ch, out int cp) ? cp : ch,
                Small = Finalize(blocks[(ch, "small")], "small"),
                Big = Finalize(blocks[(ch, "big")], "big"),
            }).ToList();

            var entries = Bake(sheet, width, height, glyphs, smallRows, bigRows);

            if (fillMissing)
                FillMissingBoxes(sheet, width, height);

            WriteIndexedBmp(outBmp, sheet, palette, width, height);
            Console.WriteLine($"Font: added {glyphs.Count} glyphs x2 fonts = {entries.Count} sprites -> {Path.GetFileName(outBmp)}"
                + (fillMissing ? " (+ missing-glyph boxes)" : ""));

            if (emitIntegration)
            {
                string dir = Path.GetDirectoryName(Path.GetFullPath(glyphsPath)) ?? ".";
                EmitIntegration(entries, dir);
                Console.WriteLine($"Font: wrote {Path.Combine(dir, "integration.txt")} (image-script + conversionTable wiring)");
            }

            return 0;
        }

        // ------------------------------------------------------------------
        // Core-glyph cells (from the intro image-script) + free-cell search
        // ------------------------------------------------------------------

        private static Dictionary<int, (int x, int y, int w, int h)> ParseCoreGlyphs()
        {
            // The stock charset cells are IntroScript's glyph triples between the
            // leading bank-load and the font-extension glyphs (sprite >= FontExtStart).
            var g = new Dictionary<int, (int, int, int, int)>();
            ushort[] s = AmigaScripts.IntroScript;

            int i = 0;
            while (i < s.Length && ((s[i] & 0xF000) == 0x7000 || (s[i] & 0xF000) == 0x8000))
                i += 3; // skip the bank-load opcode + its 2 pointer words

            while (i + 2 < s.Length && s[i] != 0xFFFF)
            {
                int op = s[i];
                int spr = op & 0x0FFF;
                if ((op & 0xF000) != 0xC000 || spr >= FontExtStart)
                    break; // reached the font-extension range (or a non-glyph opcode)

                int src = s[i + 1], sz = s[i + 2];
                g[spr] = (src >> 8, src & 0xFF, sz >> 8, sz & 0xFF);
                i += 3;
            }
            return g;
        }

        /// <summary>Rows in [lo,hi) whose cells (cols 0-15) hold no core glyph.</summary>
        private static List<int> FreeRows(Dictionary<int, (int x, int y, int w, int h)> core, int lo, int hi)
        {
            var occ = new HashSet<(int, int)>();
            foreach (var (x, y, _, _) in core.Values)
                occ.Add((x / 16, y / 8));

            var rows = new List<int>();
            for (int r = lo; r < hi; r++)
            {
                bool free = true;
                for (int c = 0; c < 16; c++)
                    if (occ.Contains((c, r))) { free = false; break; }
                if (free) rows.Add(r);
            }
            return rows;
        }

        // ------------------------------------------------------------------
        // glyphs.txt parsing + glyph bitmap building
        // ------------------------------------------------------------------

        private static (Dictionary<(char, string), List<string>> blocks, List<char> order)
            ReadGlyphsTxt(string path)
        {
            var blocks = new Dictionary<(char, string), List<string>>();
            var order = new List<char>();
            (char, string)? key = null;
            List<string>? rows = null;

            foreach (string line in File.ReadAllLines(path, Encoding.UTF8))
            {
                if (line.StartsWith("@"))
                {
                    if (key != null) blocks[key.Value] = rows!;
                    string s = line.Substring(1);
                    int sp = s.LastIndexOf(' ');
                    if (sp <= 0) { key = null; rows = null; continue; }
                    char ch = s.Substring(0, sp)[0];
                    string font = s.Substring(sp + 1);
                    key = (ch, font);
                    rows = new List<string>();
                    if (!order.Contains(ch)) order.Add(ch);
                }
                else if (line.Trim().Length == 0 || line.StartsWith("#"))
                {
                    if (key != null) { blocks[key.Value] = rows!; key = null; rows = null; }
                }
                else if (key != null)
                {
                    rows!.Add(line);
                }
            }
            if (key != null) blocks[key.Value] = rows!;
            return (blocks, order);
        }

        private static byte Cell(char ch) =>
            (ch == '#' || ch == '*') ? White : (ch == '+' ? Shade : (byte)0);

        private static byte[][] ParseArt(List<string> art)
        {
            int w = 0;
            foreach (string l in art) if (l.Length > w) w = l.Length;

            var outp = new byte[art.Count][];
            for (int r = 0; r < art.Count; r++)
            {
                string line = art[r];
                var row = new byte[w];
                for (int c = 0; c < w; c++)
                    row[c] = c < line.Length ? Cell(line[c]) : (byte)0;
                outp[r] = row;
            }
            return outp;
        }

        /// <summary>Paint the 1px down-right drop shadow (index 8) under each white pixel.</summary>
        private static byte[][] FillShadow(byte[][] bmp)
        {
            int h = bmp.Length;
            int w = h > 0 ? bmp[0].Length : 0;
            var o = new byte[h][];
            for (int r = 0; r < h; r++) o[r] = (byte[])bmp[r].Clone();

            for (int r = 0; r < h; r++)
                for (int c = 0; c < w; c++)
                    if (bmp[r][c] == White && r + 1 < h && c + 1 < w && o[r + 1][c + 1] == 0)
                        o[r + 1][c + 1] = Shade;
            return o;
        }

        /// <summary>8x8 white art -> sheet bitmap: keep font height (6 small / 8 big),
        /// add the shadow, trim to the natural glyph width.</summary>
        private static byte[][] Finalize(List<string> art8, string font)
        {
            int hRows = font == "small" ? 6 : 8;
            var sub = art8.Take(hRows).ToList();
            if (sub.Count == 0) sub.Add("");
            var bmp = FillShadow(ParseArt(sub));

            int width = 1;
            foreach (var r in bmp)
                for (int c = 0; c < r.Length; c++)
                    if (r[c] != 0 && c + 1 > width) width = c + 1;

            var res = new byte[bmp.Length][];
            for (int r = 0; r < bmp.Length; r++)
            {
                int take = Math.Min(width, bmp[r].Length);
                var row = new byte[take];
                Array.Copy(bmp[r], row, take);
                res[r] = row;
            }
            return res;
        }

        // ------------------------------------------------------------------
        // Placement into free cells
        // ------------------------------------------------------------------

        private static List<Entry> Bake(
            byte[,] sheet, int sheetW, int sheetH,
            List<Glyph> glyphs, List<int> smallRows, List<int> bigRows)
        {
            var entries = new List<Entry>();
            int nextIdx = FontExtStart;

            void Lay(string font, List<int> rows)
            {
                int ri = 0, col = 0;
                foreach (var g in glyphs)
                {
                    byte[][] bmp = font == "small" ? g.Small : g.Big;
                    int w = bmp.Length > 0 ? bmp[0].Length : 0;
                    int h = bmp.Length;

                    if (col >= 16) { col = 0; ri++; }
                    if (ri >= rows.Count)
                        throw new InvalidOperationException(
                            $"Not enough free {font} rows in the charset for {glyphs.Count} glyphs.");

                    int r = rows[ri];
                    int x = col * 16, y = r * 8;

                    // Clear this whole 16x8 cell (removes any 'X' filler), then draw.
                    for (int cc = 0; cc < 16; cc++)
                        for (int rr = 0; rr < 8; rr++)
                            if (y + rr < sheetH && x + cc < sheetW)
                                sheet[y + rr, x + cc] = 0;

                    for (int rr = 0; rr < h; rr++)
                        for (int cc = 0; cc < w; cc++)
                            if (bmp[rr][cc] != 0 && y + rr < sheetH && x + cc < sheetW)
                                sheet[y + rr, x + cc] = bmp[rr][cc];

                    entries.Add(new Entry { Idx = nextIdx, Font = font, Cp = g.Cp, Char = g.Char, X = x, Y = y, W = w, H = h });
                    nextIdx++;
                    col++;
                }
            }

            Lay("small", smallRows);
            Lay("big", bigRows);
            return entries;
        }

        /// <summary>
        /// Draw the "missing glyph" box into every empty cell of rows 0-27 (small box in
        /// the small-font half, big box in the big-font half), matching the stock Amiga
        /// charset. Cells that already hold a glyph (or an existing box, e.g. an OCS
        /// charset) are non-empty and left untouched. Rows 28-31 stay blank.
        /// </summary>
        private static void FillMissingBoxes(byte[,] sheet, int sheetW, int sheetH)
        {
            for (int row = 0; row < BoxFillRows; row++)
            {
                byte[][] box = row < SplitRow ? SmallBox : BigBox;
                for (int col = 0; col < 16; col++)
                {
                    int x = col * 16, y = row * 8;
                    if (x + 16 > sheetW || y + 8 > sheetH) continue;

                    bool empty = true;
                    for (int rr = 0; rr < 8 && empty; rr++)
                        for (int cc = 0; cc < 16; cc++)
                            if (sheet[y + rr, x + cc] != 0) { empty = false; break; }
                    if (!empty) continue;

                    for (int rr = 0; rr < box.Length; rr++)
                        for (int cc = 0; cc < box[rr].Length; cc++)
                            if (box[rr][cc] != 0)
                                sheet[y + rr, x + cc] = box[rr][cc];
                }
            }
        }

        // ------------------------------------------------------------------
        // integration.txt (image-script + conversionTable wiring)
        // ------------------------------------------------------------------

        private static void EmitIntegration(List<Entry> entries, string dir)
        {
            var lines = new List<string>
            {
                "# image-script entries to append (before 0xFFFF):"
            };
            foreach (var e in entries)
                lines.Add($"0x{(0xC000 | e.Idx):X4}, 0x{((e.X << 8) | e.Y):X4}, 0x{((e.W << 8) | e.H):X4},   " +
                          $"# '{e.Char}' {e.Font}  sprite {e.Idx} @ ({e.X},{e.Y}) {e.W}x{e.H}");

            lines.Add("");
            lines.Add("# conversionTable updates (idx = codepoint-0x20):");
            var smallMap = new Dictionary<int, int>();
            var bigMap = new Dictionary<int, int>();
            foreach (var e in entries)
                (e.Font == "small" ? smallMap : bigMap)[e.Cp] = e.Idx;

            var cps = new SortedSet<int>(smallMap.Keys);
            foreach (int k in Existing.Keys) cps.Add(k);
            foreach (int cp in cps)
            {
                int s = smallMap.TryGetValue(cp, out int sv) ? sv : Existing[cp];
                string bigConv = bigMap.TryGetValue(cp, out int bv)
                    ? (bv - BigOffset).ToString()
                    : (Existing.TryGetValue(cp, out int ev) ? ev.ToString() : "None");
                lines.Add($"small.conv[0x{cp - 0x20:X2}]={s,-4} big.conv[0x{cp - 0x20:X2}]={bigConv}   # U+{cp:X4} '{(char)cp}'");
            }

            lines.Add("");
            lines.Add("# lowercase a-z -> uppercase glyph (both fonts, conv only):");
            for (char ch = 'a'; ch <= 'z'; ch++)
            {
                int up = 18 + (char.ToUpperInvariant(ch) - 'A');
                lines.Add($"conv[0x{ch - 0x20:X2}]={up,-4} # '{ch}' -> '{char.ToUpperInvariant(ch)}'");
            }

            File.WriteAllText(Path.Combine(dir, "integration.txt"), string.Join("\n", lines) + "\n");
        }

        // ------------------------------------------------------------------
        // Indexed BMP read/write (palette preserved)
        // ------------------------------------------------------------------

        private static byte[,] ReadIndexedBmp(string path, out uint[] palette, out int width, out int height)
        {
            using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
            using var br = new BinaryReader(fs);

            if (br.ReadUInt16() != 0x4D42)
                throw new InvalidDataException("Not a BMP file (missing 'BM').");
            br.ReadUInt32();              // file size
            br.ReadUInt16(); br.ReadUInt16();
            uint pixelOffset = br.ReadUInt32();

            uint infoSize = br.ReadUInt32();
            width = br.ReadInt32();
            height = br.ReadInt32();
            br.ReadUInt16();              // planes
            ushort bitCount = br.ReadUInt16();
            uint compression = br.ReadUInt32();
            br.ReadUInt32();             // sizeImage
            br.ReadInt32(); br.ReadInt32();
            uint clrUsed = br.ReadUInt32();
            br.ReadUInt32();             // clrImportant

            if (bitCount != 4 && bitCount != 8)
                throw new InvalidDataException("Only 4bpp or 8bpp indexed BMPs are supported.");
            if (compression != 0)
                throw new InvalidDataException("Compressed BMPs are not supported.");
            if (height <= 0)
                throw new InvalidDataException("Top-down BMPs are not supported here.");

            int paletteCount = clrUsed != 0 ? (int)Math.Min(clrUsed, 256u) : (bitCount == 4 ? 16 : 256);
            palette = new uint[256];
            fs.Seek(14 + infoSize, SeekOrigin.Begin);
            for (int i = 0; i < paletteCount; i++)
            {
                byte b = br.ReadByte(), g = br.ReadByte(), r = br.ReadByte();
                br.ReadByte();
                palette[i] = (uint)((r << 16) | (g << 8) | b);
            }

            fs.Seek(pixelOffset, SeekOrigin.Begin);
            int rowSize = ((width * bitCount + 31) / 32) * 4;
            var row = new byte[rowSize];
            var indices = new byte[height, width];

            for (int rowIndex = 0; rowIndex < height; rowIndex++)
            {
                if (br.Read(row, 0, rowSize) != rowSize)
                    throw new EndOfStreamException("Unexpected end of BMP pixel data.");
                int destY = height - 1 - rowIndex; // bottom-up -> top-down
                if (bitCount == 8)
                {
                    for (int x = 0; x < width; x++) indices[destY, x] = row[x];
                }
                else
                {
                    for (int x = 0; x < width; x++)
                    {
                        byte packed = row[x / 2];
                        indices[destY, x] = (x % 2 == 0) ? (byte)((packed >> 4) & 0x0F) : (byte)(packed & 0x0F);
                    }
                }
            }
            return indices;
        }

        private static void WriteIndexedBmp(string path, byte[,] indices, uint[] palette, int width, int height)
        {
            const int fileHeader = 14, infoHeader = 40, paletteBytes = 256 * 4;
            int rowSize = ((width + 3) / 4) * 4;
            int imageSize = rowSize * height;
            int pixelOffset = fileHeader + infoHeader + paletteBytes;

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".");
            using var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None);
            using var bw = new BinaryWriter(fs);

            bw.Write((ushort)0x4D42);
            bw.Write(pixelOffset + imageSize);
            bw.Write((ushort)0); bw.Write((ushort)0);
            bw.Write(pixelOffset);

            bw.Write(infoHeader);
            bw.Write(width);
            bw.Write(height);
            bw.Write((ushort)1);
            bw.Write((ushort)8);
            bw.Write(0u);
            bw.Write(imageSize);
            bw.Write(0); bw.Write(0);
            bw.Write((uint)256);
            bw.Write(0u);

            for (int i = 0; i < 256; i++)
            {
                uint argb = i < palette.Length ? palette[i] : 0u;
                bw.Write((byte)(argb & 0xFF));
                bw.Write((byte)((argb >> 8) & 0xFF));
                bw.Write((byte)((argb >> 16) & 0xFF));
                bw.Write((byte)0);
            }

            var row = new byte[rowSize];
            for (int y = height - 1; y >= 0; y--)
            {
                for (int x = 0; x < width; x++) row[x] = indices[y, x];
                for (int p = width; p < rowSize; p++) row[p] = 0;
                bw.Write(row);
            }
        }


    }
}
