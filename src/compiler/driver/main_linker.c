/* main_linker.c - 跨平台链接器组合分发器 (≤1200行)
 * FRAGMENT：由 main.c 的 main() 函数体 #include（main_codegen.c 之后），
 * 消费 main() 局部 target/generated_objects/output_path 等；下方四个
 * #include 是同一 main() 作用域内的二级拼接片段。 */
#include "linker_setup.c"
#include "linker_host.c"
#include "linker_cross.c"
#include "linker_cross_apple.c"
