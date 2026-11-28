/* AUTO-GENERATED from docs/DIAGNOSTIC_CODES.md by scripts/gen_diag_codes.py.
 * DO NOT EDIT MANUALLY — edit docs/DIAGNOSTIC_CODES.md and re-run the generator. */
#ifndef ZAN_DIAG_CODES_H
#define ZAN_DIAG_CODES_H

#include <stdbool.h>

#define ZAN_DIAG_CODE_COUNT 308

typedef enum {
    ZAN_DIAG_NONE = 0,
    ZAN_DIAG_E0001 = 1, /* conditional-compilation nesting too deep (max %d) */
    ZAN_DIAG_E0002 = 2, /* #error %s */
    ZAN_DIAG_W0003 = 3, /* #warning %s */
    ZAN_DIAG_E0004 = 4, /* unterminated multi-line comment */
    ZAN_DIAG_E0005 = 5, /* integer literal is too large for 'ulong' */
    ZAN_DIAG_E0006 = 6, /* integer literal is too large for 'long' */
    ZAN_DIAG_E0007 = 7, /* hexadecimal literal requires at least one digit after '0x' */
    ZAN_DIAG_E0008 = 8, /* integer literal is too long */
    ZAN_DIAG_E0009 = 9, /* binary literal requires at least one digit after '0b' */
    ZAN_DIAG_E0010 = 10, /* octal literal requires digits 0-7 after '0o' */
    ZAN_DIAG_E0011 = 11, /* octal literal requires at least one digit after '0o' */
    ZAN_DIAG_E0012 = 12, /* exponent requires at least one digit after 'e' */
    ZAN_DIAG_E0013 = 13, /* floating-point/decimal literal is too long */
    ZAN_DIAG_E0014 = 14, /* '\x' escape requires at least one hex digit */
    ZAN_DIAG_E0015 = 15, /* '\u' escape requires %d hex digits */
    ZAN_DIAG_E0016 = 16, /* '\%c' escape value 0x%X is a surrogate code point */
    ZAN_DIAG_E0017 = 17, /* invalid escape sequence '\%c' */
    ZAN_DIAG_E0018 = 18, /* out of memory while lexing string literal */
    ZAN_DIAG_E0019 = 19, /* unterminated string literal */
    ZAN_DIAG_E0020 = 20, /* unterminated character literal */
    ZAN_DIAG_E0021 = 21, /* unterminated interpolated string */
    ZAN_DIAG_E0022 = 22, /* interpolated strings nested more than %d deep */
    ZAN_DIAG_E0023 = 23, /* interpolation format specifier exceeds %d characters and was truncated */
    ZAN_DIAG_E0024 = 24, /* too many closing quotes in raw string literal */
    ZAN_DIAG_E0025 = 25, /* insufficient indentation for raw string literal */
    ZAN_DIAG_E0026 = 26, /* unterminated raw interpolated string */
    ZAN_DIAG_E0027 = 27, /* unterminated verbatim interpolated string */
    ZAN_DIAG_E0028 = 28, /* unterminated verbatim string literal */
    ZAN_DIAG_E0029 = 29, /* unterminated raw string literal */
    ZAN_DIAG_W0030 = 30, /* unterminated #if/#ifdef (missing #endif) */
    ZAN_DIAG_E0031 = 31, /* unexpected character '%c' (0x%02x) */
    ZAN_DIAG_E0032 = 32, /* expected '%s', got '%s' */
    ZAN_DIAG_E0033 = 33, /* '%s' is a reserved word and cannot be used as a name here */
    ZAN_DIAG_E0034 = 34, /* （动态文案：`parser_diag_expected_name` 兜底，如 expected variable name） */
    ZAN_DIAG_E0035 = 35, /* expected type declaration (class, struct, interface, enum, or delegate) */
    ZAN_DIAG_E0036 = 36, /* type nesting too deep (max %d) */
    ZAN_DIAG_E0037 = 37, /* expected type */
    ZAN_DIAG_E0038 = 38, /* array rank specifier is too deep (max 16) */
    ZAN_DIAG_E0039 = 39, /* expected 'on' in join clause */
    ZAN_DIAG_E0040 = 40, /* expected 'equals' in join clause */
    ZAN_DIAG_E0041 = 41, /* expected 'by' in group clause */
    ZAN_DIAG_E0042 = 42, /* a group clause without 'into' must be the final clause of the query */
    ZAN_DIAG_E0043 = 43, /* expected 'select' in query expression */
    ZAN_DIAG_E0044 = 44, /* nameof argument must be an identifier or member access */
    ZAN_DIAG_E0045 = 45, /* unexpected token '%s' in expression */
    ZAN_DIAG_E0046 = 46, /* expression nesting too deep (max %d) */
    ZAN_DIAG_E0047 = 47, /* expression chain too long (max %d operators); split it into statements */
    ZAN_DIAG_E0048 = 48, /* statement nesting too deep (max %d) */
    ZAN_DIAG_E0049 = 49, /* unsupported tuple deconstruction form */
    ZAN_DIAG_E0050 = 50, /* a declaration cannot be used as a single-statement body; enclose it in braces */
    ZAN_DIAG_E0051 = 51, /* expected local function body */
    ZAN_DIAG_E0052 = 52, /* iterator method '%.*s' must return IEnumerable<T> or List<T> */
    ZAN_DIAG_E0053 = 53, /* expected class, struct, interface, or enum */
    ZAN_DIAG_E0054 = 54, /* unexpected end of file inside namespace block; missing '}' */
    ZAN_DIAG_E0055 = 55, /* unexpected '}' at top level */
    ZAN_DIAG_E0056 = 56, /* declaration too large to lower: generated source exceeds %d bytes */
    ZAN_DIAG_E0057 = 57, /* event field requires a non-generic delegate type */
    ZAN_DIAG_E0058 = 58, /* event type '%.*s' is not a declared delegate */
    ZAN_DIAG_E0059 = 59, /* iterator 局部变量约束（动态文案：duplicate local '%.*s' in iterator method / must declare an explicit typ... */
    ZAN_DIAG_E0060 = 60, /* iterator method '%.*s' must be static in this release (instance iterators are not synthesized... */
    ZAN_DIAG_E0061 = 61, /* internal: generic base specialization hit unhandled AST kind %d */
    ZAN_DIAG_E1000 = 1000, /* '%.*s' is declared both as a %s and as a method in '%.*s'; give one of them a different name */
    ZAN_DIAG_E1001 = 1001, /* duplicate '%.*s' in '%.*s': a %s with this name is already declared */
    ZAN_DIAG_E1002 = 1002, /* duplicate enum member '%.*s' in '%.*s' */
    ZAN_DIAG_E1003 = 1003, /* duplicate method '%.*s' in '%.*s': a method with the same parameter types is already declared */
    ZAN_DIAG_E1004 = 1004, /* duplicate constructor in '%.*s': a constructor with the same parameter types is already declared */
    ZAN_DIAG_E1005 = 1005, /* undefined type '%.*s' */
    ZAN_DIAG_E1006 = 1006, /* using the generic type '%.*s' requires %d type arguments (got %d) */
    ZAN_DIAG_E1007 = 1007, /* the non-generic type '%.*s' cannot be used with type arguments */
    ZAN_DIAG_E1008 = 1008, /* 'Func' requires at least one type argument (the return type) */
    ZAN_DIAG_E1009 = 1009, /* type declaration '%.*s' collides with a built-in type name */
    ZAN_DIAG_E1010 = 1010, /* duplicate type declaration '%.*s' */
    ZAN_DIAG_E1011 = 1011, /* cyclic inheritance involving '%.*s' */
    ZAN_DIAG_E1012 = 1012, /* invalid base type */
    ZAN_DIAG_E1013 = 1013, /* undefined base type '%.*s' */
    ZAN_DIAG_E1014 = 1014, /* cannot derive from sealed type '%.*s' */
    ZAN_DIAG_E1015 = 1015, /* type '%.*s' does not implement interface method '%.*s' */
    ZAN_DIAG_E1016 = 1016, /* destructors ('~%.*s()') are not supported: the ARC runtime has no destructor hook on release ... */
    ZAN_DIAG_E1017 = 1017, /* enum member '%.*s' value %lld does not fit in 'int' (enum members are 32-bit) */
    ZAN_DIAG_E1018 = 1018, /* enum member '%.*s' initializer must be an integer constant expression */
    ZAN_DIAG_E1019 = 1019, /* ambiguous type '%.*s'; qualify it with its namespace */
    ZAN_DIAG_E2000 = 2000, /* %s is not allowed in a [NoRuntime] method */
    ZAN_DIAG_W2001 = 2001, /* call in a [NoRuntime] method returns an ARC-managed value with an owned +1 reference, but [No... */
    ZAN_DIAG_E2002 = 2002, /* '%.*s.%.*s' is private and cannot be accessed from '%.*s' */
    ZAN_DIAG_E2003 = 2003, /* '%.*s.%.*s' is protected and cannot be accessed from '%.*s' */
    ZAN_DIAG_E2004 = 2004, /* '%.*s.%.*s' is internal to '%.*s' and cannot be accessed from '%.*s' */
    ZAN_DIAG_E2005 = 2005, /* cannot assign to indexing expression of ReadOnlySpan */
    ZAN_DIAG_E2006 = 2006, /* property '%.*s' has no setter and cannot be assigned */
    ZAN_DIAG_E2007 = 2007, /* property '%.*s' has an init accessor and can only be assigned in an object initializer or a c... */
    ZAN_DIAG_E2008 = 2008, /* cannot assign to readonly field '%.*s' outside a constructor of '%.*s' */
    ZAN_DIAG_W2009 = 2009, /* closure captures 'this' while assigned to a member of 'this'; this creates a potential strong... */
    ZAN_DIAG_E2010 = 2010, /* '%s' must have a public parameterless constructor to satisfy 'new()' constraint for '%.*s' of... */
    ZAN_DIAG_E2011 = 2011, /* unknown constraint '%.*s' in 'where %.*s : ...' for '%s' */
    ZAN_DIAG_E2012 = 2012, /* type '%s' does not satisfy constraint '%s' for type parameter '%.*s' of '%s' */
    ZAN_DIAG_E2013 = 2013, /* extern method '%.*s' must be static; a native function has no receiver, so drop 'extern' or m... */
    ZAN_DIAG_E2014 = 2014, /* weak field '%.*s' inside value-type struct '%.*s' cannot be tracked safely; move it to a clas... */
    ZAN_DIAG_E2015 = 2015, /* weak field '%.*s' has type '%s'; weak requires an ARC-managed class or interface reference, s... */
    ZAN_DIAG_E2016 = 2016, /* the builtin type '%s' has no constructor -- use literals, casts or the conversion functions */
    ZAN_DIAG_E2017 = 2017, /* no constructor of '%s' accepts %d arguments */
    ZAN_DIAG_E2018 = 2018, /* no constructor of '%s' takes %d argument(s); a class that declares constructors has no implic... */
    ZAN_DIAG_E2019 = 2019, /* cannot apply unary '%c' to '%s' */
    ZAN_DIAG_E2020 = 2020, /* operator cannot be applied to operands of type '%s' and '%s': mixed signed/unsigned compariso... */
    ZAN_DIAG_E2021 = 2021, /* cannot convert 'void' to '%s' in %s: no implicit conversion */
    ZAN_DIAG_E2022 = 2022, /* narrowing conversion from '%s' to '%s' in %s needs an explicit cast (C# rules) */
    ZAN_DIAG_E2023 = 2023, /* cannot convert '%s' to '%s' in %s: a task started from a lambda/Action carries no result; dec... */
    ZAN_DIAG_E2024 = 2024, /* cannot convert '%s' to '%s' in %s: no implicit conversion */
    ZAN_DIAG_E2025 = 2025, /* cannot convert 'null' to '%s' in argument %d of '%.*s': null cannot be assigned to a value type */
    ZAN_DIAG_E2026 = 2026, /* type '%s' must have a public parameterless constructor to satisfy 'new()' constraint for type... */
    ZAN_DIAG_E2027 = 2027, /* type '%s' does not satisfy constraint '%s' for type parameter '%.*s' of method '%.*s' */
    ZAN_DIAG_E2028 = 2028, /* '%.*s.%.*s' takes at least %d argument%s, but %d given */
    ZAN_DIAG_E2029 = 2029, /* no overload of '%.*s.%.*s' takes %d argument%s */
    ZAN_DIAG_E2030 = 2030, /* '%.*s.%.*s' takes %d argument%s, but %d given */
    ZAN_DIAG_E2031 = 2031, /* '%.*s.%.*s' takes %d to %d arguments, but %d given */
    ZAN_DIAG_E2032 = 2032, /* cannot convert 'null' to '%s' in argument %d of '%s.%.*s': no implicit conversion */
    ZAN_DIAG_E2033 = 2033, /* cannot convert '%s' to '%s' in argument %d of '%s.%.*s': no implicit conversion */
    ZAN_DIAG_E2034 = 2034, /* cannot use a 'void' value in argument %d of '%.*s': a void call has no result */
    ZAN_DIAG_E2035 = 2035, /* cannot convert '%s' to '%s' in argument %d of '%.*s': no implicit conversion */
    ZAN_DIAG_E2036 = 2036, /* cannot use a 'void' value in argument %d of '%s' constructor: a void call has no result */
    ZAN_DIAG_E2037 = 2037, /* cannot convert '%s' to '%s' in argument %d of '%s' constructor: no implicit conversion */
    ZAN_DIAG_E2038 = 2038, /* no overload of constructor '%s' matches the given argument types */
    ZAN_DIAG_E2039 = 2039, /* '%.*s' is a method, not a value; call it as '%.*s()' */
    ZAN_DIAG_E2040 = 2040, /* cannot concatenate 'string' and '%s' -- convert the operand explicitly (e.g. call .ToStr()/re... */
    ZAN_DIAG_E2041 = 2041, /* cannot apply operator to 'decimal' and '%s' -- convert explicitly (e.g. Decimal.FromDouble) */
    ZAN_DIAG_E2042 = 2042, /* delegates are single-cast: '+='/'-=' combination is not supported -- assign directly (`d = h`... */
    ZAN_DIAG_E2043 = 2043, /* cannot apply operator to '%s' and '%s' */
    ZAN_DIAG_E2044 = 2044, /* cannot compare '%s' and '%s' -- call .Get() on the binding to compare the bound value */
    ZAN_DIAG_E2045 = 2045, /* cannot compare '%s' and '%s' */
    ZAN_DIAG_E2046 = 2046, /* cannot compare '%s' and '%s' with a relational operator: no implicit numeric conversion */
    ZAN_DIAG_E2047 = 2047, /* cannot apply logical operator to '%s' and '%s': both operands must be bool */
    ZAN_DIAG_E2048 = 2048, /* cannot apply '?' '?' to '%s' and '%s' */
    ZAN_DIAG_E2049 = 2049, /* cannot apply unary '!' to '%s': the operand must be a bool */
    ZAN_DIAG_E2050 = 2050, /* nameof requires a single identifier or member access */
    ZAN_DIAG_E2051 = 2051, /* cannot use a 'void' value in argument %d: a void call has no result */
    ZAN_DIAG_E2052 = 2052, /* cannot interpolate '%s' -- convert the operand explicitly instead of formatting the reference */
    ZAN_DIAG_E2053 = 2053, /* '%.*s' indexer takes %d index%s, but %d given */
    ZAN_DIAG_E2054 = 2054, /* no '%.*s' indexer takes an index of type '%.*s' */
    ZAN_DIAG_E2055 = 2055, /* rank-%d array indexed with %d indices */
    ZAN_DIAG_E2056 = 2056, /* no '%.*s' indexer accepts this assignment */
    ZAN_DIAG_E2057 = 2057, /* cannot instantiate a type parameter '%s'（checker 拦无 `new()` 约束写法；B-ID163② 起受约束但处于擦除体（类型实参未定）的... */
    ZAN_DIAG_E2058 = 2058, /* cannot instantiate abstract class '%s' */
    ZAN_DIAG_E2059 = 2059, /* an array creation with an initializer requires constant dimensions */
    ZAN_DIAG_E2060 = 2060, /* array initializer has %d element%s but the dimensions describe %lld */
    ZAN_DIAG_E2061 = 2061, /* '%s' has no member '%.*s' */
    ZAN_DIAG_E2062 = 2062, /* type '%s' has no Add method for a collection initializer */
    ZAN_DIAG_E2063 = 2063, /* cannot apply '?:' operator to operands of type '%s' and '%s': neither converts to the other a... */
    ZAN_DIAG_W2064 = 2064, /* unreachable switch arm: previous discard arm '_' matches all remaining values */
    ZAN_DIAG_E2065 = 2065, /* duplicate pattern in switch expression */
    ZAN_DIAG_E2066 = 2066, /* duplicate discard arm '_' in switch expression */
    ZAN_DIAG_W2067 = 2067, /* switch expression does not handle enum member '%.*s' of '%.*s'; add missing case or '_' disca... */
    ZAN_DIAG_E2068 = 2068, /* 'with' requires a record (class/struct) receiver, not '%s' */
    ZAN_DIAG_E2069 = 2069, /* '%s' has no field '%.*s' for 'with' */
    ZAN_DIAG_E2070 = 2070, /* cannot convert '%s' to 'object': value types cannot be boxed (Zan has no boxing facility) */
    ZAN_DIAG_E2071 = 2071, /* cannot convert '%s' to '%s': no such conversion */
    ZAN_DIAG_W2072 = 2072, /* returned closure captures 'this' and escapes method scope; ensure the caller does not retain ... */
    ZAN_DIAG_E2073 = 2073, /* assignment used as a condition (did you mean '=='? assignment (=) has no boolean value */
    ZAN_DIAG_W2074 = 2074, /* condition should be bool, got '%s' */
    ZAN_DIAG_W2075 = 2075, /* switch expression has non-switchable type '%s' */
    ZAN_DIAG_W2076 = 2076, /* unreachable switch case: previous default/catch-all case matches all remaining values */
    ZAN_DIAG_E2077 = 2077, /* duplicate case label in switch statement */
    ZAN_DIAG_E2078 = 2078, /* duplicate default label in switch statement */
    ZAN_DIAG_W2079 = 2079, /* switch statement does not handle enum member '%.*s' of '%.*s'; add missing case or default label */
    ZAN_DIAG_W2080 = 2080, /* switch statement on bool does not handle 'true' case; add missing case or default label */
    ZAN_DIAG_W2081 = 2081, /* switch statement on bool does not handle 'false' case; add missing case or default label */
    ZAN_DIAG_E2082 = 2082, /* cannot access member '%.*s' on 'null'; null reference dereference faults at runtime */
    ZAN_DIAG_E2083 = 2083, /* '%.*s' can return null; accessing '%.*s' on its result faults at runtime -- use '?.', or stor... */
    ZAN_DIAG_E2084 = 2084, /* type '%.*s.%.*s' %s '%.*s' */
    ZAN_DIAG_E2085 = 2085, /* '%.*s' %s '%.*s' */
    ZAN_DIAG_E2086 = 2086, /* enum type '%.*s' has no member '%.*s' */
    ZAN_DIAG_E2087 = 2087, /* cannot access member '%.*s' directly on nullable type '%s'; access via '.Value' or check '.Ha... */
    ZAN_DIAG_E2088 = 2088, /* '%.*s' is a static field of '%.*s'; qualify it with the type name ('%.*s.%.*s'), not an instance */
    ZAN_DIAG_E2089 = 2089, /* type '%s' has no member '%.*s' */
    ZAN_DIAG_E2090 = 2090, /* type '%s' has no method '%.*s' */
    ZAN_DIAG_E2091 = 2091, /* struct '%.*s' exceeds maximum nesting depth */
    ZAN_DIAG_E2092 = 2092, /* struct member '%.*s' of type '%.*s' causes a cycle in the struct layout (recursive struct has... */
    ZAN_DIAG_E2093 = 2093, /* non-abstract class '%.*s' does not implement abstract method '%.*s' inherited from '%.*s' */
    ZAN_DIAG_E2094 = 2094, /* an abstract method cannot be static: '%.*s' */
    ZAN_DIAG_E2095 = 2095, /* an abstract method cannot be async: '%.*s' */
    ZAN_DIAG_E2096 = 2096, /* class '%.*s' declares an abstract method and must be declared 'abstract' */
    ZAN_DIAG_W2097 = 2097, /* '%.*s' is declared 'virtual' but hides an inherited virtual with the same name and arity; cal... */
    ZAN_DIAG_E2098 = 2098, /* use of unassigned local variable '%.*s' */
    ZAN_DIAG_E2099 = 2099, /* the out parameter '%.*s' must be assigned before control leaves the method */
    ZAN_DIAG_E2100 = 2100, /* no parameter named '%.*s' */
    ZAN_DIAG_E2101 = 2101, /* parameter '%.*s' is already assigned */
    ZAN_DIAG_E2102 = 2102, /* named arguments must bind to the leading parameters in order */
    ZAN_DIAG_E2103 = 2103, /* no overload of '%.*s.%.*s' matches argument type(s) */
    ZAN_DIAG_E2104 = 2104, /* expression is nested too deeply to type (inference recursed %d levels); split it into statements */
    ZAN_DIAG_E2105 = 2105, /* cannot convert '%s' to '%s' in %s: a value struct and a primitive are unrelated types */
    ZAN_DIAG_E2106 = 2106, /* cannot convert '%s' to '%s' in %s: value types cannot be boxed into a reference type */
    ZAN_DIAG_E2107 = 2107, /* cannot convert '%s' to '%s' in %s: a reference type cannot be converted to a value type */
    ZAN_DIAG_E2108 = 2108, /* cannot convert '%s' to '%s' in %s: generic type arguments are invariant and must match exactly */
    ZAN_DIAG_E2109 = 2109, /* possible null reference dereference: '%.*s' is a nullable type '%s'; access via '?.' or asser... */
    ZAN_DIAG_E2110 = 2110, /* type '%.*s' cannot be declared abstract with its kind or modifiers */
    ZAN_DIAG_W2111 = 2111, /* strong reference cycle between classes: %s; ARC cannot free objects linked this way -- mark '... */
    ZAN_DIAG_E2112 = 2112, /* an abstract method cannot be private, sealed, or extern: '%.*s' (also markup handler missing ... */
    ZAN_DIAG_E2113 = 2113, /* an abstract method cannot have a body: '%.*s' (also markup binding path member missing) */
    ZAN_DIAG_E2114 = 2114, /* markup mount field '%.*s' in %s is not a field or property of '%.*s' */
    ZAN_DIAG_E2115 = 2115, /* binding property '%.*s' in %s must be an identifier (write a static value as a plain attribute) */
    ZAN_DIAG_E2116 = 2116, /* async call '%.*s' used as a value without 'await': write 'await %.*s(...)' */
    ZAN_DIAG_E2151 = 2151, /* [ModuleInitializer] can only be applied to static parameterless void methods */
    ZAN_DIAG_E3000 = 3000, /* property getter requires a receiver */
    ZAN_DIAG_E3001 = 3001, /* property setter requires a receiver */
    ZAN_DIAG_E3002 = 3002, /* instance method '%.*s' cannot be used as a value here: no receiver is in scope */
    ZAN_DIAG_E3003 = 3003, /* use of undeclared identifier '%.*s' */
    ZAN_DIAG_E3004 = 3004, /* type '%s' does not define a unary '%s' operator overload */
    ZAN_DIAG_E3005 = 3005, /* unary '%s' operator overload of '%s' has no body to call */
    ZAN_DIAG_E3006 = 3006, /* cannot assign to readonly field '%.*s' outside a constructor of '%.*s' */
    ZAN_DIAG_E3007 = 3007, /* tuple assignment is not supported; use `var (a, b) = rhs` to declare the names */
    ZAN_DIAG_E3008 = 3008, /* cannot assign through this indexed expression */
    ZAN_DIAG_E3009 = 3009, /* '%.*s' has no member '%.*s' */
    ZAN_DIAG_E3010 = 3010, /* ++/-- requires a numeric operand */
    ZAN_DIAG_E3011 = 3011, /* cannot apply ++/-- to this expression */
    ZAN_DIAG_E3012 = 3012, /* Task.Result requires a Task<T> value */
    ZAN_DIAG_E3013 = 3013, /* type '%s' has no member '%.*s' */
    ZAN_DIAG_E3014 = 3014, /* instance method group '%.*s' cannot be used as a value */
    ZAN_DIAG_E3015 = 3015, /* cannot resolve member '%.*s' */
    ZAN_DIAG_E3016 = 3016, /* orderby key must be int, long, double or string */
    ZAN_DIAG_E3017 = 3017, /* join key types must match (int, long, double or string) */
    ZAN_DIAG_E3018 = 3018, /* group key must be int or string */
    ZAN_DIAG_E3019 = 3019, /* query expression has too many join clauses (max 15) */
    ZAN_DIAG_E3020 = 3020, /* cannot form a tuple here */
    ZAN_DIAG_E3021 = 3021, /* tuple element %d has no field in its struct type */
    ZAN_DIAG_E3022 = 3022, /* object initializer can only continue a call that returns a class instance */
    ZAN_DIAG_E3023 = 3023, /* anonymous object `new { ... }` is only valid inside a typed ORM query (GroupBy/ToList/ToAggre... */
    ZAN_DIAG_E3024 = 3024, /* no constructor of '%.*s' accepts %d argument%s */
    ZAN_DIAG_E3025 = 3025, /* 'with' requires a record receiver with a synthesized clone */
    ZAN_DIAG_E3026 = 3026, /* await native extern calls are not supported on wasm32: the blocking runtime requires native w... */
    ZAN_DIAG_E3027 = 3027, /* await native extern calls are only supported inside an async method */
    ZAN_DIAG_E3028 = 3028, /* await native extern call has %d arguments; at most four scalar arguments are supported (prepa... */
    ZAN_DIAG_E3029 = 3029, /* await native extern call returns '%s', but only int, long, nint, or void can cross a suspensi... */
    ZAN_DIAG_E3030 = 3030, /* await native extern argument %d has type '%s'; only integer, nint, bool, and enum scalars are... */
    ZAN_DIAG_E3031 = 3031, /* cannot lower await native extern call: external function is not available in the generated mo... */
    ZAN_DIAG_E3032 = 3032, /* 'await' inside a lambda is not supported: a lambda is not a coroutine */
    ZAN_DIAG_E3033 = 3033, /* await Gate.Park is only supported inside an async method */
    ZAN_DIAG_E3034 = 3034, /* await Socket.%s is only supported inside an async method */
    ZAN_DIAG_E3035 = 3035, /* await Socket.RecvOv is only supported inside an async method */
    ZAN_DIAG_E3036 = 3036, /* await Socket.RecvToOv is only supported inside an async method */
    ZAN_DIAG_E3037 = 3037, /* await Socket.AcceptOv is only supported inside an async method */
    ZAN_DIAG_E3038 = 3038, /* await Socket.ResolveAsync is only supported inside an async method */
    ZAN_DIAG_E3039 = 3039, /* await Socket.ResolveSockAddr is only supported inside an async method */
    ZAN_DIAG_E3040 = 3040, /* cannot convert '%s' to 'object': value types cannot be boxed (Zan has no boxing facility) */
    ZAN_DIAG_E3041 = 3041, /* cannot use 'this' here: %s has no receiver binding */
    ZAN_DIAG_E3042 = 3042, /* cannot use 'base' here: %s has no receiver binding */
    ZAN_DIAG_E3043 = 3043, /* cannot capture a ref or out parameter in a lambda */
    ZAN_DIAG_E3044 = 3044, /* cannot capture parameter '%.*s' of type '%.*s': its runtime ownership is not representable yet */
    ZAN_DIAG_E3045 = 3045, /* cannot capture parameter '%.*s': unsupported value representation */
    ZAN_DIAG_E3046 = 3046, /* out of memory collecting the variables this lambda captures */
    ZAN_DIAG_E3047 = 3047, /* cannot convert %s method '%.*s' to %s delegate '%.*s' */
    ZAN_DIAG_E3048 = 3048, /* cannot convert '%s' to '%s' in argument: no matching constructor */
    ZAN_DIAG_E3049 = 3049, /* ref/out argument must be a variable, a field, or an element */
    ZAN_DIAG_E3050 = 3050, /* unresolved call '%.*s.%.*s': type '%.*s' has no method '%.*s' */
    ZAN_DIAG_E3051 = 3051, /* Math.%s requires an int or floating-point argument */
    ZAN_DIAG_E3052 = 3052, /* Task.Run delegate must be a parameterless void body (an Action); pass an async call for results */
    ZAN_DIAG_E3053 = 3053, /* Task.Run delegate must take no parameters */
    ZAN_DIAG_E3054 = 3054, /* Console.ErrorWriteLine takes a string: format the value with Convert.ToString first */
    ZAN_DIAG_E3055 = 3055, /* Math.%s requires numeric arguments */
    ZAN_DIAG_E3056 = 3056, /* Convert.ToDouble: cannot convert '%.*s' to a number -- pass a string or a numeric value */
    ZAN_DIAG_E3057 = 3057, /* %.*s.Parse requires a string argument */
    ZAN_DIAG_E3058 = 3058, /* %.*s.TryParse requires a string argument */
    ZAN_DIAG_E3059 = 3059, /* Convert.%.*s: cannot convert '%.*s' to a number -- pass a string or a numeric value */
    ZAN_DIAG_E3060 = 3060, /* Convert.ToString: cannot convert '%s' to a string -- call .ToString() on the value instead */
    ZAN_DIAG_E3061 = 3061, /* unsupported format specifier '%.*s' for numeric ToString() (supported: D, X, F, E, G, 0/# sha... */
    ZAN_DIAG_E3062 = 3062, /* cannot call instance method '%.*s.%.*s' via the type name: call it on an instance */
    ZAN_DIAG_E3063 = 3063, /* type '%.*s.%.*s' has no member '%.*s' */
    ZAN_DIAG_E3064 = 3064, /* unresolved call '%.*s': '%.*s' is not a known variable, type, or namespace (is the class impo... */
    ZAN_DIAG_E3065 = 3065, /* unresolved call '%.*s': type '%.*s' has no method '%.*s' */
    ZAN_DIAG_E3066 = 3066, /* '%s' has no member '%.*s' */
    ZAN_DIAG_E3067 = 3067, /* '%s.%.*s' is a property, not a method: drop the '()' */
    ZAN_DIAG_E3068 = 3068, /* this call could not be resolved to any callable (no method, delegate, operator or builtin mat... */
    ZAN_DIAG_E3069 = 3069, /* cannot capture pattern variable '%.*s' of type '%.*s': its runtime ownership is not represent... */
    ZAN_DIAG_E3070 = 3070, /* cannot capture local '%.*s' of type '%.*s': its runtime ownership is not representable yet */
    ZAN_DIAG_E3071 = 3071, /* cannot capture local '%.*s': unsupported value representation */
    ZAN_DIAG_E3072 = 3072, /* cannot initialize an array from '%.*s': declare the variable as '%.*s' instead */
    ZAN_DIAG_E3073 = 3073, /* right-hand side of a deconstruction is not a tuple */
    ZAN_DIAG_E3074 = 3074, /* case label must be a compile-time constant in an integer switch */
    ZAN_DIAG_E3075 = 3075, /* internal: async handler id %d exceeds frame handler capacity %d */
    ZAN_DIAG_E3076 = 3076, /* too many nested try/finally blocks in one function body */
    ZAN_DIAG_E3077 = 3077, /* out of memory tracking nested catch handlers */
    ZAN_DIAG_E3078 = 3078, /* foreach: GetEnumerator() returning interface '%.*s' is not supported yet; return the concrete... */
    ZAN_DIAG_E3079 = 3079, /* foreach protocol enumerator missing an async frame slot */
    ZAN_DIAG_E3080 = 3080, /* too many nested lock/try-finally blocks in one function body */
    ZAN_DIAG_E3081 = 3081, /* duplicate label '%.*s' in this function */
    ZAN_DIAG_E3082 = 3082, /* goto '%.*s' jumps into a try/catch/finally or lock block; C# forbids jumping into one -- rest... */
    ZAN_DIAG_E3083 = 3083, /* goto '%.*s' leaves (or enters) a scope with owning locals; the abandoned references would lea... */
    ZAN_DIAG_E3084 = 3084, /* goto '%.*s' leaves %d finally/lock region(s) whose exits cannot be run -- restructure with br... */
    ZAN_DIAG_E3085 = 3085, /* goto '%.*s' jumps into a finally body */
    ZAN_DIAG_E3086 = 3086, /* a rethrow (`throw;`) is only valid inside a catch block */
    ZAN_DIAG_E3088 = 3088, /* no matching %s constructor with %d argument(s) */
    ZAN_DIAG_E3089 = 3089, /* LLVM function compactor initialization failed: %s */
    ZAN_DIAG_E3090 = 3090, /* LLVM verification failed%s%s: %s */
    ZAN_DIAG_E3091 = 3091, /* failed to get target: %s */
    ZAN_DIAG_E3092 = 3092, /* failed to emit object file: %s */
    ZAN_DIAG_E3093 = 3093, /* explicit-layout type '%.*s' cannot have virtual methods */
    ZAN_DIAG_E3094 = 3094, /* field '%.*s' of explicit-layout type '%.*s' needs [FieldOffset(n)] */
    ZAN_DIAG_E3095 = 3095, /* [FieldOffset(%lu)] on '%.*s' is not %lu-byte aligned */
    ZAN_DIAG_E3096 = 3096, /* this value struct cannot be a collection element: the slot allocator cannot compute its layou... */
    ZAN_DIAG_E3097 = 3097, /* LLVM function compaction failed: %s */
    ZAN_DIAG_E3098 = 3098, /* async: an expression with side effects is evaluated before an await in the same expression; a... */
    ZAN_DIAG_E3099 = 3099, /* %s */
    ZAN_DIAG_E3100 = 3100, /* generic instantiation did not reach a fixed point within %d iterations; the type graph may be... */
    ZAN_DIAG_E3101 = 3101, /* out of memory recording obfuscated string literals (%d recorded) */
    ZAN_DIAG_E3102 = 3102, /* entry point 'Main' cannot be declared in a generic class -- move it to a non-generic type (C#... */
    ZAN_DIAG_E3103 = 3103, /* expression tree lambda: …（树 lambda 不可译形状 fail-closed——decimal 字面、不支持的运算符/方法、lambda 参数自身引用、外层变... */
    ZAN_DIAG_E3104 = 3104, /* cannot convert a lambda to async delegate '%.*s': a lambda is not a coroutine; assign an asyn... */
    ZAN_DIAG_E3105 = 3105, /* Binding.target is non-owning and cannot reference an owned temporary; keep the target in an o... */
    ZAN_DIAG_E3106 = 3106, /* argument %d of '%.*s' must be passed with 'ref'/'out' */
    ZAN_DIAG_E4000 = 4000, /* call to extern '%s' passes a struct by value, which has no C ABI classification for target '%s' */
    ZAN_DIAG_E4001 = 4001, /* shared table %s width %lld is out of range: a width must be between 1 and %lld bytes */
    ZAN_DIAG_E4002 = 4002, /* sizeof: type '%s' has no fixed unmanaged size -- only value types (structs, primitives, enums... */
} zan_diag_code_id_t;

static inline bool zan_diag_code_is_warning(int code) {
    switch (code) {
    case ZAN_DIAG_W0003:
    case ZAN_DIAG_W0030:
    case ZAN_DIAG_W2001:
    case ZAN_DIAG_W2009:
    case ZAN_DIAG_W2064:
    case ZAN_DIAG_W2067:
    case ZAN_DIAG_W2072:
    case ZAN_DIAG_W2074:
    case ZAN_DIAG_W2075:
    case ZAN_DIAG_W2076:
    case ZAN_DIAG_W2079:
    case ZAN_DIAG_W2080:
    case ZAN_DIAG_W2081:
    case ZAN_DIAG_W2097:
    case ZAN_DIAG_W2111:
        return true;
    default:
        return false;
    }
}

#endif /* ZAN_DIAG_CODES_H */
