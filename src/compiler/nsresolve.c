/* nsresolve */

#include "nsresolve.h"
#include "arena.h"
#include "diag.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../common/host_oom.h"

/* Composite Dispatcher Submodules */
#include "nsresolve/nsresolve_stamp.c"
#include "nsresolve/nsresolve_walk.c"
