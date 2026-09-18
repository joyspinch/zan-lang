using System;

static class Program
{
    static void Main()
    {
        int value = 1;
        Func<int> read = () => value;
        value = 42;
        Console.WriteLine(read());
    }
}
