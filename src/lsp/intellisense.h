/* intellisense.h -- Code intelligence (autocomplete, go-to-def, hover, signatures).
 *
 * Provides code intelligence by parsing the current file and collecting
 * symbols (classes, methods, variables, types) for autocomplete, navigation,
 * and signature help.
 */
#ifndef ZAN_INTELLISENSE_H
#define ZAN_INTELLISENSE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The symbol table and the indexed-file list grow on demand: a fixed 4096
 * symbols and 64 files silently truncated the index of any real project (the
 * standard library alone is several hundred files), so completion went quiet
 * with no way to tell why. These are only the initial capacities. */
#define INTEL_INIT_SYMBOLS     1024
#define INTEL_INIT_FILES       64
#define INTEL_MAX_COMPLETIONS  512
#define INTEL_MAX_SNIPPETS     32
#define INTEL_MAX_SIGNATURES   16
#define INTEL_MAX_PARAMS       16

/* Symbol kinds */
typedef enum {
    ISYM_CLASS,
    ISYM_STRUCT,
    ISYM_ENUM,
    ISYM_INTERFACE,
    ISYM_METHOD,
    ISYM_FIELD,
    ISYM_PROPERTY,
    ISYM_VARIABLE,
    ISYM_PARAMETER,
    ISYM_KEYWORD,
    ISYM_TYPE,
    ISYM_NAMESPACE,
    ISYM_ENUM_MEMBER,
    ISYM_EVENT,
    ISYM_SNIPPET,
    ISYM_CONSTRUCTOR
} isym_kind_t;

/* A symbol entry */
typedef struct {
    char        name[128];
    char        type_name[64];      /* return type or field type */
    char        parent[128];        /* enclosing class/struct */
    char        signature[256];     /* full signature for methods */
    char        file[512];          /* source file */
    char        doc[256];           /* documentation comment */
    isym_kind_t kind;
    int         line;               /* 0-based line number */
    int         col;                /* 0-based column */
    bool        is_static;
    int         param_count;        /* number of parameters (methods) */
} isym_t;

/* A method body extent (for scope-aware rename/references of locals). */
typedef struct {
    char        name[128];
    char        parent[128];        /* enclosing class */
    char        file[512];
    int         start_line;         /* 0-based line of the body '{' */
    int         end_line;           /* 0-based line of the matching '}' */
} imethod_t;

/* Autocomplete suggestion */
typedef struct {
    char        label[128];         /* display text */
    char        insert_text[256];   /* text to insert (may include snippets) */
    char        detail[256];        /* type/signature info */
    char        doc[256];           /* documentation */
    isym_kind_t kind;
    int         sort_priority;      /* lower = higher priority */
} completion_t;

/* Hover info */
typedef struct {
    char        text[512];          /* hover display text */
    char        doc[256];           /* documentation */
    bool        valid;
} hover_info_t;

/* Go-to-definition result */
typedef struct {
    char        file[512];
    int         line;
    int         col;
    bool        found;
} goto_def_t;

/* Signature help: parameter info for method calls */
typedef struct {
    char        label[64];          /* parameter name */
    char        type[64];           /* parameter type */
    char        doc[128];           /* parameter doc */
} param_info_t;

typedef struct {
    char        label[256];         /* full signature display */
    char        doc[256];           /* method documentation */
    param_info_t params[INTEL_MAX_PARAMS];
    int         param_count;
    int         active_param;       /* which param cursor is at */
    bool        valid;
} signature_info_t;

/* Snippet template */
typedef struct {
    char        trigger[64];        /* trigger word (e.g. "for") */
    char        label[128];         /* display label */
    char        body[512];          /* expanded text with $1, $2 placeholders */
    char        description[128];
} snippet_t;

/* Intellisense engine state */
typedef struct {
    isym_t      *symbols;           /* grown by intel_parse_file */
    int         symbol_count;
    int         symbol_cap;

    imethod_t   *methods;           /* method body extents, grown alongside */
    int         method_count;
    int         method_cap;

    completion_t completions[INTEL_MAX_COMPLETIONS];
    int          completion_count;
    int          completion_selected;
    bool         completion_active;
    int          completion_x;      /* screen position for popup */
    int          completion_y;

    /* Signature help state */
    signature_info_t signatures[INTEL_MAX_SIGNATURES];
    int          signature_count;
    int          signature_active;
    bool         signature_visible;

    /* Snippet definitions */
    snippet_t    snippets[INTEL_MAX_SNIPPETS];
    int          snippet_count;

    /* current context */
    char         current_file[512];
    char         current_word[128];
    int          current_word_start;

    /* multi-file index: track which files have been indexed */
    char         (*indexed_files)[512];
    int          indexed_file_count;
    int          indexed_file_cap;
} intellisense_t;

/* Initialize */
void intel_init(intellisense_t *is);

/* Parse a file and extract symbols */
void intel_parse_file(intellisense_t *is, const char *filepath,
                      const char *content, size_t len);

/* Clear all symbols */
void intel_clear(intellisense_t *is);

/* Release the symbol table and file list. The struct itself is the caller's
 * (it is usually malloc'd for one request). */
void intel_free(intellisense_t *is);

/* Request autocomplete at the given position.
 * `prefix` is the partial word typed so far.
 * `context_class` is the type before the dot (for member access).
 * Returns number of completions available. */
int intel_complete(intellisense_t *is, const char *prefix,
                   const char *context_class);

/* Request member completions for a specific type.
 * Called when user types "varName." or "ClassName." */
int intel_complete_members(intellisense_t *is, const char *type_name,
                           const char *prefix);

/* Namespace completions for a `using` directive: the stdlib namespace
 * map plus every namespace declared in this file and in `project`
 * (the shared project index; may be NULL). `ns_prefix` is the partial
 * namespace typed after the keyword ("" = list all). */
int intel_complete_usings(intellisense_t *is, intellisense_t *project,
                          const char *ns_prefix);

/* Get hover info for a symbol at the given name */
hover_info_t intel_hover(intellisense_t *is, const char *word);

/* Go to definition of a symbol */
goto_def_t intel_goto_def(intellisense_t *is, const char *word);

/* Signature help: get method signatures for the given method name */
signature_info_t intel_signature_help(intellisense_t *is, const char *method_name,
                                      const char *class_context);

/* Accept the selected completion. Returns the text to insert. */
const char *intel_accept(intellisense_t *is);

/* Move completion selection up/down */
void intel_select_up(intellisense_t *is);
void intel_select_down(intellisense_t *is);

/* Dismiss the completion popup */
void intel_dismiss(intellisense_t *is);

/* Register built-in snippets */
void intel_register_snippets(intellisense_t *is);

/* Find all references to a symbol */
int intel_find_references(intellisense_t *is, const char *word,
                          goto_def_t *results, int max_results);

/* Scope-aware lookup: if `word` names a local variable or parameter declared
 * inside the method whose body encloses `line` (all 0-based), return true and
 * set the enclosing method's inclusive body extent plus the symbol's
 * declaration line (parameters may sit above the body's '{' on a multi-line
 * signature; either pointer may be NULL). False when the word is not a local
 * of that method (callers fall back to document-wide scans). */
bool intel_local_extent(intellisense_t *is, const char *word, int line,
                        int *out_start_line, int *out_end_line,
                        int *out_decl_line);

/* Resolve the type of a variable name from context */
const char *intel_resolve_type(intellisense_t *is, const char *var_name);

/* Resolve the return type of a method call on a given type.
 * For example: intel_resolve_method_return(is, "List", "Where") -> "List"
 * Used for chain-call completion. */
const char *intel_resolve_method_return(intellisense_t *is, const char *type_name,
                                        const char *method_name);

/* Resolve the type at the end of a chain expression like "a.Method1().Method2"
 * `chain` is the full expression text (e.g. "myList.Where(x => x > 0).Select")
 * Returns the resolved type of the last element before the final dot.
 * `final_member` receives the text after the last dot (the completion prefix). */
const char *intel_resolve_chain(intellisense_t *is, const char *chain,
                                char *final_member, size_t final_cap);

/* --- Project-wide indexing --- */

/* Index all .zan files in a project directory (recursive).
 * `project_root` is the base directory. After indexing, all symbols
 * from the project are available for completion. */
void intel_index_project(intellisense_t *is, const char *project_root);

/* Index a list of files explicitly */
void intel_index_files(intellisense_t *is, const char **filepaths, int count);

/* --- Auto-using management --- */

/* Result of auto-using analysis */
#define INTEL_MAX_USINGS 64

typedef struct {
    char namespace_name[128];
    int  line;           /* line number where the using statement is */
    bool is_used;        /* whether anything in this namespace is referenced */
} using_entry_t;

typedef struct {
    using_entry_t usings[INTEL_MAX_USINGS];
    int           using_count;
    /* Namespaces that should be added (because a type is used but not imported) */
    char          missing_usings[INTEL_MAX_USINGS][128];
    int           missing_count;
    /* Usings that can be removed (not referenced) */
    int           unused_indices[INTEL_MAX_USINGS];
    int           unused_count;
} using_analysis_t;

/* Analyze using statements in a file: find missing and unused usings.
 * `content` is the file text, `len` its length.
 * Returns analysis result. */
using_analysis_t intel_analyze_usings(intellisense_t *is, const char *content, size_t len);

/* Generate the text for a "using" line to add */
void intel_format_using(const char *namespace_name, char *out, size_t out_cap);

/* Organize usings: sort alphabetically, remove unused, add missing.
 * Returns the new file content (caller must free). */
char *intel_organize_usings(intellisense_t *is, const char *content, size_t len,
                            size_t *out_len);

/* --- Inlay hints --- */
typedef struct {
    int  line;          /* 0-based line */
    int  col;           /* 0-based UTF-16 character/column */
    char label[64];     /* display label (e.g. ": int" or "count:") */
    int  kind;          /* 1 = Type, 2 = Parameter */
} intel_inlay_hint_t;

/* Collect inlay hints (inferred types for var, parameter names at call sites) */
int intel_collect_inlay_hints(intellisense_t *is, const char *content, size_t len,
                             intel_inlay_hint_t *hints, int max_hints);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_INTELLISENSE_H */
