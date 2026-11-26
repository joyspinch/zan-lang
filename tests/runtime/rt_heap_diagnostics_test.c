/* Standalone runtime fixture. Compile this translation unit without linker
 * allocator wrapping and pass a dedicated writable snapshot path as argv[1]. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

#include "../../src/common/zan_abi.h"

void zan_rt_fatal(const char *category, const char *message);

static int fail_growth;
static int failed_growths;

static void *test_realloc(void *p, size_t bytes) {
  if (p && fail_growth) {
    failed_growths++;
    return NULL;
  }
  return realloc(p, bytes);
}

#define ZAN_GC_REALLOC test_realloc
#include "../../src/runtime/timer/rt_timer_gc.c"

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c);                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

void zan_rt_fatal(const char *category, const char *message) {
  fprintf(stderr, "Unexpected fatal [%s]: %s\n", category, message);
  abort();
}

static void *g_test_descs[512];
static int64_t g_test_desc_count = 1;

static uint64_t register_test_desc(const zan_arc_desc_t *desc) {
  for (int64_t i = 1; i < g_test_desc_count; i++) {
    if (g_test_descs[i] == (void *)desc)
      return (uint64_t)i;
  }
  if (g_test_desc_count >= (int64_t)(sizeof(g_test_descs) / sizeof(g_test_descs[0])))
    abort();
  uint64_t site = (uint64_t)g_test_desc_count++;
  g_test_descs[site] = (void *)desc;
  zan_rt_register_desc_table(g_test_descs, g_test_desc_count);
  return site;
}

static void *new_object_with_source(uint64_t site, size_t bytes,
                                    const char *source) {
  uint64_t *raw = calloc(1, ZAN_OBJ_HDR_SIZE + bytes);
  if (!raw)
    abort();
  raw[0] = ZAN_OBJ_HDR_INIT(site);
  void *obj = (char *)raw + ZAN_OBJ_HDR_SIZE;
  if (source) {
    zan_rt_live_register_named(obj, ZAN_OBJ_HDR_SIZE + bytes, source);
  } else {
    zan_rt_live_register(obj, ZAN_OBJ_HDR_SIZE + bytes);
  }
  return obj;
}

static void *new_object_at_site(uint64_t site, size_t bytes) {
  return new_object_with_source(site, bytes, NULL);
}

static void *new_object(const zan_arc_desc_t *desc, size_t bytes) {
  return new_object_at_site(register_test_desc(desc), bytes);
}

static void **new_array(size_t count, size_t rank, int elements_are_refs) {
  size_t shape_bytes = rank * sizeof(uint64_t);
  size_t bytes = ZAN_ARR_HDR_SIZE + shape_bytes + count * sizeof(void *);
  uint64_t *raw = calloc(1, bytes);
  if (!raw)
    abort();
  raw[0] = 1;
  raw[1] = ZAN_ARRAY_RC_MAGIC;
  raw[2] = count;
  static const zan_arc_desc_t ref_array_desc = {0};
  raw[3] = rank                ? rank
           : elements_are_refs ? (uintptr_t)&ref_array_desc
                               : ZAN_ARRAY_MAGIC;
  uint64_t *shape = (uint64_t *)((char *)raw + ZAN_ARR_HDR_SIZE);
  for (size_t i = 0; i < rank; i++)
    shape[i] = i == 0 ? count : 1;
  zan_rt_live_register_array(shape, bytes, shape_bytes, elements_are_refs);
  return (void **)shape;
}

static void delete_live(void *obj, size_t header_bytes) {
  zan_rt_live_unregister(obj);
  free((char *)obj - header_bytes);
}

static int test_numeric_site_cycle(void) {
  enum { NODE_SITE = 2, SITE_COUNT = 3 };
  zan_field_graph_t *graph = malloc(sizeof(*graph) + sizeof(graph->refs[0]));
  CHECK(graph);
  *graph =
      (zan_field_graph_t){.ref_count = 1, .instance_size = 2 * sizeof(void *)};
  graph->refs[0] = (zan_field_ref_t){.offset = sizeof(void *), .is_rc = 1};
  void *graphs[SITE_COUNT] = {[NODE_SITE] = graph};
  void **node = new_object_at_site(NODE_SITE, graph->instance_size);
  node[1] = node;

  /* The runtime may only read entries within the supplied table length. */
  zan_rt_register_site_field_graphs(graphs, NULL, 1);
  CHECK(zan_gc_detect_cycles() == 0);
  zan_rt_register_site_field_graphs(graphs, NULL, SITE_COUNT);
  CHECK(zan_gc_detect_cycles() == 1);

  node[1] = NULL;
  CHECK(zan_gc_detect_cycles() == 0);
  delete_live(node, ZAN_OBJ_HDR_SIZE);
  zan_rt_register_site_field_graphs(NULL, NULL, 0);
  free(graph);
  return 0;
}

static int test_array_cycles(void) {
  zan_field_graph_t *graph = malloc(sizeof(*graph) + sizeof(graph->refs[0]));
  CHECK(graph);
  *graph = (zan_field_graph_t){.ref_count = 1, .instance_size = sizeof(void *)};
  graph->refs[0] = (zan_field_ref_t){.is_rc = 1};
  zan_arc_desc_t desc = {.field_graph = graph};
  void **node = new_object(&desc, sizeof(*node));
  void **empty = new_array(0, 0, 1);
  CHECK(zan_gc_detect_cycles() == 0);
  delete_live(empty, ZAN_ARR_HDR_SIZE);

  for (size_t rank = 0; rank <= 2; rank += 2) {
    void **array = new_array(3, rank, 1);
    void **data = (void **)((char *)array + rank * sizeof(uint64_t));
    *node = array;
    data[2] = node;
    CHECK(zan_gc_detect_cycles() == 2);

    /* One external hold roots the array and the node reachable from it. */
    int64_t *rc = (int64_t *)((char *)array + ZAN_ARR_RC_OFF);
    *rc = 2;
    CHECK(zan_gc_detect_cycles() == 0);
    *node = NULL;
    delete_live(array, ZAN_ARR_HDR_SIZE);
  }

  void **primitive = new_array(1, 2, 0);
  uintptr_t *data = (uintptr_t *)((char *)primitive + 2 * sizeof(uint64_t));
  *data = (uintptr_t)node;
  *node = node;
  CHECK(zan_gc_detect_cycles() == 1);
  delete_live(primitive, ZAN_ARR_HDR_SIZE);
  delete_live(node, ZAN_OBJ_HDR_SIZE);
  free(graph);
  return 0;
}

typedef struct {
  int64_t count;
  int64_t capacity;
  void **data;
} test_list_t;

static int worker_stage;

static void wait_for_stage(int stage) {
  while (__atomic_load_n(&worker_stage, __ATOMIC_ACQUIRE) < stage) {
#if defined(_WIN32)
    Sleep(0);
#else
    sched_yield();
#endif
  }
}

#if defined(_WIN32)
static DWORD WINAPI mutate_list(LPVOID arg) {
#else
static void *mutate_list(void *arg) {
#endif
  test_list_t *list = arg;
  zan_rt_heap_mutator_enter();
  zan_rt_heap_mutator_enter();
  free(list->data);
  zan_rt_heap_mutator_leave();
  /* Hold the worker inside the mutation with the old pointer still installed.
   */
  __atomic_store_n(&worker_stage, 1, __ATOMIC_RELEASE);
  wait_for_stage(2);
  list->data = calloc(2, sizeof(*list->data));
  if (!list->data)
    abort();
  list->data[0] = list;
  list->capacity = 2;
  zan_rt_heap_mutator_leave();
  return 0;
}

static int test_list_mutation(void) {
  static zan_field_graph_t graph = {.kind = 1, .ref_count = 1};
  static const zan_arc_desc_t desc = {.field_graph = &graph};
  zan_rt_heap_mutator_enter();
  zan_rt_heap_mutator_enter();
  test_list_t *list = new_object(&desc, sizeof(*list));
  *list = (test_list_t){
      .count = 1, .capacity = 1, .data = calloc(1, sizeof(*list->data))};
  CHECK(list->data);
  list->data[0] = list;
  CHECK(zan_gc_detect_cycles() == 1);
  zan_rt_heap_mutator_leave();
  CHECK(zan_gc_detect_cycles() == 1);

#if defined(_WIN32)
  HANDLE thread = CreateThread(NULL, 0, mutate_list, list, 0, NULL);
  CHECK(thread);
#else
  pthread_t thread;
  CHECK(pthread_create(&thread, NULL, mutate_list, list) == 0);
#endif
  wait_for_stage(1);
  CHECK(zan_gc_detect_cycles() == -1);
  __atomic_store_n(&worker_stage, 2, __ATOMIC_RELEASE);
#if defined(_WIN32)
  CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
  CloseHandle(thread);
#else
  CHECK(pthread_join(thread, NULL) == 0);
#endif
  CHECK(zan_gc_detect_cycles() == 1);
  *ZAN_RC_PTR(list) = 0;
  free(list->data);
  /* Final-release cleanup may still have the object in the registry. */
  CHECK(zan_gc_detect_cycles() == 0);
  delete_live(list, ZAN_OBJ_HDR_SIZE);
  zan_rt_heap_mutator_leave();
  CHECK(zan_gc_detect_cycles() == 0);
  return 0;
}

typedef struct {
  const char *source;
  size_t count;
  size_t bytes;
} expected_source_row_t;

static int check_snapshot_sources(const char *path,
                                  const expected_source_row_t *rows,
                                  size_t row_count) {
  size_t total_count = 0, total_bytes = 0;
  for (size_t i = 0; i < row_count; i++) {
    total_count += rows[i].count;
    total_bytes += rows[i].bytes;
  }
  CHECK(zan_gc_write_heap_snapshot(path) == (int64_t)total_count);
  FILE *file = fopen(path, "r");
  CHECK(file);
  char line[256], expected[256];
  CHECK(fgets(line, sizeof(line), file) &&
        strcmp(line, "# Zan Heap Snapshot\n") == 0);
  snprintf(expected, sizeof(expected), "# Total Live Objects: %zu\n",
           total_count);
  CHECK(fgets(line, sizeof(line), file) && strcmp(line, expected) == 0);
  snprintf(expected, sizeof(expected), "# Total Allocated Bytes: %zu\n",
           total_bytes);
  CHECK(fgets(line, sizeof(line), file) && strcmp(line, expected) == 0);

  snprintf(expected, sizeof(expected), "%-40s %15zu %15zu\n", "SnapshotNode",
           total_count, total_bytes);
  int type_rows = 0;
  while (fgets(line, sizeof(line), file)) {
    if (strcmp(line, "# Live Objects by Type and Source\n") == 0)
      break;
    if (strcmp(line, expected) == 0)
      type_rows++;
  }
  CHECK(type_rows == 1);
  CHECK(fgets(line, sizeof(line), file) &&
        strcmp(line, "Type\tSource\tCount\tBytes\n") == 0);
  for (size_t i = 0; i < row_count; i++) {
    snprintf(expected, sizeof(expected), "SnapshotNode\t%s\t%zu\t%zu\n",
             rows[i].source, rows[i].count, rows[i].bytes);
    CHECK(fgets(line, sizeof(line), file) && strcmp(line, expected) == 0);
  }
  CHECK(!fgets(line, sizeof(line), file) && !ferror(file));
  CHECK(fclose(file) == 0);
  return 0;
}

static int test_snapshot_sources(const char *path) {
  static const char *const names[] = {"SnapshotNode"};
  static const zan_arc_desc_t desc = {.tynames_arr = (void *)names};
  static const char first[] = "fixture path/main.zan:12:4";
  static const char first_repeat[] = "fixture path/main.zan:12:4";
  static const char second[] = "fixture path/main.zan:26:8";
  const size_t header = ZAN_OBJ_HDR_SIZE;
  const uint64_t site = register_test_desc(&desc);
  void *a = new_object_with_source(site, 8, first);
  void *a_repeat = new_object_with_source(site, 16, first_repeat);
  void *b = new_object_with_source(site, 32, second);
  void *unknown = new_object_at_site(site, 0);

  const expected_source_row_t before[] = {
      {first, 2, 2 * header + 24},
      {second, 1, header + 32},
      {"<unknown>", 1, header},
  };
  CHECK(check_snapshot_sources(path, before, 3) == 0);

  delete_live(a, header);
  delete_live(b, header);
  const expected_source_row_t after[] = {
      {first, 1, header + 16},
      {"<unknown>", 1, header},
  };
  CHECK(check_snapshot_sources(path, after, 2) == 0);
  delete_live(a_repeat, header);
  CHECK(check_snapshot_sources(path, &after[1], 1) == 0);
  delete_live(unknown, header);
  CHECK(remove(path) == 0);
  return 0;
}

static int test_snapshot_oom(const char *path) {
  enum { TYPE_COUNT = 257 };
  char type_names[TYPE_COUNT][16];
  const char *names[TYPE_COUNT];
  zan_arc_desc_t descs[TYPE_COUNT];
  void *objects[TYPE_COUNT];
  for (int i = 0; i < TYPE_COUNT; i++) {
    snprintf(type_names[i], sizeof(type_names[i]), "HeapType%03d", i);
    names[i] = type_names[i];
    descs[i] = (zan_arc_desc_t){.tynames_arr = &names[i]};
    objects[i] = new_object(&descs[i], 0);
  }
  FILE *file = fopen(path, "w");
  CHECK(file);
  CHECK(fputs("prior snapshot\n", file) >= 0);
  CHECK(fclose(file) == 0);

  fail_growth = 1;
  int64_t result = zan_gc_write_heap_snapshot(path);
  fail_growth = 0;
  CHECK(result == -1 && failed_growths == 1);
  char line[256];
  file = fopen(path, "r");
  CHECK(file);
  CHECK(fgets(line, sizeof(line), file) &&
        strcmp(line, "prior snapshot\n") == 0);
  CHECK(fclose(file) == 0);

  CHECK(zan_gc_write_heap_snapshot(path) == TYPE_COUNT);
  file = fopen(path, "r");
  CHECK(file);
  int seen[TYPE_COUNT] = {0};
  while (fgets(line, sizeof(line), file)) {
    int id;
    size_t count, bytes;
    if (sscanf(line, "HeapType%d %zu %zu", &id, &count, &bytes) != 3)
      continue;
    CHECK(id >= 0 && id < TYPE_COUNT);
    CHECK(count == 1 && bytes == ZAN_OBJ_HDR_SIZE);
    seen[id]++;
  }
  CHECK(fclose(file) == 0);
  CHECK(remove(path) == 0);
  for (int i = 0; i < TYPE_COUNT; i++) {
    CHECK(seen[i] == 1);
    delete_live(objects[i], ZAN_OBJ_HDR_SIZE);
  }
  return 0;
}

int main(int argc, char **argv) {
  CHECK(argc == 2);
#if defined(_WIN32)
  /* The standalone C linker can select UCRT instead of the Zan program's CRT.
   */
  CHECK(LoadLibraryA("msvcrt.dll"));
#endif
  CHECK(zan_gc_detect_cycles() == -1);
  zan_rt_heap_diagnostics_enable();
  CHECK(zan_gc_detect_cycles() == 0);
  CHECK(test_list_mutation() == 0);
  zan_rt_heap_mutator_enter();
  CHECK(test_numeric_site_cycle() == 0);
  CHECK(test_array_cycles() == 0);
  CHECK(test_snapshot_sources(argv[1]) == 0);
  CHECK(test_snapshot_oom(argv[1]) == 0);
  zan_rt_heap_mutator_leave();
  puts("PASS heap diagnostics");
  return 0;
}
