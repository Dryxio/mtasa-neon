using System;
using System.IO;
using System.Threading;

// This fixture exercises the same redirected ASCII line input used by MTA's
// Windows server without binding ports or modifying a developer installation.
public static class Program
{
    public static void Main()
    {
        while (true)
        {
            string line = Console.ReadLine();
            if (line == null)
            {
                // Real MTA keeps running when its redirected console reaches
                // EOF. The fixture mirrors that lifecycle edge explicitly.
                Thread.Sleep(50);
                continue;
            }
            File.AppendAllText("commands.log", line + "\n");
        }
    }
}
