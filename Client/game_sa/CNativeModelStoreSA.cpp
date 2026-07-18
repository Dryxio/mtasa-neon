/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeModelStoreSA.cpp
 *  PURPOSE:     Opt-in native model-store foundation for extended worlds
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeModelStoreSA.h"
#include "SharedUtil.Hash.h"
#include "SharedUtil.Misc.h"
#include "gamesa_init.h"

#include <cstdarg>
#include <cstddef>

namespace
{
    constexpr const char* FEATURE_ENVIRONMENT = "MTA_NATIVE_BW_MODEL_STORES";
    constexpr DWORD       EXPECTED_IMAGE_BASE = 0x00400000;
    constexpr DWORD       OVERFLOW_EXIT_CODE = 0x4D54414E;  // "MTAN"

    struct SExecutableIdentity
    {
        const char* name;
        const char* sha256;
        DWORD       imageSize;
        DWORD       timestamp;
        DWORD       checksum;
    };

    // MTA's ProgramData runtime copy appends sections but preserves every
    // instruction audited by the patch manifest. Keep both files pinned to an
    // exact PE tuple and full-file digest; matching code sites alone is not an
    // authorization to patch an otherwise unknown executable.
    constexpr SExecutableIdentity EXECUTABLE_IDENTITIES[] = {
        {"hoodlum-raw", "72ae59e44c761389e354a50dc6215e964fe771121e2f4b1877273a493ceecc9b", 0x008B1000, 0x427101CA, 0x00DC5BEA},
        {"mta-programdata", "77485627b4ef17f92819318050d501e171c7ab84ceffe5091b973b9e29f9cc98", 0x01177000, 0x437101CA, 0x00DC29E6},
    };

    enum class EStoreKind : BYTE
    {
        Atomic,
        DamageAtomic,
        Time,
        Count,
    };

    enum class EPatchAction : BYTE
    {
        Patch,
        ValidateOnly,
    };

    struct SStoreDefinition
    {
        EStoreKind  kind;
        const char* name;
        DWORD       originalBase;
        DWORD       originalCapacity;
        DWORD       newCapacity;
        DWORD       stride;
        DWORD       constructorAddress;
        DWORD       vtable;
    };

    struct SConstructorSignature
    {
        EStoreKind kind;
        DWORD      address;
        BYTE       expected[16];
    };

    struct SCrtRoutineSignature
    {
        EStoreKind  kind;
        const char* role;
        DWORD       address;
        BYTE        expected[16];
    };

    struct SPointerSite
    {
        EStoreKind   kind;
        DWORD        instructionAddress;
        BYTE         instructionSize;
        BYTE         operandOffset;
        DWORD        storeDisplacement;
        EPatchAction action;
        BYTE         expected[10];
    };

    struct SGrowerSite
    {
        EStoreKind kind;
        DWORD      instructionAddress;
        BYTE       expected[5];
    };

    struct SCollisionBufferDefinition
    {
        DWORD originalCapacity;
        DWORD newCapacity;
    };

    struct SCollisionPointerSite
    {
        DWORD instructionAddress;
        BYTE  instructionSize;
        BYTE  operandOffset;
        DWORD bufferDisplacement;
        BYTE  expected[10];
    };

    struct SCollisionNopSite
    {
        DWORD instructionAddress;
        BYTE  expected[5];
    };

#define NATIVE_MODEL_STORE_DEFINITION(kind, originalBase, originalCapacity, newCapacity, stride, constructorAddress, vtable) \
    {EStoreKind::kind, #kind, originalBase, originalCapacity, newCapacity, stride, constructorAddress, vtable},
#define NATIVE_MODEL_STORE_CONSTRUCTOR(...)
#define NATIVE_MODEL_STORE_CRT_ROUTINE(...)
#define NATIVE_MODEL_STORE_POINTER(...)
#define NATIVE_MODEL_STORE_GROWER(...)
#define NATIVE_COLLISION_BUFFER_DEFINITION(...)
#define NATIVE_COLLISION_BUFFER_POINTER(...)
#define NATIVE_COLLISION_BUFFER_NOP(...)
    constexpr SStoreDefinition STORE_DEFINITIONS[] = {
#include "CNativeModelStoreSA.Manifest.inc"
    };
#undef NATIVE_MODEL_STORE_DEFINITION
#undef NATIVE_MODEL_STORE_CONSTRUCTOR
#undef NATIVE_MODEL_STORE_CRT_ROUTINE
#undef NATIVE_MODEL_STORE_POINTER
#undef NATIVE_MODEL_STORE_GROWER
#undef NATIVE_COLLISION_BUFFER_DEFINITION
#undef NATIVE_COLLISION_BUFFER_POINTER
#undef NATIVE_COLLISION_BUFFER_NOP

#define NATIVE_MODEL_STORE_DEFINITION(...)
#define NATIVE_MODEL_STORE_CONSTRUCTOR(kind, address, ...) {EStoreKind::kind, address, {__VA_ARGS__}},
#define NATIVE_MODEL_STORE_CRT_ROUTINE(...)
#define NATIVE_MODEL_STORE_POINTER(...)
#define NATIVE_MODEL_STORE_GROWER(...)
#define NATIVE_COLLISION_BUFFER_DEFINITION(...)
#define NATIVE_COLLISION_BUFFER_POINTER(...)
#define NATIVE_COLLISION_BUFFER_NOP(...)
    constexpr SConstructorSignature CONSTRUCTOR_SIGNATURES[] = {
#include "CNativeModelStoreSA.Manifest.inc"
    };
#undef NATIVE_MODEL_STORE_DEFINITION
#undef NATIVE_MODEL_STORE_CONSTRUCTOR
#undef NATIVE_MODEL_STORE_CRT_ROUTINE
#undef NATIVE_MODEL_STORE_POINTER
#undef NATIVE_MODEL_STORE_GROWER
#undef NATIVE_COLLISION_BUFFER_DEFINITION
#undef NATIVE_COLLISION_BUFFER_POINTER
#undef NATIVE_COLLISION_BUFFER_NOP

#define NATIVE_MODEL_STORE_DEFINITION(...)
#define NATIVE_MODEL_STORE_CONSTRUCTOR(...)
#define NATIVE_MODEL_STORE_CRT_ROUTINE(kind, role, address, ...) {EStoreKind::kind, #role, address, {__VA_ARGS__}},
#define NATIVE_MODEL_STORE_POINTER(...)
#define NATIVE_MODEL_STORE_GROWER(...)
#define NATIVE_COLLISION_BUFFER_DEFINITION(...)
#define NATIVE_COLLISION_BUFFER_POINTER(...)
#define NATIVE_COLLISION_BUFFER_NOP(...)
    constexpr SCrtRoutineSignature CRT_ROUTINE_SIGNATURES[] = {
#include "CNativeModelStoreSA.Manifest.inc"
    };
#undef NATIVE_MODEL_STORE_DEFINITION
#undef NATIVE_MODEL_STORE_CONSTRUCTOR
#undef NATIVE_MODEL_STORE_CRT_ROUTINE
#undef NATIVE_MODEL_STORE_POINTER
#undef NATIVE_MODEL_STORE_GROWER
#undef NATIVE_COLLISION_BUFFER_DEFINITION
#undef NATIVE_COLLISION_BUFFER_POINTER
#undef NATIVE_COLLISION_BUFFER_NOP

#define NATIVE_MODEL_STORE_DEFINITION(...)
#define NATIVE_MODEL_STORE_CONSTRUCTOR(...)
#define NATIVE_MODEL_STORE_CRT_ROUTINE(...)
#define NATIVE_MODEL_STORE_POINTER(kind, address, size, operandOffset, displacement, action, ...) \
    {EStoreKind::kind, address, size, operandOffset, displacement, EPatchAction::action, {__VA_ARGS__}},
#define NATIVE_MODEL_STORE_GROWER(...)
#define NATIVE_COLLISION_BUFFER_DEFINITION(...)
#define NATIVE_COLLISION_BUFFER_POINTER(...)
#define NATIVE_COLLISION_BUFFER_NOP(...)
    constexpr SPointerSite POINTER_SITES[] = {
#include "CNativeModelStoreSA.Manifest.inc"
    };
#undef NATIVE_MODEL_STORE_DEFINITION
#undef NATIVE_MODEL_STORE_CONSTRUCTOR
#undef NATIVE_MODEL_STORE_CRT_ROUTINE
#undef NATIVE_MODEL_STORE_POINTER
#undef NATIVE_MODEL_STORE_GROWER
#undef NATIVE_COLLISION_BUFFER_DEFINITION
#undef NATIVE_COLLISION_BUFFER_POINTER
#undef NATIVE_COLLISION_BUFFER_NOP

#define NATIVE_MODEL_STORE_DEFINITION(...)
#define NATIVE_MODEL_STORE_CONSTRUCTOR(...)
#define NATIVE_MODEL_STORE_CRT_ROUTINE(...)
#define NATIVE_MODEL_STORE_POINTER(...)
#define NATIVE_MODEL_STORE_GROWER(kind, address, ...) {EStoreKind::kind, address, {__VA_ARGS__}},
#define NATIVE_COLLISION_BUFFER_DEFINITION(...)
#define NATIVE_COLLISION_BUFFER_POINTER(...)
#define NATIVE_COLLISION_BUFFER_NOP(...)
    constexpr SGrowerSite GROWER_SITES[] = {
#include "CNativeModelStoreSA.Manifest.inc"
    };
#undef NATIVE_MODEL_STORE_DEFINITION
#undef NATIVE_MODEL_STORE_CONSTRUCTOR
#undef NATIVE_MODEL_STORE_CRT_ROUTINE
#undef NATIVE_MODEL_STORE_POINTER
#undef NATIVE_MODEL_STORE_GROWER
#undef NATIVE_COLLISION_BUFFER_DEFINITION
#undef NATIVE_COLLISION_BUFFER_POINTER
#undef NATIVE_COLLISION_BUFFER_NOP

#define NATIVE_MODEL_STORE_DEFINITION(...)
#define NATIVE_MODEL_STORE_CONSTRUCTOR(...)
#define NATIVE_MODEL_STORE_CRT_ROUTINE(...)
#define NATIVE_MODEL_STORE_POINTER(...)
#define NATIVE_MODEL_STORE_GROWER(...)
#define NATIVE_COLLISION_BUFFER_DEFINITION(originalCapacity, newCapacity) {originalCapacity, newCapacity},
#define NATIVE_COLLISION_BUFFER_POINTER(...)
#define NATIVE_COLLISION_BUFFER_NOP(...)
    constexpr SCollisionBufferDefinition COLLISION_BUFFER_DEFINITIONS[] = {
#include "CNativeModelStoreSA.Manifest.inc"
    };
#undef NATIVE_MODEL_STORE_DEFINITION
#undef NATIVE_MODEL_STORE_CONSTRUCTOR
#undef NATIVE_MODEL_STORE_CRT_ROUTINE
#undef NATIVE_MODEL_STORE_POINTER
#undef NATIVE_MODEL_STORE_GROWER
#undef NATIVE_COLLISION_BUFFER_DEFINITION
#undef NATIVE_COLLISION_BUFFER_POINTER
#undef NATIVE_COLLISION_BUFFER_NOP

#define NATIVE_MODEL_STORE_DEFINITION(...)
#define NATIVE_MODEL_STORE_CONSTRUCTOR(...)
#define NATIVE_MODEL_STORE_CRT_ROUTINE(...)
#define NATIVE_MODEL_STORE_POINTER(...)
#define NATIVE_MODEL_STORE_GROWER(...)
#define NATIVE_COLLISION_BUFFER_DEFINITION(...)
#define NATIVE_COLLISION_BUFFER_POINTER(address, size, operandOffset, displacement, ...) {address, size, operandOffset, displacement, {__VA_ARGS__}},
#define NATIVE_COLLISION_BUFFER_NOP(...)
    constexpr SCollisionPointerSite COLLISION_POINTER_SITES[] = {
#include "CNativeModelStoreSA.Manifest.inc"
    };
#undef NATIVE_MODEL_STORE_DEFINITION
#undef NATIVE_MODEL_STORE_CONSTRUCTOR
#undef NATIVE_MODEL_STORE_CRT_ROUTINE
#undef NATIVE_MODEL_STORE_POINTER
#undef NATIVE_MODEL_STORE_GROWER
#undef NATIVE_COLLISION_BUFFER_DEFINITION
#undef NATIVE_COLLISION_BUFFER_POINTER
#undef NATIVE_COLLISION_BUFFER_NOP

#define NATIVE_MODEL_STORE_DEFINITION(...)
#define NATIVE_MODEL_STORE_CONSTRUCTOR(...)
#define NATIVE_MODEL_STORE_CRT_ROUTINE(...)
#define NATIVE_MODEL_STORE_POINTER(...)
#define NATIVE_MODEL_STORE_GROWER(...)
#define NATIVE_COLLISION_BUFFER_DEFINITION(...)
#define NATIVE_COLLISION_BUFFER_POINTER(...)
#define NATIVE_COLLISION_BUFFER_NOP(address, ...) {address, {__VA_ARGS__}},
    constexpr SCollisionNopSite COLLISION_NOP_SITES[] = {
#include "CNativeModelStoreSA.Manifest.inc"
    };
#undef NATIVE_MODEL_STORE_DEFINITION
#undef NATIVE_MODEL_STORE_CONSTRUCTOR
#undef NATIVE_MODEL_STORE_CRT_ROUTINE
#undef NATIVE_MODEL_STORE_POINTER
#undef NATIVE_MODEL_STORE_GROWER
#undef NATIVE_COLLISION_BUFFER_DEFINITION
#undef NATIVE_COLLISION_BUFFER_POINTER
#undef NATIVE_COLLISION_BUFFER_NOP

    static_assert(std::size(STORE_DEFINITIONS) == static_cast<size_t>(EStoreKind::Count));
    static_assert(std::size(COLLISION_BUFFER_DEFINITIONS) == 1);

    struct SStoreHeader
    {
        DWORD count;
        BYTE  objects[1];
    };

    static_assert(offsetof(SStoreHeader, objects) == 4, "GTA CStore objects must immediately follow its count");

    struct SStoreState
    {
        SStoreHeader* store;
        DWORD         occupiedAtInstall;
        DWORD         highWater;
    };

    SStoreState                g_storeStates[static_cast<size_t>(EStoreKind::Count)]{};
    BYTE*                      g_collisionBuffer = nullptr;
    bool                       g_installed = false;
    const SExecutableIdentity* g_executableIdentity = nullptr;

    const SStoreDefinition& GetDefinition(EStoreKind kind)
    {
        return STORE_DEFINITIONS[static_cast<size_t>(kind)];
    }

    SStoreState& GetState(EStoreKind kind)
    {
        return g_storeStates[static_cast<size_t>(kind)];
    }

    void DebugLog(const char* format, ...)
    {
        char    message[1024]{};
        va_list arguments;
        va_start(arguments, format);
        _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
        va_end(arguments);
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        SharedUtil::WriteDebugEvent(message);
    }

    void EventLog(const char* format, ...)
    {
        char    message[1024]{};
        va_list arguments;
        va_start(arguments, format);
        _vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
        va_end(arguments);
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        SharedUtil::WriteDebugEvent(message);
    }

    bool IsRangeInExpectedImage(DWORD address, size_t size, DWORD imageSize)
    {
        if (address < EXPECTED_IMAGE_BASE || size > imageSize)
            return false;
        const DWORD offset = address - EXPECTED_IMAGE_BASE;
        return offset <= imageSize - size;
    }

    bool ValidateBytes(DWORD address, const BYTE* expected, size_t size, DWORD imageSize, const char* purpose)
    {
        if (!IsRangeInExpectedImage(address, size, imageSize))
        {
            DebugLog("[NativeBW] preflight failed: %s address 0x%08X is outside the executable image", purpose, address);
            return false;
        }
        if (memcmp(reinterpret_cast<const void*>(address), expected, size) != 0)
        {
            DebugLog("[NativeBW] preflight failed: unexpected bytes for %s at 0x%08X", purpose, address);
            return false;
        }
        return true;
    }

    bool ValidateExecutable(eGameVersion gameVersion, const char* executablePath, DWORD& imageSize, const SExecutableIdentity*& validatedIdentity)
    {
        const HMODULE module = GetModuleHandle(nullptr);
        if (module != reinterpret_cast<HMODULE>(EXPECTED_IMAGE_BASE))
        {
            DebugLog("[NativeBW] preflight failed: executable base=%p expected=0x%08X", module, EXPECTED_IMAGE_BASE);
            return false;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            DebugLog("[NativeBW] preflight failed: invalid DOS header");
            return false;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt->OptionalHeader.ImageBase != EXPECTED_IMAGE_BASE || gameVersion != VERSION_US_10)
        {
            DebugLog("[NativeBW] preflight failed: unsupported executable architecture/version=%d machine=0x%04X magic=0x%04X base=0x%08X", gameVersion,
                     nt->FileHeader.Machine, nt->OptionalHeader.Magic, nt->OptionalHeader.ImageBase);
            return false;
        }

        const SExecutableIdentity* identity = nullptr;
        for (const SExecutableIdentity& candidate : EXECUTABLE_IDENTITIES)
        {
            if (nt->OptionalHeader.SizeOfImage == candidate.imageSize && nt->FileHeader.TimeDateStamp == candidate.timestamp &&
                nt->OptionalHeader.CheckSum == candidate.checksum)
            {
                identity = &candidate;
                break;
            }
        }
        if (!identity)
        {
            DebugLog("[NativeBW] preflight failed: unsupported executable tuple timestamp=0x%08X checksum=0x%08X image=0x%08X", nt->FileHeader.TimeDateStamp,
                     nt->OptionalHeader.CheckSum, nt->OptionalHeader.SizeOfImage);
            return false;
        }

        const SString digest = SharedUtil::GenerateSha256HexStringFromFile(executablePath);
        if (_stricmp(digest.c_str(), identity->sha256) != 0)
        {
            DebugLog("[NativeBW] preflight failed: executable=%s sha256=%s expected=%s identity=%s", executablePath, digest.c_str(), identity->sha256,
                     identity->name);
            return false;
        }

        imageSize = nt->OptionalHeader.SizeOfImage;
        validatedIdentity = identity;
        DebugLog("[NativeBW] executable identity=%s sha256=%s timestamp=0x%08X checksum=0x%08X image=0x%08X", identity->name, identity->sha256,
                 identity->timestamp, identity->checksum, identity->imageSize);
        return true;
    }

    bool ValidateManifest(DWORD imageSize)
    {
        for (const SConstructorSignature& signature : CONSTRUCTOR_SIGNATURES)
        {
            if (!ValidateBytes(signature.address, signature.expected, sizeof(signature.expected), imageSize, "model constructor"))
                return false;
        }
        for (const SCrtRoutineSignature& signature : CRT_ROUTINE_SIGNATURES)
        {
            if (!ValidateBytes(signature.address, signature.expected, sizeof(signature.expected), imageSize, signature.role))
                return false;
        }

        for (const SPointerSite& site : POINTER_SITES)
        {
            if (site.instructionSize > sizeof(site.expected) || site.operandOffset + sizeof(DWORD) > site.instructionSize ||
                !ValidateBytes(site.instructionAddress, site.expected, site.instructionSize, imageSize, "model-store pointer"))
                return false;

            DWORD encodedPointer = 0;
            memcpy(&encodedPointer, site.expected + site.operandOffset, sizeof(encodedPointer));
            if (encodedPointer != GetDefinition(site.kind).originalBase + site.storeDisplacement)
            {
                DebugLog("[NativeBW] preflight failed: manifest pointer mismatch at 0x%08X", site.instructionAddress);
                return false;
            }
        }

        for (const SGrowerSite& site : GROWER_SITES)
        {
            if (!ValidateBytes(site.instructionAddress, site.expected, sizeof(site.expected), imageSize, "model-store grower call"))
                return false;
        }
        for (const SCollisionPointerSite& site : COLLISION_POINTER_SITES)
        {
            if (site.instructionSize > sizeof(site.expected) || site.operandOffset + sizeof(DWORD) > site.instructionSize ||
                !ValidateBytes(site.instructionAddress, site.expected, site.instructionSize, imageSize, "collision-buffer pointer"))
                return false;
        }
        for (const SCollisionNopSite& site : COLLISION_NOP_SITES)
        {
            if (!ValidateBytes(site.instructionAddress, site.expected, sizeof(site.expected), imageSize, "scratchpad call"))
                return false;
        }
        return true;
    }

    bool ValidateOriginalStores()
    {
        for (const SStoreDefinition& definition : STORE_DEFINITIONS)
        {
            const auto* store = reinterpret_cast<const SStoreHeader*>(definition.originalBase);
            if (store->count > definition.originalCapacity)
            {
                DebugLog("[NativeBW] preflight failed: %s occupied=%u exceeds stock capacity=%u", definition.name, store->count, definition.originalCapacity);
                return false;
            }

            DWORD firstVtable = 0;
            memcpy(&firstVtable, store->objects, sizeof(firstVtable));
            if (firstVtable != definition.vtable)
            {
                DebugLog("[NativeBW] preflight failed: %s constructor layout vtable=0x%08X expected=0x%08X", definition.name, firstVtable, definition.vtable);
                return false;
            }
        }
        return true;
    }

    void ReleaseUncommittedAllocations()
    {
        for (SStoreState& state : g_storeStates)
        {
            if (state.store)
                VirtualFree(state.store, 0, MEM_RELEASE);
            state = {};
        }
        if (g_collisionBuffer)
            VirtualFree(g_collisionBuffer, 0, MEM_RELEASE);
        g_collisionBuffer = nullptr;
    }

    bool AllocateStoresAndCollisionBuffer()
    {
        using Constructor = void*(__thiscall*)(void*);

        for (const SStoreDefinition& definition : STORE_DEFINITIONS)
        {
            const size_t bytes = offsetof(SStoreHeader, objects) + static_cast<size_t>(definition.newCapacity) * definition.stride;
            auto*        store = static_cast<SStoreHeader*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (!store)
            {
                DebugLog("[NativeBW] allocation failed: %s bytes=%zu error=%u", definition.name, bytes, GetLastError());
                ReleaseUncommittedAllocations();
                return false;
            }

            SStoreState& state = GetState(definition.kind);
            state.store = store;
            state.occupiedAtInstall = reinterpret_cast<const SStoreHeader*>(definition.originalBase)->count;
            state.highWater = state.occupiedAtInstall;

            // MTA reaches this point after GTA's CRT constructed the stock inline
            // arrays. Construct every relocated slot explicitly and leave the CRT
            // destructor table aimed at stock storage: these allocations live for
            // the process and must not be destroyed through a stale static table.
            Constructor constructor = reinterpret_cast<Constructor>(definition.constructorAddress);
            for (DWORD index = 0; index < definition.newCapacity; ++index)
                constructor(store->objects + static_cast<size_t>(index) * definition.stride);
            store->count = 0;
        }

        const DWORD collisionCapacity = COLLISION_BUFFER_DEFINITIONS[0].newCapacity;
        g_collisionBuffer = static_cast<BYTE*>(VirtualAlloc(nullptr, collisionCapacity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!g_collisionBuffer)
        {
            DebugLog("[NativeBW] allocation failed: collision buffer bytes=%u error=%u", collisionCapacity, GetLastError());
            ReleaseUncommittedAllocations();
            return false;
        }
        return true;
    }

    __declspec(noreturn) void FailOnStoreOverflow(EStoreKind kind, int modelId)
    {
        const SStoreDefinition& definition = GetDefinition(kind);
        const SStoreState&      state = GetState(kind);
        DebugLog("[NativeBW] FATAL: %s capacity exhausted while adding model=%d occupied=%u capacity=%u", definition.name, modelId,
                 state.store ? state.store->count : 0, definition.newCapacity);
        TerminateProcess(GetCurrentProcess(), OVERFLOW_EXIT_CODE);
        for (;;)
            Sleep(INFINITE);
    }

    void* AddModelChecked(EStoreKind kind, int modelId, DWORD originalFunction)
    {
        using AddModel = void*(__cdecl*)(int);
        SStoreState& state = GetState(kind);
        if (!state.store || state.store->count >= GetDefinition(kind).newCapacity)
            FailOnStoreOverflow(kind, modelId);

        void* result = reinterpret_cast<AddModel>(originalFunction)(modelId);
        if (state.store->count > state.highWater)
            state.highWater = state.store->count;
        return result;
    }

    void* __cdecl AddAtomicModelChecked(int modelId)
    {
        return AddModelChecked(EStoreKind::Atomic, modelId, 0x004C6620);
    }

    void* __cdecl AddDamageAtomicModelChecked(int modelId)
    {
        return AddModelChecked(EStoreKind::DamageAtomic, modelId, 0x004C6650);
    }

    void* __cdecl AddTimeModelChecked(int modelId)
    {
        return AddModelChecked(EStoreKind::Time, modelId, 0x004C66B0);
    }

    DWORD GrowerWrapper(EStoreKind kind)
    {
        switch (kind)
        {
            case EStoreKind::Atomic:
                return reinterpret_cast<DWORD>(&AddAtomicModelChecked);
            case EStoreKind::DamageAtomic:
                return reinterpret_cast<DWORD>(&AddDamageAtomicModelChecked);
            case EStoreKind::Time:
                return reinterpret_cast<DWORD>(&AddTimeModelChecked);
            default:
                return 0;
        }
    }

    void CommitPatchSet()
    {
        for (const SPointerSite& site : POINTER_SITES)
        {
            if (site.action == EPatchAction::Patch)
            {
                const DWORD relocated = reinterpret_cast<DWORD>(GetState(site.kind).store) + site.storeDisplacement;
                MemPut<DWORD>(site.instructionAddress + site.operandOffset, relocated);
            }
        }
        for (const SGrowerSite& site : GROWER_SITES)
        {
            const DWORD relative = GrowerWrapper(site.kind) - (site.instructionAddress + 5);
            MemPut<DWORD>(site.instructionAddress + 1, relative);
        }
        for (const SCollisionPointerSite& site : COLLISION_POINTER_SITES)
        {
            const DWORD relocated = reinterpret_cast<DWORD>(g_collisionBuffer) + site.bufferDisplacement;
            MemPut<DWORD>(site.instructionAddress + site.operandOffset, relocated);
        }
        constexpr BYTE NOPS[5] = {0x90, 0x90, 0x90, 0x90, 0x90};
        for (const SCollisionNopSite& site : COLLISION_NOP_SITES)
            MemCpy(reinterpret_cast<void*>(site.instructionAddress), NOPS, sizeof(NOPS));
    }

    bool InstallValidated(eGameVersion gameVersion, std::string& error)
    {
        if (g_installed)
        {
            error = "native model-store foundation is already installed";
            return false;
        }

        char        executablePath[MAX_PATH]{};
        const DWORD executablePathLength = GetModuleFileNameA(nullptr, executablePath, sizeof(executablePath));
        if (!executablePathLength || executablePathLength >= sizeof(executablePath))
        {
            error = SString("executable path is unavailable or truncated win32=%u", GetLastError());
            return false;
        }

        DWORD                      imageSize = 0;
        const SExecutableIdentity* identity = nullptr;
        if (!ValidateExecutable(gameVersion, executablePath, imageSize, identity) || !ValidateManifest(imageSize) || !ValidateOriginalStores())
        {
            error = "executable identity, patch manifest, or stock stores failed validation";
            return false;
        }
        if (!AllocateStoresAndCollisionBuffer())
        {
            error = "native model-store allocation failed before executable mutation";
            return false;
        }
        if (!ValidateManifest(imageSize) || !ValidateOriginalStores())
        {
            error = "executable patch sites or stock stores changed before the first native write";
            return false;
        }

        g_executableIdentity = identity;

        // Every instruction and allocation is revalidated after authorization
        // claim and before this first write. GTA has not populated the stores.
        CommitPatchSet();
        g_installed = true;
        return true;
    }
}  // namespace

void CNativeModelStoreSA::InstallFromEnvironment(eGameVersion gameVersion)
{
    char  value[8]{};
    DWORD valueLength = GetEnvironmentVariableA(FEATURE_ENVIRONMENT, value, sizeof(value));
    if (valueLength != 1 || value[0] != '1')
    {
        OutputDebugStringA("[NativeBW] mode=off (MTA_NATIVE_BW_MODEL_STORES is not exactly 1)\n");
        return;
    }

    std::string error;
    if (!InstallValidated(gameVersion, error))
    {
        DebugLog("[NativeBW] mode=refused detail=%s; stock stores and collision buffers remain active", error.c_str());
        return;
    }

    DebugLog("[NativeBW] mode=active executable=%s timestamp=0x%08X checksum=0x%08X image=0x%08X collision=%p capacity=%u (stock=%u)",
             g_executableIdentity->name, g_executableIdentity->timestamp, g_executableIdentity->checksum, g_executableIdentity->imageSize, g_collisionBuffer,
             COLLISION_BUFFER_DEFINITIONS[0].newCapacity, COLLISION_BUFFER_DEFINITIONS[0].originalCapacity);
}

bool CNativeModelStoreSA::InstallForAuthorizedStartup(eGameVersion gameVersion, std::string& error)
{
    if (!InstallValidated(gameVersion, error))
        return false;

    DebugLog("[NativeWorldAuthorization] state=model-stores-prepared executable=%s nativeWrites=yes activation=prepared", g_executableIdentity->name);
    return true;
}

bool CNativeModelStoreSA::ValidateExecutableAndPatchManifestReadOnly(eGameVersion gameVersion, std::string& error)
{
    char        executablePath[MAX_PATH]{};
    const DWORD executablePathLength = GetModuleFileNameA(nullptr, executablePath, sizeof(executablePath));
    if (!executablePathLength || executablePathLength >= sizeof(executablePath))
    {
        error = SString("executable path is unavailable or truncated win32=%u", GetLastError());
        return false;
    }

    DWORD                      imageSize = 0;
    const SExecutableIdentity* identity = nullptr;
    if (!ValidateExecutable(gameVersion, executablePath, imageSize, identity) || !ValidateManifest(imageSize) || !ValidateOriginalStores())
    {
        error = "executable identity, patch manifest, or stock stores failed read-only validation";
        return false;
    }
    return identity != nullptr;
}

void CNativeModelStoreSA::LogDiagnostics(const char* context)
{
    if (!g_installed)
        return;

    EventLog("[NativeBW] %s mode=active executable=%s timestamp=0x%08X checksum=0x%08X image=0x%08X collision=%p capacity=%u (stock=%u)", context,
             g_executableIdentity->name, g_executableIdentity->timestamp, g_executableIdentity->checksum, g_executableIdentity->imageSize, g_collisionBuffer,
             COLLISION_BUFFER_DEFINITIONS[0].newCapacity, COLLISION_BUFFER_DEFINITIONS[0].originalCapacity);

    for (const SStoreDefinition& definition : STORE_DEFINITIONS)
    {
        SStoreState& state = GetState(definition.kind);
        if (state.store->count > state.highWater)
            state.highWater = state.store->count;
        EventLog("[NativeBW] %s store=%s old=0x%08X new=%p capacity=%u occupied=%u startupOccupied=%u highWater=%u", context, definition.name,
                 definition.originalBase, state.store, definition.newCapacity, state.store->count, state.occupiedAtInstall, state.highWater);
    }
}

bool CNativeModelStoreSA::IsInstalled()
{
    return g_installed;
}

const char* CNativeModelStoreSA::GetExecutableIdentityName()
{
    return g_executableIdentity ? g_executableIdentity->name : nullptr;
}

bool CNativeModelStoreSA::GetUsage(unsigned int& atomic, unsigned int& damageAtomic, unsigned int& time)
{
    if (!g_installed)
        return false;

    atomic = GetState(EStoreKind::Atomic).store->count;
    damageAtomic = GetState(EStoreKind::DamageAtomic).store->count;
    time = GetState(EStoreKind::Time).store->count;
    return true;
}
