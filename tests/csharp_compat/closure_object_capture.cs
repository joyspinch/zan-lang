using System;

class Program
{
    static void Main()
    {
        object value = "first";
        Func<string> read = () => Convert.ToString(value);
        value = "second";
        Console.WriteLine(read());
    }
}
