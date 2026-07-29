declare signext i8 @i8_id(i8 signext)
declare zeroext i8 @u8_id(i8 zeroext)
declare signext i16 @i16_id(i16 signext)
declare zeroext i16 @u16_id(i16 zeroext)
declare zeroext i1 @b_id(i1 zeroext)
declare i32 @i32_id(i32)
declare i64 @i64_id(i64)
declare i64 @c_id(i64)
declare i32 @mixed(i8 signext, i32, i16 zeroext, i1 zeroext, i64)
call signext i8 @i8_id(i8 signext
call zeroext i1 @b_id(i1 zeroext
call i32 @mixed(i8 signext
