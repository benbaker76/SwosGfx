using System;

namespace SwosGfx
{
    /// <summary>
    /// Core DOS sprite codec:
    /// - ChainSprite:  internal SWOS planar/"chained" format -> linear chunky bytes.
    /// - UnchainSprite: chunky -> internal SWOS planar/"chained" format.
    /// - DrawSprite: nibble-based blitter (4bpp src -> 8bpp dst).
    ///
    /// This is a direct C# translation of the C helpers you posted.
    /// </summary>
    public static class DosSpriteCodec
    {
        /// <summary>
        /// As in the C code: 256 - 8 = 248.
        /// This is the max bytes per line used by the temp buffer.
        /// </summary>
        private const int LineBufferSize = 256 - 8; // 248

        // --------------------------------------------------------------------
        // rcl8 / get_bits_step / put_bits_step
        // --------------------------------------------------------------------

        private static void Rcl8(ref byte v, ref int cf)
        {
            int newCf = (v >> 7) & 1;                 // old MSB -> CF
            v = (byte)((v << 1) | (cf & 1));         // old CF -> bit 0
            cf = newCf;
        }

        /// <summary>
        /// GET bits from BL/BH/CL/CH into AL (GetBits macro).
        /// Matches get_bits_step() from the C code.
        /// </summary>
        private static void GetBitsStep(
            ref byte al,
            ref byte bl,
            ref byte bh,
            ref byte cl,
            ref byte ch,
            ref int cf)
        {
            // rcl bl,1; rcl al,1; rcl bh,1; rcl al,1;
            // rcl cl,1; rcl al,1; rcl ch,1; rcl al,1
            Rcl8(ref bl, ref cf);
            Rcl8(ref al, ref cf);
            Rcl8(ref bh, ref cf);
            Rcl8(ref al, ref cf);
            Rcl8(ref cl, ref cf);
            Rcl8(ref al, ref cf);
            Rcl8(ref ch, ref cf);
            Rcl8(ref al, ref cf);
        }

        /// <summary>
        /// PUT bits from AL into BL/BH/CL/CH (PutBits macro).
        /// Matches put_bits_step() from the C code.
        /// </summary>
        private static void PutBitsStep(
            ref byte al,
            ref byte bl,
            ref byte bh,
            ref byte cl,
            ref byte ch,
            ref int cf)
        {
            // rcl al,1; rcl bl,1; rcl al,1; rcl bh,1;
            // rcl al,1; rcl cl,1; rcl al,1; rcl ch,1
            Rcl8(ref al, ref cf);
            Rcl8(ref bl, ref cf);
            Rcl8(ref al, ref cf);
            Rcl8(ref bh, ref cf);
            Rcl8(ref al, ref cf);
            Rcl8(ref cl, ref cf);
            Rcl8(ref al, ref cf);
            Rcl8(ref ch, ref cf);
        }

        // --------------------------------------------------------------------
        // Little-endian 16-bit access
        // --------------------------------------------------------------------

        private static ushort ReadLe16(byte[] data, int offset)
        {
            return (ushort)(data[offset] | (data[offset + 1] << 8));
        }

        private static void WriteLe16(byte[] data, int offset, ushort value)
        {
            data[offset] = (byte)(value & 0xFF);
            data[offset + 1] = (byte)(value >> 8);
        }

        // --------------------------------------------------------------------
        // decode_quad / encode_quad
        // --------------------------------------------------------------------

        /// <summary>
        /// Decode one quad: 8 bytes of planar/chain data -> 8 "chunky" bytes.
        /// Matches decode_quad() (GetBits/STOSB sequence) from the C code.
        /// </summary>
        private static void DecodeQuad(byte[] chain, byte[] dst, int dstOffset)
        {
            byte bl, bh, cl, ch, al;
            int cf = 0; // carry flag

            // First half
            ch = chain[0];
            cl = chain[2];
            bh = chain[4];
            bl = chain[6];
            al = 0;

            for (int i = 0; i < 4; ++i)
            {
                GetBitsStep(ref al, ref bl, ref bh, ref cl, ref ch, ref cf);
                GetBitsStep(ref al, ref bl, ref bh, ref cl, ref ch, ref cf);
                dst[dstOffset++] = al;
            }

            // Second half - reuse same CF
            ch = chain[1];
            cl = chain[3];
            bh = chain[5];
            bl = chain[7];
            al = 0;

            for (int i = 0; i < 4; ++i)
            {
                GetBitsStep(ref al, ref bl, ref bh, ref cl, ref ch, ref cf);
                GetBitsStep(ref al, ref bl, ref bh, ref cl, ref ch, ref cf);
                dst[dstOffset++] = al;
            }
        }

        /// <summary>
        /// Encode one quad: 8 chunky bytes -> 8 bytes planar/chain.
        /// Matches encode_quad() (PutBits/mov [edi+X],reg) from the C code.
        /// </summary>
        private static void EncodeQuad(byte[] src, int srcOffset, byte[] chain)
        {
            byte bl = 0, bh = 0, cl = 0, ch = 0, al;
            int cf = 0; // carry flag shared across halves

            // first 4 bytes
            for (int i = 0; i < 4; ++i)
            {
                al = src[srcOffset + i];
                PutBitsStep(ref al, ref bl, ref bh, ref cl, ref ch, ref cf);
                PutBitsStep(ref al, ref bl, ref bh, ref cl, ref ch, ref cf);
            }

            chain[0] = ch;
            chain[2] = cl;
            chain[4] = bh;
            chain[6] = bl;

            // next 4 bytes
            for (int i = 4; i < 8; ++i)
            {
                al = src[srcOffset + i];
                PutBitsStep(ref al, ref bl, ref bh, ref cl, ref ch, ref cf);
                PutBitsStep(ref al, ref bl, ref bh, ref cl, ref ch, ref cf);
            }

            chain[1] = ch;
            chain[3] = cl;
            chain[5] = bh;
            chain[7] = bl;
        }

        // --------------------------------------------------------------------
        // ChainSprite / UnchainSprite
        // --------------------------------------------------------------------

        /// <summary>
        /// ChainSprite: decode SWOS sprite internal format into linear/"unchained"
        /// chunky layout.
        ///
        /// This is a C# translation of:
        ///     void ChainSprite(Sprite *spr)
        ///
        /// data:    sprite pixel buffer (in-place transform)
        /// nLines:  spr->nlines  (height)
        /// wQuads:  spr->wquads  (width unit from sprite header)
        /// </summary>
        public static void ChainSprite(byte[] data, int nLines, int wQuads)
        {
            if (data == null)
                throw new ArgumentNullException(nameof(data));
            if (nLines <= 0 || wQuads <= 0)
                return;

            int bytesPerLine = wQuads << 3;    // wquads * 8
            int units4 = wQuads << 1;    // wquads * 2
            int numQuads = units4 >> 1;    // (wquads*2)/2 == wquads

            if (bytesPerLine > LineBufferSize)
                throw new ArgumentException("bytes_per_line exceeds LINE_BUFFER_SIZE");

            var lineBuffer = new byte[LineBufferSize];
            var chainBuffer = new byte[8];

            int dataOffset = 0;

            for (int line = 0; line < nLines; ++line)
            {
                int srcLineOffset = dataOffset;
                int dstOffset = 0;

                // process all quads in this line
                for (int q = 0; q < numQuads; ++q)
                {
                    int p = srcLineOffset + q * 2;

                    ushort w0 = ReadLe16(data, p + units4 * 0);
                    ushort w1 = ReadLe16(data, p + units4 * 1);
                    ushort w2 = ReadLe16(data, p + units4 * 2);
                    ushort w3 = ReadLe16(data, p + units4 * 3);

                    // match chain_buffer packing
                    chainBuffer[0] = (byte)(w0 & 0xFF);
                    chainBuffer[1] = (byte)(w0 >> 8);
                    chainBuffer[2] = (byte)(w1 & 0xFF);
                    chainBuffer[3] = (byte)(w1 >> 8);
                    chainBuffer[4] = (byte)(w2 & 0xFF);
                    chainBuffer[5] = (byte)(w2 >> 8);
                    chainBuffer[6] = (byte)(w3 & 0xFF);
                    chainBuffer[7] = (byte)(w3 >> 8);

                    DecodeQuad(chainBuffer, lineBuffer, dstOffset);
                    dstOffset += 8;
                }

                // copy decoded line back into sprite buffer
                Buffer.BlockCopy(lineBuffer, 0, data, srcLineOffset, bytesPerLine);

                dataOffset += bytesPerLine; // next line
            }
        }

        /// <summary>
        /// UnchainSprite: encode from linear chunky layout back into SWOS internal
        /// planar/"chained" format.
        ///
        /// C# translation of:
        ///     void UnchainSprite(Sprite *spr)
        /// </summary>
        public static void UnchainSprite(byte[] data, int nLines, int wQuads)
        {
            if (data == null)
                throw new ArgumentNullException(nameof(data));
            if (nLines <= 0 || wQuads <= 0)
                return;

            int bytesPerLine = wQuads << 3;   // same as ChainSprite
            int units4 = wQuads << 1;   // # of 4-byte units
            int numQuads = units4 >> 1;   // number of 8-pixel quads

            if (bytesPerLine > LineBufferSize)
                throw new ArgumentException("bytes_per_line exceeds LINE_BUFFER_SIZE");

            var lineBuffer = new byte[LineBufferSize];
            var chainBuffer = new byte[8];

            int dataOffset = 0;

            for (int line = 0; line < nLines; ++line)
            {
                int destLineOffset = dataOffset;

                // copy current line into temp buffer (chunky source)
                Buffer.BlockCopy(data, destLineOffset, lineBuffer, 0, bytesPerLine);

                int srcPixelsOffset = 0;              // chunky source
                int destWordBaseOffset = destLineOffset; // planar destination

                for (int q = 0; q < numQuads; ++q)
                {
                    // 8 chunky bytes -> chain_buffer
                    EncodeQuad(lineBuffer, srcPixelsOffset, chainBuffer);
                    srcPixelsOffset += 8;

                    ushort w0 = (ushort)(chainBuffer[0] | (chainBuffer[1] << 8));
                    ushort w1 = (ushort)(chainBuffer[2] | (chainBuffer[3] << 8));
                    ushort w2 = (ushort)(chainBuffer[4] | (chainBuffer[5] << 8));
                    ushort w3 = (ushort)(chainBuffer[6] | (chainBuffer[7] << 8));

                    int p = destWordBaseOffset;
                    WriteLe16(data, p + units4 * 0, w0);
                    WriteLe16(data, p + units4 * 1, w1);
                    WriteLe16(data, p + units4 * 2, w2);
                    WriteLe16(data, p + units4 * 3, w3);

                    destWordBaseOffset += 2; // next word column
                }

                dataOffset += bytesPerLine; // next line
            }
        }

        // --------------------------------------------------------------------
        // DrawSprite (4bpp packed source -> 8bpp dest)
        // --------------------------------------------------------------------

        /// <summary>
        /// C# translation of:
        ///
        /// void _DrawSprite(
        ///     const uint8_t* from,
        ///     uint8_t*       where,
        ///     int            delta,
        ///     int            spr_delta,
        ///     int            width,
        ///     int            height,
        ///     int            col,
        ///     int            odd);
        ///
        /// from      - pointer to source pixels (4-bit per pixel, 2 pixels per byte)
        /// where     - pointer to destination screen (1 byte per pixel)
        /// delta     - distance (in bytes) between end of last sprite pixel and end of screen line
        /// sprDelta  - extra bytes to skip in source sprite between lines
        /// width     - width in pixels to draw
        /// height    - height in pixels to draw
        /// col       - drawing color; if < 0, use sprite's own colors; if >= 0, draw solid colour
        /// odd       - if true, first sprite pixel comes from low nibble of first byte
        /// </summary>
        public static void DrawSprite(
            byte[] from,
            int fromOffset,
            byte[] where,
            int whereOffset,
            int delta,
            int sprDelta,
            int width,
            int height,
            int col,
            bool odd)
        {
            if (from == null) throw new ArgumentNullException(nameof(from));
            if (where == null) throw new ArgumentNullException(nameof(where));
            if (width <= 0 || height <= 0)
                return;

            int ecx = height;          // line counter
            int edx = width;           // width in pixels
            int ebx = delta;           // screen delta
            int edi = whereOffset;     // dest pointer (index)
            int esi = fromOffset;      // src pointer (index)
            int ebp = sprDelta;        // sprite line delta

            // -------- colored path (col >= 0) --------
            if (col >= 0)
            {
                int color = col & 0xFF; // AL = colour

                for (; ; )
                {
                    int savedEax = color;
                    int savedEcx = ecx;
                    int savedEdx = edx;
                    int savedEbx = ebx;

                    ecx = edx;              // ECX = width
                    int dh = color & 0xFF;  // DH = colour
                    int eax = odd ? 1 : 0;

                    int gotoColNextByte;
                    int al = 0;
                    int dl = 0;

                    if (eax == 0)
                    {
                        // even: start from high nibble of first byte
                        gotoColNextByte = 1;
                    }
                    else
                    {
                        // odd: start at second nibble of first byte (low nibble)
                        al = from[esi++];
                        al &= 0x0F;
                        edx = 0;
                        gotoColNextByte = 0; // jump straight to "next_pixel" first iteration
                    }

                    // per-pixel loop
                    for (; ; )
                    {
                        if (gotoColNextByte != 0)
                        {
                            // .col_next_byte
                            eax = 0;
                            al = from[esi++];
                            eax = (eax & unchecked((int)0xFFFFFF00)) | (al & 0xFF);

                            // ror eax, 4
                            int low4 = eax & 0x0F;
                            eax = (int)(((uint)eax >> 4) | (low4 << 28));
                            al = eax & 0xFF;

                            if (al != 0)
                            {
                                dl = dh;
                                if (al == 8)
                                    dl = 8;
                                where[edi] = (byte)dl;
                            }
                        }
                        else
                        {
                            // first iteration for odd-case
                            gotoColNextByte = 1;
                        }

                        // .next_pixel
                        edi++;
                        ecx--;
                        if (ecx == 0)
                            break;

                        // second pixel from same source byte
                        eax = (int)((uint)eax >> 28);
                        al = eax & 0xFF;
                        if (al != 0)
                        {
                            dl = dh;
                            if (al == 8)
                                dl = 8;
                            where[edi] = (byte)dl;
                        }

                        edi++;
                        ecx--;
                        if (ecx != 0)
                            continue;
                        break;
                    }

                    // .col_end_bytes_loop epilogue
                    ebx = savedEbx;
                    edx = savedEdx;
                    ecx = savedEcx;
                    color = savedEax;

                    edi += ebx;  // dest += delta
                    esi += ebp;  // src  += spr_delta

                    ecx--;
                    if (ecx != 0)
                        continue; // next line
                    break;
                }

                return;
            }

            // -------- normal (non-colored) path (col < 0) --------
            for (; ; )
            {
                int savedEcx = ecx;
                int savedEdx = edx;
                int savedEbx = ebx;

                ecx = edx;               // ECX = width
                int eax = odd ? 1 : 0;
                int al = 0;
                int dl = 0;
                int gotoNextByte;

                if (eax == 0)
                {
                    // even start
                    gotoNextByte = 1;
                }
                else
                {
                    // odd start: begin on low nibble of first byte
                    al = from[esi++];
                    al &= 0x0F;
                    edx = 0;
                    gotoNextByte = 0;
                }

                for (; ; )
                {
                    if (gotoNextByte != 0)
                    {
                        // .next_byte
                        eax = 0;
                        edx = 0;
                        al = from[esi++];
                        eax = (eax & unchecked((int)0xFFFFFF00)) | (al & 0xFF);

                        // ror eax, 4
                        int low4 = eax & 0x0F;
                        eax = (int)(((uint)eax >> 4) | (low4 << 28));
                        al = eax & 0xFF;

                        // first pixel from this byte
                        byte bl = where[edi];
                        if (al != 0)
                            bl = (byte)al;
                        where[edi] = bl;

                        edi++;
                        ecx--;
                        if (ecx == 0)
                            break;

                        // second pixel from same byte
                        eax = (int)((uint)eax >> 28);
                        al = eax & 0xFF;
                        gotoNextByte = 0; // fall into .second_pixel
                    }

                    // .second_pixel
                    {
                        byte bl = where[edi];
                        if (al != 0)
                            bl = (byte)al;
                        where[edi] = bl;
                    }

                    edi++;
                    ecx--;
                    if (ecx != 0)
                    {
                        gotoNextByte = 1; // back to .next_byte
                        continue;
                    }
                    break;
                }

                // .end_bytes_loop epilogue
                ebx = savedEbx;
                edx = savedEdx;
                ecx = savedEcx;

                edi += ebx;  // dest += delta
                esi += ebp;  // src  += spr_delta

                ecx--;
                if (ecx != 0)
                    continue; // next line
                break;
            }
        }
    }
}
