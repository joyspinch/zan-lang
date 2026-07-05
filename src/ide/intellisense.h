/* intellisense.h -- Code intelligence (autocomplete, go-to-def, hover).
 *
 * Provides basic code intelligence by parsing the current file and collecting
 * symbols (classes, methods, variables, types) for autocomplete and navigation.
 */
#ifndef ZAN_INTELLISENSE_H
#define ZAN_INTELLISENSE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INTEL_MAX_SYMBOLS   2048
#define INTEL_MAX_COMPLETIONS 64

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
    ISYM_NAMESPACE
} isym_kind_t;

/* A symbol entry */
typedef struct {
    char        name[128];
    char        type_name[64];      /* return type or field type */
    char        parent[128];        /* enclosing class/struct */
    char        signature[256];     /* full signature for methods */
    char        file[512];          /* source file */
    isym_kind_t kind;
    int         line;               /* 0-based line number */
    int         col;                /* 0-based column */
} isym_t;

/* Autocomplete suggestion */
typedef struct {
    char        label[128];         /* display text */
    char        insert_text[128];   /* text to insert */
    char        detail[256];        /* type/signature info */
    isym_kind_t kind;
} completion_t;

/* Hover info */
typedef struct {
    char        text[512];          /* hover display text */
    bool        valid;
} hover_info_t;

/* Go-to-definition result */
typedef struct {
    char        file[512];
    int         line;
    int         col;
    bool        found;
} goto_def_t;

/* Intellisense engine state */
typedef struct {
    isym_t      symbols[INTEL_MAX_SYMBOLS];
    int         symbol_count;

    completion_t completions[INTEL_MAX_COMPLETIONS];
    int          completion_count;
    int          completion_selected;
    bool         completion_active;
    int          completion_x;      /* screen position for popup */
    int          completion_y;

    /* current context */
    char         current_file[512];
    char         current_word[128];
    int          current_word_start;
} intellisense_t;

/* Initialize */
void intel_init(intellisense_t *is);

/* Parse a file and extract symbols */
void intel_parse_file(intellisense_t *is, const char *filepath,
                      const char *content, size_t len);

/* Clear all symbols */
void intel_clear(intellisense_t *is);

/* Request autocomplete at the given position.
 * `prefix` is the partial word typed so far.
 * Returns number of completions available. */
int intel_complete(intellisense_t *is, const char *prefix,
                   const char *context_class);

/* Get hover info for a symbol at the given name */
hover_info_t intel_hover(intellisense_t *is, const char *word);

/* Go to definition of a symbol */
goto_def_t intel_goto_def(intellisense_t *is, const char *word);

/* Accept the selected completion. Returns the text to insert. */
const char *intel_accept(intellisense_t *is);

/* Move completion selection up/down */
void intel_select_up(intellisense_t *is);
void intel_select_down(intellisense_t *is);

/* Dismiss the completion popup */
void intel_dismiss(intellisense_t *is);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_INTELLISENSE_H */
