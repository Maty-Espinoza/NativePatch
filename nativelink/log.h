// NativeLink log (implementation in log.c, included by main.c) and plugin version. Also defines the descriptor
// shared with NativeDraw (VDESC), through which the Vita writes the log to the card.
#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <psptypes.h>

// Log in ms0:/nativelink.log (the previous session stays in nativelink_prev.log). Lines go to a RAM buffer and a
// thread writes them every ~1 s, so the game thread never waits for the card. Holding R when opening the game enables
// sync mode: each line is written at once (slower, but nothing is lost if the game hangs).
// Each line starts with the frame number (how many times sceDisplaySetFrameBuf was called).

#define NATIVELINK_VERSION "0.0.3.6" // tools/build.sh reads it from here

extern u32 log_frame;

void logInit(int sync);
void logWrite(const char *s, int n);
void logFlush(void);
void logGameId(void);
int logOnce(u32 key); // 1 the first time 'key' is seen, 0 afterwards

// Channel with NativeDraw: the Vita writes the log, so the PSP does not wait for the card. It sits in an uncached page
// of the plugin zone; the mailbox has seq (raised by the PSP when posting a request) and done (set equal by NativeDraw
// when finished).
//   +0x00 "GPV1": NativeLink is present (restored on each flip: Adrenaline 8 clears the zone when loading the game)
//   +0x20 "LOGV": NativeDraw serves the mailbox (always restored)
//   +0x40 log: seq, done, bytes (text in VLOG_BUF, 16 KB)
// Without NativeDraw (or if it does not respond in 2 s) the PSP writes with sceIo to ms0:/nativelink.log.
#define VDESC ((volatile u32 *)(0x0A7FF000 | 0x40000000))
#define VLOG 16
#define VLOG_BUF 0x0A7FA000

// Writes a printf-format line (up to 160 bytes with the frame number; the excess is cut)
#define LOG(...) do { \
  char _b[256]; \
  int _n = snprintf(_b, sizeof(_b), "%06lu ", log_frame); \
  _n += snprintf(_b + _n, sizeof(_b) - _n, __VA_ARGS__); \
  logWrite(_b, _n < (int)sizeof(_b) ? _n : (int)sizeof(_b) - 1); \
} while (0)

// For events repeated every frame: only written the first time per 'key'
#define LOG_ONCE(key, ...) do { if (logOnce(key)) LOG(__VA_ARGS__); } while (0)

#endif
