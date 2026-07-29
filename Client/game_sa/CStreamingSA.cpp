/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CStreamingSA.cpp
 *  PURPOSE:     Data streamer
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include <core/CCoreInterface.h>
#include "CStreamingSA.h"
#include "CModelInfoSA.h"
#include "CNativeWorldPackSA.h"
#include "Fileapi.h"
#include "processthreadsapi.h"
#include "CGameSA.h"

extern CCoreInterface* g_pCore;
extern CGameSA*        pGame;

HANDLE* phStreamingThread = (HANDLE*)0x8E4008;
uint32(&CStreamingSA::ms_streamingHalfOfBufferSizeBlocks) = *(uint32*)0x8E4CA8;
void* (&CStreamingSA::ms_pStreamingBuffer)[2] = *(void* (*)[2])0x8E4CAC;

namespace
{
    //
    // Used in LoadAllRequestedModels to record state
    //
    struct SPassStats
    {
        bool  bLoadingBigModel;
        DWORD numPriorityRequests;
        DWORD numModelsRequested;
        DWORD memoryUsed;

        void Record()
        {
#define VAR_CStreaming_bLoadingBigModel    0x08E4A58
#define VAR_CStreaming_numPriorityRequests 0x08E4BA0
#define VAR_CStreaming_numModelsRequested  0x08E4CB8
#define VAR_CStreaming_memoryUsed          0x08E4CB4

            bLoadingBigModel = *(BYTE*)VAR_CStreaming_bLoadingBigModel != 0;
            numPriorityRequests = *(DWORD*)VAR_CStreaming_numPriorityRequests;
            numModelsRequested = *(DWORD*)VAR_CStreaming_numModelsRequested;
            memoryUsed = *(DWORD*)VAR_CStreaming_memoryUsed;
        }
    };

    constexpr size_t RESERVED_STREAMS_NUM = 10;  // GTA3 + 9 SFX archives(FEET, GENRL, PAIN_A, SCRIPT, SPC_EA, SPC_FA, SPC_GA, SPC_NA, SPC_PA)
    constexpr size_t MAX_STREAMS_NUM = 255;
    constexpr size_t MAX_IMAGES_NUM = MAX_STREAMS_NUM - RESERVED_STREAMS_NUM;
    constexpr size_t MIN_IMAGES_NUM = 6;  // GTA3(yes, it is presented twice), GTA_INT, CARREC, SCRIPT, CUTSCENE, PLAYER
    constexpr size_t DEFERRED_PLAYER_ARCHIVE_ID = MIN_IMAGES_NUM - 1;

    struct SStreamingChannelTelemetryMirror
    {
        int32_t  modelIds[16];
        int32_t  modelStreamingBufferOffsets[16];
        int32_t  state;
        int32_t  loadingLevel;
        uint32_t position;
        int32_t  sectorCount;
        int32_t  totalTries;
        int32_t  cdStreamStatus;
    };
    static_assert(sizeof(SStreamingChannelTelemetryMirror) == 0x98, "Invalid streaming channel telemetry mirror size");

    std::uint64_t HashTelemetryBytes(std::uint64_t hash, const void* data, size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    bool ArchiveCapacitiesEqual(const SStreamingArchiveCapacitySA& left, const SStreamingArchiveCapacitySA& right)
    {
        return left.archiveCapacity == right.archiveCapacity && left.streamHandleCapacity == right.streamHandleCapacity &&
               left.streamNameCapacity == right.streamNameCapacity;
    }

    bool ArchiveInfoEqual(const CArchiveInfo& left, const CArchiveInfo& right)
    {
        return std::memcmp(&left, &right, sizeof(left)) == 0;
    }

    bool StreamNameEqual(const SStreamName& left, const SStreamName& right)
    {
        return std::memcmp(&left, &right, sizeof(left)) == 0;
    }

    bool CaptureStreamingFileIdentity(HANDLE handle, SStreamingArchiveFileIdentitySA& identity, std::string& error)
    {
        if (!handle || handle == INVALID_HANDLE_VALUE || GetFileType(handle) != FILE_TYPE_DISK)
        {
            error = "streaming archive ownership does not reference a disk handle";
            return false;
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(handle, &information))
        {
            error = "streaming archive ownership file identity is unavailable";
            return false;
        }

        identity.volumeSerialNumber = information.dwVolumeSerialNumber;
        identity.fileIndexHigh = information.nFileIndexHigh;
        identity.fileIndexLow = information.nFileIndexLow;
        identity.fileSizeHigh = information.nFileSizeHigh;
        identity.fileSizeLow = information.nFileSizeLow;
        return true;
    }

    bool StreamingFileIdentityEqual(const SStreamingArchiveFileIdentitySA& left, const SStreamingArchiveFileIdentitySA& right)
    {
        return left.volumeSerialNumber == right.volumeSerialNumber && left.fileIndexHigh == right.fileIndexHigh && left.fileIndexLow == right.fileIndexLow &&
               left.fileSizeHigh == right.fileSizeHigh && left.fileSizeLow == right.fileSizeLow;
    }

    std::uint64_t HashArchiveOwnership(const SStreamingArchiveOwnershipSA& ownership)
    {
        std::uint64_t hash = 14695981039346656037ULL;
        hash = HashTelemetryBytes(hash, &ownership.allocation, sizeof(ownership.allocation));
        hash = HashTelemetryBytes(hash, &ownership.capacityBefore.archiveCapacity, sizeof(ownership.capacityBefore.archiveCapacity));
        hash = HashTelemetryBytes(hash, &ownership.capacityBefore.streamHandleCapacity, sizeof(ownership.capacityBefore.streamHandleCapacity));
        hash = HashTelemetryBytes(hash, &ownership.capacityBefore.streamNameCapacity, sizeof(ownership.capacityBefore.streamNameCapacity));
        hash = HashTelemetryBytes(hash, &ownership.capacitySealed.archiveCapacity, sizeof(ownership.capacitySealed.archiveCapacity));
        hash = HashTelemetryBytes(hash, &ownership.capacitySealed.streamHandleCapacity, sizeof(ownership.capacitySealed.streamHandleCapacity));
        hash = HashTelemetryBytes(hash, &ownership.capacitySealed.streamNameCapacity, sizeof(ownership.capacitySealed.streamNameCapacity));
        hash = HashTelemetryBytes(hash, &ownership.archiveBefore, sizeof(ownership.archiveBefore));
        hash = HashTelemetryBytes(hash, &ownership.archiveSealed, sizeof(ownership.archiveSealed));
        hash = HashTelemetryBytes(hash, &ownership.streamNameBefore, sizeof(ownership.streamNameBefore));
        hash = HashTelemetryBytes(hash, &ownership.streamNameSealed, sizeof(ownership.streamNameSealed));
        hash = HashTelemetryBytes(hash, &ownership.streamHandleBefore, sizeof(ownership.streamHandleBefore));
        hash = HashTelemetryBytes(hash, &ownership.streamHandleSealed, sizeof(ownership.streamHandleSealed));
        hash = HashTelemetryBytes(hash, &ownership.fileIdentity, sizeof(ownership.fileIdentity));
        return hash;
    }
}  // namespace

bool IsUpgradeModelId(DWORD dwModelID)
{
    return dwModelID >= 1000 && dwModelID <= 1193;
}

CStreamingSA::CStreamingSA()
{
    m_streamingInfo = pGame->GetStreamingInfoArray();
    m_streamingInfoCount = pGame->GetCountOfAllFileIDs();

    // Allocate the default number of archives in order to keep modded games working as before.
    SetArchivesNum(VAR_DefaultMaxArchives);

    // Copy the default data
    HANDLE(&defaultStreamingHandlers)[32] = *(HANDLE(*)[32])0x8E4010;
    SStreamName(&defaultStreamingNames)[32] = *(SStreamName(*)[32])0x8E4098;
    CArchiveInfo(&defaultAchiveInfo)[8] = *(CArchiveInfo(*)[8])0x8E48D8;

    std::memcpy(m_StreamHandles.data(), defaultStreamingHandlers, sizeof(HANDLE) * std::min(m_StreamHandles.size(), (size_t)VAR_DefaultStreamHandlersMaxCount));
    std::memcpy(m_StreamNames.data(), defaultStreamingNames, sizeof(SStreamName) * std::min(m_StreamNames.size(), (size_t)VAR_DefaultStreamHandlersMaxCount));
    std::memcpy(m_Imgs.data(), defaultAchiveInfo, sizeof(CArchiveInfo) * std::min(m_Imgs.size(), (size_t)VAR_DefaultMaxArchives));
}

void CStreamingSA::SetArchivesNum(size_t imagesNum)
{
    if (imagesNum < MIN_IMAGES_NUM || imagesNum > MAX_IMAGES_NUM)
        return;

    /*
        IMGs
    */
    if (m_Imgs.size() != imagesNum)
    {
        try
        {
            m_Imgs.resize(imagesNum);
        }
        catch (const std::bad_alloc&)
        {
            return;
        }

        const auto   pImgs = m_Imgs.data();
        const size_t uiImgsSize = sizeof(CArchiveInfo) * m_Imgs.size();

        // CStreaming::AddImageToList
        MemPutFast<DWORD>((void*)0x1567B94, (DWORD)pImgs);
        MemPutFast<DWORD>((void*)0x1567BA2, (DWORD)pImgs + uiImgsSize);
        MemPutFast<DWORD>((void*)0x1567BBA, (DWORD)pImgs);
        MemPutFast<DWORD>((void*)0x1567BD6, (DWORD)pImgs + 0x2C);
        MemPutFast<DWORD>((void*)0x1567BE3, (DWORD)pImgs + 0x28);

        // CStreaming::InitImageList
        MemPut<DWORD>((void*)0x4083C1, (DWORD)pImgs + 0x2C);
        MemPut<DWORD>((void*)0x4083DE, (DWORD)pImgs + 0x2C + uiImgsSize);
        MemPut<DWORD>((void*)0x4083E9, (DWORD)pImgs);
        MemPut<DWORD>((void*)0x4083FA, (DWORD)pImgs + uiImgsSize);
        MemPut<DWORD>((void*)0x40840B, (DWORD)pImgs);
        MemPut<DWORD>((void*)0x40841A, (DWORD)pImgs + uiImgsSize);
        MemPut<DWORD>((void*)0x40843B, (DWORD)pImgs);
        MemPut<DWORD>((void*)0x40845B, (DWORD)pImgs + 0x2C);
        MemPut<DWORD>((void*)0x408461, (DWORD)pImgs + 0x28);
        MemPut<DWORD>((void*)0x408479, (DWORD)pImgs);
        MemPut<DWORD>((void*)0x4084A2, (DWORD)pImgs + 0x2C);
        MemPut<DWORD>((void*)0x4084A8, (DWORD)pImgs + 0x28);

        // CStreaming::loadArchives
        MemPut<DWORD>((void*)0x5B82F1, (DWORD)pImgs);
        MemPut<DWORD>((void*)0x5B82FD, (DWORD)pImgs);
        MemPut<DWORD>((void*)0x5B8303, (DWORD)pImgs + uiImgsSize);

        // CStreamingInfo::GetCdPosn
        MemPut<DWORD>((void*)0x40757F, (DWORD)pImgs + 0x2C);

        // CStreaming::GetNextFileOnCd
        MemPut<DWORD>((void*)0x408FDC, (DWORD)pImgs + 0x2C);

        // CStreaming::requestSpecialModel
        MemPut<DWORD>((void*)0x409D5A, (DWORD)pImgs + 0x2C);

        // CStreaming::RequestModelStream
        MemPut<DWORD>((void*)0x40CC54, (DWORD)pImgs + 0x2C);
        MemPut<DWORD>((void*)0x40CCC7, (DWORD)pImgs + 0x2C);

        // CStreamingInfo::GetCdPosnAndSize
        MemPutFast<DWORD>((void*)0x1560E68, (DWORD)pImgs + 0x2C);

        // sub_40A080
        MemPutFast<DWORD>((void*)0x15663E7, (DWORD)pImgs + 0x2C);
    }

    /*
        Stream handles
    */
    const size_t handlesNum = Clamp((size_t)VAR_DefaultStreamHandlersMaxCount, imagesNum + RESERVED_STREAMS_NUM, MAX_STREAMS_NUM);
    if (m_StreamHandles.size() != handlesNum)
    {
        try
        {
            m_StreamHandles.resize(handlesNum);
            m_StreamNames.resize(handlesNum);
        }
        catch (const std::bad_alloc&)
        {
            return;
        }

        const auto pStreamHandles = m_StreamHandles.data();
        const auto pStreamNames = m_StreamNames.data();

        // _GetFileSizeOfTheFirstStream
        MemPutFast<DWORD>((void*)0x15700D1, (DWORD)pStreamHandles);

        // _closeAllStreams
        MemPut<DWORD>((void*)0x4066C7, (DWORD)pStreamNames);
        MemPut<DWORD>((void*)0x4066D6, (DWORD)pStreamHandles);
        MemPut<DWORD>((void*)0x4066ED, (DWORD)pStreamHandles);

        // _sub_406710
        MemPut<DWORD>((void*)0x406737, (DWORD)pStreamHandles);

        // _sub_406750
        MemPut<DWORD>((void*)0x40676C, (DWORD)pStreamNames);
        MemPut<DWORD>((void*)0x406797, (DWORD)pStreamHandles);

        // _openStream
        DWORD dwExeCodePtr = (DWORD)0x01564A94;

        MemPutFast<WORD>((void*)(dwExeCodePtr), (WORD)0x048B);  // mov     eax, _streamHandles[esi*4]
        MemPutFast<BYTE>((void*)(dwExeCodePtr + 2), (BYTE)0xB5);
        MemPutFast<DWORD>((void*)(dwExeCodePtr + 3), (DWORD)pStreamHandles);

        MemPutFast<WORD>((void*)(dwExeCodePtr + 7), (WORD)0xC085);

        MemPutFast<WORD>((void*)(dwExeCodePtr + 9), (WORD)0x840F);
        MemPutFast<DWORD>((void*)(dwExeCodePtr + 11), (DWORD)(0x01564B31 - (dwExeCodePtr + 15)));

        MemPutFast<BYTE>((void*)(dwExeCodePtr + 15), (BYTE)0x46);
        MemPutFast<WORD>((void*)(dwExeCodePtr + 16), (WORD)0xFE81);
        MemPutFast<DWORD>((void*)(dwExeCodePtr + 18), (DWORD)(handlesNum - 1));  // MAX_NUMBER_OF_STREAM_HANDLES

        MemPutFast<BYTE>((void*)(dwExeCodePtr + 22), (BYTE)0x7C);
        MemPutFast<BYTE>((void*)(dwExeCodePtr + 23), (BYTE)(dwExeCodePtr - (dwExeCodePtr + 24)));

        MemPutFast<BYTE>((void*)(dwExeCodePtr + 24), (BYTE)0xE9);
        MemPutFast<DWORD>((void*)(dwExeCodePtr + 25), (DWORD)(0x01564B31 - (dwExeCodePtr + 29)));
        // end of loop creation

        MemPutFast<DWORD>((void*)0x1564B74, (DWORD)pStreamHandles);
        MemPutFast<DWORD>((void*)0x1564B8C, (DWORD)pStreamNames);

        // _sub_4068A0
        MemPut<DWORD>((void*)0x4068AB, (DWORD)pStreamHandles);
        MemPut<DWORD>((void*)0x4068C2, (DWORD)pStreamHandles);
        MemPut<DWORD>((void*)0x4068D0, (DWORD)pStreamHandles);
        MemPut<DWORD>((void*)0x4068DD, (DWORD)pStreamNames);

        // _readStream
        MemPutFast<DWORD>((void*)0x156C2E8, (DWORD)pStreamHandles);

        // _initStreaming
        MemPut<DWORD>((void*)0x406B7C, (DWORD)pStreamHandles);
        MemPut<DWORD>((void*)0x406B81, (DWORD)pStreamNames);
        MemPut<DWORD>((void*)0x406B98, (DWORD)(pStreamNames + sizeof(SStreamName) * (handlesNum - 1)));
    }
}

void CStreamingSA::RequestModel(DWORD dwModelID, DWORD dwFlags)
{
    if (IsUpgradeModelId(dwModelID))
    {
        DWORD dwFunc = FUNC_RequestVehicleUpgrade;
        // clang-format off
        __asm
        {
            push    dwFlags
            push    dwModelID
            call    dwFunc
            add     esp, 8
        }
        // clang-format on
    }
    else
    {
        DWORD dwFunction = FUNC_CStreaming__RequestModel;
        // clang-format off
        __asm
        {
            push    dwFlags
            push    dwModelID
            call    dwFunction
            add     esp, 8
        }
        // clang-format on
    }
}

void CStreamingSA::RemoveModel(std::uint32_t model)
{
    using Signature = void(__cdecl*)(std::uint32_t);
    const auto function = reinterpret_cast<Signature>(0x4089A0);
    function(model);
}

void CStreamingSA::LoadAllRequestedModels(bool bOnlyPriorityModels, const char* szTag)
{
    TIMEUS startTime = GetTimeUs();

    DWORD dwFunction = FUNC_LoadAllRequestedModels;
    DWORD dwOnlyPriorityModels = bOnlyPriorityModels;
    // clang-format off
    __asm
    {
        push    dwOnlyPriorityModels
        call    dwFunction
        add     esp, 4
    }
    // clang-format on

    if (IS_TIMING_CHECKPOINTS())
    {
        uint deltaTimeMs = (GetTimeUs() - startTime) / 1000;
        if (deltaTimeMs > 2)
            TIMING_DETAIL(SString("LoadAllRequestedModels( %d, %s ) took %d ms", bOnlyPriorityModels, szTag, deltaTimeMs));
    }
}

bool CStreamingSA::HasModelLoaded(DWORD dwModelID)
{
    if (IsUpgradeModelId(dwModelID))
    {
        bool  bReturn;
        DWORD dwFunc = FUNC_CStreaming__HasVehicleUpgradeLoaded;
        // clang-format off
        __asm
        {
            push    dwModelID
            call    dwFunc
            add     esp, 0x4
            mov     bReturn, al
        }
        // clang-format on
        return bReturn;
    }
    else
    {
        DWORD dwFunc = FUNC_CStreaming__HasModelLoaded;
        bool  bReturn = 0;
        // clang-format off
        __asm
        {
            push    dwModelID
            call    dwFunc
            mov     bReturn, al
            pop     eax
        }
        // clang-format on

        return bReturn;
    }
}

void CStreamingSA::RequestSpecialModel(DWORD model, const char* szTexture, DWORD channel)
{
    DWORD dwFunc = FUNC_CStreaming_RequestSpecialModel;
    // clang-format off
    __asm
    {
        push    channel
        push    szTexture
        push    model
        call    dwFunc
        add     esp, 0xC
    }
    // clang-format on
}

void CStreamingSA::ReinitStreaming()
{
    typedef int(__cdecl * Function_ReInitStreaming)();
    ((Function_ReInitStreaming)(0x40E560))();
}

// ReinitStreaming should be called after this.
// Otherwise the model wont be restreamed
// TODO: Somehow restream a single model instead of the whole world
void CStreamingSA::SetStreamingInfo(uint modelid, unsigned char usStreamID, uint uiOffset, ushort usSize, uint uiNextInImg)
{
    CStreamingInfo* pItemInfo = GetStreamingInfo(modelid);
    if (!pItemInfo)
        return;

    // We remove the existing RwObject because, after switching the archive, the streamer will load a new one.
    // ReInit doesn't delete all RwObjects unless certain conditions are met.
    // In this case, we must force-remove the RwObject from memory, because it is no longer used,
    // and due to the archive change the streamer no longer detects it and therefore won't delete it.
    // As a result, a memory leak occurs after every call to engineImageLinkDFF.
    // Only DFF models have a valid CBaseModelInfoSAInterface with an RwObject.
    // TXD and other higher model IDs don't — their CModelInfoSA::m_pInterface is uninitialized.
    if (modelid < pGame->GetBaseIDforTXD())
    {
        if (CModelInfo* modelInfo = g_pCore->GetGame()->GetModelInfo(modelid); modelInfo && modelInfo->GetRwObject())
            RemoveModel(modelid);
    }

    // Change nextInImg field for prev model
    for (std::uint32_t id = 0; id < m_streamingInfoCount; ++id)
    {
        CStreamingInfo& info = m_streamingInfo[id];
        if (info.archiveId == pItemInfo->archiveId)
        {
            // Check if the block after `info` is the beginning of `pItemInfo`'s block
            if (info.offsetInBlocks + info.sizeInBlocks == pItemInfo->offsetInBlocks)
            {
                info.nextInImg = -1;
                break;
            }
        }
    }

    pItemInfo->archiveId = usStreamID;
    pItemInfo->offsetInBlocks = uiOffset;
    pItemInfo->sizeInBlocks = usSize;
    pItemInfo->nextInImg = static_cast<uint16_t>(uiNextInImg);
}

CStreamingInfo* CStreamingSA::GetStreamingInfo(uint modelid)
{
    return modelid < m_streamingInfoCount ? &m_streamingInfo[modelid] : nullptr;
}

unsigned char CStreamingSA::GetUnusedArchive()
{
    // Get internal IMG id
    // By default gta sa uses 6 of 8 IMG archives
    for (size_t i = 6; i < m_Imgs.size(); i++)
    {
        if (!m_Imgs[i].uiStreamHandleId)
            return (unsigned char)i;
    }
    return INVALID_ARCHIVE_ID;
}

unsigned char CStreamingSA::GetUnusedStreamHandle()
{
    for (size_t i = 0; i < m_StreamHandles.size(); i++)
    {
        if (!m_StreamHandles[i])
            return (unsigned char)i;
    }
    return INVALID_STREAM_ID;
}

unsigned char CStreamingSA::AddArchive(const wchar_t* szFilePath)
{
    auto ucArchiveId = GetUnusedArchive();
    if (ucArchiveId == INVALID_ARCHIVE_ID)
    {
        // Allocate some extra archives
        AllocateArchive();

        // Give it a second try
        ucArchiveId = GetUnusedArchive();
        if (ucArchiveId == INVALID_ARCHIVE_ID)
            return INVALID_ARCHIVE_ID;
    }

    // Get free stream handler id
    const auto ucStreamID = GetUnusedStreamHandle();
    if (ucStreamID == INVALID_STREAM_ID)
        return INVALID_ARCHIVE_ID;

    // Create new stream handler
    const auto streamCreateFlags = *(DWORD*)0x8E3FE0;
    HANDLE     hFile = CreateFileW(szFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                   streamCreateFlags | FILE_ATTRIBUTE_READONLY | FILE_FLAG_RANDOM_ACCESS, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return INVALID_ARCHIVE_ID;

    // Register stream handler
    m_StreamHandles[ucStreamID] = hFile;

    // Register archive data
    m_Imgs[ucArchiveId].uiStreamHandleId = (ucStreamID << 24);

    return ucArchiveId;
}

void CStreamingSA::RemoveArchive(unsigned char ucArchiveID)
{
    unsigned int uiStreamHandlerID = m_Imgs[ucArchiveID].uiStreamHandleId >> 24;
    if (!uiStreamHandlerID)
        return;

    m_Imgs[ucArchiveID].uiStreamHandleId = 0;

    CloseHandle(m_StreamHandles[uiStreamHandlerID]);
    m_StreamHandles[uiStreamHandlerID] = NULL;
}

SStreamingLifecycleTelemetrySA CStreamingSA::GetLifecycleTelemetry() const
{
    SStreamingLifecycleTelemetrySA result;
    result.archiveCapacity = m_Imgs.size();
    result.archiveLimit = MAX_IMAGES_NUM;
    result.streamHandleCapacity = m_StreamHandles.size();
    result.streamNameCapacity = m_StreamNames.size();
    result.streamTableShapeValid = m_StreamHandles.size() == m_StreamNames.size();
    result.requestedModels = *reinterpret_cast<const int*>(0x8E4CB8);
    result.priorityRequests = *reinterpret_cast<const unsigned int*>(0x8E4BA0);
    result.channelError = *reinterpret_cast<const int*>(0x8E4B90);
    result.loadingBigModel = *reinterpret_cast<const BYTE*>(0x8E4A58) != 0;
    result.memoryUsedBytes = *reinterpret_cast<const unsigned int*>(0x8E4CB4);
    result.halfBufferBlocks = ms_streamingHalfOfBufferSizeBlocks;
    result.archiveStateHash = 14695981039346656037ULL;
    result.streamHandleStateHash = 14695981039346656037ULL;

    const auto* workerStreams = *reinterpret_cast<SGtaStream* const*>(0x8E3FFC);
    result.streamTableShapeValid = result.streamTableShapeValid && workerStreams;
    if (workerStreams)
        for (size_t channelIndex = 0; channelIndex < 2; ++channelIndex)
        {
            result.busyWorkerStreams += workerStreams[channelIndex].bInUse != 0;
            result.lockedWorkerBuffers += workerStreams[channelIndex].bLocked != 0;
        }

    result.archiveStateHash = HashTelemetryBytes(result.archiveStateHash, &result.archiveCapacity, sizeof(result.archiveCapacity));
    result.streamHandleStateHash = HashTelemetryBytes(result.streamHandleStateHash, &result.streamHandleCapacity, sizeof(result.streamHandleCapacity));

    for (size_t index = 0; index < m_Imgs.size(); ++index)
    {
        const CArchiveInfo& archive = m_Imgs[index];
        result.namedArchives += archive.szName[0] != '\0';
        result.boundArchives += index < MIN_IMAGES_NUM ? archive.szName[0] != '\0' : archive.uiStreamHandleId != 0;
        result.archiveStateHash = HashTelemetryBytes(result.archiveStateHash, &archive, sizeof(archive));
    }
    const size_t streamTableEntries = std::min(m_StreamHandles.size(), m_StreamNames.size());
    for (size_t index = 0; index < streamTableEntries; ++index)
    {
        const HANDLE handle = m_StreamHandles[index];
        result.openStreamHandles += handle != nullptr && handle != INVALID_HANDLE_VALUE;
        result.streamHandleStateHash = HashTelemetryBytes(result.streamHandleStateHash, &handle, sizeof(handle));
        result.streamHandleStateHash = HashTelemetryBytes(result.streamHandleStateHash, &m_StreamNames[index], sizeof(m_StreamNames[index]));
    }
    for (size_t index = streamTableEntries; index < m_StreamHandles.size(); ++index)
    {
        const HANDLE handle = m_StreamHandles[index];
        result.openStreamHandles += handle != nullptr && handle != INVALID_HANDLE_VALUE;
        result.streamHandleStateHash = HashTelemetryBytes(result.streamHandleStateHash, &handle, sizeof(handle));
    }

    const auto* channels = reinterpret_cast<const SStreamingChannelTelemetryMirror*>(0x8E4A60);
    for (size_t channelIndex = 0; channelIndex < std::size(result.channels); ++channelIndex)
    {
        SStreamingLifecycleTelemetrySA::SChannel& target = result.channels[channelIndex];
        const SStreamingChannelTelemetryMirror&   source = channels[channelIndex];
        target.state = source.state;
        target.sectorCount = source.sectorCount;
        target.cdStreamStatus = source.cdStreamStatus;
        for (int32_t modelId : source.modelIds)
            target.modelCount += modelId >= 0;
    }
    return result;
}

bool CStreamingSA::PlanArchiveAllocations(size_t count, std::vector<SStreamingArchiveAllocationSA>& plan, std::string& error) const
{
    if (m_StreamHandles.empty() || m_StreamHandles.size() > MAX_STREAMS_NUM || m_StreamNames.size() != m_StreamHandles.size() ||
        m_Imgs.size() < MIN_IMAGES_NUM || m_Imgs.size() > MAX_IMAGES_NUM || !m_StreamHandles[0] || m_StreamHandles[0] == INVALID_HANDLE_VALUE)
    {
        error = "streaming archive foundation or reserved handle zero is invalid";
        return false;
    }

    std::vector<bool> usedArchives(m_Imgs.size());
    std::vector<bool> usedHandles(m_StreamHandles.size());
    std::vector<bool> archiveHandles(m_StreamHandles.size());
    for (size_t index = 0; index < m_Imgs.size(); ++index)
    {
        const CArchiveInfo& archive = m_Imgs[index];
        if (index < MIN_IMAGES_NUM)
        {
            // GTA reserves slots 0..5 even when the current startup phase has
            // not populated every stock descriptor yet. GetUnusedArchive
            // never returns them, so keep them unavailable to the plan while
            // accepting an entirely empty deferred stock slot.
            usedArchives[index] = true;
            if (!archive.szName[0])
            {
                if (index != DEFERRED_PLAYER_ARCHIVE_ID)
                {
                    error = "streaming archive foundation has an unnamed required stock archive";
                    return false;
                }
                if (archive.uiStreamHandleId != 0)
                {
                    error = "streaming archive foundation has a deferred player archive with a bound handle";
                    return false;
                }
                continue;
            }
        }
        else
            usedArchives[index] = archive.uiStreamHandleId != 0;
        if (!usedArchives[index])
            continue;
        const unsigned int encodedHandle = archive.uiStreamHandleId;
        const size_t       handleId = encodedHandle >> 24;
        if ((encodedHandle & 0x00FFFFFFU) != 0 || handleId >= m_StreamHandles.size() || !m_StreamHandles[handleId] ||
            m_StreamHandles[handleId] == INVALID_HANDLE_VALUE)
        {
            error = "streaming archive foundation references an invalid stream handle";
            return false;
        }
        if (archiveHandles[handleId])
        {
            error = "streaming archive foundation aliases a stream handle between stock archives";
            return false;
        }
        if (archive.szName[0])
        {
            const auto archiveNameEnd = std::find(std::begin(archive.szName), std::end(archive.szName), '\0');
            const auto streamNameEnd = std::find(std::begin(m_StreamNames[handleId].szName), std::end(m_StreamNames[handleId].szName), '\0');
            if (archiveNameEnd == std::end(archive.szName) || streamNameEnd == std::end(m_StreamNames[handleId].szName))
            {
                error = "streaming archive foundation contains an unterminated name";
                return false;
            }
            const size_t archiveNameLength = std::distance(std::begin(archive.szName), archiveNameEnd);
            const size_t streamNameLength = std::distance(std::begin(m_StreamNames[handleId].szName), streamNameEnd);
            if (archiveNameLength != streamNameLength || _strnicmp(archive.szName, m_StreamNames[handleId].szName, archiveNameLength) != 0)
            {
                error = "streaming archive foundation descriptor and stream names differ";
                return false;
            }
        }
        archiveHandles[handleId] = true;
    }
    for (size_t index = 0; index < m_StreamHandles.size(); ++index)
        // GetUnusedStreamHandle only treats a null entry as reusable. Mirror
        // that rule exactly so an invalid-but-owned handle cannot shift every
        // subsequent archive prediction away from GTA's allocator.
        usedHandles[index] = m_StreamHandles[index] != nullptr;

    plan.clear();
    plan.reserve(count);
    for (size_t addition = 0; addition < count; ++addition)
    {
        size_t archiveId = usedArchives.size();
        for (size_t index = MIN_IMAGES_NUM; index < usedArchives.size(); ++index)
            if (!usedArchives[index])
            {
                archiveId = index;
                break;
            }
        if (archiveId == usedArchives.size())
        {
            const size_t grownSize = std::min(usedArchives.size() + usedArchives.size() * 2 + 1, MAX_IMAGES_NUM);
            if (grownSize == usedArchives.size())
            {
                error = "streaming archive allocation plan exhausted the archive table";
                return false;
            }
            usedArchives.resize(grownSize);
            const size_t grownHandleCount = Clamp(static_cast<size_t>(VAR_DefaultStreamHandlersMaxCount), grownSize + RESERVED_STREAMS_NUM, MAX_STREAMS_NUM);
            usedHandles.resize(grownHandleCount);
            archiveId = std::max(static_cast<size_t>(MIN_IMAGES_NUM), m_Imgs.size());
            while (archiveId < usedArchives.size() && usedArchives[archiveId])
                ++archiveId;
            if (archiveId == usedArchives.size())
            {
                error = "streaming archive allocation growth produced no usable slot";
                return false;
            }
        }

        size_t streamHandleId = 0;
        while (streamHandleId < usedHandles.size() && usedHandles[streamHandleId])
            ++streamHandleId;
        if (streamHandleId == 0 || streamHandleId >= usedHandles.size())
        {
            error = "streaming archive allocation plan exhausted safe stream handles";
            return false;
        }
        usedArchives[archiveId] = true;
        usedHandles[streamHandleId] = true;
        plan.push_back({static_cast<unsigned char>(archiveId), static_cast<unsigned char>(streamHandleId)});
    }
    return true;
}

bool CStreamingSA::ArchiveMatchesAllocation(const SStreamingArchiveAllocationSA& allocation) const
{
    return allocation.archiveId < m_Imgs.size() && allocation.streamHandleId < m_StreamHandles.size() && allocation.streamHandleId != 0 &&
           m_Imgs[allocation.archiveId].uiStreamHandleId == static_cast<unsigned int>(allocation.streamHandleId) << 24 &&
           m_StreamHandles[allocation.streamHandleId] != nullptr && m_StreamHandles[allocation.streamHandleId] != INVALID_HANDLE_VALUE;
}

bool CStreamingSA::CaptureArchiveAllocationOwnership(const SStreamingArchiveAllocationSA& allocation, SStreamingArchiveOwnershipSA& ownership,
                                                     std::string& error) const
{
    if (allocation.archiveId < MIN_IMAGES_NUM || allocation.archiveId >= MAX_IMAGES_NUM || allocation.archiveId > m_Imgs.size() ||
        allocation.streamHandleId == 0 || allocation.streamHandleId >= MAX_STREAMS_NUM || allocation.streamHandleId > m_StreamHandles.size() ||
        m_Imgs.size() < MIN_IMAGES_NUM || m_Imgs.size() > MAX_IMAGES_NUM || m_StreamHandles.size() != m_StreamNames.size() ||
        m_StreamHandles.size() > MAX_STREAMS_NUM)
    {
        error = "streaming archive ownership capture has an invalid allocation or table foundation";
        return false;
    }

    ownership = {};
    ownership.allocation = allocation;
    ownership.capacityBefore = {m_Imgs.size(), m_StreamHandles.size(), m_StreamNames.size()};
    if (allocation.archiveId < m_Imgs.size())
    {
        ownership.archiveBefore = m_Imgs[allocation.archiveId];
        if (ownership.archiveBefore.uiStreamHandleId != 0)
        {
            error = SString("streaming archive ownership capture found occupied archive slot %u", allocation.archiveId);
            return false;
        }
    }
    if (allocation.streamHandleId < m_StreamHandles.size())
    {
        ownership.streamHandleBefore = m_StreamHandles[allocation.streamHandleId];
        ownership.streamNameBefore = m_StreamNames[allocation.streamHandleId];
        if (ownership.streamHandleBefore != nullptr)
        {
            error = SString("streaming archive ownership capture found occupied handle slot %u", allocation.streamHandleId);
            return false;
        }
    }

    const unsigned int encodedHandle = static_cast<unsigned int>(allocation.streamHandleId) << 24;
    for (const CArchiveInfo& archive : m_Imgs)
        if (archive.uiStreamHandleId == encodedHandle)
        {
            error = SString("streaming archive ownership capture found an existing descriptor for handle slot %u", allocation.streamHandleId);
            return false;
        }
    ownership.sealHash = HashArchiveOwnership(ownership);
    return true;
}

bool CStreamingSA::SealArchiveAllocationOwnership(SStreamingArchiveOwnershipSA& ownership, std::string& error) const
{
    const SStreamingArchiveAllocationSA& allocation = ownership.allocation;
    if (ownership.sealed || ownership.sealHash != HashArchiveOwnership(ownership) || allocation.archiveId < MIN_IMAGES_NUM ||
        allocation.archiveId >= MAX_IMAGES_NUM || allocation.streamHandleId == 0 || allocation.streamHandleId >= MAX_STREAMS_NUM ||
        ownership.archiveBefore.uiStreamHandleId != 0 || ownership.streamHandleBefore != nullptr || ownership.capacityBefore.archiveCapacity < MIN_IMAGES_NUM ||
        ownership.capacityBefore.streamHandleCapacity != ownership.capacityBefore.streamNameCapacity ||
        ownership.capacityBefore.archiveCapacity > MAX_IMAGES_NUM || ownership.capacityBefore.streamHandleCapacity > MAX_STREAMS_NUM)
    {
        error = "streaming archive ownership seal has an invalid capture";
        return false;
    }

    size_t expectedArchiveCapacity = ownership.capacityBefore.archiveCapacity;
    if (allocation.archiveId == ownership.capacityBefore.archiveCapacity)
        expectedArchiveCapacity = std::min(expectedArchiveCapacity + expectedArchiveCapacity * 2 + 1, MAX_IMAGES_NUM);
    else if (allocation.archiveId > ownership.capacityBefore.archiveCapacity)
    {
        error = "streaming archive ownership seal skipped the next archive growth boundary";
        return false;
    }
    const size_t expectedHandleCapacity =
        Clamp(static_cast<size_t>(VAR_DefaultStreamHandlersMaxCount), expectedArchiveCapacity + RESERVED_STREAMS_NUM, MAX_STREAMS_NUM);
    if (m_Imgs.size() != expectedArchiveCapacity || m_StreamHandles.size() != expectedHandleCapacity || m_StreamNames.size() != expectedHandleCapacity ||
        allocation.archiveId >= m_Imgs.size() || allocation.streamHandleId >= m_StreamHandles.size())
    {
        error = "streaming archive ownership seal observed unexpected table growth";
        return false;
    }

    CArchiveInfo expectedArchive = ownership.archiveBefore;
    expectedArchive.uiStreamHandleId = static_cast<unsigned int>(allocation.streamHandleId) << 24;
    const CArchiveInfo& archive = m_Imgs[allocation.archiveId];
    const HANDLE        handle = m_StreamHandles[allocation.streamHandleId];
    const SStreamName&  streamName = m_StreamNames[allocation.streamHandleId];
    if (!ArchiveInfoEqual(archive, expectedArchive) || handle == nullptr || handle == INVALID_HANDLE_VALUE ||
        !StreamNameEqual(streamName, ownership.streamNameBefore))
    {
        error = "streaming archive allocation differs from its captured descriptor, handle, or name slots";
        return false;
    }

    const unsigned int encodedHandle = static_cast<unsigned int>(allocation.streamHandleId) << 24;
    for (size_t archiveId = 0; archiveId < m_Imgs.size(); ++archiveId)
        if (archiveId != allocation.archiveId && m_Imgs[archiveId].uiStreamHandleId == encodedHandle)
        {
            error = "streaming archive allocation aliases its handle from another descriptor";
            return false;
        }

    SStreamingArchiveFileIdentitySA identity;
    if (!CaptureStreamingFileIdentity(handle, identity, error))
        return false;

    ownership.capacitySealed = {m_Imgs.size(), m_StreamHandles.size(), m_StreamNames.size()};
    ownership.archiveSealed = archive;
    ownership.streamNameSealed = streamName;
    ownership.streamHandleSealed = handle;
    ownership.fileIdentity = identity;
    ownership.sealHash = HashArchiveOwnership(ownership);
    ownership.sealed = true;
    return true;
}

bool CStreamingSA::ValidateArchiveAllocationOwnership(const SStreamingArchiveOwnershipSA& ownership, std::string& error) const
{
    const SStreamingArchiveAllocationSA& allocation = ownership.allocation;
    if (!ownership.sealed || ownership.sealHash != HashArchiveOwnership(ownership) || allocation.archiveId < MIN_IMAGES_NUM ||
        allocation.archiveId >= m_Imgs.size() || allocation.streamHandleId == 0 || allocation.streamHandleId >= m_StreamHandles.size() ||
        m_Imgs.size() > MAX_IMAGES_NUM || m_StreamHandles.size() != m_StreamNames.size() || m_StreamHandles.size() > MAX_STREAMS_NUM ||
        ownership.capacitySealed.streamHandleCapacity != ownership.capacitySealed.streamNameCapacity ||
        ownership.capacitySealed.archiveCapacity > m_Imgs.size() || ownership.capacitySealed.streamHandleCapacity > m_StreamHandles.size() ||
        ownership.capacitySealed.streamNameCapacity > m_StreamNames.size())
    {
        error = "streaming archive ownership validation has an invalid seal or table capacity";
        return false;
    }
    if (!ArchiveInfoEqual(m_Imgs[allocation.archiveId], ownership.archiveSealed) ||
        m_StreamHandles[allocation.streamHandleId] != ownership.streamHandleSealed ||
        !StreamNameEqual(m_StreamNames[allocation.streamHandleId], ownership.streamNameSealed))
    {
        error = SString("streaming archive ownership drifted for archive slot %u", allocation.archiveId);
        return false;
    }

    const unsigned int encodedHandle = static_cast<unsigned int>(allocation.streamHandleId) << 24;
    if (ownership.archiveSealed.uiStreamHandleId != encodedHandle || ownership.streamHandleSealed == nullptr ||
        ownership.streamHandleSealed == INVALID_HANDLE_VALUE)
    {
        error = "streaming archive ownership seal contains an invalid handle binding";
        return false;
    }
    for (size_t archiveId = 0; archiveId < m_Imgs.size(); ++archiveId)
        if (archiveId != allocation.archiveId && m_Imgs[archiveId].uiStreamHandleId == encodedHandle)
        {
            error = "streaming archive ownership handle became aliased";
            return false;
        }
    for (size_t streamHandleId = 0; streamHandleId < m_StreamHandles.size(); ++streamHandleId)
        if (streamHandleId != allocation.streamHandleId && m_StreamHandles[streamHandleId] == ownership.streamHandleSealed)
        {
            error = "streaming archive ownership OS handle became aliased";
            return false;
        }

    SStreamingArchiveFileIdentitySA identity;
    if (!CaptureStreamingFileIdentity(ownership.streamHandleSealed, identity, error))
        return false;
    if (!StreamingFileIdentityEqual(identity, ownership.fileIdentity))
    {
        error = "streaming archive ownership handle now references a different file";
        return false;
    }
    return true;
}

bool CStreamingSA::ValidateArchiveAllocationOwnershipBatch(const std::vector<SStreamingArchiveOwnershipSA>& ownerships, std::string& error) const
{
    std::array<bool, MAX_IMAGES_NUM>  archives{};
    std::array<bool, MAX_STREAMS_NUM> handles{};
    for (size_t index = 0; index < ownerships.size(); ++index)
    {
        const SStreamingArchiveOwnershipSA& ownership = ownerships[index];
        if (!ValidateArchiveAllocationOwnership(ownership, error))
            return false;
        if (archives[ownership.allocation.archiveId] || handles[ownership.allocation.streamHandleId])
        {
            error = "streaming archive ownership batch contains a duplicate archive or handle slot";
            return false;
        }
        archives[ownership.allocation.archiveId] = true;
        handles[ownership.allocation.streamHandleId] = true;
        if (index != 0 && !ArchiveCapacitiesEqual(ownership.capacityBefore, ownerships[index - 1].capacitySealed))
        {
            error = "streaming archive ownership batch was not sealed in allocation order";
            return false;
        }
    }
    return true;
}

bool CStreamingSA::RemoveArchiveAllocationsCheckedReverse(const std::vector<SStreamingArchiveOwnershipSA>& ownerships, std::string& error)
{
    if (!ValidateArchiveAllocationOwnershipBatch(ownerships, error))
        return false;

    // Capacity growth relocates GTA's archive pointers and is intentionally a
    // process-lifetime foundation. Teardown restores every retained slot but
    // never shrinks these vectors back through live executable operands.
    const SStreamingArchiveCapacitySA retainedCapacity{m_Imgs.size(), m_StreamHandles.size(), m_StreamNames.size()};
    for (auto current = ownerships.rbegin(); current != ownerships.rend(); ++current)
    {
        const unsigned int archiveId = current->allocation.archiveId;
        const unsigned int streamHandleId = current->allocation.streamHandleId;

        // Stop future GTA lookups before crossing the irreversible OS-handle
        // barrier. The caller must already have proved that channels are idle
        // and no CStreamingInfo still references this archive.
        m_Imgs[archiveId] = current->archiveBefore;
        if (!CloseHandle(current->streamHandleSealed))
        {
            m_Imgs[archiveId] = current->archiveSealed;
            error = SString("fatal partial streaming archive teardown failed to close handle slot %u", streamHandleId);
            return false;
        }
        m_StreamHandles[streamHandleId] = current->streamHandleBefore;
        m_StreamNames[streamHandleId] = current->streamNameBefore;
    }

    const SStreamingArchiveCapacitySA finalCapacity{m_Imgs.size(), m_StreamHandles.size(), m_StreamNames.size()};
    if (!ArchiveCapacitiesEqual(finalCapacity, retainedCapacity))
    {
        error = "fatal partial streaming archive teardown changed retained table capacity";
        return false;
    }
    for (const SStreamingArchiveOwnershipSA& ownership : ownerships)
    {
        const unsigned int archiveId = ownership.allocation.archiveId;
        const unsigned int streamHandleId = ownership.allocation.streamHandleId;
        if (!ArchiveInfoEqual(m_Imgs[archiveId], ownership.archiveBefore) || m_StreamHandles[streamHandleId] != ownership.streamHandleBefore ||
            !StreamNameEqual(m_StreamNames[streamHandleId], ownership.streamNameBefore))
        {
            error = "fatal partial streaming archive teardown failed its slot restoration postcondition";
            return false;
        }
    }
    return true;
}

bool CStreamingSA::SetStreamingBufferSize(uint32 numBlocks)
{
    const uint32 requestedNumBlocks = numBlocks;
    const uint32 requiredNumBlocks = CNativeWorldPackManagerSA::GetRequiredStreamingBufferSizeBlocks();
    numBlocks = std::max(numBlocks, requiredNumBlocks);
    numBlocks += numBlocks % 2;  // Make sure number is even by "rounding" it upwards. [Otherwise it can't be split in half properly]

    if (numBlocks != requestedNumBlocks && requiredNumBlocks > requestedNumBlocks)
    {
        // Script-managed IMG bookkeeping cannot see native world-pack archives.
        // Disconnect cleanup must not shrink this buffer below their largest
        // entry or GTA repeatedly retries impossible reads.
        CNativeWorldPackManagerSA::LogStreamingBufferClamp(requestedNumBlocks, numBlocks, requiredNumBlocks);
    }

    // Check if the size is the same already
    if (numBlocks == ms_streamingHalfOfBufferSizeBlocks * 2)
        return true;

    if (ms_pStreamingBuffer[0] == nullptr || ms_pStreamingBuffer[1] == nullptr)
        return false;

    // First of all, allocate the new buffer
    // NOTE: Due to a bug in the `MallocAlign` code the function will just *crash* instead of returning nullptr on alloc. failure :D
    typedef void*(__cdecl * Function_CMemoryMgr_MallocAlign)(uint32 uiCount, uint32 uiAlign);
    void* pNewBuffer = ((Function_CMemoryMgr_MallocAlign)(0x72F4C0))(numBlocks * 2048, 2048);
    if (!pNewBuffer)  // ...so this code is useless for now
        return false;

    int pointer = *(int*)0x8E3FFC;
    SGtaStream(&streaming)[5] = *(SGtaStream(*)[5])(pointer);

    // Wait while streaming thread ends tasks
    while (streaming[0].bInUse || streaming[1].bInUse)
        ;

    // Suspend streaming thread [otherwise data might become corrupted]
    SuspendThread(*phStreamingThread);

    // Calculate new buffer pointers
    void* const pNewBuff0 = pNewBuffer;
    void* const pNewBuff1 = (void*)(reinterpret_cast<uintptr_t>(pNewBuffer) + 2048u * (numBlocks / 2));

    // Copy data from old buffer to new buffer
    const auto copySizeBytes = std::min(ms_streamingHalfOfBufferSizeBlocks, numBlocks / 2) * 2048;
    MemCpyFast(pNewBuff0, ms_pStreamingBuffer[0], copySizeBytes);
    MemCpyFast(pNewBuff1, ms_pStreamingBuffer[1], copySizeBytes);

    // Now, we can deallocate the old buffer safely
    typedef void(__cdecl * Function_CMemoryMgr_FreeAlign)(void* pos);
    ((Function_CMemoryMgr_FreeAlign)(0x72F4F0))(ms_pStreamingBuffer[0]);

    // Update the buffer size now
    ms_streamingHalfOfBufferSizeBlocks = numBlocks / 2;

    // Update internal pointers too
    streaming[0].pBuffer = ms_pStreamingBuffer[0] = pNewBuff0;
    streaming[1].pBuffer = ms_pStreamingBuffer[1] = pNewBuff1;

    // Now we can resume streaming
    ResumeThread(*phStreamingThread);

    return true;
}

void CStreamingSA::MakeSpaceFor(std::uint32_t memoryToCleanInBytes)
{
    (reinterpret_cast<void(__cdecl*)(std::uint32_t)>(0x40E120))(memoryToCleanInBytes);
}

std::uint32_t CStreamingSA::GetMemoryUsed() const
{
    return *reinterpret_cast<std::uint32_t*>(0x8E4CB4);
}

void CStreamingSA::AllocateArchive()
{
    // Preallocate some extra archives to skip this procedure for the next several archives
    const size_t archivesNum = std::min(m_Imgs.size() + m_Imgs.size() * 2 + 1, MAX_IMAGES_NUM);
    SetArchivesNum(archivesNum);
}

void CStreamingSA::RemoveBigBuildings()
{
    (reinterpret_cast<void(__cdecl*)()>(0x4093B0))();
}

void CStreamingSA::LoadScene(const CVector* position)
{
    if (position)
        CNativeWorldPackManagerSA::PrepareStreamingAtPosition(*position);
    auto CStreaming_LoadScene = (void(__cdecl*)(const CVector*))FUNC_CStreaming_LoadScene;
    CStreaming_LoadScene(position);
}

void CStreamingSA::LoadSceneCollision(const CVector* position)
{
    if (position)
        CNativeWorldPackManagerSA::PrepareStreamingAtPosition(*position);
    auto CStreaming_LoadSceneCollision = (void(__cdecl*)(const CVector*))FUNC_CStreaming_LoadSceneCollision;
    CStreaming_LoadSceneCollision(position);
}

void CStreamingSA::LoadSceneInDirection(const CVector* position, float headingDegrees)
{
    constexpr float DEGREES_TO_RADIANS = 0.017453292519943295769f;
    constexpr int   STREAMING_LOADING_SCENE = 0x20;

    // Opcode 0A0B stops GTA's timer around the blocking scene load and feeds a
    // radians heading into CRenderer's directional frustum request first.
    // Keeping those calls together prevents Lua consumers from accidentally
    // reproducing only the ordinary, non-directional LoadScene half.
    if (position)
        CNativeWorldPackManagerSA::PrepareStreamingAtPosition(*position);
    (reinterpret_cast<void(__cdecl*)()>(FUNC_CTimer_Stop))();
    (reinterpret_cast<void(__cdecl*)(const CVector*, float, int)>(FUNC_CRenderer_RequestObjectsInDirection))(position, headingDegrees * DEGREES_TO_RADIANS,
                                                                                                             STREAMING_LOADING_SCENE);
    LoadScene(position);
    (reinterpret_cast<void(__cdecl*)()>(FUNC_CTimer_Update))();
}
