declare i32 @printf(ptr, ...)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define i32 @main() {
entry:
  %v0 = alloca i64
  %t1 = add i64 0, 0
  store i64 %t1, ptr %v0
  %v1 = alloca i64
  %t2 = add i64 0, 1
  store i64 %t2, ptr %v1
  br label %L1
L1:
  %t3 = load i64, ptr %v1
  %t4 = add i64 0, 10
  %t5 = icmp sle i64 %t3, %t4
  br i1 %t5, label %L2, label %L3
L2:
  %t6 = load i64, ptr %v0
  %t7 = load i64, ptr %v1
  %t8 = add i64 %t6, %t7
  store i64 %t8, ptr %v0
  %t9 = load i64, ptr %v1
  %t10 = add i64 0, 1
  %t11 = add i64 %t9, %t10
  store i64 %t11, ptr %v1
  br label %L1
L3:
  %t12 = load i64, ptr %v0
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t12)
  %v2 = alloca i64
  %t13 = add i64 0, 7
  store i64 %t13, ptr %v2
  %t14 = load i64, ptr %v2
  %t15 = add i64 0, 2
  %t16 = srem i64 %t14, %t15
  %t17 = add i64 0, 1
  %t18 = icmp eq i64 %t16, %t17
  br i1 %t18, label %L4, label %L5
L4:
  %t19 = load i64, ptr %v2
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t19)
  br label %L5
L5:
  %v3 = alloca i64
  %t20 = add i64 0, 1
  store i64 %t20, ptr %v3
  %v4 = alloca i64
  %t21 = add i64 0, 1
  store i64 %t21, ptr %v4
  br label %L6
L6:
  %t22 = load i64, ptr %v4
  %t23 = add i64 0, 5
  %t24 = icmp sle i64 %t22, %t23
  br i1 %t24, label %L7, label %L8
L7:
  %t25 = load i64, ptr %v3
  %t26 = load i64, ptr %v4
  %t27 = mul i64 %t25, %t26
  store i64 %t27, ptr %v3
  %t28 = load i64, ptr %v4
  %t29 = add i64 0, 1
  %t30 = add i64 %t28, %t29
  store i64 %t30, ptr %v4
  br label %L6
L8:
  %t31 = load i64, ptr %v3
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t31)
  ret i32 0
}
