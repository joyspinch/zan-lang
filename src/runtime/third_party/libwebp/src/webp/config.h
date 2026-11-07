/* zan_gui unity-build stub (HAVE_CONFIG_H is defined only around the vendored
 * libwebp includes in gui_runtime.c). Keeps the baseline-x86-64 SSE2 dispatch
 * and drops SSE41, whose intrinsics would need per-file -msse4.1 flags that a
 * single-TU compile cannot express. Threads stay off (no WEBP_USE_THREAD). */
#if defined(__x86_64__) || defined(_M_X64)
#define WEBP_HAVE_SSE2 1
#endif
