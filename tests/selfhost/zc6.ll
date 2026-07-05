declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define i32 @main() {
entry:
  %v0 = alloca [2 x i64]
  %t1 = add i64 0, 42
  %t2 = getelementptr i64, ptr %v0, i64 0
  store i64 %t1, ptr %t2
  %t3 = add i64 0, 7
  %t4 = getelementptr i64, ptr %v0, i64 1
  store i64 %t3, ptr %t4
  %t5 = getelementptr i64, ptr %v0, i64 0
  %t6 = load i64, ptr %t5
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t6)
  %t7 = getelementptr i64, ptr %v0, i64 1
  %t8 = load i64, ptr %t7
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t8)
  %t9 = getelementptr i64, ptr %v0, i64 0
  %t10 = load i64, ptr %t9
  %t11 = getelementptr i64, ptr %v0, i64 1
  %t12 = load i64, ptr %t11
  %t13 = add i64 %t10, %t12
  %t14 = getelementptr i64, ptr %v0, i64 0
  store i64 %t13, ptr %t14
  %t15 = getelementptr i64, ptr %v0, i64 0
  %t16 = load i64, ptr %t15
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t16)
  ret i32 0
}
