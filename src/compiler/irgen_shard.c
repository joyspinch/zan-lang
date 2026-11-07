#include "irgen_internal.h"

#include <llvm-c/IRReader.h>
#include <limits.h>
#include <time.h>
#ifndef _WIN32
#include <sys/types.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

/* Composite Dispatcher Submodules */
#include "shard/shard_index.c"
#include "shard/shard_emit.c"
#include "shard/shard_run.c"
