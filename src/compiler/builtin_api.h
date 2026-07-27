/* builtin_api.h -- the member surface of the compiler's built-in types.
 *
 * `string`, `List<T>`, `Dictionary<K,V>`, `StringBuilder` and the static
 * classes (Console, Math, File, ...) are lowered directly by irgen instead of
 * being declared in the standard library, so nothing in the AST describes what
 * they actually support. This table is that description, and it has exactly two
 * consumers:
 *
 *   - irgen's "has no member" diagnostic: a call the compiler cannot lower is
 *     an error instead of a silently emitted constant 0;
 *   - `zanc --emit-symbols`, which the IDE and the language server read so
 *     completion offers what the compiler accepts.
 *
 * Anything missing here that is genuinely supported turns into a spurious
 * error, so only add a member together with its lowering (or a standard-library
 * extension method, which resolves before the diagnostic).
 */

#ifndef ZAN_BUILTIN_API_H
#define ZAN_BUILTIN_API_H

typedef struct {
    const char *name;
    char kind;        /* 'M' method, 'P' property, 'F' field */
    const char *sig;  /* display signature for completion / signature help */
} zan_builtin_member_t;

typedef struct {
    const char *type;         /* receiver type name as irgen sees it */
    const char *name_public;  /* name the language spells it with ("Dictionary") */
    const char *display;      /* name shown in diagnostics ("Dictionary<K,V>") */
    int is_static;            /* 1 = static class (Console.X), 0 = instance */
    const zan_builtin_member_t *members;
    int member_count;
} zan_builtin_type_t;

/* All built-in types, in declaration order. */
const zan_builtin_type_t *zan_builtin_types(int *count);

/* The entry for `type` (irgen's internal name, e.g. "Dict"), or NULL. */
const zan_builtin_type_t *zan_builtin_find(const char *type);

/* Whether `type` has a member named `name` (length-delimited, not
 * NUL-terminated). Unknown types answer 1 so callers never report an error for
 * a receiver this table says nothing about. */
int zan_builtin_has_member(const char *type, const char *name, int name_len);

/* The kind ('M', 'P', 'F') of `type`'s member `name`, or 'M' when the table
   says nothing about the type or the member, so callers never reject a
   receiver this table does not describe. */
char zan_builtin_member_kind(const char *type, const char *name, int name_len);

/* The declared result of `type`'s member `name` -- the leading word of its
   signature, e.g. "string" for File.ReadAllText -- or NULL when the table does
   not describe it. */
const char *zan_builtin_member_result(const char *type, const char *name,
                                      int name_len);

#endif /* ZAN_BUILTIN_API_H */
