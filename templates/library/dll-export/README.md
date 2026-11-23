# {{NAME}}

导出 C-ABI 函数的跨平台共享库。源码在 `src/lib.zan`，C 头文件在
`include/exports.h`。

## 构建

IDE 里直接"生成"，或命令行：

```bash
zanc src/lib.zan --auto-stdlib --emit-lib -o lib{{NAME}}.so      # Linux
zanc src/lib.zan --auto-stdlib --emit-lib -o lib{{NAME}}.dylib   # macOS
zanc src/lib.zan --auto-stdlib --emit-lib -o {{NAME}}.dll        # Windows
```

输出后缀决定产物类型。检查导出表：`nm -D --defined-only lib{{NAME}}.so`
（macOS 用 `nm -gU`，Windows 用 `dumpbin /exports`）。

## 导出规则

- `public static` 方法才导出，符号名 `类名_方法名`，例如 `{{NAME}}_Add`。
- 签名只用标量：`int`→`int32_t`、`long`→`int64_t`、`double`→`double`、
  `bool`→`bool`。Zan 的 `string`/数组/对象是托管对象，不是 `char*`，别放进导出签名。
- 导出函数不让异常逃出边界，失败用哨兵返回值表达（见 `SafeDivide`）。

## 从 C 调用

```c
#include <stdio.h>
#include "exports.h"

int main(void) {
    printf("version %d\n", {{NAME}}_Version());
    printf("2 + 3 = %d\n", {{NAME}}_Add(2, 3));
    printf("fib(50) = %lld\n", (long long){{NAME}}_Fibonacci(50));
    if ({{NAME}}_SafeDivide(1, 0) == {{NAME}}_ErrorValue()) puts("divide by zero handled");
    return 0;
}
```

```bash
cc app.c -Iinclude -L. -l{{NAME}} -Wl,-rpath,'$ORIGIN' -o app   # Linux
```

## 从 Python 调用（ctypes）

```python
import ctypes, sys
name = {"win32": "{{NAME}}.dll", "darwin": "lib{{NAME}}.dylib"}.get(sys.platform, "lib{{NAME}}.so")
lib = ctypes.CDLL("./" + name)

lib.{{NAME}}_Distance.argtypes = [ctypes.c_double] * 4
lib.{{NAME}}_Distance.restype = ctypes.c_double
lib.{{NAME}}_IsPrime.restype = ctypes.c_bool
lib.{{NAME}}_Fibonacci.restype = ctypes.c_int64

print(lib.{{NAME}}_Add(2, 3))                    # 5
print(lib.{{NAME}}_Distance(0.0, 0.0, 3.0, 4.0))  # 5.0
print(lib.{{NAME}}_IsPrime(97))                  # True
print(lib.{{NAME}}_Fibonacci(50))                # 12586269025
```

ctypes 默认把返回值当 `int`，返回 `double`/`bool`/`int64` 的函数一定要设 `restype`。
