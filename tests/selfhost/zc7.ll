declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define i64 @m0(ptr %this) {
entry:
  %t1 = getelementptr i64, ptr %this, i64 0
  %t2 = load i64, ptr %t1
  %t3 = getelementptr i64, ptr %this, i64 1
  %t4 = load i64, ptr %t3
  %t5 = mul i64 %t2, %t4
  ret i64 %t5
}
define i64 @m1(ptr %this, i64 %arg0) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %t1 = getelementptr i64, ptr %this, i64 0
  %t2 = load i64, ptr %t1
  %t3 = getelementptr i64, ptr %this, i64 1
  %t4 = load i64, ptr %t3
  %t5 = mul i64 %t2, %t4
  %t6 = load i64, ptr %v0
  %t7 = mul i64 %t5, %t6
  ret i64 %t7
}
define i32 @main() {
entry:
  %v0 = alloca [2 x i64]
  %t1 = add i64 0, 10
  %t2 = getelementptr i64, ptr %v0, i64 0
  store i64 %t1, ptr %t2
  %t3 = add i64 0, 5
  %t4 = getelementptr i64, ptr %v0, i64 1
  store i64 %t3, ptr %t4
  %t5 = call i64 @m0(ptr %v0)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t5)
  %t6 = add i64 0, 3
  %t7 = call i64 @m1(ptr %v0, i64 %t6)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t7)
  ret i32 0
}
