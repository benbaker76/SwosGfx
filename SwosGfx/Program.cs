using SwosGfx;
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Reflection;

class Program
{
    private enum PlatformKind
    {
        Amiga,
        Dos
    }

    private enum InputKind
    {
        Unspecified,
        Map,
        Tmx,
        Bmp,
        Raw
    }

    private enum OutputKind
    {
        Unspecified,
        Bmp,
        Tmx,
        Map,
        Raw
    }

    private enum DosMode
    {
        Pitch,
        Picture,
        SpritesExport,
        SpritesImport
    }

    private sealed class CliOptions
    {
        public bool ShowHelp;
        public PlatformKind Platform = PlatformKind.Amiga; // default
        public bool PlatformExplicitlySet;

        // Amiga-style I/O
        public InputKind Input = InputKind.Unspecified;
        public OutputKind Output = OutputKind.Unspecified;
        public string? PaletteName;
        public int Bitplanes = 4; // default 4 planes → 16 colors
        public bool NoRnc = false;
        public int AmigaColorCount = 16;

        // Amiga directory
        public string AmigaDirectory = "Amiga";

        // DOS directory
        public string DosDirectory = "DOS";

        // DOS mode selection
        public DosMode DosMode = DosMode.Pitch;

        // DOS pitch rendering
        public int PitchIndex = 0;
        public PitchType PitchType = PitchType.Normal;
        public int DosColorCount = 256; // Reduce to N colors (16-256)

        // DOS sprite options
        public byte SpriteBackgroundIndex = 0; // menu-palette index for background (color 16 in BMP)

        public List<string> Files = new();

        // --- palette export options ---
        public bool PaletteMode = false;
        public AmigaPalette.ColorFormat PaletteColorFormat = AmigaPalette.ColorFormat.Amiga12;
        public AmigaPalette.FileFormat PaletteFileFormat = AmigaPalette.FileFormat.Asm;
        public AmigaPalette.ColorCount PaletteColorCount = AmigaPalette.ColorCount.Colors16;
        public bool PaletteFull = false;
        public PaletteFormat PaletteFormat = PaletteFormat.Act;
    }

    public static int Main(string[] args)
    {
        if (args.Length < 1)
        {
            PrintUsage();
            return 1;
        }

        // New-style CLI: first arg starts with '-'
        if (args[0].StartsWith("-", StringComparison.Ordinal))
        {
            if (!TryParseOptions(args, out var opts, out string error))
            {
                if (!string.IsNullOrEmpty(error))
                    Console.Error.WriteLine("Error: " + error);
                Console.Error.WriteLine();
                PrintUsage();
                return 1;
            }

            if (opts.ShowHelp)
            {
                PrintUsage();
                return 0;
            }

            // Palette export mode is independent of Amiga/DOS.
            if (opts.PaletteMode)
            {
                try
                {
                    AmigaPalette.OutputAllPalettes(
                        opts.Files.Count > 0 ? opts.Files[0] : null,
                        opts.PaletteColorFormat,
                        opts.PaletteFileFormat,
                        opts.PaletteColorCount,
                        opts.PaletteFull,
                        opts.PaletteFormat);

                    return 0;
                }
                catch (Exception ex)
                {
                    Console.Error.WriteLine("Error: " + ex.Message);
                    return 2;
                }
            }

            // Configure global RNC writing behavior for this run.
            AmigaRncHelper.DefaultWriteAsRnc = !opts.NoRnc;

            try
            {
                if (opts.Platform == PlatformKind.Amiga)
                {
                    var conv = new AmigaTools();
                    return RunAmiga(conv, opts);
                }
                else
                {
                    return RunDos(opts);
                }
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Error: " + ex.Message);
                return 2;
            }
        }

        PrintUsage();
        return 1;
    }

    // ======================================================================
    // New CLI: parsing and dispatch
    // ======================================================================

    private static bool TryParseOptions(string[] args, out CliOptions options, out string error)
    {
        options = new CliOptions();
        error = string.Empty;

        for (int i = 0; i < args.Length; i++)
        {
            string arg = args[i];

            if (!arg.StartsWith("-", StringComparison.Ordinal))
            {
                // Positional file
                options.Files.Add(arg);
                continue;
            }

            string lower = arg.ToLowerInvariant();

            if (lower == "-h" || lower == "-?")
            {
                options.ShowHelp = true;
                return true;
            }
            else if (lower == "-amiga")
            {
                if (options.PlatformExplicitlySet && options.Platform != PlatformKind.Amiga)
                {
                    error = "Cannot specify both -amiga and -dos.";
                    return false;
                }
                options.Platform = PlatformKind.Amiga;
                options.PlatformExplicitlySet = true;
            }
            else if (lower == "-dos")
            {
                if (options.PlatformExplicitlySet && options.Platform != PlatformKind.Dos)
                {
                    error = "Cannot specify both -amiga and -dos.";
                    return false;
                }
                options.Platform = PlatformKind.Dos;
                options.PlatformExplicitlySet = true;
            }
            else if (lower.StartsWith("-amiga-dir="))
            {
                // Preserve case (do not use 'lower' substring here)
                string value = arg.Substring("-amiga-dir=".Length);
                options.AmigaDirectory = value;
            }
            else if (lower.StartsWith("-dos-dir="))
            {
                // Preserve case (do not use 'lower' substring here)
                string value = arg.Substring("-dos-dir=".Length);
                options.DosDirectory = value;
            }
            else if (lower == "-picture")
            {
                if (options.DosMode != DosMode.Pitch)
                {
                    error = "Cannot specify multiple DOS modes (-picture, -sprites-export, -sprites-import).";
                    return false;
                }
                options.DosMode = DosMode.Picture;
            }
            else if (lower == "-sprites-export")
            {
                if (options.DosMode != DosMode.Pitch)
                {
                    error = "Cannot specify multiple DOS modes (-picture, -sprites-export, -sprites-import).";
                    return false;
                }
                options.DosMode = DosMode.SpritesExport;
            }
            else if (lower == "-sprites-import")
            {
                if (options.DosMode != DosMode.Pitch)
                {
                    error = "Cannot specify multiple DOS modes (-picture, -sprites-export, -sprites-import).";
                    return false;
                }
                options.DosMode = DosMode.SpritesImport;
            }
            else if (lower.StartsWith("-sprite-bg="))
            {
                string value = lower.Substring("-sprite-bg=".Length);
                if (!byte.TryParse(value, out byte idx))
                {
                    error = $"Invalid sprite background index '{value}'. Expected 0-255.";
                    return false;
                }
                options.SpriteBackgroundIndex = idx;
            }
            else if (lower == "-map")
            {
                if (options.Input != InputKind.Unspecified && options.Input != InputKind.Map)
                {
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw.";
                    return false;
                }
                options.Input = InputKind.Map;
            }
            else if (lower == "-tmx")
            {
                if (options.Input != InputKind.Unspecified && options.Input != InputKind.Tmx)
                {
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw.";
                    return false;
                }
                options.Input = InputKind.Tmx;
            }
            else if (lower == "-bmp")
            {
                if (options.Input != InputKind.Unspecified && options.Input != InputKind.Bmp)
                {
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw.";
                    return false;
                }
                options.Input = InputKind.Bmp;
            }
            else if (lower == "-raw")
            {
                if (options.Input != InputKind.Unspecified && options.Input != InputKind.Raw)
                {
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw.";
                    return false;
                }
                options.Input = InputKind.Raw;
            }
            else if (lower.StartsWith("-output="))
            {
                string value = lower.Substring("-output=".Length);
                if (options.Output != OutputKind.Unspecified)
                {
                    error = "Multiple -output options specified.";
                    return false;
                }

                switch (value)
                {
                    case "bmp":
                        options.Output = OutputKind.Bmp;
                        break;
                    case "tmx":
                        options.Output = OutputKind.Tmx;
                        break;
                    case "map":
                        options.Output = OutputKind.Map;
                        break;
                    case "raw":
                        options.Output = OutputKind.Raw;
                        break;
                    default:
                        error = $"Unknown output type '{value}'. Expected bmp, tmx, map, or raw.";
                        return false;
                }
            }
            else if (lower.StartsWith("-palette="))
            {
                if (options.PaletteName != null)
                {
                    error = "Multiple -palette options specified.";
                    return false;
                }
                options.PaletteName = arg.Substring("-palette=".Length); // preserve case
            }
            else if (lower.StartsWith("-bitplanes="))
            {
                string value = lower.Substring("-bitplanes=".Length);
                if (!int.TryParse(value, out int bitplanes))
                {
                    error = $"Invalid bitplanes value '{value}'. Expected integer 1-8.";
                    return false;
                }
                if (bitplanes < 1 || bitplanes > 8)
                {
                    error = "Bitplanes must be between 1 and 8 (4=16 colors, 8=256 colors).";
                    return false;
                }
                options.Bitplanes = bitplanes;
                options.AmigaColorCount = AmigaPalette.BitplanesToColors(bitplanes);
            }
            else if (lower == "-no-rnc")
            {
                options.NoRnc = true;
            }
            else if (lower.StartsWith("-pitch="))
            {
                string value = lower.Substring("-pitch=".Length);
                if (!int.TryParse(value, out int idx) || idx < 0)
                {
                    error = $"Invalid pitch index '{value}'. Expected non-negative integer.";
                    return false;
                }
                options.PitchIndex = idx;
            }
            else if (lower.StartsWith("-type="))
            {
                string value = lower.Substring("-type=".Length);
                if (!TryParseDosPitchType(value, out var pt))
                {
                    error = $"Invalid DOS pitch type '{value}'. Valid: frozen, muddy, wet, soft, normal, dry, hard.";
                    return false;
                }
                options.PitchType = pt;
            }
            else if (lower.StartsWith("-colors="))
            {
                string value = lower.Substring("-colors=".Length);
                if (!int.TryParse(value, out int colorCount) || colorCount < 16 || colorCount > 256)
                {
                    error = $"Invalid colors '{value}'. Expected range 16-256.";
                    return false;
                }
                options.AmigaColorCount = colorCount;
                options.DosColorCount = colorCount;
            }
            else if (lower == "-palettes")
            {
                options.PaletteMode = true;
            }
            else if (lower.StartsWith("-pal-color="))
            {
                string v = lower.Substring("-pal-color=".Length);
                switch (v)
                {
                    case "amiga12":
                        options.PaletteColorFormat = AmigaPalette.ColorFormat.Amiga12;
                        break;
                    case "rgb32":
                        options.PaletteColorFormat = AmigaPalette.ColorFormat.RGB32;
                        break;
                    default:
                        error = $"Invalid palette color format '{v}'. Expected amiga12 or rgb32.";
                        return false;
                }
            }
            else if (lower.StartsWith("-pal-file="))
            {
                string v = lower.Substring("-pal-file=".Length);
                switch (v)
                {
                    case "asm":
                        options.PaletteFileFormat = AmigaPalette.FileFormat.Asm;
                        break;
                    case "c":
                        options.PaletteFileFormat = AmigaPalette.FileFormat.C;
                        break;
                    case "palette":
                        options.PaletteFileFormat = AmigaPalette.FileFormat.Palette;
                        break;
                    default:
                        error = $"Invalid palette file format '{v}'. Expected asm, c, or palette.";
                        return false;
                }
            }
            else if (lower.StartsWith("-pal-count="))
            {
                string v = lower.Substring("-pal-count=".Length);
                if (!int.TryParse(v, out int count))
                {
                    error = $"Invalid palette color count '{v}'. Expected 16, 128, or 256.";
                    return false;
                }

                switch (count)
                {
                    case 16:
                        options.PaletteColorCount = AmigaPalette.ColorCount.Colors16;
                        break;
                    case 128:
                        options.PaletteColorCount = AmigaPalette.ColorCount.Colors128;
                        break;
                    case 256:
                        options.PaletteColorCount = AmigaPalette.ColorCount.Colors256;
                        break;
                    default:
                        error = $"Invalid palette color count '{v}'. Expected 16, 128, or 256.";
                        return false;
                }
            }
            else if (lower == "-pal-full")
            {
                options.PaletteFull = true;
            }
            else if (lower.StartsWith("-pal-format="))
            {
                string v = lower.Substring("-pal-format=".Length);
                switch (v)
                {
                    case "act":
                        options.PaletteFormat = PaletteFormat.Act;
                        break;
                    case "mspal":
                        options.PaletteFormat = PaletteFormat.MSPal;
                        break;
                    case "jasc":
                        options.PaletteFormat = PaletteFormat.JASC;
                        break;
                    case "gimp":
                        options.PaletteFormat = PaletteFormat.GIMP;
                        break;
                    case "paintnet":
                        options.PaletteFormat = PaletteFormat.PaintNET;
                        break;
                    default:
                        error = $"Invalid palette format '{v}'. Expected act, mspal, jasc, gimp, or paintnet.";
                        return false;
                }
            }
            else
            {
                error = $"Unknown option '{arg}'.";
                return false;
            }
        }

        if (options.ShowHelp)
            return true;

        // Palette export mode: nno Amiga/DOS validations.
        if (options.PaletteMode)
        {
            // We ignore -amiga/-dos/-map/-output/etc in this mode.
            return true;
        }

        // For Amiga, DOS-specific mode flags are invalid.
        if (options.Platform == PlatformKind.Amiga && options.DosMode != DosMode.Pitch)
        {
            error = "DOS-specific options (-picture, -sprites-export, -sprites-import) can only be used with -dos.";
            return false;
        }

        // Final validation differs for Amiga vs DOS.
        if (options.Platform == PlatformKind.Amiga)
        {
            if (options.Input == InputKind.Unspecified)
            {
                error = "No input type specified. Use one of: -map, -tmx, -bmp, -raw.";
                return false;
            }

            if (options.Output == OutputKind.Unspecified)
            {
                error = "No output type specified. Use -output=bmp|tmx|map|raw.";
                return false;
            }

            if (options.Files.Count == 2)
            {
                // Existing explicit in/out behavior stays intact.
                return true;
            }

            if (options.Files.Count == 0 || options.Files.Count == 1)
            {
                // Batch mode: input comes from AmigaDirectory; optional one arg is output directory.
                bool supported =
                    (options.Input == InputKind.Raw && options.Output == OutputKind.Bmp) ||
                    (options.Input == InputKind.Map && (options.Output == OutputKind.Bmp || options.Output == OutputKind.Tmx));

                if (!supported)
                {
                    error =
                        $"In Amiga batch mode (0 or 1 positional args), supported conversions are: " +
                        $"-raw -output=bmp, -map -output=bmp, -map -output=tmx. " +
                        $"For other conversions, specify explicit input and output paths.";
                    return false;
                }

                return true;
            }

            error = $"Too many file arguments for Amiga mode. Expected 0/1 (batch) or 2 (in out), got {options.Files.Count}.";
            return false;
        }
        else // DOS
        {
            switch (options.DosMode)
            {
                case DosMode.Pitch:
                    if (options.Output == OutputKind.Unspecified)
                    {
                        error = "In -dos pitch mode, -output=bmp or -output=tmx is required.";
                        return false;
                    }

                    if (options.Output != OutputKind.Bmp && options.Output != OutputKind.Tmx)
                    {
                        error = "In -dos pitch mode, only -output=bmp or -output=tmx is supported.";
                        return false;
                    }

                    if (options.Input != InputKind.Unspecified)
                    {
                        error = "In -dos pitch mode, do not specify -map/-tmx/-bmp/-raw; input is implicit.";
                        return false;
                    }

                    // NEW: 0 args => current dir, 1 arg => output dir
                    if (options.Files.Count > 1)
                    {
                        error = $"In -dos pitch mode, expected 0 or 1 argument (output directory), got {options.Files.Count}.";
                        return false;
                    }
                    break;

                case DosMode.Picture:
                    if (options.Output == OutputKind.Unspecified)
                    {
                        error = "In -dos -picture mode, -output=bmp is required.";
                        return false;
                    }

                    if (options.Output != OutputKind.Bmp)
                    {
                        error = "In -dos -picture mode, only -output=bmp is supported.";
                        return false;
                    }

                    if (options.Input != InputKind.Unspecified)
                    {
                        error = "In -dos -picture mode, do not specify -map/-tmx/-bmp/-raw; input is implicit (.256).";
                        return false;
                    }

                    if (options.Files.Count != 2)
                    {
                        error = $"In -dos -picture mode, expected 2 file arguments (in.256 out.bmp), got {options.Files.Count}.";
                        return false;
                    }
                    break;

                case DosMode.SpritesExport:
                    if (options.Input != InputKind.Unspecified)
                    {
                        error = "In -dos -sprites-export mode, do not specify -map/-tmx/-bmp/-raw; input is implicit from DOS folder.";
                        return false;
                    }

                    if (options.Output == OutputKind.Unspecified)
                    {
                        // Default to BMP if user didn't specify.
                        options.Output = OutputKind.Bmp;
                    }
                    else if (options.Output != OutputKind.Bmp)
                    {
                        error = "In -dos -sprites-export mode, only -output=bmp is supported.";
                        return false;
                    }

                    // NEW: 0 args => current dir, 1 arg => output dir
                    if (options.Files.Count > 1)
                    {
                        error = $"In -dos -sprites-export mode, expected 0 or 1 argument (output directory), got {options.Files.Count}.";
                        return false;
                    }
                    break;

                case DosMode.SpritesImport:
                    if (options.Input != InputKind.Unspecified)
                    {
                        error = "In -dos -sprites-import mode, do not specify -map/-tmx/-bmp/-raw; input is implicit from sprNNNN.bmp files.";
                        return false;
                    }

                    if (options.Output != OutputKind.Unspecified)
                    {
                        error = "In -dos -sprites-import mode, do not specify -output=...; changes are applied in-place to DAT/SPRITE.DAT.";
                        return false;
                    }

                    if (options.Files.Count != 1)
                    {
                        error = $"In -dos -sprites-import mode, expected 1 argument (sprites directory), got {options.Files.Count}.";
                        return false;
                    }
                    break;
            }
        }

        return true;
    }

    private static int RunAmiga(AmigaTools conv, CliOptions opts)
    {
        string appPath = AppDomain.CurrentDomain.BaseDirectory;
        string amigaDir = Path.Combine(appPath, opts.AmigaDirectory);

        (uint[] pal, uint[]? newPal) GetPaletteRequired(string fileName)
        {
            if (opts.PaletteName != null)
                return (ResolvePalette(opts.PaletteName, opts.AmigaColorCount), null);

            string name = Path.GetFileNameWithoutExtension(fileName);
            uint[]? menuPalette = opts.AmigaColorCount > 16 ? DosPalette.Menu.Take(opts.AmigaColorCount).ToArray() : null;
            uint[]? gamePalette = opts.AmigaColorCount > 16 ? DosPalette.Game.Take(opts.AmigaColorCount).ToArray() : null;

            if (name.StartsWith("LOADER", StringComparison.OrdinalIgnoreCase))
            {
                string loaderIndex = name.Substring(6).ToLower();

                switch (loaderIndex)
                {
                    case "00":
                        return (AmigaPalette.PaletteFromAmiga12(AmigaPalette.TitleLogo), menuPalette);
                    case "1":
                        return (AmigaPalette.PaletteFromAmiga12(AmigaPalette.TitleScreen), menuPalette);
                    default:
                        return (AmigaPalette.PaletteFromAmiga12(AmigaPalette.TitleText), menuPalette);
                }
            }

            if (name.StartsWith("MENU", StringComparison.OrdinalIgnoreCase) ||
                name.StartsWith("SOCCER", StringComparison.OrdinalIgnoreCase) ||
                name.Contains("DISK", StringComparison.OrdinalIgnoreCase))
            {
                return (AmigaPalette.PaletteFromAmiga12(AmigaPalette.Menu), menuPalette);
            }

            if (name.StartsWith("CJC", StringComparison.OrdinalIgnoreCase) ||
                name.StartsWith("CHARSET", StringComparison.OrdinalIgnoreCase))
            {
                return (AmigaPalette.PaletteFromAmiga12(AmigaPalette.Menu), gamePalette);
            }

            return (ResolvePalette(opts.PaletteName, opts.AmigaColorCount), null);
        }

        uint[]? GetPaletteOptional()
        {
            return opts.PaletteName != null ? ResolvePalette(opts.PaletteName, opts.AmigaColorCount) : null;
        }

        bool isBatch = opts.Files.Count <= 1;

        if (!isBatch)
        {
            // Explicit input/output (existing behavior)
            string inputPath = opts.Files[0];
            string outputPath = opts.Files[1];

            switch (opts.Output)
            {
                case OutputKind.Bmp:
                    switch (opts.Input)
                    {
                        case InputKind.Map:
                            {
                                uint[]? pal = GetPaletteOptional();
                                conv.ConvertPitchMapToBmp(inputPath, pal, outputPath, null, opts.Bitplanes, opts.AmigaColorCount);
                                return 0;
                            }

                        case InputKind.Raw:
                            {
                                (var pal, var newPal) = GetPaletteRequired(opts.Files[0]);
                                conv.ConvertRawToBmp(inputPath, pal, outputPath, newPal, opts.Bitplanes, opts.AmigaColorCount);
                                return 0;
                            }

                        default:
                            throw new ArgumentException("Invalid combination: -output=bmp requires -map or -raw as input.");
                    }

                case OutputKind.Tmx:
                    if (opts.Input != InputKind.Map)
                        throw new ArgumentException("Invalid combination: -output=tmx requires -map as input.");

                    {
                        (var pal, var newPal) = GetPaletteRequired(opts.Files[0]);
                        conv.ConvertPitchMapToTiled(inputPath, pal, outputPath, newPal, opts.Bitplanes, opts.AmigaColorCount);
                        return 0;
                    }

                case OutputKind.Map:
                    switch (opts.Input)
                    {
                        case InputKind.Tmx:
                            conv.ConvertTiledToPitchMap(inputPath, outputPath, 284, opts.Bitplanes);
                            return 0;

                        case InputKind.Bmp:
                            conv.ConvertFullPitchBmpToMap(inputPath, outputPath, 284, opts.Bitplanes);
                            return 0;

                        default:
                            throw new ArgumentException("Invalid combination: -output=map requires -tmx or -bmp as input.");
                    }

                case OutputKind.Raw:
                    if (opts.Input != InputKind.Bmp)
                        throw new ArgumentException("Invalid combination: -output=raw requires -bmp as input.");

                    {
                        (var pal, var newPal) = GetPaletteRequired(opts.Files[0]);
                        conv.ConvertBmpToRaw(inputPath, pal, outputPath, opts.Bitplanes);
                        return 0;
                    }

                default:
                    throw new ArgumentException("Unknown output kind.");
            }
        }

        // -----------------------------
        // Batch mode (0/1 positional arg)
        // -----------------------------
        string outDir = opts.Files.Count == 0 ? Environment.CurrentDirectory : opts.Files[0];
        if (string.IsNullOrWhiteSpace(outDir))
            outDir = Environment.CurrentDirectory;

        Directory.CreateDirectory(outDir);

        string inputGlob;
        string outputExt;

        switch (opts.Output)
        {
            case OutputKind.Bmp:
                outputExt = ".bmp";
                break;
            case OutputKind.Tmx:
                outputExt = ".tmx";
                break;
            default:
                throw new ArgumentException("Batch mode currently supports only -output=bmp or -output=tmx in Amiga mode.");
        }

        switch (opts.Input)
        {
            case InputKind.Raw:
                if (opts.Output != OutputKind.Bmp)
                    throw new ArgumentException("Amiga batch: -raw supports only -output=bmp.");
                inputGlob = "*.raw";
                break;

            case InputKind.Map:
                if (opts.Output != OutputKind.Bmp && opts.Output != OutputKind.Tmx)
                    throw new ArgumentException("Amiga batch: -map supports only -output=bmp or -output=tmx.");
                inputGlob = "*.map";
                break;

            default:
                throw new ArgumentException("Amiga batch mode supports only -raw or -map inputs.");
        }

        int converted = 0;

        foreach (var inPath in Directory.EnumerateFiles(amigaDir, inputGlob, SearchOption.AllDirectories))
        {
            string baseName = Path.GetFileNameWithoutExtension(inPath);
            string outPath = Path.Combine(outDir, baseName + outputExt);

            switch (opts.Output)
            {
                case OutputKind.Bmp:
                    if (opts.Input == InputKind.Raw)
                    {
                        (var pal, var newPal) = GetPaletteRequired(inPath);
                        conv.ConvertRawToBmp(inPath, pal, outPath, newPal, opts.Bitplanes, opts.AmigaColorCount);
                        converted++;
                    }
                    else if (opts.Input == InputKind.Map)
                    {
                        uint[]? pal = GetPaletteOptional();
                        conv.ConvertPitchMapToBmp(inPath, pal, outPath, null, opts.Bitplanes, opts.AmigaColorCount);
                        converted++;
                    }
                    break;

                case OutputKind.Tmx:
                    if (opts.Input != InputKind.Map)
                        throw new ArgumentException("Amiga batch: -output=tmx requires -map.");
                    {
                        (var pal, var newPal) = GetPaletteRequired(inPath);
                        conv.ConvertPitchMapToTiled(inPath, pal, outPath, newPal, opts.Bitplanes, opts.AmigaColorCount);
                        converted++;
                    }
                    break;
            }
        }

        if (converted == 0)
            Console.Error.WriteLine($"Warning: No files matched '{inputGlob}' in '{amigaDir}'.");

        return 0;
    }

    private static int RunDos(CliOptions opts)
    {
        string appPath = AppDomain.CurrentDomain.BaseDirectory;
        string dosDir = Path.Combine(appPath, opts.DosDirectory);

        switch (opts.DosMode)
        {
            case DosMode.Pitch:
                {
                    string outDir = opts.Files.Count == 0 ? Environment.CurrentDirectory : opts.Files[0];
                    var patterns = DosPitchPattern.LoadFromDirectory(dosDir);
                    var pitchRenderer = new DosPitch(patterns);

                    string typeName = opts.PitchType.ToString().ToLowerInvariant();
                    string ext = opts.Output == OutputKind.Bmp ? "bmp" : "tmx";

                    for (int i = 0; i < 6; i++)
                    {
                        string fileName = $"pitch{i + 1}-{typeName}.{ext}";
                        string outPath = Path.Combine(outDir, fileName);

                        switch (opts.Output)
                        {
                            case OutputKind.Bmp:
                                pitchRenderer.SavePitchAsBmp(
                                    pitchIndex: i,
                                    pitchType: opts.PitchType,
                                    path: outPath,
                                    colorCount: opts.DosColorCount);
                                break;

                            case OutputKind.Tmx:
                                pitchRenderer.SavePitchAsTmx(
                                    pitchIndex: i,
                                    pitchType: opts.PitchType,
                                    path: outPath,
                                    colorCount: opts.DosColorCount);
                                break;

                            default:
                                throw new ArgumentException("In -dos pitch mode, only -output=bmp or -output=tmx are supported.");
                        }
                    }

                    return 0;
                }
            case DosMode.Picture:
                {
                    if (opts.Files.Count > 1)
                    {
                        string inPic = opts.Files[0];
                        string outBmp = opts.Files[1];

                        var pic = DosPicture.Load(inPic);
                        if (pic.Error != DosPictureError.None || !pic.IsLoaded)
                            throw new InvalidOperationException($"Failed to load DOS picture '{inPic}': {pic.Error}");

                        pic.SaveAsBmp(outBmp);
                    }
                    else
                    {
                        string[] files = Directory.GetFiles(dosDir, "*.256");

                        foreach (string file in files)
                        {
                            string outBmp = Path.ChangeExtension(Path.GetFileName(file), ".bmp");
                            var pic = DosPicture.Load(file);
                            if (pic.Error != DosPictureError.None || !pic.IsLoaded)
                                throw new InvalidOperationException($"Failed to load DOS picture '{file}': {pic.Error}");

                            pic.SaveAsBmp(outBmp);
                        }
                    }

                    return 0;
                }

            case DosMode.SpritesExport:
                {
                    // NEW: 0 => current dir, 1 => output directory
                    string outDir = opts.Files.Count == 0 ? Environment.CurrentDirectory : opts.Files[0];
                    if (string.IsNullOrWhiteSpace(outDir))
                        outDir = Environment.CurrentDirectory;

                    Directory.CreateDirectory(outDir);

                    var sprites = DosSprite.Load(dosDir);

                    sprites.SaveAllSpritesToDirectory(
                        directoryPath: outDir,
                        backgroundIndex: opts.SpriteBackgroundIndex);

                    return 0;
                }

            case DosMode.SpritesImport:
                {
                    string spritesDir = opts.Files[0];

                    var sprites = DosSprite.Load(dosDir);
                    sprites.InsertAllSpritesFromDirectory(spritesDir);
                    sprites.SaveChangesToSprites();

                    return 0;
                }

            default:
                throw new ArgumentOutOfRangeException();
        }
    }

    // ======================================================================
    // Shared helpers
    // ======================================================================

    private static void PrintUsage()
    {
        Assembly assembly = Assembly.GetExecutingAssembly();
        var version = assembly.GetName().Version;

        Console.WriteLine($"SwosGfx v{version.ToString(3)} - SWOS Amiga/DOS graphics tool");
        Console.WriteLine();
        Console.WriteLine("Amiga usage:");
        Console.WriteLine("  SwosGfx -amiga -map    -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.map out.bmp");
        Console.WriteLine("  SwosGfx -amiga -map    -output=tmx  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.map out.tmx");
        Console.WriteLine("  SwosGfx -amiga -tmx    -output=map  [-bitplanes=N]    [-no-rnc]      in.tmx out.map");
        Console.WriteLine("  SwosGfx -amiga -bmp    -output=map  [-bitplanes=N]    [-no-rnc]      fullPitch.bmp out.map");
        Console.WriteLine("  SwosGfx -amiga -bmp    -output=raw  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.bmp out.raw");
        Console.WriteLine("  SwosGfx -amiga -raw    -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.raw out.bmp");
        Console.WriteLine();
        Console.WriteLine("Amiga batch usage (input from ./Amiga or -amiga-dir=..., output to current dir or optional outDir):");
        Console.WriteLine("  SwosGfx -amiga -raw -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]");
        Console.WriteLine("  SwosGfx -amiga -map -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]");
        Console.WriteLine("  SwosGfx -amiga -map -output=tmx  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]");
        Console.WriteLine();
        Console.WriteLine("DOS pitch usage (patterns from ./DOS):");
        Console.WriteLine("  SwosGfx -dos -output=bmp -pitch=N -type=normal  [-colors=N] [outDir]");
        Console.WriteLine("  SwosGfx -dos -output=tmx -pitch=N -type=normal  [-colors=N] [outDir]");
        Console.WriteLine("    (pitch defaults: -pitch=0 -type=normal)");
        Console.WriteLine();
        Console.WriteLine("DOS picture usage (.256 → BMP):");
        Console.WriteLine("  SwosGfx -dos -picture -output=bmp in.256 out.bmp");
        Console.WriteLine();
        Console.WriteLine("DOS sprite usage (SPRITE.DAT + *.DAT from ./DOS):");
        Console.WriteLine("  Export all sprites to BMP:");
        Console.WriteLine("    SwosGfx -dos -sprites-export [-sprite-bg=N] [-output=bmp] [outDir]");
        Console.WriteLine("      (writes sprNNNN.bmp, 0 <= N < 1334)");
        Console.WriteLine("  Import sprites from BMP and update DAT/SPRITE.DAT:");
        Console.WriteLine("    SwosGfx -dos -sprites-import spritesDir");
        Console.WriteLine("      (reads sprNNNN.bmp, inserts and saves changes)");
        Console.WriteLine();
        Console.WriteLine("Common options:");
        Console.WriteLine("  -amiga                    Amiga mode (default)");
        Console.WriteLine("  -dos                      DOS mode");
        Console.WriteLine("  -amiga-dir=<path>         Amiga directory (default: ./Amiga)");
        Console.WriteLine("  -dos-dir=<path>           DOS directory (default: ./DOS)");
        Console.WriteLine("  -map | -tmx | -bmp | -raw Input type (Amiga only)");
        Console.WriteLine("  -output=bmp|tmx|map|raw   Output type");
        Console.WriteLine("  -palette=<name>           Palette (Amiga): Soft, Muddy, Frozen, Dry, Normal, Hard, Wet");
        Console.WriteLine("  -bitplanes=N              Bitplanes 1-8 (default 4; 4=16 colors, 8=256)");
        Console.WriteLine("  -no-rnc                   Disable RNC compression for Amiga MAP/RAW outputs");
        Console.WriteLine("  -pitch=N                  DOS pitch index (0..MaxPitch-1), default 0");
        Console.WriteLine("  -type=name                DOS pitch type: frozen, muddy, wet, soft, normal, dry, hard");
        Console.WriteLine("  -colors=N                 DOS: remap to N colors (16-256)");
        Console.WriteLine("  -picture                  DOS: operate on a .256 picture file");
        Console.WriteLine("  -sprites-export           DOS: export all sprites to sprNNNN.bmp files");
        Console.WriteLine("  -sprites-import           DOS: import sprNNNN.bmp files into DAT/SPRITE.DAT");
        Console.WriteLine("  -sprite-bg=N              DOS sprites: menu palette index for background color (in BMP slot 16)");
        Console.WriteLine("  -h, -?                    Show this help");
        Console.WriteLine();
        Console.WriteLine("Amiga raw format dimensions:");
        Console.WriteLine("  320x256, 352x272");
        Console.WriteLine();
        Console.WriteLine("Palette export (writes multiple files):");
        Console.WriteLine("  SwosGfx -palettes");
        Console.WriteLine("    [-pal-color=amiga12|rgb32]     (default: amiga12)");
        Console.WriteLine("    [-pal-file=asm|c|palette]      (default: asm)");
        Console.WriteLine("    [-pal-count=16|128|256]        (default: 16)");
        Console.WriteLine("    [-pal-full]                    (default: only pitch-affected colors for pitches)");
        Console.WriteLine("    [-pal-format=act|mspal|jasc|gimp|paintnet]  (for -pal-file=palette, default: act)");
        Console.WriteLine("    [outDir]                       Optional output directory");
        Console.WriteLine();
    }

    private static uint[] ResolvePalette(string paletteName, int colorCount)
    {
        if (paletteName == null)
            throw new ArgumentNullException(nameof(paletteName));

        string key = paletteName.Trim().ToLowerInvariant();

        for (int i = 0; i < AmigaPalette.PitchPaletteNames.Length; i++)
        {
            if (AmigaPalette.PitchPaletteNames[i].ToLowerInvariant() != key)
                continue;

            return colorCount > 16 ? DosPalette.GetPitchPalette((PitchType)i) : AmigaPalette.PaletteFromAmiga12(AmigaPalette.Pitches[i]);
        }

        if (String.Equals(key, "menu", StringComparison.OrdinalIgnoreCase))
        {
            return colorCount > 16 ? DosPalette.Menu : AmigaPalette.PaletteFromAmiga12(AmigaPalette.Menu);
        }

        if (String.Equals(key, "game", StringComparison.OrdinalIgnoreCase))
        {
            return colorCount > 16 ? DosPalette.Game : AmigaPalette.PaletteFromAmiga12(AmigaPalette.Game);
        }

        if (String.Equals(key, "titlelogo", StringComparison.OrdinalIgnoreCase))
        {
            return AmigaPalette.PaletteFromAmiga12(AmigaPalette.TitleLogo);
        }

        if (String.Equals(key, "titletext", StringComparison.OrdinalIgnoreCase))
        {
            return AmigaPalette.PaletteFromAmiga12(AmigaPalette.TitleText);
        }

        if (String.Equals(key, "titlescreen", StringComparison.OrdinalIgnoreCase))
        {
            return AmigaPalette.PaletteFromAmiga12(AmigaPalette.TitleScreen);
        }

        throw new ArgumentException(
            $"Unknown palette '{paletteName}'. Valid names: {String.Join(", ", AmigaPalette.PitchPaletteNames)}.");
    }

    private static bool TryParseDosPitchType(string value, out PitchType type)
    {
        string v = value.Trim().ToLowerInvariant();
        type = PitchType.Normal;

        for (int i = 0; i < AmigaPalette.PitchPaletteNames.Length; i++)
        {
            if (AmigaPalette.PitchPaletteNames[i].ToLowerInvariant() != v)
                continue;

            type = (PitchType)i;
            return true;
        }

        return false;
    }
}
