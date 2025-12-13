using System;
using System.Globalization;
using System.IO;

namespace RncProPack
{
    public class RncProcessor
    {
        private const uint RNC_SIGN = 0x524E43; // 'RNC'
        private const int RNC_HEADER_SIZE = 0x12;
        private const int MAX_BUF_SIZE = 0x1E00000;

        // Expose for external checks if needed
        public const int MaxBufferSize = MAX_BUF_SIZE;

        // CRC table
        private static readonly ushort[] CrcTable = {
            0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
            0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
            0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
            0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
            0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
            0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
            0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
            0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
            0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
            0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
            0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
            0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
            0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
            0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
            0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
            0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
            0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
            0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
            0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
            0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
            0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
            0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
            0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
            0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
            0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
            0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
            0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
            0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
            0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
            0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
            0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
            0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
        };

        private static readonly byte[] MatchCountBitsTable = { 0x00, 0x0E, 0x08, 0x0A, 0x12, 0x13, 0x16 };
        private static readonly byte[] MatchCountBitsCountTable = { 0, 4, 4, 4, 5, 5, 5 };
        private static readonly byte[] MatchOffsetBitsTable = { 0x00, 0x06, 0x08, 0x09, 0x15, 0x17, 0x1D, 0x1F, 0x28, 0x29, 0x2C, 0x2D, 0x38, 0x39, 0x3C, 0x3D };
        private static readonly byte[] MatchOffsetBitsCountTable = { 1, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6 };

        private struct Huftable
        {
            public uint L1;        // +0
            public ushort L2;      // +4
            public uint L3;        // +6
            public ushort BitDepth; // +A
        }

        private class Vars
        {
            public ushort MaxMatches;
            public ushort EncKey;
            public int PackBlockSize;
            public ushort DictSize;
            public int Method;
            public char PuseMode;
            public int InputSize;
            public int FileSize;

            public int BytesLeft;
            public int PackedSize;
            public int ProcessedSize;
            public int V7;
            public int PackBlockPos;
            public ushort PackToken;
            public ushort BitCount;
            public ushort V11;
            public ushort LastMinOffset;
            public int V17;
            public int PackBlockLeftSize;
            public ushort MatchCount;
            public ushort MatchOffset;
            public int V20;
            public int V21;
            public uint BitBuffer;

            public int UnpackedSize;
            public int RncDataSize;
            public ushort UnpackedCrc;
            public ushort UnpackedCrcReal;
            public ushort PackedCrc;
            public int Leeway;
            public int ChunksCount;

            public byte[] Mem1;
            public ushort[] Mem2;
            public ushort[] Mem3;
            public ushort[] Mem4;
            public ushort[] Mem5;

            public byte[] Decoded;

            public long ReadStartOffset;
            public long WriteStartOffset;
            public byte[] Input;
            public byte[] Output;
            public byte[] Temp;
            public long InputOffset;
            public long OutputOffset;
            public long TempOffset;

            public byte[] TmpCrcData = new byte[2048];
            public Huftable[] RawTable = new Huftable[16];
            public Huftable[] PosTable = new Huftable[16];
            public Huftable[] LenTable = new Huftable[16];

            // "Pointer" like indices into mem1/decoded
            public int PackBlockStartIndex;
            public int PackBlockMaxIndex;
            public int PackBlockEndIndex;
            public int WindowIndex;

            public Vars()
            {
                EncKey = 0;
                MaxMatches = 0x1000;
                UnpackedCrcReal = 0;
                PackBlockSize = 0x3000;
                DictSize = 0xFFFF;
                Method = 1;
                PuseMode = 'p';

                ReadStartOffset = 0;
                WriteStartOffset = 0;
                InputOffset = 0;
                OutputOffset = 0;
                TempOffset = 0;

                Array.Clear(TmpCrcData, 0, TmpCrcData.Length);
                RawTable = new Huftable[16];
                PosTable = new Huftable[16];
                LenTable = new Huftable[16];
            }
        }

        // Options for processing
        public class Options
        {
            /// <summary>
            /// Mode: 'p' (pack), 'u' (unpack), 's' (search), 'e' (search & extract).
            /// </summary>
            public char Mode { get; set; }

            /// <summary>
            /// Encryption key (0 = none).
            /// </summary>
            public ushort EncKey { get; set; }

            /// <summary>
            /// Dictionary size (will be clamped per method).
            /// </summary>
            public ushort DictSize { get; set; } = 0xFFFF;

            /// <summary>
            /// Compression method: 1 or 2.
            /// </summary>
            public int Method { get; set; } = 1;

            /// <summary>
            /// Read start offset (in bytes) from the input.
            /// </summary>
            public long ReadStartOffset { get; set; }

            /// <summary>
            /// Write start offset (in bytes) into the output.
            /// </summary>
            public long WriteStartOffset { get; set; }
        }

        public class Result
        {
            public int ErrorCode { get; set; }
            public long OriginalSize { get; set; }
            public long OutputSize { get; set; }
            public int PackedSize { get; set; }
            public int FileSize { get; set; }
            public char Mode { get; set; }
        }

        // --- Public entrypoints -------------------------------------------------

        /// <summary>
        /// Core processing using memory streams.
        /// </summary>
        public Result Process(MemoryStream input, MemoryStream output, Options options)
        {
            if (input == null) throw new ArgumentNullException(nameof(input));
            if (output == null) throw new ArgumentNullException(nameof(output));
            if (options == null) throw new ArgumentNullException(nameof(options));

            byte[] fileBytes = input.ToArray();
            return ProcessBytes(fileBytes, output, options);
        }

        /// <summary>
        /// Wrapper for file streams - copies to memory streams and calls the memory-based logic.
        /// </summary>
        public Result Process(FileStream input, FileStream output, Options options)
        {
            if (input == null) throw new ArgumentNullException(nameof(input));
            if (output == null) throw new ArgumentNullException(nameof(output));
            if (options == null) throw new ArgumentNullException(nameof(options));

            using var inMem = new MemoryStream();
            input.CopyTo(inMem);
            inMem.Position = 0;

            using var outMem = new MemoryStream();
            var result = Process(inMem, outMem, options);

            // For pack/unpack modes we have data in outMem; for search-only modes, it's empty
            if (result.ErrorCode == 0 && options.Mode != 's' && options.Mode != 'e')
            {
                outMem.Position = 0;
                outMem.CopyTo(output);
            }

            return result;
        }

        // --- Internal core wrapper (byte[] based) ------------------------------

        private Result ProcessBytes(byte[] fileBytes, MemoryStream outputStream, Options options)
        {
            var v = InitVars();

            v.PuseMode = options.Mode;
            v.EncKey = options.EncKey;
            v.DictSize = options.DictSize;
            v.Method = options.Method;
            v.ReadStartOffset = options.ReadStartOffset;
            v.WriteStartOffset = options.WriteStartOffset;

            // Method / dict & matches
            if (v.PuseMode == 'p')
            {
                if (v.Method == 1)
                {
                    if (v.DictSize > 0x8000)
                        v.DictSize = 0x8000;
                    v.MaxMatches = 0x1000;
                }
                else if (v.Method == 2)
                {
                    if (v.DictSize > 0x1000)
                        v.DictSize = 0x1000;
                    v.MaxMatches = 0xFF;
                }
            }

            long actualFileLengthFromFtell = fileBytes.Length;

            if (v.ReadStartOffset >= actualFileLengthFromFtell)
            {
                v.FileSize = 0;
            }
            else
            {
                ulong effectiveSizeUll = (ulong)actualFileLengthFromFtell - (ulong)v.ReadStartOffset;
                if (effectiveSizeUll > 0xFFFFFFFFUL)
                {
                    Console.WriteLine("Error: Calculated file size to process is too large.");
                    return new Result { ErrorCode = 1, Mode = v.PuseMode };
                }
                v.FileSize = (int)effectiveSizeUll;
            }

            if (v.FileSize > MAX_BUF_SIZE)
            {
                Console.WriteLine($"Error: File size {v.FileSize} exceeds processable limit {MAX_BUF_SIZE}.");
                return new Result { ErrorCode = 1, Mode = v.PuseMode };
            }

            v.Input = new byte[v.FileSize];
            Buffer.BlockCopy(fileBytes, (int)v.ReadStartOffset, v.Input, 0, v.FileSize);

            v.Output = new byte[MAX_BUF_SIZE];
            Array.Clear(v.Output, 0, v.Output.Length);

            v.Temp = new byte[MAX_BUF_SIZE];

            int errorCode = 0;
            switch (v.PuseMode)
            {
                case 'p': errorCode = DoPack(v); break;
                case 'u': errorCode = DoUnpack(v); break;
                case 's':
                case 'e': errorCode = DoSearch(v, v.FileSize, v.PuseMode == 'e' ? 1 : 0); break;
            }

            var result = new Result
            {
                ErrorCode = errorCode,
                Mode = v.PuseMode,
                PackedSize = v.PackedSize,
                FileSize = v.FileSize
            };

            if (errorCode == 0 && v.PuseMode != 's' && v.PuseMode != 'e')
            {
                // Copy to output stream
                outputStream.Write(v.Output, 0, (int)v.OutputOffset);

                result.OutputSize = v.OutputOffset;
                int original = (v.PuseMode == 'u')
                    ? (v.PackedSize + RNC_HEADER_SIZE)
                    : v.FileSize;
                result.OriginalSize = original;
            }

            return result;
        }

        // --- Original logic below (unchanged except being moved here) ----------

        private static byte ReadByte(byte[] buf, ref long offset)
        {
            return buf[(int)offset++];
        }

        private static void WriteByte(byte[] buf, ref long offset, byte b)
        {
            buf[(int)offset++] = b;
        }

        private static byte PeekByte(byte[] buf, long offset)
        {
            return buf[(int)offset];
        }

        private static ushort PeekWordBE(byte[] buf, int index)
        {
            byte b1 = buf[index + 0];
            byte b2 = buf[index + 1];
            return (ushort)((b1 << 8) | b2);
        }

        private static ushort PeekWordBE(byte[] buf, long offset)
        {
            return PeekWordBE(buf, (int)offset);
        }

        private static ushort ReadWordBE(byte[] buf, ref long offset)
        {
            byte b1 = ReadByte(buf, ref offset);
            byte b2 = ReadByte(buf, ref offset);
            return (ushort)((b1 << 8) | b2);
        }

        private static void WriteWordBE(byte[] buf, ref long offset, ushort val)
        {
            WriteByte(buf, ref offset, (byte)((val >> 8) & 0xFF));
            WriteByte(buf, ref offset, (byte)((val >> 0) & 0xFF));
        }

        private static uint PeekDwordBE(byte[] buf, long offset)
        {
            ushort w1 = PeekWordBE(buf, offset + 0);
            ushort w2 = PeekWordBE(buf, offset + 2);
            return (uint)((w1 << 16) | w2);
        }

        private static uint ReadDwordBE(Vars v)
        {
            if (v.InputOffset + 3 > v.FileSize)
            {
                Console.WriteLine("Corrupt file.");
                Environment.Exit(1);
            }

            ushort w1 = ReadWordBE(v.Input, ref v.InputOffset);
            ushort w2 = ReadWordBE(v.Input, ref v.InputOffset);
            return (uint)((w1 << 16) | w2);
        }

        private static void WriteDwordBE(byte[] buf, ref long offset, uint val)
        {
            WriteWordBE(buf, ref offset, (ushort)(val >> 16));
            WriteWordBE(buf, ref offset, (ushort)(val & 0xFFFF));
        }

        private static void ReadBuf(byte[] dest, int destIndex, byte[] source, ref long srcOffset, int size)
        {
            Buffer.BlockCopy(source, (int)srcOffset, dest, destIndex, size);
            srcOffset += size;
        }

        private static void WriteBuf(byte[] dest, ref long destOffset, byte[] source, int srcIndex, int size)
        {
            Buffer.BlockCopy(source, srcIndex, dest, (int)destOffset, size);
            destOffset += size;
        }

        private static ushort CrcBlock(byte[] buf, long offset, int size)
        {
            ushort crc = 0;

            while (size-- > 0)
            {
                crc ^= ReadByte(buf, ref offset);
                crc = (ushort)((crc >> 8) ^ CrcTable[crc & 0xFF]);
            }

            return crc;
        }

        private static void RorW(ref ushort x)
        {
            if ((x & 1) != 0)
                x = (ushort)(0x8000 | (x >> 1));
            else
                x >>= 1;
        }

        private static Vars InitVars()
        {
            return new Vars();
        }

        private static void InitDicts(Vars v)
        {
            ushort dictSize = v.DictSize;

            for (int i = 0; i < 0x800; ++i)
            {
                int baseIdx = i * 0x10;

                v.Mem2[baseIdx + 0x0] = dictSize;
                v.Mem2[baseIdx + 0x1] = dictSize;
                v.Mem2[baseIdx + 0x2] = dictSize;
                v.Mem2[baseIdx + 0x3] = dictSize;
                v.Mem2[baseIdx + 0x4] = dictSize;
                v.Mem2[baseIdx + 0x5] = dictSize;
                v.Mem2[baseIdx + 0x6] = dictSize;
                v.Mem2[baseIdx + 0x7] = dictSize;
                v.Mem2[baseIdx + 0x8] = dictSize;
                v.Mem2[baseIdx + 0x9] = dictSize;
                v.Mem2[baseIdx + 0xA] = dictSize;
                v.Mem2[baseIdx + 0xB] = dictSize;
                v.Mem2[baseIdx + 0xC] = dictSize;
                v.Mem2[baseIdx + 0xD] = dictSize;
                v.Mem2[baseIdx + 0xE] = dictSize;
                v.Mem2[baseIdx + 0xF] = dictSize;

                v.Mem3[baseIdx + 0x0] = dictSize;
                v.Mem3[baseIdx + 0x1] = dictSize;
                v.Mem3[baseIdx + 0x2] = dictSize;
                v.Mem3[baseIdx + 0x3] = dictSize;
                v.Mem3[baseIdx + 0x4] = dictSize;
                v.Mem3[baseIdx + 0x5] = dictSize;
                v.Mem3[baseIdx + 0x6] = dictSize;
                v.Mem3[baseIdx + 0x7] = dictSize;
                v.Mem3[baseIdx + 0x8] = dictSize;
                v.Mem3[baseIdx + 0x9] = dictSize;
                v.Mem3[baseIdx + 0xA] = dictSize;
                v.Mem3[baseIdx + 0xB] = dictSize;
                v.Mem3[baseIdx + 0xC] = dictSize;
                v.Mem3[baseIdx + 0xD] = dictSize;
                v.Mem3[baseIdx + 0xE] = dictSize;
                v.Mem3[baseIdx + 0xF] = dictSize;
            }

            for (int i = 0; i < dictSize; ++i)
            {
                int idx = i & 0x7FFF;
                v.Mem5[idx] = 0;
                v.Mem4[idx] = (ushort)i;
            }

            v.LastMinOffset = 0;
        }

        private static void UpdatePackedCrc(Vars v, byte b)
        {
            ushort crc = v.PackedCrc;
            v.PackedCrc = (ushort)(CrcTable[(crc & 0xFF) ^ b] ^ (crc >> 8));
            v.PackedSize++;
        }

        private static void UpdateUnpackedCrc(Vars v, byte b)
        {
            ushort crc = v.UnpackedCrc;
            v.UnpackedCrc = (ushort)(CrcTable[(crc & 0xFF) ^ b] ^ (crc >> 8));
            v.ProcessedSize++;
        }

        private static void WriteToOutput(Vars v, byte b)
        {
            if (v.PackedSize >= (v.FileSize - RNC_HEADER_SIZE))
                return;

            WriteByte(v.Output, ref v.OutputOffset, b);
            UpdatePackedCrc(v, b);
        }

        private static byte ReadFromInput(Vars v)
        {
            byte b = ReadByte(v.Input, ref v.InputOffset);
            UpdateUnpackedCrc(v, b);
            return b;
        }

        private static void WriteBitsM2(Vars v, ushort value, int count)
        {
            uint mask = (uint)(1 << (count - 1));

            while (count-- > 0)
            {
                v.PackToken <<= 1;

                if ((value & mask) != 0)
                    v.PackToken++;

                mask >>= 1;
                v.BitCount++;

                if (v.BitCount == 8)
                {
                    WriteToOutput(v, (byte)(v.PackToken & 0xFF));

                    for (int i = 0; i < v.V11; ++i)
                        WriteToOutput(v, v.TmpCrcData[i]);

                    v.V11 = 0;

                    if ((v.ProcessedSize > v.PackedSize) &&
                        (v.ProcessedSize - v.PackedSize > v.Leeway))
                        v.Leeway = v.ProcessedSize - v.PackedSize;

                    v.BitCount = 0;
                    v.PackToken = 0;
                }
            }
        }

        private static void WriteBitsM1(Vars v, ushort value, int count)
        {
            while (count-- > 0)
            {
                v.PackToken >>= 1;
                v.PackToken |= (ushort)(((value & 1) != 0) ? 0x8000 : 0);

                value >>= 1;
                v.BitCount++;

                if (v.BitCount == 16)
                {
                    WriteToOutput(v, (byte)(v.PackToken & 0xFF));
                    WriteToOutput(v, (byte)((v.PackToken >> 8) & 0xFF));

                    for (int i = 0; i < v.V11; ++i)
                        WriteToOutput(v, v.TmpCrcData[i]);

                    v.V11 = 0;

                    if ((v.ProcessedSize > v.PackedSize) &&
                        (v.ProcessedSize - v.PackedSize > v.Leeway))
                        v.Leeway = v.ProcessedSize - v.PackedSize;

                    v.BitCount = 0;
                    v.PackToken = 0;
                }
            }
        }

        private static void WriteBits(Vars v, ushort bits, int count)
        {
            if (v.Method == 2)
                WriteBitsM2(v, bits, count);
            else
                WriteBitsM1(v, bits, count);
        }

        private static int FindMatches(Vars v)
        {
            v.MatchCount = 1;
            v.MatchOffset = 0;

            int matchOffset = 1;
            while (matchOffset < (v.PackBlockEndIndex - v.PackBlockStartIndex) &&
                   v.Mem1[v.PackBlockStartIndex + matchOffset] == v.Mem1[v.PackBlockStartIndex])
            {
                matchOffset++;
            }

            ushort firstWord = PeekWordBE(v.Mem1, v.PackBlockStartIndex);
            ushort offset = v.Mem2[firstWord & 0x7FFF];

            while (true)
            {
                if (offset == v.DictSize)
                {
                    if (v.MatchCount == 2 && v.MatchOffset > 0x100)
                    {
                        v.MatchCount = 1;
                        v.MatchOffset = 0;
                    }

                    break;
                }

                ushort restore = v.Mem4[offset & 0x7FFF];
                ushort minOffset = v.LastMinOffset;

                if (minOffset <= offset)
                    minOffset += v.DictSize;

                minOffset -= offset;

                if (PeekWordBE(v.Mem1, v.PackBlockStartIndex - minOffset) ==
                    PeekWordBE(v.Mem1, v.PackBlockStartIndex))
                {
                    ushort maxCount = v.Mem5[offset & 0x7FFF];

                    if (maxCount <= minOffset)
                    {
                        if (maxCount > matchOffset)
                        {
                            minOffset = (ushort)(minOffset - maxCount + matchOffset);
                            maxCount = (ushort)matchOffset;
                        }

                        int maxSize = v.PackBlockEndIndex - v.PackBlockStartIndex;
                        if (maxCount == matchOffset)
                        {
                            while (maxCount < maxSize &&
                                   v.Mem1[v.PackBlockStartIndex + maxCount] ==
                                   v.Mem1[v.PackBlockStartIndex + maxCount - minOffset])
                            {
                                maxCount++;
                            }
                        }
                    }
                    else
                    {
                        minOffset = 1;
                        maxCount = (ushort)matchOffset;
                    }

                    if (maxCount > v.MaxMatches)
                        maxCount = v.MaxMatches;

                    if (maxCount >= v.MatchCount)
                    {
                        v.MatchCount = maxCount;
                        v.MatchOffset = minOffset;
                    }
                }

                offset = restore;
            }

            return 0;
        }

        private static void FindAndCheckMatches(Vars v)
        {
            FindMatches(v);

            if (v.MatchCount >= 2)
            {
                if (v.PackBlockMaxIndex - v.PackBlockStartIndex >= 3)
                {
                    ushort count = v.MatchCount;
                    ushort offset = v.MatchOffset;
                    ushort minOffset = v.LastMinOffset;

                    v.LastMinOffset = (ushort)((v.LastMinOffset + 1) % v.DictSize);

                    v.PackBlockStartIndex++;
                    FindMatches(v);
                    v.PackBlockStartIndex--;

                    v.LastMinOffset = minOffset;

                    if (count < v.MatchCount)
                    {
                        count = 1;
                        offset = 0;
                    }

                    v.MatchCount = count;
                    v.MatchOffset = offset;
                }
            }
        }

        private static int BitsCount(int value)
        {
            int count = 1;
            while ((value >>= 1) != 0)
                count++;
            return count;
        }

        private static void UpdateBitsTable(Vars v, Huftable[] data, ushort bits)
        {
            int idx;
            if (bits <= 1)
                idx = bits;
            else
                idx = BitsCount(bits);

            data[idx].L1++;

            WriteWordBE(v.Temp, ref v.TempOffset, bits);
        }

        private static void EncodeMatches(Vars v, ushort w)
        {
            while (true)
            {
                ushort restore = v.Mem4[v.LastMinOffset & 0x7FFF];
                v.Mem4[v.LastMinOffset & 0x7FFF] = v.DictSize;

                if (restore != v.LastMinOffset)
                {
                    ushort bufferWord = PeekWordBE(v.Mem1, v.PackBlockStartIndex - v.DictSize);
                    v.Mem2[bufferWord & 0x7FFF] = restore;

                    if (v.DictSize == restore)
                        v.Mem3[bufferWord & 0x7FFF] = v.DictSize;
                }

                ushort bw = PeekWordBE(v.Mem1, v.PackBlockStartIndex);

                if (v.Mem2[bw & 0x7FFF] == v.DictSize)
                    v.Mem2[bw & 0x7FFF] = v.LastMinOffset;
                else
                    v.Mem4[v.Mem3[bw & 0x7FFF] & 0x7FFF] = v.LastMinOffset;

                v.Mem3[bw & 0x7FFF] = v.LastMinOffset;

                int count = 1;
                while (count < (v.PackBlockEndIndex - v.PackBlockStartIndex) &&
                       v.Mem1[v.PackBlockStartIndex + count] == v.Mem1[v.PackBlockStartIndex])
                {
                    count++;
                }

                v.Mem5[v.LastMinOffset & 0x7FFF] = (ushort)count;

                while (true)
                {
                    v.LastMinOffset = (ushort)((v.LastMinOffset + 1) % v.DictSize);
                    v.PackBlockStartIndex++;

                    if (--w == 0)
                        return;

                    if (--count <= 1)
                        break;

                    v.Mem5[v.LastMinOffset & 0x7FFF] = (ushort)count;

                    if (v.LastMinOffset != v.Mem4[v.LastMinOffset & 0x7FFF])
                    {
                        restore = v.Mem4[v.LastMinOffset & 0x7FFF];
                        v.Mem4[v.LastMinOffset & 0x7FFF] = v.LastMinOffset;

                        bw = PeekWordBE(v.Mem1, v.PackBlockStartIndex - v.DictSize);
                        v.Mem2[bw & 0x7FFF] = restore;

                        if (v.DictSize == restore)
                            v.Mem3[bw & 0x7FFF] = v.DictSize;
                    }
                }
            }
        }

        private static void Proc6(Vars v)
        {
            v.V17 = 0;
            v.PackBlockLeftSize = v.PackBlockSize;
            v.InputOffset = v.ReadStartOffset + v.V7 + v.PackBlockPos;
            v.TempOffset = 0;

            uint dataLength = 0;

            while (v.BytesLeft != 0 || v.PackBlockPos != 0)
            {
                int sizeToRead = 0xFFFF - v.DictSize - v.PackBlockPos;

                if (v.BytesLeft < sizeToRead)
                    sizeToRead = v.BytesLeft;

                v.PackBlockStartIndex = v.DictSize;
                ReadBuf(v.Mem1, v.PackBlockStartIndex + v.PackBlockPos, v.Input, ref v.InputOffset, sizeToRead);

                v.BytesLeft -= sizeToRead;
                v.PackBlockPos += sizeToRead;

                v.PackBlockMaxIndex = v.PackBlockStartIndex + v.PackBlockPos;
                v.PackBlockEndIndex = v.PackBlockStartIndex + v.PackBlockPos;

                if (v.PackBlockLeftSize < v.PackBlockPos)
                    v.PackBlockMaxIndex = v.PackBlockStartIndex + v.PackBlockLeftSize;

                while (v.PackBlockStartIndex < v.PackBlockMaxIndex - 1 && v.V17 < 0xFFFE)
                {
                    FindAndCheckMatches(v);

                    if (v.MatchCount >= 2)
                    {
                        if (v.PackBlockStartIndex + v.MatchCount <= v.PackBlockMaxIndex)
                        {
                            UpdateBitsTable(v, v.RawTable, (ushort)dataLength);
                            UpdateBitsTable(v, v.PosTable, (ushort)(v.MatchCount - 2));
                            UpdateBitsTable(v, v.LenTable, (ushort)(v.MatchOffset - 1));

                            EncodeMatches(v, v.MatchCount);
                            v.V17++;
                            dataLength = 0;
                        }
                        else
                        {
                            if (v.V17 != 0)
                                break;

                            v.MatchCount = (ushort)(v.PackBlockMaxIndex - v.PackBlockStartIndex);
                        }
                    }
                    else
                    {
                        EncodeMatches(v, 1);
                        dataLength++;
                    }
                }

                v.PackBlockPos = v.PackBlockEndIndex - v.PackBlockStartIndex;

                Buffer.BlockCopy(v.Mem1, v.PackBlockStartIndex - v.DictSize, v.Mem1, 0,
                    v.DictSize + v.PackBlockPos);

                if ((v.PackBlockMaxIndex < v.PackBlockEndIndex) ||
                    ((v.PackBlockMaxIndex == v.PackBlockEndIndex) && v.BytesLeft == 0) ||
                    v.V17 == 0xFFFE)
                    break;

                v.PackBlockLeftSize -= (v.PackBlockStartIndex - v.DictSize);
            }

            if (v.PackBlockMaxIndex == v.PackBlockEndIndex && v.BytesLeft == 0 && v.V17 != 0xFFFE)
                dataLength += (uint)v.PackBlockPos;

            UpdateBitsTable(v, v.RawTable, (ushort)dataLength);
            v.V17++;

            v.TempOffset = 0;
        }

        private static void UpdateTmpCrcData(Vars v, byte b)
        {
            if (v.BitCount != 0)
            {
                v.TmpCrcData[v.V11] = b;
                v.V11++;
            }
            else
            {
                WriteToOutput(v, b);
            }
        }

        private static void EncodeMatchesCount(Vars v, int count)
        {
            while (count > 0)
            {
                if (count >= 12)
                {
                    if ((count & 3) != 0)
                    {
                        WriteBitsM2(v, 0, 1);

                        byte b = ReadFromInput(v);
                        UpdateTmpCrcData(v, (byte)((v.EncKey ^ b) & 0xFF));

                        count--;
                    }
                    else
                    {
                        WriteBitsM2(v, 0x17, 5);

                        if (count >= 72)
                        {
                            WriteBitsM2(v, 0xF, 4);

                            for (int i = 0; i < 72; ++i)
                            {
                                byte b = ReadFromInput(v);
                                UpdateTmpCrcData(v, (byte)((v.EncKey ^ b) & 0xFF));
                            }

                            count -= 72;
                        }
                        else
                        {
                            WriteBitsM2(v, (ushort)((count - 12) >> 2), 4);

                            while (count-- > 0)
                            {
                                byte b = ReadFromInput(v);
                                UpdateTmpCrcData(v, (byte)((v.EncKey ^ b) & 0xFF));
                            }
                        }
                    }

                    RorW(ref v.EncKey);
                }
                else
                {
                    while (count != 0)
                    {
                        WriteBitsM2(v, 0, 1);

                        byte b = ReadFromInput(v);
                        UpdateTmpCrcData(v, (byte)((v.EncKey ^ b) & 0xFF));

                        RorW(ref v.EncKey);

                        count--;
                    }
                }
            }
        }

        private static void ClearTable(Huftable[] data, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                data[i].L1 = 0;
                data[i].L2 = 0xFFFF;
                data[i].L3 = 0;
                data[i].BitDepth = 0;
            }
        }

        private static int Proc17(Vars v, Huftable[] data, int count)
        {
            uint d6 = 0xFFFFFFFF;
            uint d5 = 0xFFFFFFFF;

            int i = 0;
            while (i < count)
            {
                if (data[i].L1 != 0)
                {
                    if (data[i].L1 < d5)
                    {
                        d6 = d5;
                        v.V21 = v.V20;
                        d5 = data[i].L1;
                        v.V20 = i;
                    }
                    else if (data[i].L1 < d6)
                    {
                        d6 = data[i].L1;
                        v.V21 = i;
                    }
                }

                i++;
            }

            return (d5 != 0xFFFFFFFF && d6 != 0xFFFFFFFF) ? 1 : 0;
        }

        private static uint InverseBits(uint value, int count)
        {
            uint i = 0;
            while (count-- > 0)
            {
                i <<= 1;
                if ((value & 1) != 0)
                    i |= 1;
                value >>= 1;
            }

            return i;
        }

        private static void Proc20(Huftable[] data, int count)
        {
            uint val = 0;
            uint div = 0x80000000;
            int bitCount = 1;

            while (bitCount <= 16)
            {
                int i = 0;

                while (true)
                {
                    if (i >= count)
                    {
                        bitCount++;
                        div >>= 1;
                        break;
                    }

                    if (data[i].BitDepth == bitCount)
                    {
                        data[i].L3 = InverseBits(val / div, bitCount);
                        val += div;
                    }

                    i++;
                }
            }
        }

        private static void Proc16(Vars v, Huftable[] data, int count)
        {
            int d4 = 0;
            int ve = 0;

            for (int i = 0; i < count; ++i)
            {
                if (data[i].L1 != 0)
                {
                    d4++;
                    ve = i;
                }
            }

            if (d4 == 0)
                return;

            if (d4 == 1)
            {
                data[ve].BitDepth++;
                return;
            }

            while (Proc17(v, data, count) != 0)
            {
                data[v.V20].L1 += data[v.V21].L1;
                data[v.V21].L1 = 0;
                data[v.V20].BitDepth++;

                while (data[v.V20].L2 != 0xFFFF)
                {
                    v.V20 = data[v.V20].L2;
                    data[v.V20].BitDepth++;
                }

                data[v.V20].L2 = (ushort)v.V21;
                data[v.V21].BitDepth++;

                while (data[v.V21].L2 != 0xFFFF)
                {
                    v.V21 = data[v.V21].L2;
                    data[v.V21].BitDepth++;
                }
            }

            Proc20(data, count);
        }

        private static void Proc18(Vars v, Huftable[] data, int count)
        {
            int cnt = count;

            while (cnt > 0 && data[cnt - 1].BitDepth == 0)
            {
                cnt--;
                count--;
            }

            WriteBitsM1(v, (ushort)count, 5);

            for (int i = 0; i < count; ++i)
                WriteBitsM1(v, (ushort)data[i].BitDepth, 4);
        }

        private static void Proc19(Vars v, Huftable[] data, int count)
        {
            int bits;

            if (count > 1)
                bits = BitsCount(count);
            else
                bits = count;

            WriteBitsM1(v, (ushort)data[bits].L3, data[bits].BitDepth);

            if (bits > 1)
                WriteBitsM1(v, (ushort)(count - (1 << (bits - 1))), bits - 1);
        }

        private static void CompressData2(Vars v)
        {
            int srcOffset = (int)v.ReadStartOffset;

            while (v.V7 < v.UnpackedSize)
            {
                Proc6(v);
                v.InputOffset = srcOffset;

                while (v.V17-- > 0)
                {
                    uint dataLength = ReadWordBE(v.Temp, ref v.TempOffset);
                    v.V7 += (int)dataLength;

                    EncodeMatchesCount(v, (int)dataLength);

                    if (v.V17 > 0)
                    {
                        v.MatchCount = ReadWordBE(v.Temp, ref v.TempOffset);
                        v.MatchOffset = ReadWordBE(v.Temp, ref v.TempOffset);

                        if (v.MatchCount != 0)
                        {
                            if (v.MatchCount >= 7)
                            {
                                WriteBitsM2(v, 0xF, 4);
                                UpdateTmpCrcData(v, (byte)((v.MatchCount - 6) & 0xFF));
                            }
                            else
                            {
                                WriteBitsM2(v,
                                    MatchCountBitsTable[v.MatchCount],
                                    MatchCountBitsCountTable[v.MatchCount]);
                            }

                            WriteBitsM2(v,
                                MatchOffsetBitsTable[v.MatchOffset >> 8],
                                MatchOffsetBitsCountTable[v.MatchOffset >> 8]);
                        }
                        else
                        {
                            WriteBitsM2(v, 6, 3);
                        }

                        UpdateTmpCrcData(v, (byte)(v.MatchOffset & 0xFF));

                        v.MatchCount += 2;
                        v.V7 += v.MatchCount;

                        while (v.MatchCount > 0)
                        {
                            ReadFromInput(v);
                            v.MatchCount--;
                        }
                    }
                }

                WriteBitsM2(v, 0xF, 4);
                UpdateTmpCrcData(v, 0);

                if (v.V7 >= v.UnpackedSize)
                    WriteBitsM2(v, 0, 1);
                else
                    WriteBitsM2(v, 1, 1);

                if (v.BitCount == 0)
                {
                    for (int i = 0; i < v.V11; ++i)
                        WriteToOutput(v, v.TmpCrcData[i]);

                    v.V11 = 0;
                }

                v.ChunksCount++;
                srcOffset = (int)v.InputOffset;
            }

            v.PackToken <<= (ushort)(8 - v.BitCount);

            if (v.BitCount != 0 || v.V11 != 0)
                WriteToOutput(v, (byte)(v.PackToken & 0xFF));
        }

        private static void CompressData1(Vars v)
        {
            int srcOffset = (int)v.ReadStartOffset;

            while (v.V7 < v.UnpackedSize)
            {
                ClearTable(v.LenTable, v.LenTable.Length);
                ClearTable(v.PosTable, v.PosTable.Length);
                ClearTable(v.RawTable, v.RawTable.Length);

                Proc6(v);
                v.InputOffset = srcOffset;

                Proc16(v, v.RawTable, v.RawTable.Length);
                Proc16(v, v.LenTable, v.LenTable.Length);
                Proc16(v, v.PosTable, v.PosTable.Length);

                Proc18(v, v.RawTable, v.RawTable.Length);
                Proc18(v, v.LenTable, v.LenTable.Length);
                Proc18(v, v.PosTable, v.PosTable.Length);

                WriteBitsM1(v, (ushort)v.V17, 16);

                while (v.V17 > 0)
                {
                    v.V17--;

                    uint dataLength = ReadWordBE(v.Temp, ref v.TempOffset);
                    v.V7 += (int)dataLength;

                    Proc19(v, v.RawTable, (int)dataLength);

                    if (dataLength != 0)
                    {
                        while (dataLength-- > 0)
                        {
                            byte b = ReadFromInput(v);

                            byte x = (byte)((v.EncKey ^ b) & 0xFF);

                            if (v.BitCount == 0)
                                WriteToOutput(v, x);
                            else
                            {
                                v.TmpCrcData[v.V11] = x;
                                v.V11++;
                            }
                        }

                        RorW(ref v.EncKey);
                    }

                    if (v.V17 > 0)
                    {
                        v.MatchCount = ReadWordBE(v.Temp, ref v.TempOffset);
                        v.MatchOffset = ReadWordBE(v.Temp, ref v.TempOffset);

                        Proc19(v, v.LenTable, v.MatchOffset);
                        Proc19(v, v.PosTable, v.MatchCount);

                        v.MatchCount += 2;
                        v.V7 += v.MatchCount;

                        while (v.MatchCount-- > 0)
                            ReadFromInput(v);
                    }
                }

                if (v.BitCount == 0)
                {
                    for (int i = 0; i < v.V11; ++i)
                        WriteToOutput(v, v.TmpCrcData[i]);

                    v.V11 = 0;
                }

                v.ChunksCount++;
                srcOffset = (int)v.InputOffset;
            }

            v.PackToken >>= (ushort)(16 - v.BitCount);

            if (v.BitCount != 0 || v.V11 != 0)
                WriteToOutput(v, (byte)(v.PackToken & 0xFF));

            if (v.BitCount > 8 || v.V11 != 0)
                WriteToOutput(v, (byte)((v.PackToken >> 8) & 0xFF));
        }

        private static void DoPackData(Vars v)
        {
            v.UnpackedSize = v.FileSize;
            v.PackedSize = v.FileSize;
            v.BytesLeft = v.FileSize;

            if (v.FileSize <= RNC_HEADER_SIZE)
                return;

            v.UnpackedCrc = 0;
            v.PackedCrc = 0;

            v.PackedSize = 0;
            v.ProcessedSize = 0;
            v.V7 = 0;
            v.PackBlockPos = 0;
            v.PackToken = 0;
            v.BitCount = 0;
            v.V11 = 0;
            v.Leeway = 0;
            v.ChunksCount = 0;

            v.Mem1 = new byte[0xFFFF];
            v.Mem2 = new ushort[0x10000];
            v.Mem3 = new ushort[0x10000];
            v.Mem4 = new ushort[0x10000];
            v.Mem5 = new ushort[0x10000];

            InitDicts(v);

            WriteDwordBE(v.Output, ref v.OutputOffset, ((RNC_SIGN << 8) | (uint)(v.Method & 0xFF)));
            WriteDwordBE(v.Output, ref v.OutputOffset, (uint)v.UnpackedSize);
            WriteDwordBE(v.Output, ref v.OutputOffset, 0);
            WriteWordBE(v.Output, ref v.OutputOffset, 0);
            WriteWordBE(v.Output, ref v.OutputOffset, 0);
            WriteWordBE(v.Output, ref v.OutputOffset, 0);

            ushort key = v.EncKey;
            WriteBits(v, 0, 1); // no lock
            WriteBits(v, (ushort)(v.EncKey != 0 ? 1 : 0), 1);

            switch (v.Method)
            {
                case 1: CompressData1(v); break;
                case 2: CompressData2(v); break;
            }

            for (int i = 0; i < v.V11; ++i)
                WriteToOutput(v, v.TmpCrcData[i]);

            v.V11 = 0;

            v.EncKey = key;

            if (v.Leeway > (v.UnpackedSize - v.PackedSize))
                v.Leeway -= (v.UnpackedSize - v.PackedSize);
            else
                v.Leeway = 0;

            if (v.Method == 2)
                v.Leeway += 2;

            v.PackedSize = (int)(v.OutputOffset - v.WriteStartOffset);

            v.OutputOffset = v.WriteStartOffset + 8;
            WriteDwordBE(v.Output, ref v.OutputOffset, (uint)(v.PackedSize - RNC_HEADER_SIZE));
            WriteWordBE(v.Output, ref v.OutputOffset, v.UnpackedCrc);
            WriteWordBE(v.Output, ref v.OutputOffset, v.PackedCrc);

            WriteByte(v.Output, ref v.OutputOffset, (byte)v.Leeway);
            WriteByte(v.Output, ref v.OutputOffset, (byte)v.ChunksCount);

            v.OutputOffset = v.PackedSize + v.WriteStartOffset;
            v.InputOffset = v.UnpackedSize + v.ReadStartOffset;

            v.Mem1 = null;
            v.Mem2 = null;
            v.Mem3 = null;
            v.Mem4 = null;
            v.Mem5 = null;
        }

        private static int DoPack(Vars v)
        {
            if (v.FileSize <= RNC_HEADER_SIZE)
                return 2;

            v.InputOffset = 0;
            v.OutputOffset = 0;

            if ((PeekDwordBE(v.Input, v.InputOffset) >> 8) == RNC_SIGN)
                return 3;

            DoPackData(v);
            return 0;
        }

        private static byte ReadSourceByte(Vars v)
        {
            if (v.PackBlockStartIndex == 0xFFFD)
            {
                int leftSize = v.FileSize - (int)v.InputOffset;
                int sizeToRead;

                if (leftSize <= 0xFFFD)
                    sizeToRead = leftSize;
                else
                    sizeToRead = 0xFFFD;

                v.PackBlockStartIndex = 0;
                ReadBuf(v.Mem1, v.PackBlockStartIndex, v.Input, ref v.InputOffset, sizeToRead);

                if (leftSize - sizeToRead > 2)
                    leftSize = 2;
                else
                    leftSize -= sizeToRead;

                ReadBuf(v.Mem1, sizeToRead, v.Input, ref v.InputOffset, leftSize);
                v.InputOffset -= leftSize;
            }

            return v.Mem1[v.PackBlockStartIndex++];
        }

        private static uint InputBitsM2(Vars v, short count)
        {
            uint bits = 0;

            while (count-- > 0)
            {
                if (v.BitCount == 0)
                {
                    v.BitBuffer = ReadSourceByte(v);
                    v.BitCount = 8;
                }

                bits <<= 1;

                if ((v.BitBuffer & 0x80) != 0)
                    bits |= 1;

                v.BitBuffer <<= 1;
                v.BitCount--;
            }

            return bits;
        }

        private static uint InputBitsM1(Vars v, short count)
        {
            uint bits = 0;
            uint prevBits = 1;

            while (count-- > 0)
            {
                if (v.BitCount == 0)
                {
                    byte b1 = ReadSourceByte(v);
                    byte b2 = ReadSourceByte(v);

                    v.BitBuffer = (uint)(
                        (v.Mem1[v.PackBlockStartIndex + 1] << 24) |
                        (v.Mem1[v.PackBlockStartIndex + 0] << 16) |
                        (b2 << 8) |
                        b1);

                    v.BitCount = 16;
                }

                if ((v.BitBuffer & 1) != 0)
                    bits |= prevBits;

                v.BitBuffer >>= 1;
                prevBits <<= 1;
                v.BitCount--;
            }

            return bits;
        }

        private static int InputBits(Vars v, short count)
        {
            if (v.Method != 2)
                return (int)InputBitsM1(v, count);
            else
                return (int)InputBitsM2(v, count);
        }

        private static void DecodeMatchCount(Vars v)
        {
            v.MatchCount = (ushort)(InputBitsM2(v, 1) + 4);

            if (InputBitsM2(v, 1) != 0)
                v.MatchCount = (ushort)(((v.MatchCount - 1) << 1) + InputBitsM2(v, 1));
        }

        private static void DecodeMatchOffset(Vars v)
        {
            v.MatchOffset = 0;
            if (InputBitsM2(v, 1) != 0)
            {
                v.MatchOffset = (ushort)InputBitsM2(v, 1);

                if (InputBitsM2(v, 1) != 0)
                {
                    v.MatchOffset = (ushort)(((v.MatchOffset << 1) | InputBitsM2(v, 1)) | 4);

                    if (InputBitsM2(v, 1) == 0)
                        v.MatchOffset = (ushort)((v.MatchOffset << 1) | InputBitsM2(v, 1));
                }
                else if (v.MatchOffset == 0)
                    v.MatchOffset = (ushort)(InputBitsM2(v, 1) + 2);
            }

            v.MatchOffset = (ushort)(((v.MatchOffset << 8) | ReadSourceByte(v)) + 1);
        }

        private static void WriteDecodedByte(Vars v, byte b)
        {
            if (v.WindowIndex == 0xFFFF)
            {
                WriteBuf(v.Output, ref v.OutputOffset, v.Decoded, v.DictSize, 0xFFFF - v.DictSize);
                Buffer.BlockCopy(v.Decoded, v.WindowIndex - v.DictSize, v.Decoded, 0, v.DictSize);
                v.WindowIndex = v.DictSize;
            }

            v.Decoded[v.WindowIndex++] = b;
            v.UnpackedCrcReal = (ushort)(CrcTable[(v.UnpackedCrcReal ^ b) & 0xFF] ^ (v.UnpackedCrcReal >> 8));
        }

        private static int UnpackDataM2(Vars v)
        {
            while (v.ProcessedSize < v.InputSize)
            {
                while (true)
                {
                    if (InputBitsM2(v, 1) == 0)
                    {
                        WriteDecodedByte(v, (byte)((v.EncKey ^ ReadSourceByte(v)) & 0xFF));
                        RorW(ref v.EncKey);
                        v.ProcessedSize++;
                    }
                    else
                    {
                        if (InputBitsM2(v, 1) != 0)
                        {
                            if (InputBitsM2(v, 1) != 0)
                            {
                                if (InputBitsM2(v, 1) != 0)
                                {
                                    v.MatchCount = (ushort)(ReadSourceByte(v) + 8);

                                    if (v.MatchCount == 8)
                                    {
                                        InputBitsM2(v, 1);
                                        break;
                                    }
                                }
                                else
                                    v.MatchCount = 3;

                                DecodeMatchOffset(v);
                            }
                            else
                            {
                                v.MatchCount = 2;
                                v.MatchOffset = (ushort)(ReadSourceByte(v) + 1);
                            }

                            v.ProcessedSize += v.MatchCount;

                            while (v.MatchCount-- > 0)
                                WriteDecodedByte(v, v.Decoded[v.WindowIndex - v.MatchOffset]);
                        }
                        else
                        {
                            DecodeMatchCount(v);

                            if (v.MatchCount != 9)
                            {
                                DecodeMatchOffset(v);
                                v.ProcessedSize += v.MatchCount;

                                while (v.MatchCount-- > 0)
                                    WriteDecodedByte(v, v.Decoded[v.WindowIndex - v.MatchOffset]);
                            }
                            else
                            {
                                uint dataLength = (InputBitsM2(v, 4) << 2) + 12;
                                v.ProcessedSize += (int)dataLength;

                                while (dataLength-- > 0)
                                    WriteDecodedByte(v, (byte)((v.EncKey ^ ReadSourceByte(v)) & 0xFF));

                                RorW(ref v.EncKey);
                            }
                        }
                    }
                }
            }

            WriteBuf(v.Output, ref v.OutputOffset, v.Decoded, v.DictSize, v.WindowIndex - v.DictSize);
            return 0;
        }

        private static void MakeHufTable(Vars v, Huftable[] data, int count)
        {
            ClearTable(data, count);

            int leafNodes = (int)InputBitsM1(v, 5);

            if (leafNodes != 0)
            {
                if (leafNodes > 16)
                    leafNodes = 16;

                for (int i = 0; i < leafNodes; ++i)
                    data[i].BitDepth = (ushort)InputBitsM1(v, 4);

                Proc20(data, leafNodes);
            }
        }

        private static uint DecodeTableData(Vars v, Huftable[] data)
        {
            uint i = 0;

            while (true)
            {
                if (data[i].BitDepth != 0 &&
                    data[i].L3 == (v.BitBuffer & ((1u << data[i].BitDepth) - 1)))
                {
                    InputBitsM1(v, (short)data[i].BitDepth);

                    if (i < 2)
                        return i;

                    return InputBitsM1(v, (short)(i - 1)) | (1u << ((int)i - 1));
                }

                i++;
            }
        }

        private static int UnpackDataM1(Vars v)
        {
            while (v.ProcessedSize < v.InputSize)
            {
                MakeHufTable(v, v.RawTable, v.RawTable.Length);
                MakeHufTable(v, v.LenTable, v.LenTable.Length);
                MakeHufTable(v, v.PosTable, v.PosTable.Length);

                int subChunks = (int)InputBitsM1(v, 16);

                while (subChunks-- > 0)
                {
                    uint dataLength = DecodeTableData(v, v.RawTable);
                    v.ProcessedSize += (int)dataLength;

                    if (dataLength != 0)
                    {
                        while (dataLength-- > 0)
                            WriteDecodedByte(v, (byte)((v.EncKey ^ ReadSourceByte(v)) & 0xFF));

                        RorW(ref v.EncKey);

                        v.BitBuffer =
                            (uint)((((v.Mem1[v.PackBlockStartIndex + 2] << 16) |
                                     (v.Mem1[v.PackBlockStartIndex + 1] << 8) |
                                     v.Mem1[v.PackBlockStartIndex + 0]) << v.BitCount) |
                                   (v.BitBuffer & ((1u << v.BitCount) - 1)));
                    }

                    if (subChunks > 0)
                    {
                        v.MatchOffset = (ushort)(DecodeTableData(v, v.LenTable) + 1);
                        v.MatchCount = (ushort)(DecodeTableData(v, v.PosTable) + 2);
                        v.ProcessedSize += v.MatchCount;

                        while (v.MatchCount-- > 0)
                            WriteDecodedByte(v, v.Decoded[v.WindowIndex - v.MatchOffset]);
                    }
                }
            }

            WriteBuf(v.Output, ref v.OutputOffset, v.Decoded, v.DictSize, v.WindowIndex - v.DictSize);
            return 0;
        }

        private static int DoUnpackData(Vars v)
        {
            long startPos = v.InputOffset;

            uint sign = ReadDwordBE(v);
            if ((sign >> 8) != RNC_SIGN)
                return 6;

            v.Method = (int)(sign & 3);
            v.InputSize = (int)ReadDwordBE(v);
            v.PackedSize = (int)ReadDwordBE(v);

            if (v.FileSize < v.PackedSize)
                return 7;

            v.UnpackedCrc = ReadWordBE(v.Input, ref v.InputOffset);
            v.PackedCrc = ReadWordBE(v.Input, ref v.InputOffset);

            // v.leeway, v.chunks_count
            ReadByte(v.Input, ref v.InputOffset);
            ReadByte(v.Input, ref v.InputOffset);

            // sanity on input_size
            if (v.InputSize > MAX_BUF_SIZE)
            {
                Console.WriteLine($"Error: Declared unpacked size in header ({v.InputSize}) is too large.");
                return 8;
            }

            if (v.PackedSize > v.FileSize && v.PackedSize > MAX_BUF_SIZE)
            {
                Console.WriteLine($"Error: Declared packed size in header ({v.PackedSize}) is too large.");
                return 8;
            }

            if (CrcBlock(v.Input, v.InputOffset, v.PackedSize) != v.PackedCrc)
                return 4;

            // Adjust dict_size for UNPACK based on actual method from the header
            // (the original C does this in main() before calling do_unpack).
            if (v.PuseMode != 'p')
            {
                if (v.Method == 1)
                {
                    if (v.DictSize > 0x8000)
                        v.DictSize = 0x8000;
                    if (v.DictSize < 0x400)
                        v.DictSize = 0x400;
                    v.MaxMatches = 0x1000;
                }
                else if (v.Method == 2)
                {
                    if (v.DictSize > 0x1000)
                        v.DictSize = 0x1000;
                    if (v.DictSize < 0x400)
                        v.DictSize = 0x400;
                    v.MaxMatches = 0xFF;
                }
            }

            v.Mem1 = new byte[0xFFFF];
            v.Decoded = new byte[0xFFFF];
            v.PackBlockStartIndex = 0xFFFD;
            v.WindowIndex = v.DictSize;

            v.UnpackedCrcReal = 0;
            v.BitCount = 0;
            v.BitBuffer = 0;
            v.ProcessedSize = 0;

            ushort specifiedKey = v.EncKey;

            int errorCode = 0;

            if (InputBits(v, 1) != 0 && v.PuseMode == 'p')
                errorCode = 9;

            if (errorCode == 0)
            {
                if (InputBits(v, 1) != 0 && v.EncKey == 0)
                    errorCode = 10;
            }

            if (errorCode == 0)
            {
                switch (v.Method)
                {
                    case 1: errorCode = UnpackDataM1(v); break;
                    case 2: errorCode = UnpackDataM2(v); break;
                }
            }

            v.EncKey = specifiedKey;

            v.Mem1 = null;
            v.Decoded = null;

            v.InputOffset = startPos + v.PackedSize + RNC_HEADER_SIZE;

            if (errorCode != 0)
                return errorCode;

            if (v.UnpackedCrc != v.UnpackedCrcReal)
                return 5;

            return 0;
        }

        private static int DoUnpack(Vars v)
        {
            v.PackedSize = v.FileSize;

            if (v.FileSize < RNC_HEADER_SIZE)
                return 6;

            return DoUnpackData(v);
        }

        private static int DoSearch(Vars v, int inputSize, int save)
        {
            int errorCode = 11;
            int hasRncs = 0;

            uint currentOffset = 0;
            while (currentOffset + RNC_HEADER_SIZE <= inputSize)
            {
                v.ReadStartOffset = currentOffset;
                v.FileSize = inputSize - (int)currentOffset;
                v.InputOffset = 0;
                v.OutputOffset = 0;

                byte[] inputBackup = v.Input;
                v.Input = new byte[v.FileSize];
                Buffer.BlockCopy(inputBackup, (int)currentOffset, v.Input, 0, v.FileSize);

                errorCode = DoUnpack(v);

                v.Input = inputBackup;

                if (errorCode == 0)
                {
                    Console.WriteLine($"RNC archive found: 0x{currentOffset:X6} ({v.PackedSize + RNC_HEADER_SIZE:D6}/{v.OutputOffset} bytes)");
                    uint advance = (uint)(v.PackedSize + RNC_HEADER_SIZE);

                    if (advance == 0 || currentOffset + advance < currentOffset || v.PackedSize > inputSize)
                    {
                        currentOffset++;
                    }
                    else
                    {
                        currentOffset += advance;
                    }

                    hasRncs = 1;

                    if (save != 0)
                    {
                        try
                        {
                            Directory.CreateDirectory("extracted");
                        }
                        catch (Exception)
                        {
                            errorCode = 12;
                            break;
                        }

                        string outName = Path.Combine("extracted", $"data_{v.ReadStartOffset:X6}.bin");
                        try
                        {
                            File.WriteAllBytes(outName, v.Output[..(int)v.OutputOffset]);
                        }
                        catch (Exception ex)
                        {
                            errorCode = 12;
                            Console.Error.WriteLine("Write failed: " + ex.Message);
                            break;
                        }
                    }
                }
                else
                {
                    switch (errorCode)
                    {
                        case 4: Console.WriteLine($"Position 0x{currentOffset:X6}: Packed CRC is wrong!"); break;
                        case 5: Console.WriteLine($"Position 0x{currentOffset:X6}: Unpacked CRC is wrong!"); break;
                        case 9: Console.WriteLine($"Position 0x{currentOffset:X6}: File already packed!"); break;
                        case 10: Console.WriteLine($"Position 0x{currentOffset:X6}: Decryption key required!"); break;
                    }

                    currentOffset++;
                }
            }

            return hasRncs != 0 ? 0 : ((errorCode == 6) ? 11 : errorCode);
        }
    }
}
