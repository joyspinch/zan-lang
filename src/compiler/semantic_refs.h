#ifndef ZAN_SEMANTIC_REFS_H
#define ZAN_SEMANTIC_REFS_H

#include "checker.h"
#include "host_oom.h"
#include <stdlib.h>

/* The checker interns names in the arena. Consume the results before freeing
 * it. */
typedef struct {
  zan_istr_t name;
  zan_sym_kind_t kind;
  bool is_declaration;
  bool renameable;
  zan_loc_t declaration;
  zan_loc_t occurrence;
  uint32_t declaration_length;
  uint32_t occurrence_length;
} semantic_ref_t;

typedef struct {
  uint32_t file_id;
  bool all_files;
  semantic_ref_t *items;
  size_t count;
  size_t capacity;
} semantic_refs_t;

static inline bool semantic_ref_same_location(zan_loc_t a, zan_loc_t b) {
  return a.file_id == b.file_id && a.offset == b.offset;
}

static void semantic_refs_append(semantic_refs_t *refs, semantic_ref_t ref) {
  if (refs->count == refs->capacity) {
    if (refs->capacity > SIZE_MAX / (2 * sizeof(*refs->items)))
      zan_host_oom();
    refs->capacity = refs->capacity ? 2 * refs->capacity : 128;
    refs->items = realloc(refs->items, refs->capacity * sizeof(*refs->items));
    if (!refs->items)
      zan_host_oom();
  }
  refs->items[refs->count++] = ref;
}

static void semantic_refs_record(void *context, zan_istr_t name,
                                 bool is_parameter, zan_loc_t declaration,
                                 zan_loc_t occurrence) {
  semantic_refs_t *refs = context;
  if (!refs->all_files && (occurrence.file_id != refs->file_id ||
                           declaration.file_id != refs->file_id))
    return;
  semantic_refs_append(
      refs,
      (semantic_ref_t){name, is_parameter ? SYM_PARAM : SYM_LOCAL,
                       semantic_ref_same_location(declaration, occurrence),
                       true, declaration, occurrence, name.len, name.len});
}

static inline void
semantic_refs_record_symbol(void *context, const zan_symbol_t *symbol,
                            const zan_ast_node_t *occurrence) {
  const zan_ast_node_t *decl = symbol->decl;
  if (!decl->name_len || !occurrence->name_len || !decl->name_loc.line ||
      !occurrence->name_loc.line)
    return;
  zan_istr_t name = zan_ast_orig_name(decl);
  if (!name.len)
    name = symbol->name;
  bool renameable =
      symbol->kind != SYM_CONSTRUCTOR && name.len == decl->name_len &&
      !(symbol->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT)) &&
      (!symbol->parent || symbol->parent->kind != SYM_INTERFACE);
  semantic_ref_t ref = {
      name,
      symbol->kind,
      semantic_ref_same_location(decl->name_loc, occurrence->name_loc),
      renameable,
      decl->name_loc,
      occurrence->name_loc,
      decl->name_len,
      occurrence->name_len};
  semantic_refs_append(context, ref);
  if (occurrence == decl && decl->meta) {
    const zan_ast_list_t *parts = &decl->meta->partial_decls;
    for (int i = 0; i < parts->count; i++) {
      const zan_ast_node_t *part = parts->items[i];
      if (!part->name_len)
        continue;
      ref.occurrence = part->name_loc;
      ref.occurrence_length = part->name_len;
      semantic_refs_append(context, ref);
    }
  }
}

static int semantic_ref_source_order(const void *left, const void *right) {
  zan_loc_t a = ((const semantic_ref_t *)left)->occurrence;
  zan_loc_t b = ((const semantic_ref_t *)right)->occurrence;
  if (a.file_id != b.file_id)
    return (a.file_id > b.file_id) - (a.file_id < b.file_id);
  return (a.offset > b.offset) - (a.offset < b.offset);
}

static bool semantic_ref_same_symbol(const semantic_ref_t *a,
                                     const semantic_ref_t *b) {
  return a->kind == b->kind &&
         semantic_ref_same_location(a->declaration, b->declaration);
}

static void semantic_refs_sort(semantic_refs_t *refs) {
  if (refs->count > 1)
    qsort(refs->items, refs->count, sizeof(*refs->items),
          semantic_ref_source_order);
}

static const semantic_ref_t *semantic_refs_select(const semantic_refs_t *refs,
                                                  uint32_t line, uint32_t col,
                                                  bool *ambiguous) {
  const semantic_ref_t *symbol = NULL;
  for (size_t i = 0; i < refs->count; i++) {
    const semantic_ref_t *ref = &refs->items[i];
    if (ref->occurrence.file_id != refs->file_id ||
        ref->occurrence.line != line || col < ref->occurrence.col ||
        col - ref->occurrence.col >= ref->occurrence_length)
      continue;
    if (symbol && !semantic_ref_same_symbol(symbol, ref)) {
      if (ambiguous)
        *ambiguous = true;
      return NULL;
    }
    symbol = ref;
  }
  return symbol;
}

/* Advance over one source token. Specialization and repeated checking can
 * report it more than once. */
static const semantic_ref_t *semantic_refs_next(const semantic_refs_t *refs,
                                                const semantic_ref_t *symbol,
                                                size_t *index,
                                                bool *ambiguous) {
  const semantic_ref_t *ref = &refs->items[(*index)++];
  bool match = semantic_ref_same_symbol(symbol, ref);
  bool other = !match;
  while (*index < refs->count &&
         semantic_ref_same_location(refs->items[*index].occurrence,
                                    ref->occurrence)) {
    bool same = semantic_ref_same_symbol(symbol, &refs->items[(*index)++]);
    match |= same;
    other |= !same;
  }
  if (match && other)
    *ambiguous = true;
  return match && !other ? ref : NULL;
}

#endif
