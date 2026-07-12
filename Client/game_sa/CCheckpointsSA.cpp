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
#include "CCheckpointSA.h"

namespace
{
    constexpr std::size_t GTA_CHECKPOINT_COUNT = 32;
    constexpr WORD        UNUSED_CHECKPOINT_TYPE = 257;

    static_assert(MAX_CHECKPOINTS % 8 == 0, "GTA's unrolled checkpoint search requires a multiple-of-eight capacity");

    CCheckpointSAInterface* g_pCheckpointArray = reinterpret_cast<CCheckpointSAInterface*>(ARRAY_CHECKPOINTS);

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

    static CCheckpointSAInterface checkpointArray[MAX_CHECKPOINTS]{};
    for (CCheckpointSAInterface& checkpoint : checkpointArray)
        InitializeUnusedCheckpoint(checkpoint);

    // GTA initializes the original pool before CGameSA constructs this wrapper.
    // Preserve those entries so native mission checkpoints survive relocation.
    MemCpyFast(checkpointArray, reinterpret_cast<const void*>(ARRAY_CHECKPOINTS), GTA_CHECKPOINT_COUNT * sizeof(CCheckpointSAInterface));
    g_pCheckpointArray = checkpointArray;

    // CCheckpoints::Init.
    PatchCheckpointAddress(0x722881, CheckpointField(checkpointArray, 0x2));
    PatchCheckpointAddress(0x7228E1, CheckpointField(checkpointArray + MAX_CHECKPOINTS, 0x2));

    // CCheckpoints::UpdatePos.
    PatchCheckpointAddress(0x722907, CheckpointField(checkpointArray, 0x4));
    PatchCheckpointAddress(0x72291A, CheckpointField(checkpointArray + MAX_CHECKPOINTS, 0x4));
    PatchCheckpointAddress(0x72292C, CheckpointField(checkpointArray, 0x10));
    PatchCheckpointAddress(0x722935, CheckpointField(checkpointArray, 0x14));
    PatchCheckpointAddress(0x72293C, checkpointArray);
    PatchCheckpointAddress(0x722948, CheckpointField(checkpointArray, 0x34));
    PatchCheckpointAddress(0x722951, CheckpointField(checkpointArray, 0x18));
    PatchCheckpointAddress(0x722961, CheckpointField(checkpointArray, 0x18));

    // CCheckpoints::SetHeading and CCheckpoints::Update.
    PatchCheckpointAddress(0x722977, CheckpointField(checkpointArray, 0x4));
    PatchCheckpointAddress(0x722989, CheckpointField(checkpointArray + MAX_CHECKPOINTS, 0x4));
    PatchCheckpointAddress(0x7229A3, CheckpointField(checkpointArray, 0x1C));
    PatchCheckpointAddress(0x7229D7, CheckpointField(checkpointArray, 0x24));
    PatchCheckpointAddress(0x722C28, CheckpointField(checkpointArray + MAX_CHECKPOINTS, 0x24));

    // CCheckpoints::PlaceMarker. Its replacement scan is unrolled in groups of
    // eight, and 4096 remains exactly divisible by that group size.
    PatchCheckpointAddress(0x722C82, CheckpointField(checkpointArray, 0x4));
    PatchCheckpointAddress(0x722CA8, CheckpointField(checkpointArray + MAX_CHECKPOINTS, 0x4));
    PatchCheckpointAddress(0x722CBC, checkpointArray);
    PatchCheckpointAddress(0x722D7D, checkpointArray);
    PatchCheckpointAddress(0x722D8D, checkpointArray + MAX_CHECKPOINTS);
    PatchCheckpointAddress(0x722D9A, checkpointArray);
    PatchCheckpointAddress(0x722DA7, CheckpointField(checkpointArray, 0x30));
    PatchCheckpointAddress(0x722EF0, CheckpointField(checkpointArray + MAX_CHECKPOINTS, 0x30));

    // CCheckpoints::DeleteCP and CCheckpoints::Render.
    PatchCheckpointAddress(0x722FCB, CheckpointField(checkpointArray, 0x4));
    PatchCheckpointAddress(0x722FEF, CheckpointField(checkpointArray + MAX_CHECKPOINTS, 0x4));
    PatchCheckpointAddress(0x726062, checkpointArray);
    PatchCheckpointAddress(0x726079, checkpointArray + MAX_CHECKPOINTS);

    bPatched = true;
}

CCheckpointsSA::CCheckpointsSA()
{
    RelocateCheckpointArray();

    for (int i = 0; i < MAX_CHECKPOINTS; i++)
        Checkpoints[i] = new CCheckpointSA(&GetCheckpointArray()[i]);
}

CCheckpointsSA::~CCheckpointsSA()
{
    for (int i = 0; i < MAX_CHECKPOINTS; i++)
    {
        delete Checkpoints[i];
    }
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
    for (int i = 0; i < MAX_CHECKPOINTS; i++)
    {
        if (!Checkpoints[i]->IsActive())
            return Checkpoints[i];
    }
    return NULL;
}

CCheckpoint* CCheckpointsSA::FindMarker(DWORD identifier)
{
    for (CCheckpointSA* checkpoint : Checkpoints)
    {
        if (checkpoint->GetIdentifier() == identifier)
            return checkpoint;
    }

    return nullptr;
}

unsigned int CCheckpointsSA::GetCount() const
{
    unsigned int count = 0;
    for (CCheckpointSA* checkpoint : Checkpoints)
    {
        if (checkpoint->IsActive())
            ++count;
    }
    return count;
}
