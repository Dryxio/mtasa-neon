#pragma once
#include "CRocketSonyGamepad.h"
#include <windows.h>
#include <hidsdi.h>
#include <vector>

namespace RocketSoccer
{
    // Independent, read-only HID handle: do not change Sony report modes,
    // vibration, LEDs or MTA's DirectInput acquisition. No synchronous frame I/O.
    class SonyHidGamepad
    {
        HMODULE hid = LoadLibraryExW(L"hid.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        using GetCaps = NTSTATUS(__stdcall*)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);
        using SetBuffers = BOOLEAN(__stdcall*)(HANDLE, ULONG);
        GetCaps                       getCaps = hid ? reinterpret_cast<GetCaps>(GetProcAddress(hid, "HidP_GetCaps")) : nullptr;
        SetBuffers                    setBuffers = hid ? reinterpret_cast<SetBuffers>(GetProcAddress(hid, "HidD_SetNumInputBuffers")) : nullptr;
        HANDLE                        device = INVALID_HANDLE_VALUE;
        OVERLAPPED                    io{};
        std::array<std::uint8_t, 128> buffer{};
        DWORD                         reportSize = 0;
        unsigned                      product = 0;
        bool                          pending = false, valid = false;
        ULONGLONG                     nextScan = 0, lastReport = 0;
        GamepadSnapshot               last{};

        void Close()
        {
            if (device != INVALID_HANDLE_VALUE)
            {
                if (pending)
                {
                    CancelIoEx(device, &io);
                    DWORD ignored;
                    GetOverlappedResult(device, &io, &ignored, TRUE);
                }
                CloseHandle(device);
            }
            if (io.hEvent)
                CloseHandle(io.hEvent);
            device = INVALID_HANDLE_VALUE;
            io = {};
            pending = valid = false;
        }
        void Discover(ULONGLONG now)
        {
            nextScan = now + 2000;
            if (!getCaps)
                return;
            UINT count = 0;
            if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1) || count > 1024)
                return;
            std::vector<RAWINPUTDEVICELIST> devices(count);
            const UINT                      found = GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST));
            if (found == UINT(-1))
                return;
            for (UINT i = 0; i < found; ++i)
            {
                RID_DEVICE_INFO info{};
                info.cbSize = sizeof(info);
                UINT length = sizeof(info);
                if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICEINFO, &info, &length) == UINT(-1) || info.dwType != RIM_TYPEHID ||
                    info.hid.usUsagePage != 1 || (info.hid.usUsage != 4 && info.hid.usUsage != 5) || !IsSonyGamepad(info.hid.dwVendorId, info.hid.dwProductId))
                    continue;
                length = 0;
                if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_PREPARSEDDATA, nullptr, &length) == UINT(-1) || !length || length > 65536)
                    continue;
                std::vector<std::uint8_t> preparsed(length);
                if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_PREPARSEDDATA, preparsed.data(), &length) == UINT(-1))
                    continue;
                HIDP_CAPS caps{};
                if (getCaps(reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data()), &caps) < 0 || !caps.InputReportByteLength ||
                    caps.InputReportByteLength > buffer.size())
                    continue;
                length = 0;
                if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICENAME, nullptr, &length) == UINT(-1) || !length || length > 32768)
                    continue;
                std::vector<wchar_t> path(length + 1);
                if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICENAME, path.data(), &length) == UINT(-1))
                    continue;
                device = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                if (device == INVALID_HANDLE_VALUE)
                    continue;
                io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!io.hEvent)
                {
                    Close();
                    continue;
                }
                if (setBuffers)
                    setBuffers(device, 2);  // Avoid replaying a backlog after focus loss.
                reportSize = caps.InputReportByteLength;
                product = info.hid.dwProductId;
                lastReport = now;
                break;
            }
        }

    public:
        SonyHidGamepad() = default;
        SonyHidGamepad(const SonyHidGamepad&) = delete;
        SonyHidGamepad& operator=(const SonyHidGamepad&) = delete;
        ~SonyHidGamepad()
        {
            Close();
            if (hid)
                FreeLibrary(hid);
        }
        bool Poll(GamepadSnapshot& snapshot, ULONGLONG now)
        {
            if (device == INVALID_HANDLE_VALUE && now >= nextScan)
                Discover(now);
            if (device == INVALID_HANDLE_VALUE)
                return false;
            // Drain a bounded number of completed packets; never wait for input.
            for (unsigned attempt = 0; attempt < 8; ++attempt)
            {
                DWORD bytes = 0;
                if (pending)
                {
                    if (!GetOverlappedResult(device, &io, &bytes, FALSE))
                    {
                        if (GetLastError() == ERROR_IO_INCOMPLETE)
                            break;
                        Close();
                        return false;
                    }
                    pending = false;
                }
                else
                {
                    ResetEvent(io.hEvent);
                    if (!ReadFile(device, buffer.data(), reportSize, &bytes, &io))
                    {
                        if (GetLastError() == ERROR_IO_PENDING)
                        {
                            pending = true;
                            break;
                        }
                        Close();
                        return false;
                    }
                }
                if (DecodeSonyGamepad(product, buffer.data(), bytes, last))
                {
                    valid = true;
                    lastReport = now;
                }
            }
            // Never leave throttle held when a wireless pad disappears silently.
            if (now - lastReport > (valid ? 250 : 1000))
            {
                Close();
                return false;
            }
            if (!valid)
                return false;
            snapshot = last;
            return true;
        }
    };
}
