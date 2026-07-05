declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
@.ifmt = private unnamed_addr constant [5 x i8] c"%lld\00"
@.sfmt = private unnamed_addr constant [3 x i8] c"%s\00"
@.str0 = private unnamed_addr constant [9 x i8] [i8 99, i8 111, i8 117, i8 110, i8 116, i8 32, i8 61, i8 32, i8 0]
@.str1 = private unnamed_addr constant [9 x i8] [i8 116, i8 111, i8 116, i8 97, i8 108, i8 32, i8 61, i8 32, i8 0]
@.str2 = private unnamed_addr constant [5 x i8] [i8 100, i8 111, i8 110, i8 101, i8 0]
define void @f0(ptr %arg0, i64 %arg1) {
entry:
  %v0 = alloca ptr
  store ptr %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %t1 = load ptr, ptr %v0
  call i32 (ptr, ...) @printf(ptr @.sfmt, ptr %t1)
  %t2 = load i64, ptr %v1
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t2)
  ret void
}
define i32 @main() {
entry:
  %v0 = alloca ptr
  store ptr @.str0, ptr %v0
  %t1 = load ptr, ptr %v0
  %t2 = add i64 0, 3
  call void @f0(ptr %t1, i64 %t2)
  %v1 = alloca ptr
  store ptr @.str1, ptr %v1
  %t3 = load ptr, ptr %v1
  %t4 = add i64 0, 42
  call void @f0(ptr %t3, i64 %t4)
  %v2 = alloca ptr
  store ptr @.str2, ptr %v2
  %t5 = load ptr, ptr %v2
  call i32 @puts(ptr %t5)
  ret i32 0
}
