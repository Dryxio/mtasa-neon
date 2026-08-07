/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CCheckpointsSA.cpp
 *  PURPOSE:     Checkpoint entity manager
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CCheckpointsSA.h"
#include "C3DMarkersSA.h"
#include "CCheckpointSA.h"

namespace
{
    constexpr std::size_t GTA_CHECKPOINT_COUNT = 32;
    constexpr std::size_t PROCESS_LIMIT_GROWTH = 32;
    constexpr WORD        UNUSED_CHECKPOINT_TYPE = 257;

    static_assert(MAX_CHECKPOINTS % 8 == 0, "GTA's unrolled checkpoint search requires a multiple-of-eight capacity");

    CCheckpointSAInterface  g_checkpointArray[MAX_CHECKPOINTS]{};
    CCheckpointSAInterface* g_pCheckpointArray = reinterpret_cast<CCheckpointSAInterface*>(ARRAY_CHECKPOINTS);
    CCheckpointsSA*         g_checkpointManager{};
    std::size_t             g_checkpointProcessLimit{};

    void PatchCheckpointAddress(std::uintptr_t operandAddress, const void* value)
    {
        MemPut<DWORD>(operandAddress, reinterpret_cast<DWORD>(value));
    }

    BYTE* CheckpointField(void* base, std::size_t offset)
    {
        return reinterpret_cast<BYTE*>(base) + offset;
    }

    void InitializeUnusedCheckpoint(CCheckpointSAInterface& checkpoint)
    {
        checkpoint.m_nType = UNUSED_CHECKPOINT_TYPE;
        checkpoint.m_rotFlag = true;
        checkpoint.rwColour = 0xFFFFFFFF;
        checkpoint.m_nPulsePeriod = 1024;
        checkpoint.m_nRotateRate = 5;
        checkpoint.m_fPulseFraction = 0.25f;
        checkpoint.m_fSize = 1.0f;
    }

    void SetCheckpointProcessLimit(std::size_t requestedLimit)
    {
        constexpr std::size_t groupSize = 8;
        const std::size_t     clampedLimit = std::clamp<std::size_t>(requestedLimit, GTA_CHECKPOINT_COUNT, MAX_CHECKPOINTS);
        const std::size_t     limit = std::min<std::size_t>(MAX_CHECKPOINTS, (clampedLimit + groupSize - 1) & ~(groupSize - 1));
        if (limit == g_checkpointProcessLimit)
            return;

        // Init still covers the complete allocation. Runtime lookup, update,
        // deletion, and render loops follow only the occupied working set.
        PatchCheckpointAddress(0x72291A, CheckpointField(g_checkpointArray + limit, 0x4));
        PatchCheckpointAddress(0x722989, CheckpointField(g_checkpointArray + limit, 0x4));
        PatchCheckpointAddress(0x722C28, CheckpointField(g_checkpointArray + limit, 0x24));
        PatchCheckpointAddress(0x722CA8, CheckpointField(g_checkpointArray + limit, 0x4));
        PatchCheckpointAddress(0x722D8D, g_checkpointArray + limit);
        PatchCheckpointAddress(0x722EF0, CheckpointField(g_checkpointArray + limit, 0x30));
        PatchCheckpointAddress(0x722FEF, CheckpointField(g_checkpointArray + limit, 0x4));
        PatchCheckpointAddress(0x726079, g_checkpointArray + limit);
        g_checkpointProcessLimit = limit;
    }

    void RefreshCheckpointProcessLimit()
    {
        std::size_t requiredLimit = GTA_CHECKPOINT_COUNT;
        for (std::size_t index = g_checkpointProcessLimit; index > GTA_CHECKPOINT_COUNT; --index)
        {
            const CCheckpointSAInterface& checkpoint = g_checkpointArray[index - 1];
            if (checkpoint.m_bIsUsed || checkpoint.m_nType != UNUSED_CHECKPOINT_TYPE)
            {
                requiredLimit = index;
                break;
            }
        }
        SetCheckpointProcessLimit(requiredLimit);
    }
}  // namespace

CCheckpointSAInterface* CCheckpointsSA::GetCheckpointArray()
{
    return g_pCheckpointArray;
}

void CCheckpointsSA::RelocateCheckpointArray()
{
    static bool bPatched = false;
    if (bPatched)
        return;

    for (CCheckpointSAInterface& checkpoint : g_checkpointArray)
        InitializeUnusedCheckpoint(checkpoint);

    // GTA initializes the original pool before CGameSA constructs this wrapper.
    // Preserve those entries so native mission checkpoints survive relocation.
    MemCpyFast(g_checkpointArray, reinterpret_cast<const void*>(ARRAY_CHECKPOINTS), GTA_CHECKPOINT_COUNT * sizeof(CCheckpointSAInterface));
    g_pCheckpointArray = g_checkpointArray;

    // CCheckpoints::Init.
    PatchCheckpointAddress(0x722881, CheckpointField(g_checkpointArray, 0x2));
    PatchCheckpointAddress(0x7228E1, CheckpointField(g_checkpointArray + MAX_CHECKPOINTS, 0x2));

    // CCheckpoints::UpdatePos.
    PatchCheckpointAddress(0x722907, CheckpointField(g_checkpointArray, 0x4));
    PatchCheckpointAddress(0x72292C, CheckpointField(g_checkpointArray, 0x10));
    PatchCheckpointAddress(0x722935, CheckpointField(g_checkpointArray, 0x14));
    PatchCheckpointAddress(0x72293C, g_checkpointArray);
    PatchCheckpointAddress(0x722948, CheckpointField(g_checkpointArray, 0x34));
    PatchCheckpointAddress(0x722951, CheckpointField(g_checkpointArray, 0x18));
    PatchCheckpointAddress(0x722961, CheckpointField(g_checkpointArray, 0x18));

    // CCheckpoints::SetHeading and CCheckpoints::Update.
    PatchCheckpointAddress(0x722977, CheckpointField(g_checkpointArray, 0x4));
    PatchCheckpointAddress(0x7229A3, CheckpointField(g_checkpointArray, 0x1C));
    PatchCheckpointAddress(0x7229D7, CheckpointField(g_checkpointArray, 0x24));

    // CCheckpoints::PlaceMarker. Its replacement scan is unrolled in groups of
    // eight, and 4096 remains exactly divisible by that group size.
    PatchCheckpointAddress(0x722C82, CheckpointField(g_checkpointArray, 0x4));
    PatchCheckpointAddress(0x722CBC, g_checkpointArray);
    PatchCheckpointAddress(0x722D7D, g_checkpointArray);
    PatchCheckpointAddress(0x722D9A, g_checkpointArray);
    PatchCheckpointAddress(0x722DA7, CheckpointField(g_checkpointArray, 0x30));

    // CCheckpoints::DeleteCP and CCheckpoints::Render.
    PatchCheckpointAddress(0x722FCB, CheckpointField(g_checkpointArray, 0x4));
    PatchCheckpointAddress(0x726062, g_checkpointArray);

    SetCheckpointProcessLimit(GTA_CHECKPOINT_COUNT);

    bPatched = true;
}

CCheckpointsSA::CCheckpointsSA()
{
    RelocateCheckpointArray();
    g_checkpointManager = this;

    for (int i = 0; i < MAX_CHECKPOINTS; i++)
        Checkpoints[i].SetInterface(&GetCheckpointArray()[i]);
}

CCheckpointSA* CCheckpointsSA::FromInterface(CCheckpointSAInterface* checkpointInterface)
{
    if (!checkpointInterface || !g_checkpointManager)
        return nullptr;

    const std::uintptr_t checkpointAddress = reinterpret_cast<std::uintptr_t>(checkpointInterface);
    const std::uintptr_t arrayAddress = reinterpret_cast<std::uintptr_t>(g_checkpointArray);
    const std::uintptr_t arrayEndAddress = arrayAddress + sizeof(g_checkpointArray);
    if (checkpointAddress < arrayAddress || checkpointAddress >= arrayEndAddress || (checkpointAddress - arrayAddress) % sizeof(CCheckpointSAInterface) != 0)
    {
        return nullptr;
    }

    return &g_checkpointManager->Checkpoints[(checkpointAddress - arrayAddress) / sizeof(CCheckpointSAInterface)];
}

/**
 * \todo Update default color to SA's orange instead of VC's pink
 */
CCheckpoint* CCheckpointsSA::CreateCheckpoint(DWORD Identifier, WORD wType, CVector* vecPosition, CVector* vecPointDir, float fSize, float fPulseFraction,
                                              const SharedUtil::SColor color)
{
    /*
    static CCheckpoint  *PlaceMarker(unsigned int nIdentifier, unsigned short nType, CVector &vecPosition, CVector &pointDir,
    float fSize, unsigned char r, unsigned char g, unsigned char b, unsigned char a, unsigned short nPeriod, float fPulseFrac, short nRotRate);
    */

    CCheckpoint* Checkpoint = FindFreeMarker();
    if (Checkpoint)
    {
        ((CCheckpointSA*)(Checkpoint))->SetIdentifier(Identifier);
        ((CCheckpointSA*)(Checkpoint))->Activate();
        ((CCheckpointSA*)(Checkpoint))->SetType(wType);
        Checkpoint->SetPosition(vecPosition);
        Checkpoint->SetPointDirection(vecPointDir);
        Checkpoint->SetSize(fSize);
        Checkpoint->SetColor(color);
        Checkpoint->SetPulsePeriod(1024);
        ((CCheckpointSA*)(Checkpoint))->SetPulseFraction(fPulseFraction);
        Checkpoint->SetRotateRate(1);

        return Checkpoint;
    }
    return NULL;
}

CCheckpoint* CCheckpointsSA::FindFreeMarker()
{
    for (std::size_t i = 0; i < g_checkpointProcessLimit; i++)
    {
        if (!Checkpoints[i].IsActive())
            return &Checkpoints[i];
    }

    if (g_checkpointProcessLimit < MAX_CHECKPOINTS)
    {
        const std::size_t firstNewSlot = g_checkpointProcessLimit;
        SetCheckpointProcessLimit(std::min<std::size_t>(MAX_CHECKPOINTS, g_checkpointProcessLimit + PROCESS_LIMIT_GROWTH));
        return &Checkpoints[firstNewSlot];
    }
    return NULL;
}

CCheckpoint* CCheckpointsSA::FindMarker(DWORD identifier)
{
    for (std::size_t index = 0; index < g_checkpointProcessLimit; ++index)
    {
        if (Checkpoints[index].GetIdentifier() == identifier)
            return &Checkpoints[index];
    }

    return nullptr;
}

void CCheckpointsSA::BeginFrame()
{
    RefreshCheckpointProcessLimit();

    std::size_t expectedArrowCount = 0;
    for (std::size_t index = 0; index < g_checkpointProcessLimit; ++index)
    {
        if (!Checkpoints[index].IsActive())
            continue;

        if (Checkpoints[index].GetType() == CHECKPOINT_TUBE)
            ++expectedArrowCount;
    }
    C3DMarkersSA::SetExpectedDirectionArrowCount(expectedArrowCount);
}

unsigned int CCheckpointsSA::GetCount() const
{
    unsigned int count = 0;
    for (std::size_t index = 0; index < g_checkpointProcessLimit; ++index)
    {
        if (Checkpoints[index].GetInterface()->m_bIsUsed)
            ++count;
    }
    return count;
}

unsigned int CCheckpointsSA::GetProcessLimit() const
{
    return static_cast<unsigned int>(g_checkpointProcessLimit);
}

unsigned int CCheckpointsSA::GetRequired3DMarkerSlots() const
{
    unsigned int requiredSlots = 0;
    for (std::size_t index = 0; index < g_checkpointProcessLimit; ++index)
    {
        const CCheckpointSAInterface* checkpoint = Checkpoints[index].GetInterface();
        if (!checkpoint->m_bIsUsed)
            continue;

        const DWORD type = checkpoint->m_nType;
        if (type == CHECKPOINT_TORUSTHROUGH)
            requiredSlots += 4;
        else if (type < CHECKPOINT_NUM)
            ++requiredSlots;
    }
    return std::min<unsigned int>(requiredSlots, MAX_3D_MARKERS);
}
