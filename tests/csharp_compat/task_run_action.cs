using System;
using System.Threading;
using System.Threading.Tasks;

sealed class AtomicLong
{
    private long value;

    public AtomicLong(long initial) { value = initial; }
    public long Load() { return Interlocked.Read(ref value); }
    public void Store(long next) { Interlocked.Exchange(ref value, next); }
}

static class Program
{
    static void Main()
    {
        var gate = new AtomicLong(0);
        var bodyThread = new AtomicLong(0);
        long mainThread = Environment.CurrentManagedThreadId;

        Task task = Task.Run(() =>
        {
            bodyThread.Store(Environment.CurrentManagedThreadId);
            while (gate.Load() == 0) Thread.Sleep(1);
            Console.WriteLine("body");
        });

        Console.WriteLine("returned");
        while (bodyThread.Load() == 0) Thread.Sleep(1);
        Console.WriteLine("same-thread=" + (mainThread == bodyThread.Load() ? "yes" : "no"));
        Console.WriteLine("completed-before-release=" + (task.IsCompleted ? "yes" : "no"));
        gate.Store(1);
        task.Wait();
        Console.WriteLine("completed-after-wait=" + (task.IsCompleted ? "yes" : "no"));
    }
}
