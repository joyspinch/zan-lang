declare i32 @printf(ptr, ...)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define i64 @f0(i64 %arg0) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %t1 = load i64, ptr %v0
  %t2 = add i64 0, 0
  %t3 = icmp slt i64 %t1, %t2
  br i1 %t3, label %L1, label %L2
L1:
  %t4 = add i64 0, 0
  %t5 = add i64 0, 1
  %t6 = sub i64 %t4, %t5
  ret i64 %t6
L2:
  %t7 = load i64, ptr %v0
  %t8 = add i64 0, 0
  %t9 = icmp eq i64 %t7, %t8
  br i1 %t9, label %L4, label %L5
L4:
  %t10 = add i64 0, 0
  ret i64 %t10
L5:
  %t11 = add i64 0, 1
  ret i64 %t11
L6:
  br label %L3
L3:
  ret i64 0
}
define i64 @f1(i64 %arg0, i64 %arg1, i64 %arg2) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %v2 = alloca i64
  store i64 %arg2, ptr %v2
  %t1 = load i64, ptr %v0
  %t2 = load i64, ptr %v1
  %t3 = icmp sge i64 %t1, %t2
  %t4 = load i64, ptr %v0
  %t5 = load i64, ptr %v2
  %t6 = icmp sle i64 %t4, %t5
  %t7 = and i1 %t3, %t6
  br i1 %t7, label %L1, label %L2
L1:
  %t8 = add i64 0, 1
  ret i64 %t8
L2:
  br label %L3
L3:
  %t9 = add i64 0, 0
  ret i64 %t9
}
define i64 @f2(i64 %arg0, i64 %arg1) {
entry:
  %v0 = alloca i64
  store i64 %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %t1 = load i64, ptr %v0
  %t2 = add i64 0, 1
  %t3 = icmp eq i64 %t1, %t2
  %t4 = load i64, ptr %v1
  %t5 = add i64 0, 1
  %t6 = icmp eq i64 %t4, %t5
  %t7 = or i1 %t3, %t6
  br i1 %t7, label %L1, label %L2
L1:
  %t8 = add i64 0, 1
  ret i64 %t8
L2:
  br label %L3
L3:
  %t9 = add i64 0, 0
  ret i64 %t9
}
define i32 @main() {
entry:
  %t1 = add i64 0, 0
  %t2 = add i64 0, 5
  %t3 = sub i64 %t1, %t2
  %t4 = call i64 @f0(i64 %t3)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t4)
  %t5 = add i64 0, 0
  %t6 = call i64 @f0(i64 %t5)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t6)
  %t7 = add i64 0, 9
  %t8 = call i64 @f0(i64 %t7)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t8)
  %t9 = add i64 0, 5
  %t10 = add i64 0, 1
  %t11 = add i64 0, 10
  %t12 = call i64 @f1(i64 %t9, i64 %t10, i64 %t11)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t12)
  %t13 = add i64 0, 15
  %t14 = add i64 0, 1
  %t15 = add i64 0, 10
  %t16 = call i64 @f1(i64 %t13, i64 %t14, i64 %t15)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t16)
  %t17 = add i64 0, 0
  %t18 = add i64 0, 1
  %t19 = call i64 @f2(i64 %t17, i64 %t18)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t19)
  %t20 = add i64 0, 0
  %t21 = add i64 0, 0
  %t22 = call i64 @f2(i64 %t20, i64 %t21)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t22)
  ret i32 0
}
