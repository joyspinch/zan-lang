declare i32 @printf(ptr, ...)
@.fmt = private unnamed_addr constant [6 x i8] c"%lld\0A\00"
define i32 @main() {
entry:
  %t1 = add i64 0, 40
  %t2 = add i64 0, 2
  %t3 = add i64 %t1, %t2
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t3)
  %t4 = add i64 0, 6
  %t5 = add i64 0, 7
  %t6 = mul i64 %t4, %t5
  %t7 = add i64 0, 1
  %t8 = sub i64 %t6, %t7
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t8)
  %t9 = add i64 0, 2
  %t10 = add i64 0, 3
  %t11 = add i64 %t9, %t10
  %t12 = add i64 0, 4
  %t13 = mul i64 %t11, %t12
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t13)
  %t14 = add i64 0, 100
  %t15 = add i64 0, 7
  %t16 = add i64 0, 8
  %t17 = mul i64 %t15, %t16
  %t18 = sub i64 %t14, %t17
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t18)
  %t19 = add i64 0, 17
  %t20 = add i64 0, 5
  %t21 = srem i64 %t19, %t20
  call i32 (ptr, ...) @printf(ptr @.fmt, i64 %t21)
  ret i32 0
}
