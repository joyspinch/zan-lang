# System.IO.Watch

> 源码: `packages/Zan.IO/src/System/IO/Watch/DirectoryWatcher.zan`


## DirectoryWatcher (class)

- static List<FileSnapshot> lastSnapshot=new List<FileSnapshot>();

- static bool watching=false;

- static bool Watch(string path, FileChangeFn callback)

- static void Stop()

- static void Poll()

- static List<FileSnapshot> Snapshot()

- static void Compare(List<FileSnapshot> current)

- static string dir="";

- static FileChangeFn cb;


## FileChange (class)

- static int Added()

- static int Removed()

- static int Modified()


## FileSnapshot (class)

- string name;

- long lastWriteTime;

- long size;

- FileSnapshot(string name, long lastWriteTime, long size)


## void (delegate)

`delegate void FileChangeFn(string name, int change);`
