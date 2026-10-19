// swosgfx -- the command line over the swosgfx library: the C# SwosGfx's
// switches (options.cpp), each mode one call into lib/ (modes.cpp).
#include <cstdio>
#include <string>
#include <vector>

#include "modes.h"
#include "options.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    std::vector<std::string> args(argv + 1, argv + argc);
    Options opts;
    std::string error;
    if (!ParseOptions(args, opts, error)) {
        if (!error.empty())
            std::fprintf(stderr, "Error: %s\n\n", error.c_str());
        PrintUsage();
        return 1;
    }
    if (opts.showHelp) {
        PrintUsage();
        return 0;
    }
    if (opts.showVersion) {
        std::printf("swosgfx %s\n", SWOSGFX_VERSION);
        return 0;
    }
    return RunMode(opts, argv[0]);
}
