// options.h: the options of NativeDraw, all in one file on the Vita: ux0:data/nativedraw/options.txt, one "name=value"
// per line, read once when Adrenaline opens (nativedraw.c, optLoad). A missing line is the default (off, or the value
// each reader assumes); a value starting with "no" turns an on/off option off. The names and values:
// docs/dev/en/logs-and-switches.md; the development measurements are also listed in measures.h.
#ifndef OPTIONS_H
#define OPTIONS_H
void optLoad(void);
const char *optStr(const char *name);  // the value of name's line up to the end of the line, or NULL without the line
int optOn(const char *name);           // 1 if the line exists and its value does not start with "no"
void optSet(const char *name, const char *value);  // rewrites options.txt with that line changed or added
#endif
