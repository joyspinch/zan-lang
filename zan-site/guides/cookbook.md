# 代码示例库（Cookbook）

> 每个示例都是**经过 `zanc` 真实编译验证**的最小可运行程序（源文件见仓库
> `_scratch/cookbook/`，verify 脚本 `_scratch/verify_cookbook.py`）。
> 直接抄走改改就能用；凡示例中出现的符号/字段均以 stdlib 源码为准。

## 基础与语言

### Hello World 与入口

```zan
// cookbook: Hello World 与入口
using System;
class Cookbook {
    static void Main() {
        Console.WriteLine("Hello, Zan!");
    }
}
```

### 命令行参数与退出码

```zan
// cookbook: 命令行参数与退出码
using System;
class Cookbook {
    static int Main() {
        int n = Environment.ArgCount();
        if (n > 0) { Console.WriteLine("arg0=" + Environment.ArgAt(0)); }
        return 0;
    }
}
```

### 变量、常量与可空类型

```zan
// cookbook: 变量、常量与可空类型
using System;
class Cookbook {
    static void Main() {
        var n = 42;                    // int
        long big = 42L;
        double d = 3.14;
        float f = 3.14f;
        bool ok = true;
        char c = 'Z';
        string s = "text";
        const int MAX = 100;
        int? maybe = null;
        if (maybe.HasValue) { Console.WriteLine(maybe.Value); }
        Console.WriteLine(n + big + (int)d + (ok ? 1 : 0) + c + s + MAX);
    }
}
```

### 控制流 if/for/foreach/while/do/switch

```zan
// cookbook: 控制流 if/for/foreach/while/do/switch
using System;
class Cookbook {
    static void Main() {
        for (int i = 0; i < 3; i++) { Console.WriteLine(i); }
        var list = new List<int> { 1, 2, 3 };
        foreach (var v in list) { Console.WriteLine(v); }
        int x = 2;
        if (x > 1) { Console.WriteLine("big"); } else { Console.WriteLine("small"); }
        while (x > 0) { x--; }
        do { x++; } while (x < 1);
        switch (x) { case 0: Console.WriteLine("zero"); break; default: break; }
    }
}
```

### switch 表达式与 is 模式匹配

```zan
// cookbook: switch 表达式与 is 模式匹配
using System;
class Box { public string Tag; }
class Cookbook {
    static void Main() {
        object o = new Box { Tag = "box" };
        string desc = o switch {
            Box b when b.Tag == "box" => "a box",
            Box b => b.Tag,
            null => "none",
            _ => "other"
        };
        if (o is Box b2) { Console.WriteLine(b2.Tag); }
        if (o is not null) { Console.WriteLine(desc); }
    }
}
```

### 异常处理

```zan
// cookbook: 异常处理
using System;
class Cookbook {
    static void Main() {
        try {
            throw new ArgumentException("bad");
        } catch (ArgumentException e) {
            Console.WriteLine(e.Message);
        } catch (Exception e) {
            Console.WriteLine("other: " + e.Message);
        } finally {
            Console.WriteLine("done");
        }
    }
}
```

### 字符串操作与插值格式

```zan
// cookbook: 字符串操作与插值格式
using System;
class Cookbook {
    static void Main() {
        string s = "  hello, zan  ";
        Console.WriteLine(s.Trim());
        Console.WriteLine(s.Trim().ToUpper());
        Console.WriteLine(s.Trim().Replace(",", ";"));
        Console.WriteLine(s.Trim().Substring(0, 5));
        Console.WriteLine(s.Contains("zan"));
        Console.WriteLine(s.IndexOf("z"));
        List<string> parts = s.Trim().Split(",");
        Console.WriteLine(parts.Count);
        Console.WriteLine(string.Format("x={0} y={1}", 1, 2));
        Console.WriteLine(String.Join("-", parts));
        Console.WriteLine($"value={42:D4} hex={255:X2}");
    }
}
```

### List/Dictionary/HashSet/Stack/Queue

```zan
// cookbook: List/Dictionary/HashSet/Stack/Queue
using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
class Cookbook {
    static void Main() {
        var list = new List<int> { 3, 1, 2 };
        list.Add(4); list.Insert(0, 0);
        Console.WriteLine(list.Count + " " + list[1]);
        list.RemoveAt(list.Count - 1);
        List<int> sorted = list.Where(x => x > 0).OrderBy(x => x).ToList();
        Console.WriteLine(sorted.Count);

        var dict = new Dict<string, int>();
        dict["a"] = 1; dict.Add("b", 2);
        int v; if (dict.TryGetValue("a", out v)) { Console.WriteLine(v); }
        Console.WriteLine(dict.ContainsKey("b") + " " + dict.Count);
        foreach (var k in dict.Keys) { Console.WriteLine(k + "=" + dict[k]); }

        // 注意：stdlib 的 HashSet<T> 目前没有公开构造（0.2.3），
        // 去重/成员查询请用 Dict<T, bool> 或 List 自行维护。

        var st = new Stack<int>(); st.Push(1); st.Push(2); Console.WriteLine(st.Pop());
        var q = new Queue<int>(); q.Enqueue(1); q.Enqueue(2); Console.WriteLine(q.Dequeue());
    }
}
```

### JSON 解析/字段读取/构建

```zan
// cookbook: JSON 解析/字段读取/构建
using System;
using System.Json;
class Cookbook {
    static void Main() {
        string raw = "{\"name\":\"zan\",\"n\":42,\"ok\":true,\"items\":[1,2]}";
        JsonValue v = JsonValue.Parse(raw);
        Console.WriteLine(v.Get("name")?.AsString());
        Console.WriteLine(v.Get("n")?.AsInt());
        Console.WriteLine(v.Get("ok")?.AsBool());
        Console.WriteLine(v.Get("items")?.At(1)?.AsInt());
        v.Set("extra", JsonValue.NewStr("x"));
        Console.WriteLine(v.ToJson());

        JsonValue obj = JsonValue.NewObject();
        obj.Set("a", JsonValue.NewNum(1));
        obj.Set("b", JsonValue.NewBool(true));
        Console.WriteLine("built: " + obj.ToJson());
    }
}
```

### 文件读写（文本/行/二进制/追加/复制删除）

```zan
// cookbook: 文件读写（文本/行/二进制/追加/复制删除）
using System;
using System.IO;
class Cookbook {
    static void Main() {
        string path = "_scratch/cookbook/demo.txt";
        File.WriteAllText(path, "line1\nline2\n");
        File.AppendAllText(path, "line3\n");
        string all = File.ReadAllText(path);
        Console.WriteLine(all.Trim());
        List<string> lines = File.ReadAllLines(path);
        Console.WriteLine("lines=" + lines.Count);
        File.WriteAllLines(path + ".2", lines);
        if (File.Exists(path)) {
            File.Delete(path + ".bak");
            File.Copy(path, path + ".bak");
        }
        byte[] data = File.ReadAllBytes(path + ".2");
        File.WriteAllBytes(path + ".bin", data);
        Console.WriteLine(File.GetSize(path) + " bytes");
        File.Delete(path + ".2"); File.Delete(path + ".bak"); File.Delete(path + ".bin"); File.Delete(path);
        Console.WriteLine("done");
    }
}
```

### 路径组合与目录操作

```zan
// cookbook: 路径组合与目录操作
using System;
using System.IO;
class Cookbook {
    static void Main() {
        string p = Path.Combine(Path.Combine("a", "b"), "c.txt");
        Console.WriteLine(Path.GetFileName(p));
        Console.WriteLine(Path.GetExtension(p));
        Console.WriteLine(Path.GetFileNameWithoutExtension(p));
        Console.WriteLine(Path.GetDirectoryName(p));
        Console.WriteLine(Directory.Exists("_scratch"));
        Directory.CreateDirectoryRecursive("_scratch/cookbook/tmp");
        Console.WriteLine(Directory.GetFiles("_scratch").Count);
        Directory.DeleteRecursive("_scratch/cookbook/tmp");
    }
}
```

### LINQ 链式查询

```zan
// cookbook: LINQ 链式查询
using System;
using System.Linq;
class Cookbook {
    static void Main() {
        var xs = new List<int> { 1, 2, 3, 4, 5 };
        // 注意：var + 泛型方法 + lambda 实参会被错误推断为 string（0.2.3 编译器缺陷），
        // 一律显式标注结果类型（或写显式泛型实参 f<int>(...)）。
        List<int> evens = xs.Where(x => x % 2 == 0).ToList();
        List<int> squares = xs.Select(x => x * x).ToList();
        List<int> sorted = xs.OrderByDescending(x => x).ToList();
        Console.WriteLine(evens.Count + " " + squares[0] + " " + sorted[0]);
        Console.WriteLine(xs.Any(x => x > 4) + " " + xs.All(x => x > 0));
        Console.WriteLine(xs.Sum() + " " + xs.Min() + " " + xs.Max());
        Console.WriteLine(xs.First() + " " + xs.Last());
        Console.WriteLine(xs.Take(2).Count() + " " + xs.Skip(2).Count());
        int acc = Enumerable.Aggregate<int, int>(xs, 0, (int sum, int x) => sum + x);
        Console.WriteLine(acc);
    }
}
```

## 网络与数据

### 数学/随机/日期/时间跨度/Guid

```zan
// cookbook: 数学/随机/日期/时间跨度/Guid
using System;
class Cookbook {
    static void Main() {
        Console.WriteLine(Math.Sqrt(16) + " " + Math.Pow(2, 10) + " " + Math.Max(1, 2));
        Random r = new Random();
        Console.WriteLine(r.Next());
        string s = DateTime.Now().ToString();
        Console.WriteLine(s.Length > 10);
        TimeSpan since = TimeSpan.FromMinutes(90);
        Console.WriteLine(since.Minutes + " " + (int)since.TotalSeconds);
        Console.WriteLine(Guid.New().Length);
    }
}
```

### async/await、Task 并发与协程 API

```zan
// cookbook: async/await、Task 并发与协程 API
using System;
using System.Threading;
class Cookbook {
    static async int Main() {
        Console.WriteLine(await DelayAnd(2));
        var t1 = Task.Run(() => Work(1));
        var t2 = Task.Run(() => Work(2));
        List<int> all = await Task.WhenAll(t1, t2);
        Console.WriteLine("sum=" + (all[0] + all[1]));
        long h = Task.Spawn(() => Work(3));
        while (!Task.IsDone(h)) { await Task.Delay(5); }
        return 0;
    }
    static async int DelayAnd(int v) { await Task.Delay(10); return v * 10; }
    static int Work(int v) { Thread.Sleep(20); return v; }
}
```

### Thread 与 Channel（协程通信）

```zan
// cookbook: Thread 与 Channel（协程通信）
using System;
using System.Threading;
class Cookbook {
    static void Main() {
        Thread.Start(() => { Console.WriteLine("worker " + Thread.CurrentId()); });
        Thread.Sleep(50);
        Channel ch = new Channel(4);
        Thread.Start(() => {
            ch.Send("task1");
            ch.Send("task2");
            ch.Close();
        });
        while (!ch.IsClosed) {
            Console.WriteLine("got " + ch.Receive());
        }
    }
}
```

### 哈希/Base64/Hex/AES（System.Security.Cryptography，纯 Zan 实现）

```zan
// cookbook: 哈希/Base64/Hex/AES（System.Security.Cryptography，纯 Zan 实现）
using System;
using System.Security.Cryptography;
class Cookbook {
    static void Main() {
        // Sha256.Hash(msg, len) -> byte[]；Hex/Base64 的 Encode 都接收 (buf, len)
        byte[] h = Sha256.Hash("hello", 5);
        Console.WriteLine(Hex.Encode(h, h.Length));
        Console.WriteLine(Base64.Encode(h, h.Length).Length);
        List<int> outLen = new List<int>();
        byte[] dec0 = Base64.Decode("aGVsbG8=", outLen);
        Console.WriteLine(dec0?.Length);
        // 注意：Aes.EncryptCbc 的 key/iv/data 参数是字符串（UTF-8），
        // 长度参数是字节数，输出长度经 out List<int> 回传。
        List<int> aesLen = new List<int>();
        List<int> decLen = new List<int>();
        byte[] aes = Aes.EncryptCbc("0123456789abcdef", 16, "0123456789abcdef", "hello!", 6, aesLen);
        Console.WriteLine(aes?.Length);
        byte[] dec = Aes.DecryptCbc("0123456789abcdef", 16, "0123456789abcdef", aes, aes.Length, decLen);
        Console.WriteLine(dec?.Length);
    }
}
```

### HTTP 客户端（静态/实例、GET/POST）

```zan
// cookbook: HTTP 客户端（静态/实例、GET/POST）
using System;
using System.Net;
using System.Net.Http.Client;
using System.Json;
class Cookbook {
    static async int Main() {
        string body = await HttpClient.GetAsync("example.com", 80, "/");
        Console.WriteLine(body.Length);
        HttpClient c = new HttpClient("example.com", 80);
        string post = await c.PostAsync("/", "a=1");
        Console.WriteLine(post.Length);
        JsonValue v = JsonValue.Parse("{\"ok\":true}");
        Console.WriteLine(v.Get("ok")?.AsBool());
        return 0;
    }
}
```

### HTTP 服务端（HttpServer + OnRequest）

```zan
// cookbook: HTTP 服务端（HttpServer + OnRequest）
using System;
using System.Net;
using System.Net.Http;
class Cookbook {
    static async int Main() {
        HttpServer s = new HttpServer("127.0.0.1", 8899);
        s.OnRequest(Handle);
        await s.Start();
        Console.WriteLine("serving 8899");
        return 0;
    }
    static async HttpResponse Handle(HttpRequest req) {
        if (req.path == "/hello") { return HttpResponse.Text("hi"); }
        return HttpResponse.Json("{\"ok\":true}");
    }
}
```

### server-mvc（System.Web 属性路由级 WebApp）

```zan
// cookbook: server-mvc（System.Web 属性路由级 WebApp）
using System;
using System.Web;
class Program {
    static async int Main() {
        WebApp app = new WebApp("127.0.0.1", 8900);
        app.Get("/", (HttpContext ctx) => HttpResponse.Text("home"));
        app.Get("/api/x", (HttpContext ctx) => HttpResponse.Json("{\"ok\":true}"));
        await app.Start();
        return 0;
    }
}
```

### SQLite（连接/建表/参数化查询）

```zan
// cookbook: SQLite（连接/建表/参数化查询）
using System;
using System.Data;
using System.Data.Sqlite;
class Cookbook {
    static void Main() {
        string db = "_scratch/cookbook/demo.db";
        SqliteConnection conn = SqliteConnection.Open(db);
        conn.Execute("create table if not exists t(id int, name text)");
        conn.Execute("insert into t values(?, ?)", new DbParams().Add("1").Add("zan"));
        DbResult r = conn.Query("select id, name from t where id = ?", new DbParams().Add("1"));
        Console.WriteLine(r.RowCount + " " + r.GetString(0, 1));
        conn.Execute("drop table t");
        conn.Close();
        System.IO.File.Delete(db);
    }
}
```

### ORM Model（Define/建表/插入/查询/更新/删除）

```zan
// cookbook: ORM Model（Define/建表/插入/查询/更新/删除）
using System;
using System.Data;
using System.Data.Orm;
using System.Data.Sqlite;
class Cookbook {
    static void Main() {
        string db = "_scratch/cookbook/orm.db";
        SqliteConnection conn = SqliteConnection.Open(db);
        Model user = Model.Define("users")
            .Column("id", "INTEGER", true)
            .Column("name", "TEXT")
            .Column("age", "INTEGER");
        user.CreateTable(conn);
        ModelRow row = new ModelRow().Set("name", "Alice").Set("age", "30");
        user.Insert(conn, row);
        List<ModelRow> rows = user.Select(conn).Where("age > 18").Execute();
        Console.WriteLine(rows.Count + " " + rows[0].Get("name"));
        user.Update(conn).Set("age", "31").Where("name = 'Alice'").Execute();
        user.Delete(conn).Where("name = 'Alice'").Execute();
        user.DropTable(conn);
        conn.Close();
        System.IO.File.Delete(db);
    }
}
```

### Redis 键值读写（异步）

```zan
// cookbook: Redis 键值读写（异步）
using System;
using System.Data.Redis;
class Cookbook {
    static async int Main() {
        RedisClient r = await RedisClient.ConnectAsync("127.0.0.1", 6379, 2000);
        await r.SetAsync("cb:k", "v1");
        string v = await r.GetAsync("cb:k");
        Console.WriteLine(v);
        await r.DelAsync("cb:k");
        r.Close();
        return 0;
    }
}
```

### 正则匹配与 URL/Base64 编码

```zan
// cookbook: 正则匹配与 URL/Base64 编码
using System;
using System.Text;
using System.Text.RegularExpressions;
class Cookbook {
    static void Main() {
        Regex re = Regex.Compile("(\\d+)-(\\d+)");
        if (re.IsMatch("12-34")) {
            Match m = re.Find("12-34");
            Console.WriteLine(m.Group(1) + " " + m.Group(2));
        }
        Console.WriteLine(Encoding.UrlEncode("中文 a=b"));
        Console.WriteLine(Encoding.Base64Encode("zan"));
        Console.WriteLine(Encoding.Base64Decode("emFu"));
    }
}
```

### FFI / DllImport（C 运行时与平台库）

```zan
// cookbook: FFI / DllImport（C 运行时与平台库）
using System;
class Cookbook {
    [DllImport("crt", EntryPoint = "getenv")]
    static extern string getenv(string name);
    static void Main() {
        string p = getenv("PATH");
        Console.WriteLine(p != null && p.Length > 0);
    }
}
```

### 条件编译（#if WINDOWS 等预定义符号）

```zan
// cookbook: 条件编译（#if WINDOWS 等预定义符号）
using System;
class Cookbook {
    static void Main() {
#if WINDOWS
        Console.WriteLine("os=windows");
#elif LINUX
        Console.WriteLine("os=linux");
#elif MACOS
        Console.WriteLine("os=macos");
#else
        Console.WriteLine("os=other");
#endif
    }
}
```

### 声明全形态（interface/struct/enum/delegate/record/tuple/partial）

```zan
// cookbook: 声明全形态（interface/struct/enum/delegate/record/tuple/partial）
using System;
interface IShape { float Area(); }
struct Vec2 { public float X; public float Y; public float Length() { return (float)Math.Sqrt(X * X + Y * Y); } }
enum Color { Red, Green = 5, Blue }
delegate int BinOp(int a, int b);
record Point(int x, int y);
class Shape : IShape { public float Area() { return 1; } }
partial class Cookbook {
    static void Main() {
        Point p = new Point(1, 2);
        Console.WriteLine(p.x + " " + p.ToString());
        var t = (1, "hello");
        var (a, b) = t;
        Console.WriteLine(a + " " + b + " " + t.Item2);
        Vec2 v = new Vec2 { X = 3, Y = 4 };
        Console.WriteLine(v.Length());
        Console.WriteLine(Color.Green + " " + (int)Color.Blue);
        BinOp add = (int x, int y) => x + y;
        Console.WriteLine(add(2, 3));
    }
}
```

### 泛型类/方法/委托 与 where 约束

```zan
// cookbook: 泛型类/方法/委托 与 where 约束
using System;
interface IHasName { string Name(); }
class Box<T> { public T Value; public Box(T v) { Value = v; } }
class Named : IHasName { public string Name() { return "x"; } }
class Cookbook {
    static T First<T>(List<T> xs) { return xs[0]; }
    static R Map<T, R>(T v, Func<T, R> f) { return f(v); }
    static void Main() {
        Box<int> box = new Box<int>(42);
        Console.WriteLine(box.Value);
        List<string> xs = new List<string> { "a", "b" };
        Console.WriteLine(First(xs));
        Console.WriteLine(Map(3, (int n) => n * 2));
    }
}
```

### 公开 API 惯例（property/事件/运算符/索引器/扩展方法/静态类）

```zan
// cookbook: 公开 API 惯例（property/事件/运算符/索引器/扩展方法/静态类）
using System;
class Temp {
    int _c;
    public int C { get { return _c; } set { _c = value; } }
    public int this[int i] {
        get { return i * 10; }
    }
    public static Temp operator +(Temp a, Temp b) {
        return new Temp { C = a.C + b.C };
    }
}
static class Ext {
    public static bool IsEven(this int n) { return n % 2 == 0; }
}
class Cookbook {
    static void Main() {
        Temp t = new Temp(); t.C = 5;
        Temp s = t + new Temp { C = 3 };
        Console.WriteLine(s.C + " " + t[2]);
        int n = 4;
        Console.WriteLine(n.IsEven());  // 扩展方法：this int n
    }
}
```

## GUI 开发

### 最小窗口（Form + 根容器）

```zan
// cookbook: 最小窗口（Form + 根容器）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Hello Zan", 420, 300);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        form.Add(root);
        form.Run();
    }
}
```

### 按钮点击事件（OnClick 链式方法 / Click += 两种写法）

```zan
// cookbook: 按钮点击事件（OnClick 链式方法 / Click += 两种写法）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Events", 420, 300);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        Label label = new Label("clicked 0 times");
        int n = 0;
        Button ok = new Button("OK");
        ok.OnClick(() => { n = n + 1; label.Text = "clicked " + n + " times"; });
        Button reset = new Button("Reset");
        reset.Click += () => { n = 0; label.Text = "clicked 0 times"; };
        Panel col = Panel.Column().Gap(8);
        col.With(label).With(ok).With(reset);
        root.Add(col);
        form.Add(root);
        form.Run();
    }
}
```

### 输入控件（Input/TextArea/Checkbox/Switch/Slider + 读值）

```zan
// cookbook: 输入控件（Input/TextArea/Checkbox/Switch/Slider + 读值）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Form", 480, 400);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        Input name = new Input("姓名");
        name.data = new string("");
        TextArea note = new TextArea("备注");
        Checkbox ck = new Checkbox("记住我");
        Switch sw = new Switch();
        Slider slider = new Slider(0, 100);
        Label out1 = new Label("");
        Button readBtn = new Button("读取");
        readBtn.OnClick(() => {
            string v = name.data;
            out1.Text = v + " | " + note.data + " | " + slider.Value();
        });
        Panel col = Panel.Column().Gap(6);
        col.With(name).With(note).With(ck).With(sw).With(slider).With(readBtn).With(out1);
        root.Add(col);
        form.Add(root);
        form.Run();
    }
}
```

### 布局（Dock / Row / Column / Gap / 自由布局）

```zan
// cookbook: 布局（Dock / Row / Column / Gap / 自由布局）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Layout", 600, 400);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        // 顶部横条（dock=1 顶）
        Panel bar = Panel.Row().Gap(8);
        bar.Dock(Dock.Top()).Prefer(0, 40);
        bar.With(new Button("New")).With(new Button("Open"));
        // 左侧栏（dock=3 左）
        Panel side = Panel.Column().Gap(6);
        side.Dock(Dock.Left()).Prefer(160, 0);
        side.With(new Button("A")).With(new Button("B")).With(new Button("C"));
        // 填充区（dock=5）
        Panel main = Panel.Column().Gap(8);
        main.Dock(Dock.Fill());
        main.With(new Label("content")).With(new Button("Add"));
        root.Add(bar); root.Add(side); root.Add(main);
        form.Add(root);
        form.Run();
    }
}
```

### 样式（内联链式 / CSS 类 / UseAppCss / 主题切换）

```zan
// cookbook: 样式（内联链式 / CSS 类 / UseAppCss / 主题切换）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Style", 480, 360);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        // 内联链式样式（返回 this）
        Button a = new Button("Inline");
        a.Bg(0x223355).Radius(10).Border(0x5b6cff, 2).TextColor(0xffffff).Gradient(0x5b6cff, 0x9b5cff);
        // CSS 类 + UseAppCss（应用级样式，不随皮肤失效）
        App app = form.GetApp();
        app.UseAppCss("button.pink { background:#e91e63; color:#fff; border-color:#e91e63 }");
        Button b = new Button("Pink"); b.Class = "pink";
        Button c = new Button("Light skin"); c.Class = "primary";
        c.OnClick(() => { app.UseSkin("light"); app.ReloadSkin(); });
        Panel col = Panel.Column().Gap(10);
        col.With(a).With(b).With(c);
        root.Add(col);
        form.Add(root);
        form.Run();
    }
}
```

### 标签页（Tabs + SlotHost 页面槽）

```zan
// cookbook: 标签页（Tabs + SlotHost 页面槽）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Tabs", 520, 380);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        Tabs tabs = new Tabs(Tabs.Card(), Tabs.Horizontal());
        tabs.Add("概览", false);
        tabs.Add("详情", true);
        tabs.Dock(Dock.Fill());
        Label l1 = new Label("page 1");
        Label l2 = new Label("page 2");
        Panel p0 = tabs.SlotHost(0); p0.Add(l1);
        Panel p1 = tabs.SlotHost(1); p1.Add(l2);
        root.Add(tabs);
        form.Add(root);
        form.Run();
    }
}
```

### 数据网格（DataGrid<T> 实体绑定 + 类型化列）

```zan
// cookbook: 数据网格（DataGrid<T> 实体绑定 + 类型化列）
using System;
using Gui;
using Gui.Widget;
using Gui.Component.DataTable;
class User {
    public string Name;
    public int Age;
}
class Program {
    static void Main() {
        Form form = Form.Create("Grid", 560, 380);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        List<User> users = new List<User>();
        users.Add(new User { Name = "Alice", Age = 30 });
        users.Add(new User { Name = "Bob", Age = 24 });
        DataGrid<User> grid = new DataGrid<User>();
        grid.Col("Name", 180, (User u) => u.Name);
        grid.NumCol("Age", 90, (User u) => u.Age);
        grid.Bind(users);
        grid.Dock(Dock.Fill());
        root.Add(grid);
        form.Add(root);
        form.Run();
    }
}
```

### 自定义绘制控件（继承 Control + OnPaint/OnMeasure + Canvas）

```zan
// cookbook: 自定义绘制控件（继承 Control + OnPaint/OnMeasure + Canvas）
using System;
using Gui;
using Gui.Widget;
class Dial : Control {
    int value;
    public void InitDial(string n) { this.InitControl(n, 1); value = 42; }
    public override void OnPaint(App app) {
        Canvas c = app.canvas;
        c.FillRoundRect(this.bx, this.by, this.bw, this.bh, 12, 0x101828);
        c.FillCircle(bx + bw / 2, by + bh / 2, 36, 0x5b6cff);
        c.DrawText(bx + 10, by + 10, "value=" + value, 0xffffff, app.Scale(14));
    }
    public override void OnMeasure(App app) {
        prefW = app.Scale(140); prefH = app.Scale(140);
    }
    public override string Kind() { return "Dial"; }
}
class Program {
    static void Main() {
        Form form = Form.Create("Paint", 480, 360);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        Dial d = new Dial();
        d.InitDial("dial");
        Panel col = Panel.Column().Gap(8);
        col.With(d).With(new Label("custom control"));
        root.Add(col);
        form.Add(root);
        form.Run();
    }
}
```

### 即时模式渲染（Render + Ui.Clicked，免布局器）

```zan
// cookbook: 即时模式渲染（Render + Ui.Clicked，免布局器）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        App app = App.CreateDark("Immediate", 480, 320);
        Button add = new Button { Text = "+ Add", Class = "primary outline small" };
        int n = 0;
        app.Show();
        app.SetFrameBody(() => {
            WidgetId.ResetFrame();
            int id = add.Render(app, app.Scale(24), app.Scale(40), app.Scale(96));
            app.canvas.DrawText(app.Scale(24), app.Scale(80), "count " + n, 0xffffff, app.Scale(14));
            if (Ui.Clicked(app, id)) { n = n + 1; app.RequestRedraw(); }
            app.RenderChrome("Immediate");
        });
        WidgetId.Mark();
        while (app.isRunning) {
            app.ProcessEvent();
            app.BeginFrame();
            app.RequestRedraw();
            app.PresentFrame();
        }
    }
}
```

### 内嵌网页视图（WebViewBox，Windows=WebView2，macOS=WKWebView）

```zan
// cookbook: 内嵌网页视图（WebViewBox，Windows=WebView2，macOS=WKWebView）
using System;
using Gui;
using Gui.Widget;
using Gui.Component.WebView;
class Program {
    static void Main() {
        Form form = Form.Create("Browser", 640, 480);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        WebViewBox wv = new WebViewBox();
        wv.SetStartUrl("https://example.com");
        wv.Dock(Dock.Fill());
        root.Add(wv);
        form.Add(root);
        form.Run();
    }
}
```

### 对话框与提示（MessageBox / Layer 弹层）

```zan
// cookbook: 对话框与提示（MessageBox / Layer 弹层）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Dialog", 480, 320);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        Button mbox = new Button("MessageBox");
        mbox.OnClick(() => { System.MessageBox.Show("hello", "tip"); });
        Button layer = new Button("Layer Alert");
        layer.OnClick(() => { LayerState.Alert("标题", "内容", 0); });
        Panel col = Panel.Column().Gap(8);
        col.With(mbox).With(layer);
        root.Add(col);
        form.Add(root);
        form.Run();
    }
}
```

### 双向绑定（Binding.data 直接指向模型字段）

```zan
// cookbook: 双向绑定（Binding.data 直接指向模型字段）
using System;
using Gui;
using Gui.Widget;

class Model { public string inputText; }

class Program {
    static void Main() {
        Form form = Form.Create("Bind", 460, 300);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        Model model = new Model();
        model.inputText = "Zan";
        Input box = new Input("type here");
        box.data = model.inputText;       // 双向绑定：输入写回字段，字段变化每帧重读
        Label echo = new Label("");
        echo.Text = model.inputText;   // Binding<string> 字段：模型变化每帧重读
        Panel col = Panel.Column().Gap(8);
        col.With(box).With(echo);
        root.Add(col);
        form.Add(root);
        form.Run();
    }
}
```

### 杂项控件（Progress/Pagination/ToolStrip/StatusBar/Rate/Statistic）

```zan
// cookbook: 杂项控件（Progress/Pagination/ToolStrip/StatusBar/Rate/Statistic）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Widgets", 560, 420);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        ToolStrip bar = new ToolStrip();
        bar.Dock(Dock.Top()).Prefer(0, 34);
        bar.SetItemsText("New:plus|Open:folder|-|>Help:info");
        StatusBar status = new StatusBar();
        status.Dock(Dock.Bottom()).Prefer(0, 26);
        status.SetItemsText("Ready|>UTF-8");
        Progress p = new Progress(0, 1); p.SetPercent(60);
        Rate rate = new Rate();
        Pagination pg = new Pagination(); pg.SetTotal(5);
        Statistic stat = new Statistic(); stat.Value = "1,024";
        Panel col = Panel.Column().Gap(10);
        col.With(p).With(rate).With(pg).With(stat);
        root.Add(bar); root.Add(col); root.Add(status);
        form.Add(root);
        form.Run();
    }
}
```

### 侧边栏 + 页面切换（动态 Add/RemoveAll）

```zan
// cookbook: 侧边栏 + 页面切换（动态 Add/RemoveAll）
using System;
using Gui;
using Gui.Widget;
class Program {
    static void Main() {
        Form form = Form.Create("Shell", 720, 440);
        Panel root = Panel.Root("root");
        root.Dock(Dock.Fill());
        Panel nav = Panel.Column().Gap(6);
        nav.Dock(Dock.Left()).Prefer(160, 0);
        Panel body = Panel.Column();
        body.Dock(Dock.Fill());
        Button home = new Button("Home");
        Button about = new Button("About");
        home.OnClick(() => { body.RemoveAll(); body.Add(new Label("Home page")); });
        about.OnClick(() => { body.RemoveAll(); body.Add(new Label("About page")); });
        nav.With(home).With(about);
        body.Add(new Label("Home page"));
        root.Add(nav); root.Add(body);
        form.Add(root);
        form.Run();
    }
}
```

