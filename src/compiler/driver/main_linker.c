/* main_linker.c - 跨平台链接器组合分发器 (≤1200行)
 * FRAGMENT: host=main
 * 拼接片段：由 main.c 的 main() 函数体 #include（main_codegen.c 之后）。
 * consumes: target, generated_objects, obj_path, publish_mode,
 *   cross_compiling, no_icon, icon_path, emit_lib, default_icon_explicit,
 *   extra_link_inputs, extra_link_input_count
 * defines: icon_obj, link_ret
 * （下方四个 #include 是同一 main() 作用域内的二级拼接片段。） */
#include "linker_setup.c"
#include "linker_host.c"
#include "linker_cross.c"
#include "linker_cross_apple.c"
