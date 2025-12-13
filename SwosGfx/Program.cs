using SwosGfx;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;

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
        public RawFormat? RawFormat;
        public string? PaletteName;
        public int Bitplanes = 4; // default 4 planes → 16 colors
        public bool NoRnc = false;

        // DOS directory
        public string DosDirectory = "DOS";

        // DOS mode selection
        public DosMode DosMode = DosMode.Pitch;

        // DOS pitch rendering
        public int PitchIndex = 0;
        public DosPitchType PitchType = DosPitchType.Normal;
        public int Colors = 256; // Reduce to N colors (16-256)

        // DOS sprite options
        public byte SpriteBackgroundIndex = 0; // menu-palette index for background (color 16 in BMP)

        public List<string> Files = new();

        // --- NEW: palette export options ---
        public bool PaletteMode = false;
        public AmigaPalette.ColorFormat PaletteColorFormat = AmigaPalette.ColorFormat.Amiga12;
        public AmigaPalette.FileFormat PaletteFileFormat = AmigaPalette.FileFormat.Asm;
        public AmigaPalette.ColorCount PaletteColorCount = AmigaPalette.ColorCount.Colors16;
        public bool PaletteFull = false;
        public PaletteFormat PaletteFormat = PaletteFormat.Act;
    }

    public static int Main(string[] args)
    {
        //args = new string[] { "-map", "-output=bmp", "-bitplanes=7", "Amiga_AGA\\SWCPICH1.MAP", "pitch1-amiga-aga.bmp" };
        //args = new string[] { "-palettes", "-pal-color=rgb32", "-pal-file=palette", "-pal-count=256", "-pal-full", "-pal-format=act" };

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
            else if (lower.StartsWith("-dos-dir="))
            {
                string value = lower.Substring("-dos-dir=".Length);
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
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw320, -raw352.";
                    return false;
                }
                options.Input = InputKind.Map;
            }
            else if (lower == "-tmx")
            {
                if (options.Input != InputKind.Unspecified && options.Input != InputKind.Tmx)
                {
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw320, -raw352.";
                    return false;
                }
                options.Input = InputKind.Tmx;
            }
            else if (lower == "-bmp")
            {
                if (options.Input != InputKind.Unspecified && options.Input != InputKind.Bmp)
                {
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw320, -raw352.";
                    return false;
                }
                options.Input = InputKind.Bmp;
            }
            else if (lower == "-raw320")
            {
                if (options.Input != InputKind.Unspecified && options.Input != InputKind.Raw)
                {
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw320, -raw352.";
                    return false;
                }
                if (options.RawFormat.HasValue && options.RawFormat.Value != RawFormat.Raw320x256)
                {
                    error = "Conflicting RAW formats specified (-raw320 and -raw352).";
                    return false;
                }
                options.Input = InputKind.Raw;
                options.RawFormat = RawFormat.Raw320x256;
            }
            else if (lower == "-raw352")
            {
                if (options.Input != InputKind.Unspecified && options.Input != InputKind.Raw)
                {
                    error = "Multiple input kinds specified. Use only one of: -map, -tmx, -bmp, -raw320, -raw352.";
                    return false;
                }
                if (options.RawFormat.HasValue && options.RawFormat.Value != RawFormat.Raw352x272)
                {
                    error = "Conflicting RAW formats specified (-raw320 and -raw352).";
                    return false;
                }
                options.Input = InputKind.Raw;
                options.RawFormat = RawFormat.Raw352x272;
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
                if (!int.TryParse(value, out int planes))
                {
                    error = $"Invalid bitplanes value '{value}'. Expected integer 1-8.";
                    return false;
                }
                if (planes < 1 || planes > 8)
                {
                    error = "Bitplanes must be between 1 and 8 (4=16 colors, 8=256 colors).";
                    return false;
                }
                options.Bitplanes = planes;
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
                if (!int.TryParse(value, out int colors) || colors < 16 || colors > 256)
                {
                    error = $"Invalid colors '{value}'. Expected range 16-256.";
                    return false;
                }
                options.Colors = colors;
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

        // Palette export mode: no input/output files, no Amiga/DOS validations.
        if (options.PaletteMode)
        {
            if (options.Files.Count != 0)
            {
                error = "Palette export (-palettes) does not take input/output files.";
                return false;
            }

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
                error = "No input type specified. Use one of: -map, -tmx, -bmp, -raw320, -raw352.";
                return false;
            }

            if (options.Output == OutputKind.Unspecified)
            {
                error = "No output type specified. Use -output=bmp|tmx|map|raw.";
                return false;
            }

            if ((options.Input == InputKind.Raw || options.Output == OutputKind.Raw) && !options.RawFormat.HasValue)
            {
                error = "RAW format must be specified using -raw320 or -raw352.";
                return false;
            }

            if (options.Files.Count != 2)
            {
                error = $"Expected 2 file arguments (input and output), got {options.Files.Count}.";
                return false;
            }
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
                        error = "In -dos pitch mode, do not specify -map/-tmx/-bmp/-raw320/-raw352; input is implicit.";
                        return false;
                    }

                    if (options.Files.Count != 1)
                    {
                        error = $"In -dos pitch mode, expected 1 file argument (output path), got {options.Files.Count}.";
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
                        error = "In -dos -picture mode, do not specify -map/-tmx/-bmp/-raw320/-raw352; input is implicit (.256).";
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
                        error = "In -dos -sprites-export mode, do not specify -map/-tmx/-bmp/-raw320/-raw352; input is implicit from DOS folder.";
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

                    if (options.Files.Count != 1)
                    {
                        error = $"In -dos -sprites-export mode, expected 1 argument (output directory), got {options.Files.Count}.";
                        return false;
                    }
                    break;

                case DosMode.SpritesImport:
                    if (options.Input != InputKind.Unspecified)
                    {
                        error = "In -dos -sprites-import mode, do not specify -map/-tmx/-bmp/-raw320/-raw352; input is implicit from sprNNNN.bmp files.";
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
        string inputPath = opts.Files[0];
        string outputPath = opts.Files[1];
        int bitplanes = opts.Bitplanes;

        uint[] GetPaletteRequired()
        {
            if (opts.PaletteName == null)
                return opts.Bitplanes > 4 ? DosPalette.Menu : AmigaPalette.Menu;

            return ResolvePalette(opts.PaletteName, opts.Bitplanes);
        }

        uint[]? GetPaletteOptional()
        {
            return opts.PaletteName != null ? ResolvePalette(opts.PaletteName, opts.Bitplanes) : null;
        }

        switch (opts.Output)
        {
            case OutputKind.Bmp:
                switch (opts.Input)
                {
                    case InputKind.Map:
                        {
                            uint[]? pal = GetPaletteOptional();
                            conv.ConvertPitchMapToBmp(inputPath, pal, outputPath, bitplanes);
                            return 0;
                        }

                    case InputKind.Raw:
                        {
                            if (!opts.RawFormat.HasValue)
                                throw new ArgumentException("RAW format must be specified using -raw320 or -raw352.");

                            uint[] pal = GetPaletteRequired();
                            conv.ConvertRawToBmp(inputPath, pal, outputPath, opts.RawFormat.Value, bitplanes);
                            return 0;
                        }

                    default:
                        throw new ArgumentException("Invalid combination: -output=bmp requires -map or -raw320/-raw352 as input.");
                }

            case OutputKind.Tmx:
                if (opts.Input != InputKind.Map)
                    throw new ArgumentException("Invalid combination: -output=tmx requires -map as input.");

                {
                    uint[] pal = GetPaletteRequired();
                    conv.ConvertPitchMapToTiled(inputPath, pal, outputPath, bitplanes);
                    return 0;
                }

            case OutputKind.Map:
                switch (opts.Input)
                {
                    case InputKind.Tmx:
                        conv.ConvertTiledToPitchMap(inputPath, outputPath, 284, bitplanes);
                        return 0;

                    case InputKind.Bmp:
                        conv.ConvertFullPitchBmpToMap(inputPath, outputPath, 284, bitplanes);
                        return 0;

                    default:
                        throw new ArgumentException("Invalid combination: -output=map requires -tmx or -bmp as input.");
                }

            case OutputKind.Raw:
                if (opts.Input != InputKind.Bmp)
                    throw new ArgumentException("Invalid combination: -output=raw requires -bmp as input.");

                if (!opts.RawFormat.HasValue)
                    throw new ArgumentException("RAW format must be specified using -raw320 or -raw352.");

                {
                    uint[] pal = GetPaletteRequired();
                    conv.ConvertBmpToRaw(inputPath, pal, outputPath, opts.RawFormat.Value, bitplanes);
                    return 0;
                }

            default:
                throw new ArgumentException("Unknown output kind.");
        }
    }

    private static int RunDos(CliOptions opts)
    {
        string appPath = AppDomain.CurrentDomain.BaseDirectory;
        string dosDir = Path.Combine(appPath, opts.DosDirectory);

        switch (opts.DosMode)
        {
            case DosMode.Pitch:
                {
                    string outPath = opts.Files[0];
                    var patterns = DosPitchPattern.LoadFromDirectory(dosDir);
                    var pitchRenderer = new DosPitch(patterns);

                    switch (opts.Output)
                    {
                        case OutputKind.Bmp:
                            pitchRenderer.SavePitchAsBmp(
                                pitchIndex: opts.PitchIndex,
                                pitchType: opts.PitchType,
                                path: outPath,
                                colorCount: opts.Colors);
                            return 0;

                        case OutputKind.Tmx:
                            pitchRenderer.SavePitchAsTmx(
                                pitchIndex: opts.PitchIndex,
                                pitchType: opts.PitchType,
                                path: outPath,
                                colorCount: opts.Colors);
                            return 0;

                        default:
                            throw new ArgumentException("In -dos pitch mode, only -output=bmp or -output=tmx are supported.");
                    }
                }

            case DosMode.Picture:
                {
                    string inPic = opts.Files[0];
                    string outBmp = opts.Files[1];

                    var pic = DosPicture.Load(inPic);
                    if (pic.Error != DosPictureError.None || !pic.IsLoaded)
                        throw new InvalidOperationException($"Failed to load DOS picture '{inPic}': {pic.Error}");

                    pic.SaveAsBmp(outBmp);
                    return 0;
                }

            case DosMode.SpritesExport:
                {
                    string outDir = opts.Files[0];
                    Directory.CreateDirectory(outDir);

                    var sprites = DosSprite.Load(dosDir);

                    // Assumes you have static palettes somewhere, e.g. DosPal.MenuPalette / DosPal.GamePalette.
                    // Adjust these names if your actual palette class uses different identifiers.
                    uint[] menuPal = DosPalette.Menu;
                    uint[] gamePal = DosPalette.Game;

                    sprites.SaveAllSpritesToDirectory(
                        directoryPath: outDir,
                        backgroundIndex: opts.SpriteBackgroundIndex,
                        menuPal: menuPal,
                        gamePal: gamePal);

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
        FileVersionInfo fileVersionInfo = FileVersionInfo.GetVersionInfo(assembly.Location);

        Console.WriteLine($"SwosGfx v{fileVersionInfo.ProductVersion} - SWOS Amiga/DOS graphics tool");
        Console.WriteLine();
        Console.WriteLine("Amiga usage:");
        Console.WriteLine("  SwosGfx -amiga -map    -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.map out.bmp");
        Console.WriteLine("  SwosGfx -amiga -map    -output=tmx  -palette=<name>   [-bitplanes=N] [-no-rnc] in.map out.tmx");
        Console.WriteLine("  SwosGfx -amiga -tmx    -output=map  [-bitplanes=N]    [-no-rnc]      in.tmx out.map");
        Console.WriteLine("  SwosGfx -amiga -bmp    -output=map  [-bitplanes=N]    [-no-rnc]      fullPitch.bmp out.map");
        Console.WriteLine("  SwosGfx -amiga -bmp    -output=raw  -raw320 -palette=<name> [-bitplanes=N] [-no-rnc] in.bmp out.raw");
        Console.WriteLine("  SwosGfx -amiga -bmp    -output=raw  -raw352 -palette=<name> [-bitplanes=N] [-no-rnc] in.bmp out.raw");
        Console.WriteLine("  SwosGfx -amiga -raw320 -output=bmp  -palette=<name> [-bitplanes=N] [-no-rnc] in.raw out.bmp");
        Console.WriteLine("  SwosGfx -amiga -raw352 -output=bmp  -palette=<name> [-bitplanes=N] [-no-rnc] in.raw out.bmp");
        Console.WriteLine();
        Console.WriteLine("DOS pitch usage (patterns from ./DOS):");
        Console.WriteLine("  SwosGfx -dos -output=bmp -pitch=N -type=normal  [-colors128] out.bmp");
        Console.WriteLine("  SwosGfx -dos -output=tmx -pitch=N -type=normal  [-colors128] out.tmx");
        Console.WriteLine("    (pitch defaults: -pitch=0 -type=normal)");
        Console.WriteLine();
        Console.WriteLine("DOS picture usage (.256 → BMP):");
        Console.WriteLine("  SwosGfx -dos -picture -output=bmp in.256 out.bmp");
        Console.WriteLine();
        Console.WriteLine("DOS sprite usage (SPRITE.DAT + *.DAT from ./DOS):");
        Console.WriteLine("  Export all sprites to BMP:");
        Console.WriteLine("    SwosGfx -dos -sprites-export [-sprite-bg=N] [-output=bmp] outDir");
        Console.WriteLine("      (writes sprNNNN.bmp, 0 <= N < 1334)");
        Console.WriteLine("  Import sprites from BMP and update DAT/SPRITE.DAT:");
        Console.WriteLine("    SwosGfx -dos -sprites-import spritesDir");
        Console.WriteLine("      (reads sprNNNN.bmp, inserts and saves changes)");
        Console.WriteLine();
        Console.WriteLine("Common options:");
        Console.WriteLine("  -amiga                    Amiga mode (default)");
        Console.WriteLine("  -dos                      DOS mode");
        Console.WriteLine("  -dos-dir=<path>           DOS directory (default: ./DOS)");
        Console.WriteLine("  -map | -tmx | -bmp        Input type (Amiga only)");
        Console.WriteLine("  -raw320 | -raw352         RAW input or RAW format (Amiga only)");
        Console.WriteLine("  -output=bmp|tmx|map|raw   Output type");
        Console.WriteLine("  -palette=<name>           Palette (Amiga): Soft, Muddy, Frozen, Dry, Normal, Hard, Wet");
        Console.WriteLine("  -bitplanes=N              Bitplanes 1-8 (default 4; 4=16 colors, 8=256)");
        Console.WriteLine("  -no-rnc                   Disable RNC compression for Amiga MAP/RAW outputs");
        Console.WriteLine("  -pitch=N                  DOS pitch index (0..MaxPitch-1), default 0");
        Console.WriteLine("  -type=name                DOS pitch type: frozen, muddy, wet, soft, normal, dry, hard");
        Console.WriteLine("  -colors=N                 DOS: remap to N colors");
        Console.WriteLine("  -picture                  DOS: operate on a .256 picture file");
        Console.WriteLine("  -sprites-export           DOS: export all sprites to sprNNNN.bmp files");
        Console.WriteLine("  -sprites-import           DOS: import sprNNNN.bmp files into DAT/SPRITE.DAT");
        Console.WriteLine("  -sprite-bg=N              DOS sprites: menu palette index for background color (in BMP slot 16)");
        Console.WriteLine("  -h, -?                    Show this help");
        Console.WriteLine();
        Console.WriteLine("Bitplanes (optional, default 4):");
        Console.WriteLine();
        Console.WriteLine("  4 = 16 colors, 5 = 32, 6 = 64, 7 = 128, 8 = 256");
        Console.WriteLine();
        Console.WriteLine("Palette export (no input/output files, writes multiple files):");
        Console.WriteLine("  SwosGfx -palettes");
        Console.WriteLine("    [-pal-color=amiga12|rgb32]     (default: amiga12)");
        Console.WriteLine("    [-pal-file=asm|c|palette]      (default: asm)");
        Console.WriteLine("    [-pal-count=16|128|256]        (default: 16)");
        Console.WriteLine("    [-pal-full]                    (default: only pitch-affected colors for pitches)");
        Console.WriteLine("    [-pal-format=act|mspal|jasc|gimp|paintnet]  (for -pal-file=palette, default: act)");
        Console.WriteLine();
    }

    private static uint[] ResolvePalette(string paletteName, int bitplanes)
    {
        if (paletteName == null)
            throw new ArgumentNullException(nameof(paletteName));

        string key = paletteName.Trim().ToLowerInvariant();

        for (int i = 0; i < AmigaPalette.PitchPaletteNames.Length; i++)
        {
            if (AmigaPalette.PitchPaletteNames[i].ToLowerInvariant() != key)
                continue;

            return bitplanes > 4 ? DosPalette.Pitches[i] : AmigaPalette.Pitches[i];
        }

        throw new ArgumentException(
                    $"Unknown palette '{paletteName}'. Valid names: {String.Join(", ", AmigaPalette.PitchPaletteNames)}.");
    }

    private static bool TryParseDosPitchType(string value, out DosPitchType type)
    {
        string v = value.Trim().ToLowerInvariant();

        for (int i = 0; i < AmigaPalette.PitchPaletteNames.Length; i++)
        {
            if (AmigaPalette.PitchPaletteNames[i].ToLowerInvariant() != v)
                continue;

            type = (DosPitchType)i;
            return true;
        }

        type = DosPitchType.Normal;
        return false;
    }
}
