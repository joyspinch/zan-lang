#include "irgen_internal.h"
/* irgen_async.c: 异步/协程 lowering 的 composite dispatcher——自身不含函数体，
 * 按域拆分为 async/ 下五个拼接片段（fragment），经 #include 组成单一 TU
 * （CMakeLists 仅列本文件）：
 *   async/irgen_async_slots.c    帧结果槽转换、槽访问、cancel/suspend/preempt
 *   async/irgen_async_pending.c  pending 栈、complete/epilogue
 *   async/irgen_async_coops.c    co 运行时 thunk、异步异常处理
 *   async/irgen_async_anf.c      await ANF 正规化
 *   async/irgen_async_scan.c     帧布局扫描
 * 46 个导出函数声明在 irgen_internal.h；文件级 static 均为各自片段私有
 * （无跨片段引用，拆分已勘定），同名冲突由 policy_unity_order 机检兜底。 */
#include "async/irgen_async_slots.c"
#include "async/irgen_async_pending.c"
#include "async/irgen_async_coops.c"
#include "async/irgen_async_anf.c"
#include "async/irgen_async_scan.c"
