// These tiny adapters are also compiled by the Win32 harness. They change only
// the range submitted to GTA, never the shared model's 2DFX definition.
#pragma once
#include <cstdint>

namespace DistantLightNativeTransitions
{
    static bool           enabled = false;
    static std::uintptr_t firstReturn = 0x6FCEAE;
    static std::uintptr_t normalReturn = 0x6FCFC9;
    static std::uintptr_t updateReturn = 0x6FD047;
    static std::uintptr_t trafficReturn = 0x49DCF8;

    // clang-format off
    static __declspec(naked) void First()
    {
#ifdef MTA_VERIFY_HOOK_LOCAL_SIZE
        MTA_VERIFY_HOOK_LOCAL_SIZE;
#endif
        __asm {
            mov eax, [esi + 14h]
            cmp byte ptr [enabled], 0
            je original
            mov eax, 43960000h
        original:
            xor ecx, ecx
            jmp dword ptr [firstReturn]
        }
    }
    static __declspec(naked) void Normal()
    {
#ifdef MTA_VERIFY_HOOK_LOCAL_SIZE
        MTA_VERIFY_HOOK_LOCAL_SIZE;
#endif
        __asm {
            pushfd
            mov ecx, [esi + 14h]
            cmp byte ptr [enabled], 0
            je original
            mov ecx, 43960000h
        original:
            popfd
            push edx
            push eax
            jmp dword ptr [normalReturn]
        }
    }
    static __declspec(naked) void Update()
    {
#ifdef MTA_VERIFY_HOOK_LOCAL_SIZE
        MTA_VERIFY_HOOK_LOCAL_SIZE;
#endif
        __asm {
            pushfd
            mov ecx, [esi + 14h]
            cmp byte ptr [enabled], 0
            je original
            mov ecx, 43960000h
        original:
            popfd
            mov eax, [esp + 1ch]
            jmp dword ptr [updateReturn]
        }
    }
    static __declspec(naked) void Traffic()
    {
#ifdef MTA_VERIFY_HOOK_LOCAL_SIZE
        MTA_VERIFY_HOOK_LOCAL_SIZE;
#endif
        __asm {
            pushfd
            cmp byte ptr [enabled], 0
            je original
            popfd
            push 44098000h
            jmp dword ptr [trafficReturn]
        original:
            popfd
            push 42480000h
            jmp dword ptr [trafficReturn]
        }
    }
    // clang-format on
}
