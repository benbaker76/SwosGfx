// paths.h -- where the data folders are, and small file-system helpers.
#pragma once

#include <string>

// The folder this program is in ("" when it cannot be found).
std::string ExeDir(const char *argv0);

// A -dos-dir/-amiga-dir/-jag-dir value: absolute as given; relative, the
// current folder's when it exists there, else the one beside the program (the
// C# SwosGfx looked only beside itself).
std::string ResolveDataDir(const std::string &dir, const char *argv0);

bool IsDir(const std::string &path);
bool IsFile(const std::string &path);
std::string JoinPath(const std::string &a, const std::string &b);
std::string CurrentDir();
