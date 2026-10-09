# System.Globalization

> 源码: `packages/Zan.Globalization/src/System/Globalization/Lang.zan`, `packages/Zan.Globalization/src/System/Globalization/Lunar.zan`


## Lang (class)

- static string code="";

- static Dictionary <string, Dictionary <string, string>> packs;

- static List<string> dirs;

- static string Code()

- static bool Set(string newCode)

- static void LoadDir(string langDir)

- static void LoadDirs(List<string> langDirs)

- static string Tr(string key, string dflt)

- static string Tr(string key)

- static Dictionary <string, string> Pack(string code)


## Lunar (class)

- static string GanName(int i)

- static string ZhiName(int i)

- static string AnimalName(int i)

- static string MonthBase(int m)

- static string DigitCn(int n)

- static string DayName(int day)

- static string TermName(int t)

- static string YearData()

- static string JieQiData()

- static int YearCode(int ly)

- static int JieQiDay(int year, int term)

- static int HexDigit(string ch)

- static int Digit(string ch)

- static int Mod(int a, int b)

- static int TotalDays()

- static int MonthLen(int code, int m)

- static int LeapMonth(int code)

- static int LeapLen(int code)

- static int YearLen(int code)

- static LunarDate FromSolar(int year, int month, int day)

- static LunarDate Now()

- static string YearGanZhi(int year, int month, int day)

- static string MonthGanZhi(int year, int month, int day)

- static string DayGanZhi(int year, int month, int day)

- static string Zodiac(int year, int month, int day)

- static string SolarTerm(int year, int month, int day)

- static int DaysFromCivil(int year, int month, int day)


## LunarDate (class)

- int solarYear;

- int solarMonth;

- int solarDay;

- int year;

- int month;

- int day;

- bool isLeap;

- LunarDate(int sy, int sm, int sd, int y, int m, int d, bool leap)

- int Year()

- int Month()

- int Day()

- bool IsLeapMonth()

- string YearGanZhi()

- string Zodiac()

- string MonthGanZhi()

- string DayGanZhi()

- string MonthName()

- string DayName()

- string ToString()
