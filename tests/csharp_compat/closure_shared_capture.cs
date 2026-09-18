using System;

static class Program
{
    static void Main(string[] args)
    {
        int value = 1;
        Func<int> read = () => value;
        value = 42;
        Console.WriteLine(read());

        Func<int> readArgs = () => args.Length;
        args = new string[2];
        Console.WriteLine(readArgs());
    }
}
