# System.Globalization

> 源码: `stdlib/System/Globalization/Lunar.zan`


## Lunar (class)

农历(阴历)查询:公历 → 农历日期,以及干支(年/月/日)、生肖、
农历月日中文名与二十四节气。纯算法 + 内置数据表,全平台一致。

LunarDate d = Lunar.FromSolar(2024, 2, 10);
d.YearGanZhi();      // "甲辰"
d.MonthName();       // "正月"
d.DayName();         // "初一"
d.ToString();        // "甲辰年正月初一"
Lunar.SolarTerm(2024, 2, 4);   // "立春"(非节气日返回 "")

数据与规则说明:
- 支持范围 1900-01-31(农历庚子年正月初一)~ 2100-12-31;
- 农历年表(每年各月大小、闰月号与闰月大小)与二十四节气逐日表
均由权威历表(lunar_python,紫金山天文台历算)导出,并与该库在
1900..2100 全部 73384 天逐日核对一致;
- 年干支以农历正月初一为界(与生肖一致);月干支以节气月为界
(立春为年界、十二节为月界,与通书一致);日干支按公历日计算;
- 节气按"当天"匹配:返回该日的节气名,非节气日返回空串。

- static string GanName(int i)
  - 天干名,`i` 任意整数(内部取模 10)。

- static string ZhiName(int i)
  - 地支名,`i` 任意整数(内部取模 12)。

- static string AnimalName(int i)
  - 生肖名(地支序):子鼠、丑牛、寅虎、卯兔、辰龙、巳蛇、
    午马、未羊、申猴、酉鸡、戌狗、亥猪。

- static string MonthBase(int m)
  - 农历月份名:正月、二月 ... 冬月(十一)、腊月(十二)。

- static string DigitCn(int n)
  - 数字 1..9 的中文写法。

- static string DayName(int day)
  - 农历日名:初一 .. 三十(含初十、二十、廿九等)。

- static string TermName(int t)
  - 二十四节气名(序:小寒、大寒、立春 ... 冬至)。

- static string YearData()
  - 农历年表:1900..2100 每年 5 位十六进制(空格分隔)。
    编码:bit0..11 = 1~12 月大小(1=30 天),bit12..15 = 闰月号
    (0=无闰),bit16 = 闰月大小。

- static string JieQiData()
  - 二十四节气日表:1900..2100 每年 24 个节气(序同
    TermName)各 2 位十进制的"日",共 48 字符/年,连续拼接。

- static int YearCode(int ly)
  - 农历年的编码(见 YearData)。

- static int JieQiDay(int year, int term)
  - 某年某节气(0..23)在当月第几日。

- static int HexDigit(string ch)
  - 十六进制字符 → 数值(0..15);未知字符返回 0。

- static int Digit(string ch)
  - 十进制字符 → 数值(0..9)。

- static int Mod(int a, int b)
  - 正负通用的取模(结果与 `b` 同号,恒非负)。

- static int TotalDays()
  - 农历支持范围的总天数(1900-01-31 ~ 2100-12-31)。

- static int MonthLen(int code, int m)
  - 某公历月第 1 个月(大小月)的天数。

- static int LeapMonth(int code)
  - 闰月号(0 = 无闰月)。

- static int LeapLen(int code)
  - 闰月天数。

- static int YearLen(int code)
  - 某农历年的总天数。

- static LunarDate FromSolar(int year, int month, int day)
  - 公历日期 → 农历日期。超出 1900-01-31 ~ 2100-12-31 抛
    ArgumentException。

- static LunarDate Now()
  - 今天的农历日期(按 DateTime.Now,UTC 时刻,同 DateTime 约定)。

- static string YearGanZhi(int year, int month, int day)
  - 年干支(以农历正月初一为界)。

- static string MonthGanZhi(int year, int month, int day)
  - 月干支(以节气月为界)。

- static string DayGanZhi(int year, int month, int day)
  - 日干支,如 "甲辰"。按公历日直接计算,不限于农历支持范围。

- static string Zodiac(int year, int month, int day)
  - 生肖(以农历正月初一为界)。

- static string SolarTerm(int year, int month, int day)
  - 当日节气名(小寒..冬至);非节气日返回 ""。

- static int DaysFromCivil(int year, int month, int day)
  - 公历日序数(自 1970-01-01 起),与 DateTime 算法一致。


## LunarDate (class)

一个农历日期:年/月/日 + 是否闰月,及干支/生肖/中文名查询。

- int solarYear;

- int solarMonth;

- int solarDay;

- int year;

- int month;

- int day;

- bool isLeap;

- LunarDate(int sy, int sm, int sd, int y, int m, int d, bool leap)

- int Year()
  - 农历年。

- int Month()
  - 农历月 1..12。

- int Day()
  - 农历日 1..30。

- bool IsLeapMonth()
  - 是否闰月。

- string YearGanZhi()
  - 年干支,如 "甲辰"(按农历正月初一为界,与生肖一致)。

- string Zodiac()
  - 生肖,如 "龙"。

- string MonthGanZhi()
  - 月干支(节气月,以立春为年界、十二节为月界),如 "丙寅"。
    与农历月份无关,闰月沿用其所在节气月。

- string DayGanZhi()
  - 日干支,如 "甲辰"(按公历日直接计算,不限于农历范围)。

- string MonthName()
  - 农历月份中文名,闰月带 "闰" 前缀,如 "正月"、"闰二月"。

- string DayName()
  - 农历日中文名,如 "初一"、"廿三"。

- string ToString()
  - 完整表示,如 "甲辰年正月初一"、"癸卯年闰二月初一"。
