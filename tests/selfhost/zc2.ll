declare i32 @printf(ptr, ...)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define i64 @f0(i64 %arg0) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %t1 = load i64, ptr %v0
  %t2 = add i64 0, 2
  %t3 = icmp slt i64 %t1, %t2
  br i1 %t3, label %L1, label %L2
L1:
  %t4 = load i64, ptr %v0
  ret i64 %t4
L2:
  %t5 = load i64, ptr %v0
  %t6 = add i64 0, 1
  %t7 = sub i64 %t5, %t6
  %t8 = call i64 @f0(i64 %t7)
  %t9 = load i64, ptr %v0
  %t10 = add i64 0, 2
  %t11 = sub i64 %t9, %t10
  %t12 = call i64 @f0(i64 %t11)
  %t13 = add i64 %t8, %t12
  ret i64 %t13
}
define i64 @f1(i64 %arg0, i64 %arg1) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  br label %L1
L1:
  %t1 = load i64, ptr %v1
  %t2 = add i64 0, 0
  %t3 = icmp ne i64 %t1, %t2
  br i1 %t3, label %L2, label %L3
L2:
  %v2 = alloca i64
  %t4 = load i64, ptr %v0
  %t5 = load i64, ptr %v1
  %t6 = srem i64 %t4, %t5
  store i64 %t6, ptr %v2
  %t7 = load i64, ptr %v1
  store i64 %t7, ptr %v0
  %t8 = load i64, ptr %v2
  store i64 %t8, ptr %v1
  br label %L1
L3:
  %t9 = load i64, ptr %v0
  ret i64 %t9
}
define i32 @main() {
entry:
  %t1 = add i64 0, 10
  %t2 = call i64 @f0(i64 %t1)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t2)
  %t3 = add i64 0, 48
  %t4 = add i64 0, 36
  %t5 = call i64 @f1(i64 %t3, i64 %t4)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t5)
  %v0 = alloca i64
  %t6 = add i64 0, 0
  store i64 %t6, ptr %v0
  %v1 = alloca i64
  %t7 = add i64 0, 0
  store i64 %t7, ptr %v1
  br label %L1
L1:
  %t8 = load i64, ptr %v1
  %t9 = add i64 0, 5
  %t10 = icmp slt i64 %t8, %t9
  br i1 %t10, label %L2, label %L3
L2:
  %t11 = load i64, ptr %v0
  %t12 = load i64, ptr %v1
  %t13 = call i64 @f0(i64 %t12)
  %t14 = add i64 %t11, %t13
  store i64 %t14, ptr %v0
  %t15 = load i64, ptr %v1
  %t16 = add i64 0, 1
  %t17 = add i64 %t15, %t16
  store i64 %t17, ptr %v1
  br label %L1
L3:
  %t18 = load i64, ptr %v0
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t18)
  ret i32 0
}
