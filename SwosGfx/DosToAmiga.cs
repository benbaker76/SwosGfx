using System;
using System.Collections.Generic;
using System.IO;

namespace SwosGfx
{
    /// <summary>
    /// DOS -> Amiga bank reconstruction.
    ///
    /// The Amiga version of SWOS stores graphics as packed 320x256 4-bitplane
    /// "banks". Which sprite sits at which (x, y) inside each bank is defined by the
    /// Amiga "image command streams", interpreted by ProcessImageCommandStream in
    /// the SWOS engine. The engine normally reads those scripts to EXTRACT bank
    /// pixels -> sprite slots.
    ///
    /// This class inverts that: it walks the same scripts and, for every draw
    /// opcode, PLACES the equivalent DOS sprite pixels into a fresh bank buffer at
    /// the exact source position the engine reads from. The result is a set of
    /// 320x256 banks with identical sprite layout to the originals, but sourced from
    /// the DOS graphics + DOS palette - the intermediate 8bpp images that later
    /// become 8-bitplane 256-colour Amiga AGA RAW banks.
    ///
    /// Two sprite-index conventions apply, depending on the script:
    ///   - gameplay (menuColorAndTextScript): DOS index = Amiga index + 114.
    ///   - menu / charset (introMenuImageScript, menuBGImageCmdStream): identity
    ///     (DOS index = Amiga index); Amiga >= 227 entries are font-sheet
    ///     extensions with no DOS source and are skipped.
    ///
    /// Script data and bank-load offsets are transcribed from the SWOS_C_SDL port
    /// (src/*_image_script*.inc + swos_globals.c). The opcode interpreter mirrors
    /// src/image_cmd.c exactly.
    /// </summary>
    public static class DosToAmiga
    {
        // All atlas banks are 320x256.
        private const int BankWidth = 320;
        private const int BankHeight = 256;

        // DOS sprite index = Amiga index + 114 for gameplay sprites (>= 227).
        private const int DosToAmigaOffset = 114;

        // Menu/charset (identity-mapped) DOS sprites only exist below this index.
        private const int MenuSpriteLimit = 227;

        // First Amiga sprite index of the team1-slot player block.
        private const int TeamBlockBaseIndex = 227;
        private const int TeamSpriteCount = 303;

        // Load-opcode word offsets inside MenuColorTextScript (gameplay banks),
        // matched to each bank via swos_globals.c patch table.
        private const int LoadCjcgrafs = 0;
        private const int LoadTeam1Slot = 405;   // player kits (produces CJCTEAM1/2/3)
        private const int LoadCjcteamg = 3418;
        private const int LoadCjcbits = 4547;
        private const int LoadSoccerS = 4694;    // big R/S sprites -> Game palette 0x70..0x7F
        private const int LoadCjcbench = 4889;

        // Load-opcode word offsets inside the menu/charset scripts.
        private const int LoadCharset = 0;       // in IntroScript
        private const int LoadMenus = 0;         // in MenuBgScript
        private const int LoadMenus2 = 392;      // in MenuBgScript

        private enum MapMode { GameplayPlus114, MenuIdentity }

        private sealed class Placement
        {
            public int AmigaIndex;
            public int X0, Y0;     // source pixel position inside the bank
            public int WinW, WinH; // extract window (clip target)
        }

        /// <summary>
        /// Rebuild all reconstructable banks from the DOS graphics in
        /// <paramref name="dosDir"/> and write them as 8bpp BMPs into
        /// <paramref name="outDir"/>.
        /// </summary>
        public static int ConvertBanks(string dosDir, string outDir)
        {
            if (dosDir == null) throw new ArgumentNullException(nameof(dosDir));
            if (outDir == null) throw new ArgumentNullException(nameof(outDir));

            Directory.CreateDirectory(outDir);

            var gameplay = Interpret(AmigaScripts.MenuColorTextScript);
            var intro = Interpret(AmigaScripts.IntroScript);
            var menu = Interpret(AmigaScripts.MenuBgScript);

            // Global DOS sprite model (SCORE/GOAL1/BENCH/CHARSET ranges).
            var model = DosSprite.Load(dosDir);

            // The three player-kit files, indexed by ordinal (shared layout).
            var team1 = DosSprite.LoadSpritesFromDat(Path.Combine(dosDir, "TEAM1.DAT"), TeamSpriteCount);
            var team2 = DosSprite.LoadSpritesFromDat(Path.Combine(dosDir, "TEAM2.DAT"), TeamSpriteCount);
            var team3 = DosSprite.LoadSpritesFromDat(Path.Combine(dosDir, "TEAM3.DAT"), TeamSpriteCount);

            int banks = 0;

            // ---- Gameplay banks (DOS = Amiga + 114, Game palette) ----
            banks += RenderGlobalBank(gameplay, LoadCjcgrafs, model, outDir, "CJCGRAFS", rsOffset: false, MapMode.GameplayPlus114, DosPalette.Game);
            banks += RenderGlobalBank(gameplay, LoadCjcteamg, model, outDir, "CJCTEAMG", rsOffset: false, MapMode.GameplayPlus114, DosPalette.Game);
            banks += RenderGlobalBank(gameplay, LoadCjcbits,  model, outDir, "CJCBITS",  rsOffset: false, MapMode.GameplayPlus114, DosPalette.Game);
            banks += RenderGlobalBank(gameplay, LoadSoccerS,  model, outDir, "SOCCER_S", rsOffset: true,  MapMode.GameplayPlus114, DosPalette.Game);
            banks += RenderGlobalBank(gameplay, LoadCjcbench, model, outDir, "CJCBENCH", rsOffset: false, MapMode.GameplayPlus114, DosPalette.Game);

            // All three kit banks share the team1-slot layout, differing only in
            // pixel source (TEAM1/TEAM2/TEAM3.DAT). The team2-slot block is the same
            // layout for the second on-screen team and is intentionally not emitted.
            banks += RenderTeamBank(gameplay, LoadTeam1Slot, team1, outDir, "CJCTEAM1", DosPalette.Game);
            banks += RenderTeamBank(gameplay, LoadTeam1Slot, team2, outDir, "CJCTEAM2", DosPalette.Game);
            banks += RenderTeamBank(gameplay, LoadTeam1Slot, team3, outDir, "CJCTEAM3", DosPalette.Game);

            // ---- Menu / charset banks (identity mapping, Menu palette) ----
            banks += RenderGlobalBank(intro, LoadCharset, model, outDir, "CHARSET", rsOffset: false, MapMode.MenuIdentity, DosPalette.Menu);
            banks += RenderGlobalBank(menu,  LoadMenus,   model, outDir, "MENUS",   rsOffset: false, MapMode.MenuIdentity, DosPalette.Menu);
            banks += RenderGlobalBank(menu,  LoadMenus2,  model, outDir, "MENUS2",  rsOffset: false, MapMode.MenuIdentity, DosPalette.Menu);

            if (banks == 0)
                Console.Error.WriteLine("Warning: no banks were produced (script interpretation found no placements).");
            else
                Console.WriteLine($"DOS -> Amiga: wrote {banks} banks to {outDir}");

            return 0;
        }

        // ------------------------------------------------------------------
        // Script interpreter (mirror of ProcessImageCommandStream, image_cmd.c)
        // ------------------------------------------------------------------

        private static Dictionary<int, List<Placement>> Interpret(ushort[] s)
        {
            var result = new Dictionary<int, List<Placement>>();
            int n = s.Length;
            int i = 0;
            int curLoad = -1;
            List<Placement>? curList = null;

            for (;;)
            {
                if (i >= n) break;
                int op = s[i++];
                if (op == 0xFFFF) break;

                int t = op & 0xF000;
                int spr = op & 0x0FFF;

                if (t == 0x7000 || t == 0x8000)
                {
                    curLoad = i - 1;               // index of the load opcode
                    if (!result.TryGetValue(curLoad, out curList))
                    {
                        curList = new List<Placement>();
                        result[curLoad] = curList;
                    }
                    i += 2;                         // skip filename pointer (2 words)
                    continue;
                }

                int xOff = 0, yOff = 0, w = 0, h = 0;

                switch (t)
                {
                    case 0xC000: // byte x/y, byte w/h
                        { int v = s[i++]; xOff = Hi(v); yOff = Lo(v); v = s[i++]; w = Hi(v); h = Lo(v); }
                        break;
                    case 0x1000: // word x/y, byte w/h
                        { xOff = s[i++]; yOff = s[i++]; int v = s[i++]; w = Hi(v); h = Lo(v); }
                        break;
                    case 0x5000: // word x/y, byte w/h (maskSel inline)
                        { xOff = s[i++]; yOff = s[i++]; int v = s[i++]; w = Hi(v); h = Lo(v); }
                        break;
                    case 0xD000: // byte x/y, byte w/h + byte dstX/Y
                        { int v = s[i++]; xOff = Hi(v); yOff = Lo(v); v = s[i++]; w = Hi(v); h = Lo(v); i += 1; }
                        break;
                    case 0x2000: // word x/y, byte w/h + byte dstX/Y
                        { xOff = s[i++]; yOff = s[i++]; int v = s[i++]; w = Hi(v); h = Lo(v); i += 1; }
                        break;
                    case 0xE000: // byte x/y, byte w/h + byte dstX/Y + word maskSel
                        { int v = s[i++]; xOff = Hi(v); yOff = Lo(v); v = s[i++]; w = Hi(v); h = Lo(v); i += 2; }
                        break;
                    case 0x3000: // word x/y, byte w/h + byte dstX/Y + word maskSel
                        { xOff = s[i++]; yOff = s[i++]; int v = s[i++]; w = Hi(v); h = Lo(v); i += 2; }
                        break;
                    case 0xA000: // word x/y, byte w/h + byte dstX/Y (planesFlag=1)
                        { xOff = s[i++]; yOff = s[i++]; int v = s[i++]; w = Hi(v); h = Lo(v); i += 1; }
                        break;
                    case 0xF000: // byte x/y, byte w/h + byte dstX/Y + word maskSel (planesFlag=1)
                        { int v = s[i++]; xOff = Hi(v); yOff = Lo(v); v = s[i++]; w = Hi(v); h = Lo(v); i += 2; }
                        break;
                    case 0xB000: // word x/y, byte w/h + byte dstX/Y + word maskSel (planesFlag=1)
                        { xOff = s[i++]; yOff = s[i++]; int v = s[i++]; w = Hi(v); h = Lo(v); i += 2; }
                        break;
                    case 0x9000: // draw from saved pointer (checkerboard fill, no source pixels)
                        i += 2;
                        continue;
                    default:
                        throw new InvalidDataException($"Unknown image-script opcode 0x{op:X4} at word {i - 1}.");
                }

                if (curList == null)
                    continue; // draw before any bank load (should not happen)

                int x0 = ((xOff >> 3) & 0xFFFE) * 8;   // matches blitDispatch source byte offset
                int y0 = yOff;
                int winW = (((w + 15) >> 4) * 16);     // wquads * 16
                int winH = h;

                curList.Add(new Placement { AmigaIndex = spr, X0 = x0, Y0 = y0, WinW = winW, WinH = winH });
            }

            return result;
        }

        private static int Hi(int v) => (v >> 8) & 0xFF;
        private static int Lo(int v) => v & 0xFF;

        // ------------------------------------------------------------------
        // Bank rendering
        // ------------------------------------------------------------------

        private static int RenderGlobalBank(
            Dictionary<int, List<Placement>> byLoad, int loadOffset,
            DosSprite model, string outDir, string name, bool rsOffset,
            MapMode mode, uint[] palette)
        {
            if (!byLoad.TryGetValue(loadOffset, out var places))
            {
                Console.Error.WriteLine($"Warning: bank '{name}' has no load block at offset {loadOffset}.");
                return 0;
            }

            var buf = new byte[BankWidth * BankHeight];

            foreach (var p in places)
            {
                int dosIdx;
                if (mode == MapMode.MenuIdentity)
                {
                    // Menu/charset sprites map 1:1; entries at/above the limit are
                    // font-sheet extensions with no DOS source.
                    if (p.AmigaIndex >= MenuSpriteLimit) continue;
                    dosIdx = p.AmigaIndex;
                }
                else
                {
                    dosIdx = p.AmigaIndex + DosToAmigaOffset;
                }

                if (dosIdx < 0 || dosIdx >= DosSprite.NumSprites)
                    continue;

                Blit(buf, model[dosIdx], p, rsOffset);
            }

            WriteBankBmp(Path.Combine(outDir, name + ".bmp"), buf, palette);
            return 1;
        }

        private static int RenderTeamBank(
            Dictionary<int, List<Placement>> byLoad, int loadOffset,
            DosSprite.Sprite[] team, string outDir, string name, uint[] palette)
        {
            if (!byLoad.TryGetValue(loadOffset, out var places))
            {
                Console.Error.WriteLine($"Warning: team bank '{name}' has no load block at offset {loadOffset}.");
                return 0;
            }

            var buf = new byte[BankWidth * BankHeight];

            foreach (var p in places)
            {
                int ordinal = p.AmigaIndex - TeamBlockBaseIndex;
                if (ordinal < 0 || ordinal >= team.Length)
                    continue;
                Blit(buf, team[ordinal], p, rsOffset: false);
            }

            WriteBankBmp(Path.Combine(outDir, name + ".bmp"), buf, palette);
            return 1;
        }

        private static void Blit(byte[] buf, DosSprite.Sprite? spr, Placement p, bool rsOffset)
        {
            if (spr == null) return;

            byte[] px = DosSprite.DecodeSpriteChunky(spr, out int sw, out int sh);
            if (sw <= 0 || sh <= 0) return;

            int limW = Math.Min(sw, p.WinW);
            int limH = Math.Min(sh, p.WinH);

            for (int y = 0; y < limH; y++)
            {
                int by = p.Y0 + y;
                if (by < 0 || by >= BankHeight) continue;

                int srcRow = y * sw;
                int dstRow = by * BankWidth;

                for (int x = 0; x < limW; x++)
                {
                    byte v = px[srcRow + x];
                    if (v == 0) continue;          // index 0 = transparent

                    int bx = p.X0 + x;
                    if (bx < 0 || bx >= BankWidth) continue;

                    buf[dstRow + bx] = rsOffset ? (byte)(v | 0x70) : v;
                }
            }
        }

        // ------------------------------------------------------------------
        // 8bpp BMP writer
        // ------------------------------------------------------------------

        private static void WriteBankBmp(string path, byte[] topDownPixels, uint[] palette)
        {
            const int fileHeader = 14, infoHeader = 40, paletteBytes = 256 * 4;
            int width = BankWidth, height = BankHeight;
            int rowSize = ((width + 3) / 4) * 4;      // 8bpp rows, DWORD-aligned
            int imageSize = rowSize * height;
            int pixelOffset = fileHeader + infoHeader + paletteBytes;
            int fileSize = pixelOffset + imageSize;

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)) ?? ".");

            using var fs = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None);
            using var bw = new BinaryWriter(fs);

            bw.Write((ushort)0x4D42);   // 'BM'
            bw.Write(fileSize);
            bw.Write((ushort)0);
            bw.Write((ushort)0);
            bw.Write(pixelOffset);

            bw.Write(infoHeader);
            bw.Write(width);
            bw.Write(height);           // positive = bottom-up
            bw.Write((ushort)1);
            bw.Write((ushort)8);
            bw.Write(0u);               // BI_RGB
            bw.Write(imageSize);
            bw.Write(0);
            bw.Write(0);
            bw.Write((uint)256);
            bw.Write(0u);

            for (int i = 0; i < 256; i++)
            {
                uint argb = i < palette.Length ? palette[i] : 0u;
                bw.Write((byte)(argb & 0xFF));         // B
                bw.Write((byte)((argb >> 8) & 0xFF));  // G
                bw.Write((byte)((argb >> 16) & 0xFF)); // R
                bw.Write((byte)0);                     // reserved
            }

            byte[] row = new byte[rowSize];
            for (int y = height - 1; y >= 0; y--)      // bottom-up
            {
                Buffer.BlockCopy(topDownPixels, y * width, row, 0, width);
                for (int pdx = width; pdx < rowSize; pdx++) row[pdx] = 0;
                bw.Write(row);
            }
        }
    }
}
