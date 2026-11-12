#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/runtime/rt_co.h"

int main(void) {
    const size_t sizes[] = { 256, 257, 512, 1024, 2048, 2049 };
    for (int pass = 0; pass < 1000; ++pass) {
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            void *frame = __zan_co_frame_alloc(sizes[i]);
            assert(frame);
            assert(zan_task_live_count() == 1);
            for (size_t j = 0; j < sizes[i]; ++j)
                assert(((unsigned char *)frame)[j] == 0);
            __zan_co_frame_free(frame);
            assert(zan_task_live_count() == 0);
        }
        /* A legacy raw frame has the scheduler header but no allocator prefix. */
        void *raw = calloc(1, 256);
        assert(raw);
        __zan_co_frame_free(raw);
        assert(zan_task_live_count() == 0);
    }
    assert(__zan_co_frame_alloc(SIZE_MAX) == NULL);
    assert(zan_task_live_count() == 0);
    puts("task frame allocator: pooled classes, large blocks, raw frames, overflow passed");
    return 0;
}
