#ifndef ZAN_PROCMEM_H
#define ZAN_PROCMEM_H

/* Process memory snapshot for --time / ZAN_PROBE_MEM, in bytes.
 * commit: private committed memory (Windows pagefile usage, macOS
 *         phys_footprint, Linux VmRSS); peak: its high-water mark (macOS
 *         ledger peak, Linux VmHWM); ws: resident set. Zero when unknown. */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#endif

typedef struct {
    size_t commit;
    size_t peak;
    size_t ws;
} zan_procmem_t;

static inline zan_procmem_t zan_procmem_read(void) {
    zan_procmem_t m = {0, 0, 0};
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        m.commit = pmc.PagefileUsage;
        m.peak = pmc.PeakPagefileUsage;
        m.ws = pmc.WorkingSetSize;
    }
#elif defined(__APPLE__)
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        m.commit = (size_t)info.phys_footprint;
        m.ws = (size_t)info.resident_size;
        if (count >= TASK_VM_INFO_REV2_COUNT) m.peak = (size_t)info.ledger_phys_footprint_peak;
    }
    if (!m.peak) {
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) == 0) m.peak = (size_t)ru.ru_maxrss; /* bytes */
    }
#elif defined(__linux__)
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            unsigned long kb = 0;
            if (sscanf(line, "VmRSS: %lu kB", &kb) == 1) {
                m.commit = (size_t)kb * 1024;
                m.ws = (size_t)kb * 1024;
            } else if (sscanf(line, "VmHWM: %lu kB", &kb) == 1) {
                m.peak = (size_t)kb * 1024;
            }
        }
        fclose(f);
    }
#endif
    return m;
}

#endif /* ZAN_PROCMEM_H */
