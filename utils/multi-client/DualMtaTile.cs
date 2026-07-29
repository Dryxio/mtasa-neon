using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class Program
{
    private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    private static extern bool SetProcessDPIAware();

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr window);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr window, StringBuilder text, int count);

    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    private static extern bool SystemParametersInfo(uint action, uint parameter, ref Rect rect, uint flags);

    public static int Main()
    {
        SetProcessDPIAware();

        // MTA creates its GTA windows well after the loader processes appear. Waiting here
        // lets the macOS launcher remain simple and also covers slower debug/VM startups.
        IntPtr primary = IntPtr.Zero;
        IntPtr secondary = IntPtr.Zero;
        for (int attempt = 0; attempt < 240 && (primary == IntPtr.Zero || secondary == IntPtr.Zero); ++attempt)
        {
            foreach (Process process in Process.GetProcessesByName("gta_sa"))
            {
                process.Refresh();
                if (process.MainWindowTitle.Contains("[CL2]"))
                    secondary = process.MainWindowHandle;
                else if (process.MainWindowTitle == "MTA: San Andreas")
                    primary = process.MainWindowHandle;
            }

            if (primary == IntPtr.Zero || secondary == IntPtr.Zero)
                Thread.Sleep(500);
        }

        if (primary == IntPtr.Zero || secondary == IntPtr.Zero)
        {
            Console.Error.WriteLine("Could not find both MTA windows within 120 seconds.");
            return 2;
        }

        // The visible server console otherwise obscures the left client in the Parallels
        // desktop. It keeps running normally after its terminal window is minimized.
        IntPtr server = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr parameter)
        {
            if (!IsWindowVisible(window))
                return true;

            var title = new StringBuilder(256);
            GetWindowText(window, title, title.Capacity);
            if (title.ToString().IndexOf("MTA Server64.exe", StringComparison.OrdinalIgnoreCase) >= 0)
                server = window;
            return true;
        }, IntPtr.Zero);

        if (server != IntPtr.Zero)
            ShowWindow(server, 6); // SW_MINIMIZE

        var workArea = new Rect();
        if (!SystemParametersInfo(48, 0, ref workArea, 0)) // SPI_GETWORKAREA
            return 3;

        int workWidth = workArea.Right - workArea.Left;
        int workHeight = workArea.Bottom - workArea.Top;
        int leftWidth = workWidth / 2;
        int rightWidth = workWidth - leftWidth;
        // The development profiles use 1440x900 (16:10), which fills more of the
        // tall Parallels desktop while still leaving two non-overlapping columns.
        int height = Math.Min((int)Math.Round(leftWidth * 10.0 / 16.0), workHeight);
        int y = workArea.Top + (workHeight - height) / 2;

        ShowWindow(primary, 9);   // SW_RESTORE
        ShowWindow(secondary, 9); // SW_RESTORE

        const uint showWindow = 0x0040; // SWP_SHOWWINDOW
        if (!SetWindowPos(primary, IntPtr.Zero, workArea.Left, y, leftWidth, height, showWindow))
            return 4;
        if (!SetWindowPos(secondary, IntPtr.Zero, workArea.Left + leftWidth, y, rightWidth, height, showWindow))
            return 5;

        return 0;
    }
}
