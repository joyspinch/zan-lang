declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define i64 @m0(ptr %this) {
entry:
  %t1 = getelementptr i64, ptr %this, i64 0
  %t2 = load i64, ptr %t1
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t2)
  %t3 = getelementptr i64, ptr %this, i64 1
  %t4 = load i64, ptr %t3
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t4)
  %t5 = add i64 0, 0
  ret i64 %t5
}
define void @f1(ptr %arg0, i64 %arg1) {
entry:
  %v0 = alloca ptr
  store ptr %arg0, ptr %v0
  %v1 = alloca i64
  store i64 %arg1, ptr %v1
  %v2 = alloca i64
  %t1 = add i64 0, 0
  store i64 %t1, ptr %v2
  br label %L1
L1:
  %t2 = load i64, ptr %v2
  %t3 = load i64, ptr %v1
  %t4 = icmp slt i64 %t2, %t3
  br i1 %t4, label %L2, label %L3
L2:
  %t5 = load i64, ptr %v2
  %t6 = load i64, ptr %v2
  %t7 = load i64, ptr %v2
  %t8 = mul i64 %t6, %t7
  %t9 = load ptr, ptr %v0
  %t10 = getelementptr i64, ptr %t9, i64 %t5
  store i64 %t8, ptr %t10
  %t11 = load i64, ptr %v2
  %t12 = add i64 0, 1
  %t13 = add i64 %t11, %t12
  store i64 %t13, ptr %v2
  br label %L1
L3:
  ret void
}
define i64 @f2(ptr %arg0, i64 %arg1) {
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
define i32 @main() {
entry:
  %b0 = alloca [5 x i64]
  %v0 = alloca ptr
  store ptr %b0, ptr %v0
  %t1 = load ptr, ptr %v0
  %t2 = add i64 0, 5
  call void @f1(ptr %t1, i64 %t2)
  %t3 = load ptr, ptr %v0
  %t4 = add i64 0, 5
  %t5 = call i64 @f2(ptr %t3, i64 %t4)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t5)
  %v1 = alloca [2 x i64]
  %t6 = add i64 0, 6
  %t7 = getelementptr i64, ptr %v1, i64 0
  store i64 %t6, ptr %t7
  %t8 = add i64 0, 7
  %t9 = getelementptr i64, ptr %v1, i64 1
  store i64 %t8, ptr %t9
  %t10 = call i64 @m0(ptr %v1)
  ret i32 0
}
