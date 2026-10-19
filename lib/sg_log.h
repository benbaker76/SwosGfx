/*
 * sg_log.h -- where a converter's progress lines go.
 *
 * The C# SwosGfx prints a line per step to the console ("DOS -> Amiga: wrote 11
 * banks to ...", "  CJCBENCH  56 Jaguar /   5 DOS", "Scaled ..."). The library
 * does not print: an entry point that has such lines takes a SgLogFn and hands
 * it each line, without the newline, word for word as SwosGfx wrote it. The
 * command line prints them to stdout; the setup app can show them or pass NULL
 * to have none.
 */
#ifndef SWOSGFX_LOG_H
#define SWOSGFX_LOG_H

typedef void (*SgLogFn)(void *user, const char *line);

#endif
