declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define i64 @f0(ptr %arg0, i64 %arg1) {
entry:
  %v0 = alloca ptr
  store ptr %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %v2 = alloca i64
  %t1 = add i64 0, 0
  store i64 %t1, ptr %v2
  %v3 = alloca i64
  %t2 = add i64 0, 0
  store i64 %t2, ptr %v3
  br label %L1
L1:
  %t3 = load i64, ptr %v3
  %t4 = load i64, ptr %v1
  %t5 = icmp slt i64 %t3, %t4
  br i1 %t5, label %L2, label %L3
L2:
  %t6 = load i64, ptr %v2
  %t7 = load i64, ptr %v3
  %t8 = load ptr, ptr %v0
  %t9 = getelementptr i64, ptr %t8, i64 %t7
  %t10 = load i64, ptr %t9
  %t11 = add i64 %t6, %t10
  store i64 %t11, ptr %v2
  %t12 = load i64, ptr %v3
  %t13 = add i64 0, 1
  %t14 = add i64 %t12, %t13
  store i64 %t14, ptr %v3
  br label %L1
L3:
  %t15 = load i64, ptr %v2
  ret i64 %t15
}
define i64 @f1(ptr %arg0, i64 %arg1) {
entry:
  %v0 = alloca ptr
  store ptr %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %v2 = alloca i64
  %t1 = add i64 0, 0
  store i64 %t1, ptr %v2
  %v3 = alloca i64
  %t2 = add i64 0, 0
  store i64 %t2, ptr %v3
  br label %L1
L1:
  %t3 = load i64, ptr %v3
  %t4 = load i64, ptr %v1
  %t5 = icmp slt i64 %t3, %t4
  br i1 %t5, label %L2, label %L3
L2:
  %t6 = load i64, ptr %v2
  %t7 = load i64, ptr %v3
  %t8 = load ptr, ptr %v0
  %t9 = getelementptr i64, ptr %t8, i64 %t7
  %t10 = load i64, ptr %t9
  %t11 = load i64, ptr %v3
  %t12 = load ptr, ptr %v0
  %t13 = getelementptr i64, ptr %t12, i64 %t11
  %t14 = load i64, ptr %t13
  %t15 = mul i64 %t10, %t14
  %t16 = add i64 %t6, %t15
  store i64 %t16, ptr %v2
  %t17 = load i64, ptr %v3
  %t18 = add i64 0, 1
  %t19 = add i64 %t17, %t18
  store i64 %t19, ptr %v3
  br label %L1
L3:
  %t20 = load i64, ptr %v2
  ret i64 %t20
}
define i32 @main() {
entry:
  %b0 = alloca [5 x i64]
  %v0 = alloca ptr
  store ptr %b0, ptr %v0
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
  %t7 = add i64 0, 1
  %t8 = add i64 %t6, %t7
  %t9 = load ptr, ptr %v0
  %t10 = getelementptr i64, ptr %t9, i64 %t5
  store i64 %t8, ptr %t10
  %t11 = load i64, ptr %v1
  %t12 = add i64 0, 1
  %t13 = add i64 %t11, %t12
  store i64 %t13, ptr %v1
  br label %L1
L3:
  %t14 = load ptr, ptr %v0
  %t15 = add i64 0, 5
  %t16 = call i64 @f0(ptr %t14, i64 %t15)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t16)
  %t17 = load ptr, ptr %v0
  %t18 = add i64 0, 5
  %t19 = call i64 @f1(ptr %t17, i64 %t18)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t19)
  %t20 = add i64 0, 4
  %t21 = load ptr, ptr %v0
  %t22 = getelementptr i64, ptr %t21, i64 %t20
  %t23 = load i64, ptr %t22
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t23)
  ret i32 0
}
