using System;
using System.Threading.Tasks;

static class Program
{
    static async Task Work()
    {
        Console.WriteLine("prefix");
        await Task.Delay(20);
        Console.WriteLine("suffix");
    }

    static void Main()
    {
        Task task = Work();
        Console.WriteLine("returned");
        task.Wait();
    }
}
