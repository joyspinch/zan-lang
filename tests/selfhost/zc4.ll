declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
@.str0 = private unnamed_addr constant [15 x i8] [i8 104, i8 101, i8 108, i8 108, i8 111, i8 32, i8 102, i8 114, i8 111, i8 109, i8 32, i8 122, i8 97, i8 110, i8 0]
@.str1 = private unnamed_addr constant [5 x i8] [i8 116, i8 105, i8 99, i8 107, i8 0]
@.str2 = private unnamed_addr constant [5 x i8] [i8 100, i8 111, i8 110, i8 101, i8 0]
define i64 @f0(i64 %arg0, i64 %arg1) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %t1 = load i64, ptr %v0
  %t2 = load i64, ptr %v1
  %t3 = add i64 %t1, %t2
  ret i64 %t3
}
define i32 @main() {
entry:
  call i32 @puts(ptr @.str0)
  %t1 = add i64 0, 3
  %t2 = add i64 0, 4
  %t3 = call i64 @f0(i64 %t1, i64 %t2)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t3)
  %v0 = alloca i64
  %t4 = add i64 0, 0
  store i64 %t4, ptr %v0
  br label %L1
L1:
  %t5 = load i64, ptr %v0
  %t6 = add i64 0, 3
  %t7 = icmp slt i64 %t5, %t6
  br i1 %t7, label %L2, label %L3
L2:
  call i32 @puts(ptr @.str1)
  %t8 = load i64, ptr %v0
  %t9 = add i64 0, 1
  %t10 = add i64 %t8, %t9
  store i64 %t10, ptr %v0
  br label %L1
L3:
  call i32 @puts(ptr @.str2)
  ret i32 0
}
