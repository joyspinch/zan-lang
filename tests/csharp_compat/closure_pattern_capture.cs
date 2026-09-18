using System;

class Box
{
    public int Value;
}

class Program
{
    static void Main()
    {
        object item = new Box { Value = 5 };
        switch (item)
        {
            case Box value:
                Func<int> read = () => value.Value;
                value = new Box { Value = 33 };
                Console.WriteLine(read());
                break;
        }

        object rejected = item;
        bool allowPattern = rejected == null;
        switch (rejected)
        {
            case Box skipped when allowPattern:
                Func<int> never = () => skipped.Value;
                Console.WriteLine(never());
                break;
            default:
                Console.WriteLine("guard-miss");
                break;
        }
    }
}
