/* Included by lsp_main.c after the project reference collector. */

typedef struct {
    const semantic_ref_t *ref;
    uint32_t new_offset;
} project_rename_edit_t;

static bool rename_location_before(zan_loc_t a, zan_loc_t b) {
    return a.file_id < b.file_id || (a.file_id == b.file_id && a.offset < b.offset);
}

static size_t rename_ref_at(const semantic_refs_t *refs, zan_loc_t loc) {
    size_t lo = 0, hi = refs->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (rename_location_before(refs->items[mid].occurrence, loc)) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Check source tokens for uses that the compiler has yet to observe. */
static bool project_rename_covers_name(lsp_server_t *s, const lsp_project_refs_t *project,
                                       const char *name, size_t length) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    bool covered = true;
    for (uint32_t file = 0; covered && file <= (uint32_t)project->sources.count; file++) {
        const char *text = project_reference_source(project, file)->text;
        zan_lexer_t lex;
        zan_lexer_init(&lex, text, strlen(text), file, arena, diag);
        for (zan_token_t token = zan_lexer_next(&lex); token.kind != TK_EOF;
             token = zan_lexer_next(&lex)) {
            if (lsp_cancel_hit(s)) { covered = false; break; }
            if (token.kind != TK_IDENT || token.str_val.len != length ||
                memcmp(token.str_val.str, name, length) != 0) continue;
            size_t i = rename_ref_at(&project->refs, token.loc);
            if (i == project->refs.count ||
                !semantic_ref_same_location(project->refs.items[i].occurrence, token.loc)) {
                covered = false;
                break;
            }
        }
    }
    covered = covered && !zan_diag_has_errors(diag);
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
    return covered;
}

static uint32_t project_rename_offset(const project_rename_edit_t *edits, size_t count,
                                      uint32_t length, zan_loc_t loc) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (rename_location_before(edits[mid].ref->occurrence, loc)) lo = mid + 1;
        else hi = mid;
    }
    if (lo < count && semantic_ref_same_location(edits[lo].ref->occurrence, loc))
        return edits[lo].new_offset;
    if (lo == 0 || edits[lo - 1].ref->occurrence.file_id != loc.file_id) return loc.offset;
    const project_rename_edit_t *prev = &edits[lo - 1];
    return prev->new_offset + length + loc.offset - prev->ref->occurrence.offset -
           prev->ref->occurrence_length;
}

static bool project_rename_bindings_match(lsp_server_t *s, const semantic_refs_t *before,
                                          const semantic_refs_t *after,
                                          const semantic_ref_t *symbol,
                                          const project_rename_edit_t *edits, size_t count,
                                          uint32_t length) {
    semantic_ref_t renamed = *symbol;
    renamed.declaration.offset = project_rename_offset(edits, count, length, symbol->declaration);
    bool ambiguous = false;
    size_t seen = 0;
    for (size_t i = 0; i < after->count;) {
        if (lsp_cancel_hit(s)) return false;
        if (semantic_refs_next(after, &renamed, &i, &ambiguous)) seen++;
    }
    if (ambiguous || seen != count) return false;

    for (size_t i = 0; i < before->count; i++) {
        if (lsp_cancel_hit(s)) return false;
        semantic_ref_t expected = before->items[i];
        expected.occurrence.offset = project_rename_offset(edits, count, length, expected.occurrence);
        expected.declaration.offset = project_rename_offset(edits, count, length, expected.declaration);
        size_t j = rename_ref_at(after, expected.occurrence);
        while (j < after->count &&
               semantic_ref_same_location(after->items[j].occurrence, expected.occurrence) &&
               !semantic_ref_same_symbol(&after->items[j], &expected)) j++;
        if (j == after->count ||
            !semantic_ref_same_location(after->items[j].occurrence, expected.occurrence)) return false;
    }
    return true;
}

static bool project_rename_file(const lsp_diag_source_t *source, char **edited,
                                project_rename_edit_t *edits, size_t count,
                                const char *new_name, json_value *changes) {
    size_t length = strlen(source->text), new_length = strlen(new_name);
    if (length >= UINT32_MAX || count > (UINT32_MAX - 1 - length) / new_length) return false;
    char *text = malloc(length + count * new_length + 1);
    if (!text) zan_host_oom();
    *edited = text;
    json_value *file_edits = json_new_arr();
    json_obj_set(changes, source->uri, file_edits);
    size_t read = 0, write = 0;
    for (size_t i = 0; i < count; i++) {
        project_rename_edit_t *edit = &edits[i];
        const semantic_ref_t *ref = edit->ref;
        size_t at = ref->occurrence.offset, old_length = ref->occurrence_length;
        if (at < read || at > length || old_length > length - at || old_length != ref->name.len ||
            memcmp(source->text + at, ref->name.str, old_length) != 0) return false;
        memcpy(text + write, source->text + read, at - read);
        write += at - read;
        edit->new_offset = (uint32_t)write;
        memcpy(text + write, new_name, new_length);
        write += new_length;
        read = at + old_length;
        json_value *item = json_new_obj();
        json_obj_set(item, "range", semantic_reference_range(source->text, ref->occurrence,
                                                             ref->occurrence_length));
        json_obj_set(item, "newText", json_new_str(new_name));
        json_arr_add(file_edits, item);
    }
    memcpy(text + write, source->text + read, length - read + 1);
    return true;
}

static json_value *project_rename_changes(lsp_server_t *s, const lsp_project_refs_t *project,
                                          const semantic_ref_t *symbol, const char *new_name) {
    size_t name_length = strlen(new_name);
    if (name_length > UINT32_MAX || !project_rename_covers_name(s, project, new_name, name_length))
        return NULL;
    project_rename_edit_t *edits = calloc(project->refs.count, sizeof(*edits));
    if (!edits) zan_host_oom();
    size_t count = 0;
    bool ambiguous = false;
    for (size_t i = 0; i < project->refs.count;) {
        const semantic_ref_t *ref = semantic_refs_next(&project->refs, symbol, &i, &ambiguous);
        if (ref) edits[count++].ref = ref;
    }
    size_t file_count = (size_t)project->sources.count + 1;
    lsp_diag_source_t *buffers = calloc(file_count, sizeof(*buffers));
    if (!buffers) zan_host_oom();
    lsp_project_refs_t rebound = {.sources = project->sources};
    rebound.sources.items = buffers + 1;
    json_value *changes = json_new_obj();
    bool valid = count > 0 && !ambiguous;
    size_t first = 0;
    for (uint32_t file = 0; valid && file < file_count; file++) {
        if (lsp_cancel_hit(s)) { valid = false; break; }
        const lsp_diag_source_t *source = project_reference_source(project, file);
        buffers[file] = *source;
        size_t end = first;
        while (end < count && edits[end].ref->occurrence.file_id == file) end++;
        if (end == first) continue;
        valid = project_rename_file(source, &buffers[file].text, edits + first, end - first,
                                    new_name, changes);
        first = end;
    }
    if (valid && first == count) {
        rebound.primary = buffers[0];
        check_project_references(s, &rebound);
        valid = rebound.complete && project_rename_bindings_match(s, &project->refs, &rebound.refs,
                                                                  symbol, edits, count,
                                                                  (uint32_t)name_length);
    } else {
        valid = false;
    }
    clear_project_reference_analysis(&rebound);
    for (uint32_t file = 0; file < file_count; file++) {
        if (buffers[file].text != project_reference_source(project, file)->text)
            free(buffers[file].text);
    }
    free(buffers);
    free(edits);
    if (!valid) { json_free(changes); return NULL; }
    json_value *result = json_new_obj();
    json_obj_set(result, "changes", changes);
    return result;
}

static bool handle_project_rename(lsp_server_t *s, json_value *id, const lsp_doc_t *doc,
                                  int line, int character, const char *new_name) {
    lsp_project_refs_t project = {0};
    collect_project_references(s, doc, &project);
    bool handled = project.sources.workspace || lsp_cancel_hit(s);
    json_value *result = NULL;
    bool ambiguous = false, conflict = false;
    if (project.sources.workspace && !lsp_cancel_hit(s)) {
        lsp_doc_unlock(s);
        const semantic_ref_t *symbol = lookup_project_references(&project, line, character, &ambiguous);
        if (symbol && (symbol->kind == SYM_LOCAL || symbol->kind == SYM_PARAM)) {
            handled = false;
        } else if (symbol && project.complete && symbol->renameable &&
                   project_rename_covers_name(s, &project, symbol->name.str, symbol->name.len)) {
            if (new_name) {
                result = project_rename_changes(s, &project, symbol, new_name);
                conflict = result == NULL;
            } else {
                result = json_new_obj();
                json_obj_set(result, "range", semantic_reference_range(project.primary.text,
                    symbol->occurrence, symbol->occurrence_length));
                json_obj_set(result, "placeholder", json_new_str(symbol->name.str));
            }
        }
        lsp_doc_lock(s);
    }
    free_project_references(&project);
    if (lsp_cancel_hit(s)) {
        json_free(result);
        send_response_error(s, id, -32800, "Request cancelled");
        return true;
    }
    if (!handled) return false;
    if (ambiguous || conflict) {
        send_response_error(s, id, -32602, ambiguous ? "The symbol binding is ambiguous" :
                            "Cannot rename without changing or leaving an unverified binding");
    } else {
        send_response(s, id, result ? result : json_new_null());
    }
    return true;
}
