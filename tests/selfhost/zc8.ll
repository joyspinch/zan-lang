declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define void @m0(ptr %this, i64 %arg0, i64 %arg1) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %t1 = load i64, ptr %v0
  %t2 = getelementptr i64, ptr %this, i64 0
  store i64 %t1, ptr %t2
  %t3 = load i64, ptr %v1
  %t4 = getelementptr i64, ptr %this, i64 1
  store i64 %t3, ptr %t4
  ret void
}
define i64 @m1(ptr %this) {
entry:
  %t1 = getelementptr i64, ptr %this, i64 0
  %t2 = load i64, ptr %t1
  %t3 = getelementptr i64, ptr %this, i64 1
  %t4 = load i64, ptr %t3
  %t5 = mul i64 %t2, %t4
  ret i64 %t5
}
define i32 @main() {
entry:
  %v0 = alloca [2 x i64]
  %t1 = add i64 0, 10
  %t2 = add i64 0, 5
  call void @m0(ptr %v0, i64 %t1, i64 %t2)
  %t3 = call i64 @m1(ptr %v0)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t3)
  %t4 = getelementptr i64, ptr %v0, i64 0
  %t5 = load i64, ptr %t4
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t5)
  %t6 = getelementptr i64, ptr %v0, i64 1
  %t7 = load i64, ptr %t6
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t7)
  ret i32 0
}
