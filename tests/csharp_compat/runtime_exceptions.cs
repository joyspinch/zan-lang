using System;

static class Program
{
    static void Main()
    {
        int zero = 0;
        try
        {
            Console.WriteLine(10 / zero);
        }
        catch (DivideByZeroException)
        {
            Console.WriteLine("divide=DivideByZeroException");
        }

        int[] values = new int[1];
        try
        {
            Console.WriteLine(values[2]);
        }
        catch (IndexOutOfRangeException)
        {
            Console.WriteLine("index=IndexOutOfRangeException");
        }

        object value = null;
        try
        {
            Console.WriteLine(value.ToString());
        }
        catch (NullReferenceException)
        {
            Console.WriteLine("null=NullReferenceException");
        }
    }
}
