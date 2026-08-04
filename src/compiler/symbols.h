/* symbols.h -- `zanc --emit-symbols`: the API surface of a compilation as a
 * flat text index.
 *
 * The IDE's completion engine and the language server used to carry their own
 * hand-written copy of what the standard library offers, which drifted from the
 * compiler (offering File.OpenRead(), Math.Clamp(), string.PadLeft() -- none of
 * which existed). This dumps the real thing: every type and member the parser
 * saw, plus the built-in types from builtin_api.c.
 *
 * The format is one record per line, TAB-separated, so consumers need no JSON
 * parser:
 *
 *   T<TAB>kind<TAB>namespace<TAB>name<TAB>bases(comma-separated)
 *   M<TAB>ownerType<TAB>name<TAB>kind<TAB>static<TAB>signature<TAB>flags
 *
 * `kind` is one of class/struct/interface/enum for types and M/P/F/E/C
 * (method/property/field/event/constructor) for members. `flags` is optional
 * and carries `x` for an extern (DllImport) declaration, which is an FFI
 * binding rather than API a caller should be offered. Built-in types are
 * emitted as ordinary T/M records with the namespace `*builtin*`.
 */

#ifndef ZAN_SYMBOLS_H
#define ZAN_SYMBOLS_H

#include "ast.h"

/* Writes the index for `unit` to `path`. Returns 0 on success. */
int zan_symbols_emit(zan_ast_node_t *unit, const char *path);

#endif /* ZAN_SYMBOLS_H */
