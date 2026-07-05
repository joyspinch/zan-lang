declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
declare i64 @strlen(ptr)
declare ptr @malloc(i64)
declare i32 @snprintf(ptr, i64, ptr, ...)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
@.ifmt = private unnamed_addr constant [5 x i8] c"%lld\00"
@.sfmt = private unnamed_addr constant [3 x i8] c"%s\00"
@.str0 = private unnamed_addr constant [5 x i8] [i8 32, i8 32, i8 37, i8 116, i8 0]
@.str1 = private unnamed_addr constant [12 x i8] [i8 32, i8 61, i8 32, i8 97, i8 100, i8 100, i8 32, i8 105, i8 54, i8 52, i8 32, i8 0]
@.str2 = private unnamed_addr constant [3 x i8] [i8 44, i8 32, i8 0]
define void @f0(i64 %arg0, i64 %arg1, i64 %arg2) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %v2 = alloca i64
  store i64 %arg2, ptr %v2
  call i32 (ptr, ...) @printf(ptr @.sfmt, ptr @.str0)
  %t1 = load i64, ptr %v0
  %t2 = call ptr @malloc(i64 32)
  %t3 = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %t2, i64 32, ptr @.ifmt, i64 %t1)
  call i32 (ptr, ...) @printf(ptr @.sfmt, ptr %t2)
  call i32 (ptr, ...) @printf(ptr @.sfmt, ptr @.str1)
  %t4 = load i64, ptr %v1
  %t5 = call ptr @malloc(i64 32)
  %t6 = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %t5, i64 32, ptr @.ifmt, i64 %t4)
  call i32 (ptr, ...) @printf(ptr @.sfmt, ptr %t5)
  call i32 (ptr, ...) @printf(ptr @.sfmt, ptr @.str2)
  %t7 = load i64, ptr %v2
  %t8 = call ptr @malloc(i64 32)
  %t9 = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %t8, i64 32, ptr @.ifmt, i64 %t7)
  call i32 @puts(ptr %t8)
  ret void
}
define i32 @main() {
entry:
  %v0 = alloca ptr
  %t1 = add i64 0, 7
  %t2 = call ptr @malloc(i64 32)
  %t3 = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %t2, i64 32, ptr @.ifmt, i64 %t1)
  store ptr %t2, ptr %v0
  %t4 = load ptr, ptr %v0
  call i32 @puts(ptr %t4)
  %v1 = alloca i64
  %t5 = add i64 0, 0
  store i64 %t5, ptr %v1
  br label %L1
L1:
  %t6 = load i64, ptr %v1
  %t7 = add i64 0, 3
  %t8 = icmp slt i64 %t6, %t7
  br i1 %t8, label %L2, label %L3
L2:
  %t9 = load i64, ptr %v1
  %t10 = load i64, ptr %v1
  %t11 = add i64 0, 10
  %t12 = mul i64 %t10, %t11
  %t13 = load i64, ptr %v1
  %t14 = add i64 0, 1
  %t15 = add i64 %t13, %t14
  call void @f0(i64 %t9, i64 %t12, i64 %t15)
  %t16 = load i64, ptr %v1
  %t17 = add i64 0, 1
  %t18 = add i64 %t16, %t17
  store i64 %t18, ptr %v1
  br label %L1
L3:
  ret i32 0
}
