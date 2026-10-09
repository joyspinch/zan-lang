/* symbols.h -- `zanc --emit-symbols`: the API surface of a compilation (every
 * parsed type/member plus builtin_api.c's built-ins) as a flat TSV index for
 * the IDE completion engine and language server.
 *
 * One record per line, TAB-separated:
 *   T<TAB>kind<TAB>namespace<TAB>name<TAB>bases(comma-separated)
 *   M<TAB>ownerType<TAB>name<TAB>kind<TAB>static<TAB>signature<TAB>flags
 * `kind`: class/struct/interface/enum for types; M/P/F/E/C (method/property/
 * field/event/constructor) for members. `flags` is optional; `x` = extern
 * (DllImport) FFI binding. Built-in types are ordinary T/M records with the
 * namespace `*builtin*`.
 */

#ifndef ZAN_SYMBOLS_H
#define ZAN_SYMBOLS_H

#include "ast.h"

/* Writes the index for `unit` to `path`. Returns 0 on success. */
int zan_symbols_emit(zan_ast_node_t *unit, const char *path);

#endif /* ZAN_SYMBOLS_H */
