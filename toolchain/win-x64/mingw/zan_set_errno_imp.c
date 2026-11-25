/* MSYS2's winpthreads calls _set_errno through a dllimport reference
 * (__imp__set_errno), but the msvcrt-flavoured mingw-w64 CRT implements
 * _set_errno statically in libmsvcrt.a without the __imp_ pointer. This thunk
 * supplies that pointer so a static cross link resolves. */
extern int _set_errno(int value);
int (*__imp__set_errno)(int) = _set_errno;
