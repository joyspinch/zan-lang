using System;
using System.Threading.Tasks;

static class Program
{
    static void Main()
    {
        Task<int> task = Task.Run(() => 21);
        Console.WriteLine(task.Result);
        Console.WriteLine(task.Result);
        task.Wait();
        task.Wait();
        Console.WriteLine("completed=" + (task.IsCompleted ? "yes" : "no"));
    }
}
