// version.h: NativeDraw version. Used by nativedraw.c and capture.c (logs) and presenter.c (menu).
#define NATIVEDRAW_VERSION "0.0.18"
// Development builds (-DNP_DEV, the default; PROD=1 in build.sh leaves it out) carry the measurements: DEV(x) is x
// there and nothing in production.
#ifdef NP_DEV
#define DEV(...) __VA_ARGS__
#else
#define DEV(...)
#endif
