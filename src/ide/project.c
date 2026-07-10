/* project.c -- Project tree implementation. */
#include "project.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "../common/host_oom.h"
/* ---------- helpers ---------- */

static int cmp_entries(const void *a, const void *b) {
    const project_entry_t *ea = (const project_entry_t *)a;
    const project_entry_t *eb = (const project_entry_t *)b;
    /* directories first, then alphabetical */
    if (ea->type != eb->type) return ea->type == ENTRY_DIR ? -1 : 1;
    return _stricmp(ea->name, eb->name);
}

static bool should_skip(const char *name) {
    if (name[0] == '.') return true;
    if (_stricmp(name, "build") == 0) return true;
    if (_stricmp(name, "node_modules") == 0) return true;
    if (_stricmp(name, "__pycache__") == 0) return true;
    if (_stricmp(name, ".git") == 0) return true;
    return false;
}

#ifdef _WIN32
static void scan_dir(project_tree_t *pt, const char *dir_path, int depth) {
    if (pt->entry_count >= PROJECT_MAX_ENTRIES - 1) return;
    if (depth > 10) return;

    char pattern[PROJECT_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    /* collect entries for this directory level */
    int start = pt->entry_count;

    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == '\0' ||
             (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
            continue;

        if (should_skip(fd.cFileName)) continue;
        if (pt->entry_count >= PROJECT_MAX_ENTRIES) break;

        project_entry_t *e = &pt->entries[pt->entry_count];
        strncpy(e->name, fd.cFileName, sizeof(e->name) - 1);
        snprintf(e->full_path, sizeof(e->full_path), "%s\\%s", dir_path, fd.cFileName);
        e->type = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                  ? ENTRY_DIR : ENTRY_FILE;
        e->depth = depth;
        e->expanded = false;
        e->visible = (depth == 0);
        pt->entry_count++;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

    /* sort this level */
    int count = pt->entry_count - start;
    if (count > 1) {
        qsort(&pt->entries[start], (size_t)count, sizeof(project_entry_t), cmp_entries);
    }
}
#else
static void scan_dir(project_tree_t *pt, const char *dir_path, int depth) {
    if (pt->entry_count >= PROJECT_MAX_ENTRIES - 1) return;
    if (depth > 10) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    int start = pt->entry_count;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (should_skip(de->d_name)) continue;
        if (pt->entry_count >= PROJECT_MAX_ENTRIES) break;

        project_entry_t *e = &pt->entries[pt->entry_count];
        strncpy(e->name, de->d_name, sizeof(e->name) - 1);
        snprintf(e->full_path, sizeof(e->full_path), "%s/%s", dir_path, de->d_name);

        struct stat st;
        if (stat(e->full_path, &st) == 0) {
            e->type = S_ISDIR(st.st_mode) ? ENTRY_DIR : ENTRY_FILE;
        } else {
            e->type = ENTRY_FILE;
        }
        e->depth = depth;
        e->expanded = false;
        e->visible = (depth == 0);
        pt->entry_count++;
    }
    closedir(dir);

    int count = pt->entry_count - start;
    if (count > 1) {
        qsort(&pt->entries[start], (size_t)count, sizeof(project_entry_t), cmp_entries);
    }
}
#endif

static void rebuild_visibility(project_tree_t *pt) {
    /* Mark all entries visible/hidden based on expanded parents */
    bool parent_visible[32] = {false};
    parent_visible[0] = true;

    for (int i = 0; i < pt->entry_count; i++) {
        project_entry_t *e = &pt->entries[i];
        e->visible = (e->depth == 0) || (e->depth < 32 && parent_visible[e->depth]);

        if (e->type == ENTRY_DIR && e->depth + 1 < 32) {
            parent_visible[e->depth + 1] = e->visible && e->expanded;
        }
    }
}

/* ---------- public API ---------- */

void project_init(project_tree_t *pt) {
    memset(pt, 0, sizeof(project_tree_t));
    pt->selected = -1;
    pt->visible_rows = 20;
}

bool project_open(project_tree_t *pt, const char *dir_path) {
    project_close(pt);
    strncpy(pt->root_path, dir_path, sizeof(pt->root_path) - 1);

    /* extract dir name */
    const char *name = strrchr(dir_path, '\\');
    if (!name) name = strrchr(dir_path, '/');
    name = name ? name + 1 : dir_path;
    strncpy(pt->root_name, name, sizeof(pt->root_name) - 1);

    pt->entry_count = 0;
    scan_dir(pt, dir_path, 0);
    pt->is_open = true;
    pt->selected = pt->entry_count > 0 ? 0 : -1;

    return pt->entry_count > 0;
}

void project_close(project_tree_t *pt) {
    pt->entry_count = 0;
    pt->selected = -1;
    pt->is_open = false;
    pt->root_path[0] = '\0';
    pt->root_name[0] = '\0';
}

void project_refresh(project_tree_t *pt) {
    if (!pt->is_open) return;
    char path[PROJECT_MAX_PATH];
    strncpy(path, pt->root_path, sizeof(path) - 1);
    project_open(pt, path);
}

void project_toggle(project_tree_t *pt, int index) {
    if (index < 0 || index >= pt->entry_count) return;
    project_entry_t *e = &pt->entries[index];
    if (e->type != ENTRY_DIR) return;

    if (!e->expanded) {
        /* expand: scan subdirectory and insert after this entry */
        e->expanded = true;

        /* count existing children at this level */
        int insert_pos = index + 1;
        int old_count = pt->entry_count;

        /* check if children already loaded */
        bool has_children = false;
        if (insert_pos < pt->entry_count &&
            pt->entries[insert_pos].depth == e->depth + 1) {
            has_children = true;
        }

        if (!has_children) {
            /* need to scan and insert children */
            project_tree_t temp;
            memset(&temp, 0, sizeof(temp));
            scan_dir(&temp, e->full_path, e->depth + 1);

            if (temp.entry_count > 0) {
                /* make room */
                int to_shift = old_count - insert_pos;
                if (to_shift > 0 && insert_pos + temp.entry_count < PROJECT_MAX_ENTRIES) {
                    memmove(&pt->entries[insert_pos + temp.entry_count],
                            &pt->entries[insert_pos],
                            (size_t)to_shift * sizeof(project_entry_t));
                }
                memcpy(&pt->entries[insert_pos], temp.entries,
                       (size_t)temp.entry_count * sizeof(project_entry_t));
                pt->entry_count += temp.entry_count;
            }
        }
    } else {
        e->expanded = false;
    }

    rebuild_visibility(pt);
}

project_entry_t *project_selected(project_tree_t *pt) {
    if (pt->selected < 0 || pt->selected >= pt->entry_count) return NULL;
    return &pt->entries[pt->selected];
}

int project_visible_count(const project_tree_t *pt) {
    int count = 0;
    for (int i = 0; i < pt->entry_count; i++) {
        if (pt->entries[i].visible) count++;
    }
    return count;
}

project_entry_t *project_visible_entry(project_tree_t *pt, int visible_idx) {
    int count = 0;
    for (int i = 0; i < pt->entry_count; i++) {
        if (pt->entries[i].visible) {
            if (count == visible_idx) return &pt->entries[i];
            count++;
        }
    }
    return NULL;
}

void project_select_up(project_tree_t *pt) {
    /* find previous visible entry */
    for (int i = pt->selected - 1; i >= 0; i--) {
        if (pt->entries[i].visible) {
            pt->selected = i;
            project_ensure_visible(pt);
            return;
        }
    }
}

void project_select_down(project_tree_t *pt) {
    /* find next visible entry */
    for (int i = pt->selected + 1; i < pt->entry_count; i++) {
        if (pt->entries[i].visible) {
            pt->selected = i;
            project_ensure_visible(pt);
            return;
        }
    }
}

void project_ensure_visible(project_tree_t *pt) {
    /* count visible entries up to selected */
    int vis_idx = 0;
    for (int i = 0; i < pt->selected && i < pt->entry_count; i++) {
        if (pt->entries[i].visible) vis_idx++;
    }
    if (vis_idx < pt->scroll) pt->scroll = vis_idx;
    if (vis_idx >= pt->scroll + pt->visible_rows)
        pt->scroll = vis_idx - pt->visible_rows + 1;
}
