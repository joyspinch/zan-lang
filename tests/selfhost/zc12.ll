declare i32 @printf(ptr, ...)
declare i32 @puts(ptr)
declare i64 @strlen(ptr)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
@.ifmt = private unnamed_addr constant [5 x i8] c"%lld\00"
@.sfmt = private unnamed_addr constant [3 x i8] c"%s\00"
@.str0 = private unnamed_addr constant [4 x i8] [i8 65, i8 66, i8 67, i8 0]
@.str1 = private unnamed_addr constant [7 x i8] [i8 98, i8 97, i8 110, i8 97, i8 110, i8 97, i8 0]
define i64 @f0(ptr %arg0) {
entry:
  %v0 = alloca ptr
  store ptr %arg0, ptr %v0
  %v1 = alloca i64
  %t1 = add i64 0, 0
  store i64 %t1, ptr %v1
  %v2 = alloca i64
  %t2 = add i64 0, 0
  store i64 %t2, ptr %v2
  %v3 = alloca i64
  %t3 = load ptr, ptr %v0
  %t4 = call i64 @strlen(ptr %t3)
  store i64 %t4, ptr %v3
  br label %L1
L1:
  %t5 = load i64, ptr %v2
  %t6 = load i64, ptr %v3
  %t7 = icmp slt i64 %t5, %t6
  br i1 %t7, label %L2, label %L3
L2:
  %t8 = load i64, ptr %v1
  %t9 = load i64, ptr %v2
  %t10 = load ptr, ptr %v0
  %t11 = getelementptr i8, ptr %t10, i64 %t9
  %t12 = load i8, ptr %t11
  %t13 = zext i8 %t12 to i64
  %t14 = add i64 %t8, %t13
  store i64 %t14, ptr %v1
  %t15 = load i64, ptr %v2
  %t16 = add i64 0, 1
  %t17 = add i64 %t15, %t16
  store i64 %t17, ptr %v2
  br label %L1
L3:
  %t18 = load i64, ptr %v1
  ret i64 %t18
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
  %v4 = alloca i64
  %t3 = load ptr, ptr %v0
  %t4 = call i64 @strlen(ptr %t3)
  store i64 %t4, ptr %v4
  br label %L1
L1:
  %t5 = load i64, ptr %v3
  %t6 = load i64, ptr %v4
  %t7 = icmp slt i64 %t5, %t6
  br i1 %t7, label %L2, label %L3
L2:
  %t8 = load i64, ptr %v3
  %t9 = load ptr, ptr %v0
  %t10 = getelementptr i8, ptr %t9, i64 %t8
  %t11 = load i8, ptr %t10
  %t12 = zext i8 %t11 to i64
  %t13 = load i64, ptr %v1
  %t14 = icmp eq i64 %t12, %t13
  br i1 %t14, label %L4, label %L5
L4:
  %t15 = load i64, ptr %v2
  %t16 = add i64 0, 1
  %t17 = add i64 %t15, %t16
  store i64 %t17, ptr %v2
  br label %L6
L5:
  br label %L6
L6:
  %t18 = load i64, ptr %v3
  %t19 = add i64 0, 1
  %t20 = add i64 %t18, %t19
  store i64 %t20, ptr %v3
  br label %L1
L3:
  %t21 = load i64, ptr %v2
  ret i64 %t21
}
define i32 @main() {
entry:
  %v0 = alloca ptr
  store ptr @.str0, ptr %v0
  %t1 = load ptr, ptr %v0
  %t2 = call i64 @f0(ptr %t1)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t2)
  %t3 = load ptr, ptr %v0
  %t4 = call i64 @strlen(ptr %t3)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t4)
  %v1 = alloca ptr
  store ptr @.str1, ptr %v1
  %t5 = load ptr, ptr %v1
  %t6 = add i64 0, 97
  %t7 = call i64 @f1(ptr %t5, i64 %t6)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t7)
  %t8 = load ptr, ptr %v1
  %t9 = call i64 @strlen(ptr %t8)
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t9)
  ret i32 0
}
