/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CTxdPoolSA.h"
#include "CGameSA.h"
#include "CKeyGenSA.h"

extern CGameSA* pGame;

CTxdPoolSA::CTxdPoolSA()
{
    m_ppTxdPoolInterface = (CPoolSAInterface<CTextureDictonarySAInterface>**)0xC8800C;
}

std::uint32_t CTxdPoolSA::AllocateTextureDictonarySlot(std::uint32_t uiSlotId, std::string& strTxdName)
{
    CTextureDictonarySAInterface* pTxd = (*m_ppTxdPoolInterface)->AllocateAt(uiSlotId);
    if (!pTxd)
        return -1;

    strTxdName.resize(24);

    pTxd->usUsagesCount = 0;
    pTxd->hash = pGame->GetKeyGen()->GetUppercaseKey(strTxdName.c_str());
    pTxd->rwTexDictonary = nullptr;
    pTxd->usParentIndex = -1;

    return (*m_ppTxdPoolInterface)->GetObjectIndex(pTxd);
}

void CTxdPoolSA::RemoveTextureDictonarySlot(std::uint32_t uiTxdId)
{
    CPoolSAInterface<CTextureDictonarySAInterface>* pTxdPool = *m_ppTxdPoolInterface;
    if (!pTxdPool->IsContains(uiTxdId))
        return;

    // A TXD file ID can remain linked in GTA's loaded/requested streaming lists.
    // Freeing its pool slot directly leaves RemoveLeastUsedModel able to visit the
    // stale entry and call CTxdStore::GetNumRefs on a null TxdDef. RemoveModel
    // cancels pending reads, unlinks the entry, and unloads the dictionary while
    // the pool slot is still valid.
    const std::uint32_t uiStreamingModelId = pGame->GetBaseIDforTXD() + uiTxdId;
    CStreamingInfo*     pStreamingInfo = pGame->GetStreaming()->GetStreamingInfo(uiStreamingModelId);
    pGame->GetStreaming()->RemoveModel(uiStreamingModelId);

    // RemoveModel only unloads entries known to the streamer. Preserve the raw
    // cleanup for an allocated dictionary whose streaming state was already
    // reset, without invoking CTxdStore::RemoveTxd twice for the normal path.
    CTextureDictonarySAInterface* pTxd = pTxdPool->GetObject(uiTxdId);
    if (pTxd->rwTexDictonary)
    {
        using Function_TxdReleaseSlot = std::uint32_t(__cdecl*)(std::uint32_t uiTxdId);
        reinterpret_cast<Function_TxdReleaseSlot>(0x731E90)(uiTxdId);
    }

    // Never mark the pool slot free while a streaming list can still reach it.
    // Keeping a slot allocated is safer than creating a dangling native entry if
    // GTA rejects the removal because of an unexpected state.
    constexpr std::uint16_t INVALID_STREAMING_INDEX = static_cast<std::uint16_t>(-1);
    if (pStreamingInfo->prevId != INVALID_STREAMING_INDEX || pStreamingInfo->nextId != INVALID_STREAMING_INDEX)
    {
        dassert(false);
        return;
    }

    // Preserve SetStreamingInfo's archive-chain repair before clearing the full
    // entry. A predecessor can otherwise keep nextInImg pointing at this TXD
    // file ID after the slot is reused.
    pGame->GetStreaming()->SetStreamingInfo(uiStreamingModelId, 0, 0, 0, -1);
    *pStreamingInfo = CStreamingInfo{};
    pTxdPool->Release(uiTxdId);
}

bool CTxdPoolSA::IsFreeTextureDictonarySlot(std::uint32_t uiTxdId)
{
    return (*m_ppTxdPoolInterface)->IsEmpty(uiTxdId);
}

std::uint32_t CTxdPoolSA::GetFreeTextureDictonarySlot()
{
    return (*m_ppTxdPoolInterface)->GetFreeSlot();
}
