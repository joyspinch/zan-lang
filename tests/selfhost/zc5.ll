declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
@.str0 = private unnamed_addr constant [13 x i8] [i8 115, i8 113, i8 117, i8 97, i8 114, i8 101, i8 115, i8 32, i8 115, i8 117, i8 109, i8 58, i8 0]
define i32 @main() {
entry:
  %v0 = alloca [5 x i64]
  %v1 = alloca i64
  %t1 = add i64 0, 0
  store i64 %t1, ptr %v1
  br label %L1
L1:
  %t2 = load i64, ptr %v1
  %t3 = add i64 0, 5
  %t4 = icmp slt i64 %t2, %t3
  br i1 %t4, label %L2, label %L3
L2:
  %t5 = load i64, ptr %v1
  %t6 = load i64, ptr %v1
  %t7 = load i64, ptr %v1
  %t8 = mul i64 %t6, %t7
  %t9 = getelementptr i64, ptr %v0, i64 %t5
  store i64 %t8, ptr %t9
  %t10 = load i64, ptr %v1
  %t11 = add i64 0, 1
  %t12 = add i64 %t10, %t11
  store i64 %t12, ptr %v1
  br label %L1
L3:
  %v2 = alloca i64
  %t13 = add i64 0, 0
  store i64 %t13, ptr %v2
  %v3 = alloca i64
  %t14 = add i64 0, 0
  store i64 %t14, ptr %v3
  br label %L4
L4:
  %t15 = load i64, ptr %v3
  %t16 = add i64 0, 5
  %t17 = icmp slt i64 %t15, %t16
  br i1 %t17, label %L5, label %L6
L5:
  %t18 = load i64, ptr %v2
  %t19 = load i64, ptr %v3
  %t20 = getelementptr i64, ptr %v0, i64 %t19
  %t21 = load i64, ptr %t20
  %t22 = add i64 %t18, %t21
  store i64 %t22, ptr %v2
  %t23 = load i64, ptr %v3
  %t24 = add i64 0, 1
  %t25 = add i64 %t23, %t24
  store i64 %t25, ptr %v3
  br label %L4
L6:
  call i32 @puts(ptr @.str0)
  %t26 = load i64, ptr %v2
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t26)
  %t27 = add i64 0, 3
  %t28 = getelementptr i64, ptr %v0, i64 %t27
  %t29 = load i64, ptr %t28
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t29)
  ret i32 0
}
