/* builtin_api.c -- see builtin_api.h.
 *
 * Every entry below was verified by compiling and running a call to it; members
 * that compiled but produced a wrong result (string.PadLeft, List.Sort,
 * StringBuilder.Clear, ...) are deliberately absent so they either resolve to a
 * standard-library extension method or fail to compile. Dictionary.TryGetValue
 * is present: irgen_call.c lowers it to a hash probe + conditional store into
 * the out parameter (see the Dict method block there). */

#include "builtin_api.h"

#include <string.h>

static const zan_builtin_member_t members_string[] = {
    { "Length",      'P', "int Length" },
    { "Substring",   'M', "string Substring(int start[, int length])" },
    { "IndexOf",     'M', "int IndexOf(string needle[, int startIndex])" },
    { "LastIndexOf", 'M', "int LastIndexOf(string needle)" },
    { "Contains",    'M', "bool Contains(string needle)" },
    { "StartsWith",  'M', "bool StartsWith(string prefix)" },
    { "EndsWith",    'M', "bool EndsWith(string suffix)" },
    { "Replace",     'M', "string Replace(string from, string to)" },
    { "Trim",        'M', "string Trim()" },
    { "ToUpper",     'M', "string ToUpper()" },
    { "ToLower",     'M', "string ToLower()" },
    { "Split",       'M', "List<string> Split(string separator)" },
    { "ToString",    'M', "string ToString()" },
};

static const zan_builtin_member_t members_list[] = {
    { "Count",       'P', "int Count" },
    { "Add",         'M', "void Add(T item)" },
    { "AddRange",    'M', "void AddRange(List<T> items)" },
    { "Insert",      'M', "void Insert(int index, T item)" },
    { "RemoveAt",    'M', "void RemoveAt(int index)" },
    { "Clear",       'M', "void Clear()" },
    { "Contains",    'M', "bool Contains(T item)" },
    { "IndexOf",     'M', "int IndexOf(T item)" },
    { "LastIndexOf", 'M', "int LastIndexOf(T item)" },
    { "Reverse",     'M', "void Reverse()" },
};

static const zan_builtin_member_t members_dict[] = {
    { "Count",       'P', "int Count" },
    { "Keys",        'P', "List<K> Keys" },
    { "Values",      'P', "List<V> Values" },
    { "Add",         'M', "void Add(K key, V value)" },
    { "Remove",      'M', "void Remove(K key)" },
    { "Clear",       'M', "void Clear()" },
    { "ContainsKey", 'M', "bool ContainsKey(K key)" },
    { "TryGetValue", 'M', "bool TryGetValue(K key, out V value)" },
};

static const zan_builtin_member_t members_sb[] = {
    { "Append",     'M', "void Append(object value)" },
    { "AppendLine", 'M', "void AppendLine(object value)" },
    { "Length",     'P', "int Length" },
    { "ToString",   'M', "string ToString()" },
};

static const zan_builtin_member_t members_console[] = {
    { "WriteLine",       'M', "void WriteLine(object value)" },
    { "Write",           'M', "void Write(object value)" },
    { "PrintLine",       'M', "void PrintLine(object value)" },
    { "ReadLine",        'M', "string ReadLine()" },
    { "Read",            'M', "int Read()" },
    { "ReadKey",         'M', "int ReadKey([bool intercept])" },
    { "Clear",           'M', "void Clear()" },
    { "ResetColor",      'M', "void ResetColor()" },
    { "ForegroundColor", 'P', "ConsoleColor ForegroundColor" },
    { "BackgroundColor", 'P', "ConsoleColor BackgroundColor" },
    { "Title",           'P', "string Title" },
};

static const zan_builtin_member_t members_math[] = {
    { "Abs",     'M', "double Abs(double value)" },
    { "Max",     'M', "double Max(double a, double b)" },
    { "Min",     'M', "double Min(double a, double b)" },
    { "Pow",     'M', "double Pow(double x, double y)" },
    { "Sqrt",    'M', "double Sqrt(double value)" },
    { "Round",   'M', "double Round(double value)" },
    { "Floor",   'M', "double Floor(double value)" },
    { "Ceiling", 'M', "double Ceiling(double value)" },
};

static const zan_builtin_member_t members_convert[] = {
    { "ToDouble", 'M', "double ToDouble(string text)" },
    { "ToInt32",  'M', "int ToInt32(string text)" },
    { "ToInt64",  'M', "long ToInt64(string text)" },
};

static const zan_builtin_member_t members_stringcls[] = {
    { "Format",           'M', "string Format(string format, params object[] args)" },
    { "Join",             'M', "string Join(string separator, List<string> values)" },
    { "IsNullOrEmpty",    'M', "bool IsNullOrEmpty(string value)" },
    { "CompareOrdinal",   'M', "int CompareOrdinal(string a, string b)" },
};

static const zan_builtin_member_t members_file[] = {
    { "Exists",        'M', "bool Exists(string path)" },
    { "ReadAllText",   'M', "string ReadAllText(string path)" },
    { "WriteAllText",  'M', "void WriteAllText(string path, string text)" },
    { "AppendAllText", 'M', "void AppendAllText(string path, string text)" },
    { "Delete",        'M', "void Delete(string path)" },
    { "Copy",          'M', "void Copy(string from, string to)" },
    { "Move",          'M', "void Move(string from, string to)" },
    { "GetSize",       'M', "long GetSize(string path)" },
};

static const zan_builtin_member_t members_dir[] = {
    { "Exists",              'M', "bool Exists(string path)" },
    { "CreateDirectory",     'M', "void CreateDirectory(string path)" },
    { "Delete",              'M', "void Delete(string path)" },
    { "ListNames",           'M', "string ListNames(string globPattern)" },
    { "GetCurrentDirectory", 'M', "string GetCurrentDirectory()" },
    { "SetCurrentDirectory", 'M', "void SetCurrentDirectory(string path)" },
};

static const zan_builtin_member_t members_path[] = {
    { "Combine",                    'M', "string Combine(string a, string b)" },
    { "GetFileName",                'M', "string GetFileName(string path)" },
    { "GetFileNameWithoutExtension",'M', "string GetFileNameWithoutExtension(string path)" },
    { "GetDirectoryName",           'M', "string GetDirectoryName(string path)" },
    { "GetExtension",               'M', "string GetExtension(string path)" },
    { "HasExtension",               'M', "bool HasExtension(string path)" },
    { "GetTempPath",                'M', "string GetTempPath()" },
};

static const zan_builtin_member_t members_env[] = {
    { "ArgCount", 'M', "int ArgCount()" },
    { "ArgAt",    'M', "string ArgAt(int index)" },
    { "ExeDir",   'M', "string ExeDir()" },
};

static const zan_builtin_member_t members_nativemem[] = {
    { "Alloc",     'M', "nint Alloc(long size)" },
    { "Free",      'M', "void Free(nint ptr)" },
    { "Copy",      'M', "void Copy(nint dst, nint src, long size)" },
    { "Fill",      'M', "void Fill(nint ptr, int value, long size)" },
    { "Compare",   'M', "int Compare(nint a, nint b, long size)" },
    { "GetString", 'M', "string GetString(nint ptr)" },
    { "PutString", 'M', "void PutString(nint ptr, string text)" },
};

static const zan_builtin_member_t members_task[] = {
    { "Spawn", 'M', "void Spawn(Action work)" },
};

#define BT(name, pub, disp, stat, arr) \
    { name, pub, disp, stat, arr, (int)(sizeof(arr) / sizeof((arr)[0])) }

static const zan_builtin_type_t builtin_types[] = {
    BT("string", "string", "string", 0, members_string),
    BT("List", "List", "List<T>", 0, members_list),
    BT("Dict", "Dictionary", "Dictionary<K,V>", 0, members_dict),
    BT("StringBuilder", "StringBuilder", "StringBuilder", 0, members_sb),
    BT("Console", "Console", "Console", 1, members_console),
    BT("Math", "Math", "Math", 1, members_math),
    BT("Convert", "Convert", "Convert", 1, members_convert),
    BT("String", "String", "String", 1, members_stringcls),
    BT("File", "File", "File", 1, members_file),
    BT("Directory", "Directory", "Directory", 1, members_dir),
    BT("Path", "Path", "Path", 1, members_path),
    BT("Environment", "Environment", "Environment", 1, members_env),
    BT("NativeMemory", "NativeMemory", "NativeMemory", 1, members_nativemem),
    BT("Task", "Task", "Task", 1, members_task),
};

const zan_builtin_type_t *zan_builtin_types(int *count) {
    if (count) *count = (int)(sizeof(builtin_types) / sizeof(builtin_types[0]));
    return builtin_types;
}

const zan_builtin_type_t *zan_builtin_find(const char *type) {
    if (!type) return NULL;
    for (size_t i = 0; i < sizeof(builtin_types) / sizeof(builtin_types[0]); i++) {
        if (strcmp(builtin_types[i].type, type) == 0) return &builtin_types[i];
    }
    return NULL;
}

/* Signatures start with the result type, so it is the text before the first
   space (a property's signature is "int Count", a method's "int IndexOf(...)").
   The names are static, so a small table of the results actually used keeps
   this allocation-free. */
const char *zan_builtin_member_result(const char *type, const char *name,
                                      int name_len) {
    static const char *results[] = {
        "string", "int", "long", "double", "bool", "void", "nint",
        "List<string>", "List<K>", "List<V>", "ConsoleColor",
    };
    const zan_builtin_type_t *bt = zan_builtin_find(type);
    if (!bt || !name || name_len <= 0) return NULL;
    for (int i = 0; i < bt->member_count; i++) {
        const char *m = bt->members[i].name;
        if ((int)strlen(m) != name_len || memcmp(m, name, (size_t)name_len) != 0)
            continue;
        const char *sig = bt->members[i].sig;
        const char *sp = strchr(sig, ' ');
        if (!sp) return NULL;
        size_t len = (size_t)(sp - sig);
        for (size_t r = 0; r < sizeof(results) / sizeof(results[0]); r++) {
            if (strlen(results[r]) == len && memcmp(results[r], sig, len) == 0)
                return results[r];
        }
        return NULL;
    }
    return NULL;
}

char zan_builtin_member_kind(const char *type, const char *name, int name_len) {
    const zan_builtin_type_t *bt = zan_builtin_find(type);
    if (!bt || !name || name_len <= 0) return 'M';
    for (int i = 0; i < bt->member_count; i++) {
        const char *m = bt->members[i].name;
        if ((int)strlen(m) == name_len && memcmp(m, name, (size_t)name_len) == 0)
            return bt->members[i].kind;
    }
    return 'M';
}

int zan_builtin_has_member(const char *type, const char *name, int name_len) {
    const zan_builtin_type_t *bt = zan_builtin_find(type);
    if (!bt) return 1;
    if (!name || name_len <= 0) return 1;
    for (int i = 0; i < bt->member_count; i++) {
        const char *m = bt->members[i].name;
        if ((int)strlen(m) == name_len && memcmp(m, name, (size_t)name_len) == 0)
            return 1;
    }
    return 0;
}
