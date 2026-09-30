# System.IO.Watch

> 源码: `packages/Zan.IO/src/System/IO/Watch/DirectoryWatcher.zan`


## DirectoryWatcher (class)

监听目录并上报文件变化（新增/删除/修改）。
后台线程约每 250 ms 轮询一次目录列表，并与
上一份快照按名称 + 最后写入时间比较，因此
所有平台行为一致（无需 ReadDirectoryChangesW / inotify 相关代码），
代价是最多约 250 ms 的延迟。不递归子目录。

DirectoryWatcher.Watch("C:\\data", OnChange);
...
DirectoryWatcher.Stop();

回调收到条目名与一个 `FileChange` 类型，
且必须是非捕获（non-capturing）的。

- static List<FileSnapshot> lastSnapshot=new List<FileSnapshot>();
  - 上一份快照；轮询线程每轮与它合并比较后替换。

- static bool watching=false;
  - 监听运行标志；Stop 置 false，轮询线程据此退出。

- static bool Watch(string path, FileChangeFn callback)
  - 开始监听 `path`。以下情况返回 false：
    目录不存在或已有监听器在运行。

- static void Stop()
  - 停止监听。当前这轮轮询结束后不再
    投递任何事件。

- static void Poll()
  - 轮询线程主体：每 250 ms 拍一次快照并与上一份合并比较，
    直到 Stop 把 watching 置为 false。

- static List<FileSnapshot> Snapshot()
  - 构建按名称排序的名称 + 最后写入时间 + 大小快照。当
    文件的大小或（秒级精度）写入时间发生变化时即视为
    “已修改”；同一秒内大小不变的重写无法
    通过轮询检测到——这是文档所述的延迟/精度权衡。

- static void Compare(List<FileSnapshot> current)
  - 合并上一份与当前快照，对差异触发回调。

- static string dir="";
  - 被监听的目录。

- static FileChangeFn cb;
  - 变化回调（在轮询线程上执行）。


## FileChange (class)

`DirectoryWatcher` 上报的文件系统变化类型。

- static int Added()
  - 条目新增。

- static int Removed()
  - 条目删除。

- static int Modified()
  - 内容修改（写入时间或大小变化）。


## FileSnapshot (class)

快照里的一条目记录：名称 + 最后写入时间 + 大小。

- string name;

- long lastWriteTime;

- long size;

- FileSnapshot(string name, long lastWriteTime, long size)
  - 构造一条快照记录。


## void (delegate)

`DirectoryWatcher.Watch` 的变化回调：
条目名加一个 `FileChange` 类型。

`delegate void FileChangeFn(string name, int change);`
