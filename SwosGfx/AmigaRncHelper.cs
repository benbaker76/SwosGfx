using System;
using System.IO;
using RncProPack;

namespace SwosGfx
{
    /// <summary>
    /// Helper for reading/writing Amiga-format files (map/raw)
    /// with optional RNC packing.
    /// </summary>
    public static class AmigaRncHelper
    {
        /// <summary>
        /// Global default for whether we write Amiga-format files as RNC.
        /// This is what Program will toggle.
        /// </summary>
        public static bool DefaultWriteAsRnc { get; set; } = true;

        private static bool IsRnc(byte[] data)
        {
            return data != null
                   && data.Length >= 4
                   && data[0] == (byte)'R'
                   && data[1] == (byte)'N'
                   && data[2] == (byte)'C';
        }

        private static byte[] RncUnpack(byte[] packed)
        {
            using var inMs = new MemoryStream(packed, writable: false);
            using var outMs = new MemoryStream();

            var proc = new RncProcessor();
            var options = new RncProcessor.Options
            {
                Mode = 'u'
            };

            var result = proc.Process(inMs, outMs, options);
            if (result.ErrorCode != 0)
                throw new InvalidDataException($"RNC unpack failed (error {result.ErrorCode}).");

            return outMs.ToArray();
        }

        private static byte[] RncPack(byte[] unpacked)
        {
            using var inMs = new MemoryStream(unpacked, writable: false);
            using var outMs = new MemoryStream();

            var proc = new RncProcessor();
            var options = new RncProcessor.Options
            {
                Mode = 'p',
                Method = 2
            };

            var result = proc.Process(inMs, outMs, options);
            if (result.ErrorCode != 0)
                throw new InvalidOperationException($"RNC pack failed (error {result.ErrorCode}).");

            return outMs.ToArray();
        }

        /// <summary>
        /// Read an Amiga-format file (map/raw).
        /// If it is RNC-packed, transparently unpack it.
        /// </summary>
        public static byte[] ReadAllBytes(string path)
        {
            var data = File.ReadAllBytes(path);
            return IsRnc(data) ? RncUnpack(data) : data;
        }

        /// <summary>
        /// Write an Amiga-format file (map/raw).
        /// If writeAsRnc is null, uses DefaultWriteAsRnc.
        /// </summary>
        public static void WriteAllBytes(string path, byte[] rawData, bool? writeAsRnc = null)
        {
            bool compress = writeAsRnc ?? DefaultWriteAsRnc;

            byte[] dataToWrite = rawData;

            // Safety: don't double-pack if the buffer already happens to be RNC.
            if (compress && !IsRnc(rawData))
            {
                dataToWrite = RncPack(rawData);
            }

            File.WriteAllBytes(path, dataToWrite);
        }
    }
}
