/* genmeta */

#include "genmeta.h"

#include "ast.h"
#include "zan.h"
#include "diag.h"
#include "lexer.h"
#include "../common/json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/host_oom.h"

typedef struct {
    json_value *calls;
    int call_id;
    char cur_cls[128];
    char cur_fn[128];
    zan_ast_node_t **rec_nodes;
    int *rec_ids;
    int rec_count;
    int rec_cap;
    zan_ast_node_t **pend_nodes;
    int *pend_ids;
    int pend_count;
    int pend_cap;
    const char *const *expr_names;
    int expr_count;
    const char *const *table_names;
    int table_count;
} gm_ctx_t;

/* Composite Dispatcher Submodules */
#include "genmeta/genmeta_json.c"
#include "genmeta/genmeta_export.c"
