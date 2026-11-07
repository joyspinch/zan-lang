/* embedres */

#include "embedres.h"
#include "win_utf8.h"
#include "miniz.h"
#include "../common/zan_abi.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define fopen zan_utf8_fopen
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef struct {
    char          *name;
    unsigned char *data;
    long long      len;
} zan_embed_file_t;

typedef struct {
    zan_embed_file_t *v;
    int               n;
    int               cap;
} zan_embed_list_t;

/* Composite Dispatcher Submodules */
#include "embedres/embedres_collect.c"
#include "embedres/embedres_emit.c"
