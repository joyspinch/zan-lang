/* intellisense */
#ifndef ZAN_INTELLISENSE_H
#define ZAN_INTELLISENSE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 内部辅助逻辑 */
extern volatile int intel_cancel_flag;

/* 模块核心语义抽象与接口调用契约 */
int intel_find_text_references(const char *text, const char *word, size_t *offsets, int max);

/* 内部辅助逻辑 */
#define INTEL_INIT_SYMBOLS     1024
#define INTEL_INIT_FILES       64
#define INTEL_MAX_COMPLETIONS  512
#define INTEL_MAX_SNIPPETS     32
#define INTEL_MAX_SIGNATURES   16
#define INTEL_MAX_PARAMS       16

/* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */
typedef enum {
    IVIS_PUBLIC = 0,
    IVIS_PROTECTED,
    IVIS_PRIVATE
} ivis_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char        name[128];
    char        type_name[128];     /* 模块核心语义抽象与接口调用契约 */
    char        parent[128];        /* 核心系统底层抽象与内存语义契约 */
    char        signature[256];     /* 模块核心语义抽象与接口调用契约 */
    char        file[512];          /* 核心系统底层抽象与内存语义契约 */
    char        doc[256];           /* 核心系统底层抽象与内存语义契约 */
    isym_kind_t kind;
    int         line;               /* 核心系统底层抽象与内存语义契约 */
    int         col;                /* 核心系统底层抽象与内存语义契约 */
    bool        is_static;
    ivis_t      visibility;
    int         param_count;        /* number of parameters (methods) */
    int         offset;             /* 底层系统交互与数据协议契约 */
    int         method_offset;      /* 核心系统底层抽象与内存语义契约 */
    int         scope_start_line;   /* 核心系统底层抽象与内存语义契约 */
    int         scope_start_col;    /* 核心系统底层抽象与内存语义契约 */
    int         scope_end_line;
    int         scope_end_col;
    int         scope_start_offset;
    int         scope_end_offset;
} isym_t;

/* 模块核心语义抽象与接口调用契约 */
typedef struct {
    char        name[128];
    char        parent[128];        /* 核心系统底层抽象与内存语义契约 */
    char        file[512];
    int         start_line;         /* 核心系统底层抽象与内存语义契约 */
    int         end_line;           /* 核心系统底层抽象与内存语义契约 */
    int         start_col;          /* 核心系统底层抽象与内存语义契约 */
    int         end_col;            /* 核心系统底层抽象与内存语义契约 */
    int         start_offset;
    int         end_offset;
    int         decl_offset;        /* 底层系统交互与数据协议契约 */
} imethod_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char        label[128];         /* 核心系统底层抽象与内存语义契约 */
    char        insert_text[256];   /* 核心系统底层抽象与内存语义契约 */
    char        detail[256];        /* 核心系统底层抽象与内存语义契约 */
    char        doc[256];           /* documentation */
    isym_kind_t kind;
    int         sort_priority;      /* 核心系统底层抽象与内存语义契约 */
} completion_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char        text[512];          /* 核心系统底层抽象与内存语义契约 */
    char        doc[256];           /* documentation */
    bool        valid;
} hover_info_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char        file[512];
    int         line;
    int         col;
    bool        found;
} goto_def_t;

/* 底层系统交互与数据协议契约 */
typedef struct {
    char        label[64];          /* 核心系统底层抽象与内存语义契约 */
    char        type[64];           /* 核心系统底层抽象与内存语义契约 */
    char        doc[128];           /* 核心系统底层抽象与内存语义契约 */
} param_info_t;

typedef struct {
    char        label[256];         /* 核心系统底层抽象与内存语义契约 */
    char        doc[256];           /* 核心系统底层抽象与内存语义契约 */
    param_info_t params[INTEL_MAX_PARAMS];
    int         param_count;
    int         active_param;       /* 核心系统底层抽象与内存语义契约 */
    bool        valid;
} signature_info_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char        trigger[64];        /* 核心系统底层抽象与内存语义契约 */
    char        label[128];         /* 核心系统底层抽象与内存语义契约 */
    char        body[512];          /* 核心系统底层抽象与内存语义契约 */
    char        description[128];
} snippet_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    isym_t      *symbols;           /* grown by intel_parse_file */
    int         symbol_count;
    int         symbol_cap;

    imethod_t   *methods;           /* 核心系统底层抽象与内存语义契约 */
    int         method_count;
    int         method_cap;

    completion_t completions[INTEL_MAX_COMPLETIONS];
    int          completion_count;
    int          completion_selected;
    bool         completion_active;
    int          completion_x;      /* 核心系统底层抽象与内存语义契约 */
    int          completion_y;

    /* 核心系统底层抽象与内存语义契约 */
    signature_info_t signatures[INTEL_MAX_SIGNATURES];
    int          signature_count;
    int          signature_active;
    bool         signature_visible;

    /* 核心系统底层抽象与内存语义契约 */
    snippet_t    snippets[INTEL_MAX_SNIPPETS];
    int          snippet_count;

    /* 核心系统底层抽象与内存语义契约 */
    char         current_file[512];
    char         current_word[128];
    int          current_word_start;

    /* 模块核心语义抽象与接口调用契约 */
    char         (*indexed_files)[512];
    int          indexed_file_count;
    int          indexed_file_cap;
} intellisense_t;

/* Initialize */
void intel_init(intellisense_t *is);

/* 核心系统底层抽象与内存语义契约 */
void intel_parse_file(intellisense_t *is, const char *filepath,
                      const char *content, size_t len);

/* 模块核心语义抽象与接口调用契约 */
void intel_parse_file_ex(intellisense_t *is, intellisense_t *project,
                         const char *filepath, const char *content, size_t len);

/* 核心系统底层抽象与内存语义契约 */
void intel_clear(intellisense_t *is);

/* 底层系统交互与数据协议契约 */
void intel_free(intellisense_t *is);

/* 核心系统底层抽象与内存语义契约 */
int intel_complete_pos(intellisense_t *is, const char *prefix,
                       const char *context_class, int line, int col);
int intel_complete_at(intellisense_t *is, const char *prefix,
                      const char *context_class, int line);
int intel_complete(intellisense_t *is, const char *prefix,
                   const char *context_class);

/* 内部辅助逻辑 */
int intel_complete_bare(intellisense_t *is, const char *prefix,
                        const char *from_class);

/* 底层系统交互与数据协议契约 */
int intel_complete_members_pos(intellisense_t *is, const char *type_name,
                               const char *prefix, int line, int col);
int intel_complete_members_at(intellisense_t *is, const char *type_name,
                              const char *prefix, int line);
int intel_complete_members(intellisense_t *is, const char *type_name,
                           const char *prefix);

/* 内部辅助逻辑 */
int intel_complete_usings(intellisense_t *is, intellisense_t *project,
                          const char *ns_prefix);

/* 模块核心语义抽象与接口调用契约 */
hover_info_t intel_hover_pos(intellisense_t *is, const char *word, int line, int col);
hover_info_t intel_hover_at(intellisense_t *is, const char *word, int line);
hover_info_t intel_hover(intellisense_t *is, const char *word);
/* 核心系统底层抽象与内存语义契约 */
hover_info_t intel_hover_member(intellisense_t *is, const char *type_name,
                                const char *member);
/* 核心系统底层抽象与内存语义契约 */
bool intel_goto_member(intellisense_t *is, const char *type_name,
                       const char *member, goto_def_t *out);

/* Go to definition of a symbol */
goto_def_t intel_goto_def(intellisense_t *is, const char *word);

/* 模块核心语义抽象与接口调用契约 */
signature_info_t intel_signature_help_pos_ex(intellisense_t *is, intellisense_t *project,
                                             const char *method_name, const char *class_context,
                                             int line, int col);
signature_info_t intel_signature_help_pos(intellisense_t *is, const char *method_name,
                                          const char *class_context, int line, int col);
signature_info_t intel_signature_help(intellisense_t *is, const char *method_name,
                                      const char *class_context);

/* 模块核心语义抽象与接口调用契约 */
const char *intel_accept(intellisense_t *is);

/* 核心系统底层抽象与内存语义契约 */
void intel_select_up(intellisense_t *is);
void intel_select_down(intellisense_t *is);

/* 核心系统底层抽象与内存语义契约 */
void intel_dismiss(intellisense_t *is);

/* 核心系统底层抽象与内存语义契约 */
void intel_register_snippets(intellisense_t *is);

/* 核心系统底层抽象与内存语义契约 */
int intel_find_references(intellisense_t *is, const char *word,
                          goto_def_t *results, int max_results);

/* 内部辅助逻辑 */
bool intel_local_extent(intellisense_t *is, const char *word, int line,
                        int *out_start_line, int *out_end_line,
                        int *out_decl_line);

/* 核心系统底层抽象与内存语义契约 */
bool intel_same_file(const char *a, const char *b);
const isym_t *intel_lookup_symbol_at(intellisense_t *is, const char *word,
                                    int line, int col);
/* 内部辅助逻辑 */
bool intel_is_keyword(const char *word);
/* 内部辅助逻辑 */
const isym_t *intel_lookup_symbol_any(intellisense_t *is, const char *word);
bool intel_symbol_visible_at(const intellisense_t *is, const isym_t *sym,
                             int line, int col);
bool intel_position_in(int line, int col, int start_line, int start_col,
                       int end_line, int end_col);
const char *intel_enclosing_type_at(const intellisense_t *is, int line, int col);
const imethod_t *intel_method_at(const intellisense_t *is, int line, int col);
const char *intel_resolve_type_pos(intellisense_t *is, const char *var_name,
                                 int line, int col);

/* 底层系统交互与数据协议契约 */
const char *intel_resolve_type_at(intellisense_t *is, const char *var_name, int line);
const char *intel_resolve_type(intellisense_t *is, const char *var_name);

/* 模块核心语义抽象与接口调用契约 */
const char *intel_resolve_method_return_ex(intellisense_t *is, intellisense_t *project,
                                           const char *type_name, const char *method_name);
const char *intel_resolve_method_return(intellisense_t *is, const char *type_name,
                                        const char *method_name);

/* 模块核心语义抽象与接口调用契约 */
const char *intel_resolve_chain_pos(intellisense_t *is, intellisense_t *project,
                                    const char *chain, char *final_member, size_t final_cap,
                                    int line, int col);
const char *intel_resolve_chain_ex(intellisense_t *is, intellisense_t *project,
                                   const char *chain, char *final_member, size_t final_cap,
                                   int line);
const char *intel_resolve_chain(intellisense_t *is, const char *chain,
                                char *final_member, size_t final_cap);

/* 核心系统底层抽象与内存语义契约 */

/* 核心系统底层抽象与内存语义契约 */
void intel_index_project(intellisense_t *is, const char *project_root);

/* 核心系统底层抽象与内存语义契约 */
void intel_index_files(intellisense_t *is, const char **filepaths, int count);

/* 核心系统底层抽象与内存语义契约 */

/* 核心系统底层抽象与内存语义契约 */
#define INTEL_MAX_USINGS 64

typedef struct {
    char namespace_name[128];
    int  line;           /* 底层系统交互与数据协议契约 */
    bool is_used;        /* 核心系统底层抽象与内存语义契约 */
} using_entry_t;

typedef struct {
    using_entry_t usings[INTEL_MAX_USINGS];
    int           using_count;
    /* 模块核心语义抽象与接口调用契约 */
    char          missing_usings[INTEL_MAX_USINGS][128];
    int           missing_count;
    /* 底层系统交互与数据协议契约 */
    int           unused_indices[INTEL_MAX_USINGS];
    int           unused_count;
} using_analysis_t;

/* 模块核心语义抽象与接口调用契约 */
using_analysis_t intel_analyze_usings(intellisense_t *is, const char *content, size_t len);

/* 底层系统交互与数据协议契约 */
void intel_format_using(const char *namespace_name, char *out, size_t out_cap);

/* 模块核心语义抽象与接口调用契约 */
char *intel_organize_usings(intellisense_t *is, const char *content, size_t len,
                            size_t *out_len);

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    int  line;          /* 核心系统底层抽象与内存语义契约 */
    int  col;           /* 核心系统底层抽象与内存语义契约 */
    char label[64];     /* 核心系统底层抽象与内存语义契约 */
    int  kind;          /* 1 = Type, 2 = Parameter */
} intel_inlay_hint_t;

/* 模块核心语义抽象与接口调用契约 */
int intel_collect_inlay_hints(intellisense_t *is, const char *content, size_t len,
                             intel_inlay_hint_t *hints, int max_hints);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_INTELLISENSE_H */
