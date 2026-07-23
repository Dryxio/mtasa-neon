/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/packets/CResourceStartPacket.cpp
 *  PURPOSE:     Resource start packet class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CResourceStartPacket.h"
#include "CResourceClientScriptItem.h"
#include "CResourceClientFileItem.h"
#include "CResourceScriptItem.h"
#include "CChecksum.h"
#include "CResource.h"
#include "CDummy.h"

CResourceStartPacket::CResourceStartPacket(const std::string& resourceName, CResource* resource, unsigned int startCounter)
    : m_strResourceName{resourceName}, m_pResource{resource}, m_startCounter{startCounter}
{
}

bool CResourceStartPacket::Write(NetBitStreamInterface& BitStream) const
{
    if (m_strResourceName.empty())
        return false;

    const SNativeWorldPackTransport& nativeWorldPack = m_pResource->GetNativeWorldPackTransport();
    if (nativeWorldPack.present && !m_pResource->IsClientFilesOn())
        return false;
    if (m_pResource->RequiresNativeWorldV3SetStartupCapability() && !BitStream.Can(eBitStreamVersion::NativeWorldStaticWorldV3StartupAuthorization))
    {
        // This tuple is a closed gate, not an optional transport hint. Omitting
        // it would let an old client start the coordinator as an ordinary
        // resource after the server had authorized an aggregate launch.
        return false;
    }

    // Write the resource name
    unsigned char sizeResourceName = static_cast<unsigned char>(m_strResourceName.size());
    BitStream.Write(sizeResourceName);
    if (sizeResourceName > 0)
    {
        BitStream.Write(m_strResourceName.c_str(), sizeResourceName);
    }

    // Write the start counter
    BitStream.Write(m_startCounter);

    // Write the resource id
    BitStream.Write(m_pResource->GetNetID());

    // Write the resource element id
    BitStream.Write(m_pResource->GetResourceRootElement()->GetID());

    // Write the resource dynamic element id
    BitStream.Write(m_pResource->GetDynamicElementRoot()->GetID());

    // Count the amount of 'no client cache' scripts
    unsigned short usNoClientCacheScriptCount = 0;
    if (m_pResource->IsClientScriptsOn())
    {
        for (CResourceFile* resourceFile : m_pResource->GetFiles())
        {
            if (resourceFile->GetType() == CResourceScriptItem::RESOURCE_FILE_TYPE_CLIENT_SCRIPT &&
                static_cast<CResourceClientScriptItem*>(resourceFile)->IsNoClientCache())
            {
                ++usNoClientCacheScriptCount;
            }
        }
    }
    BitStream.Write(usNoClientCacheScriptCount);

    // Write the declared min client version for this resource
    BitStream.WriteString(m_pResource->GetMinServerRequirement());
    BitStream.WriteString(m_pResource->GetMinClientRequirement());
    BitStream.WriteBit(m_pResource->IsOOPEnabledInMetaXml());
    BitStream.Write(m_pResource->GetDownloadPriorityGroup());

    const auto isNativeWorldFile = [&nativeWorldPack](const CResourceFile* resourceFile)
    { return nativeWorldPack.present && std::find(nativeWorldPack.files.begin(), nativeWorldPack.files.end(), resourceFile) != nativeWorldPack.files.end(); };

    const auto writeResourceFile = [this, &BitStream](CResourceFile* resourceFile)
    {
        if ((resourceFile->GetType() == CResourceScriptItem::RESOURCE_FILE_TYPE_CLIENT_CONFIG && m_pResource->IsClientConfigsOn()) ||
            (resourceFile->GetType() == CResourceScriptItem::RESOURCE_FILE_TYPE_CLIENT_SCRIPT && m_pResource->IsClientScriptsOn() &&
             static_cast<CResourceClientScriptItem*>(resourceFile)->IsNoClientCache() == false) ||
            (resourceFile->GetType() == CResourceScriptItem::RESOURCE_FILE_TYPE_CLIENT_FILE && m_pResource->IsClientFilesOn()))
        {
            // Write the Type of chunk to read (F - File, E - Exported Function)
            BitStream.Write(static_cast<unsigned char>('F'));

            // Write the map name
            const char* szFileName = resourceFile->GetWindowsName();
            size_t      sizeFileName = strlen(szFileName);

            // Make sure we don't have any backslashes in the name
            char* szCleanedFilename = new char[sizeFileName + 1];
            strcpy(szCleanedFilename, szFileName);
            for (unsigned int i = 0; i < sizeFileName; i++)
            {
                if (szCleanedFilename[i] == '\\')
                    szCleanedFilename[i] = '/';
            }

            BitStream.Write(static_cast<unsigned char>(sizeFileName));
            if (sizeFileName > 0)
            {
                BitStream.Write(szCleanedFilename, sizeFileName);
            }

            // ChrML: Don't forget this...
            delete[] szCleanedFilename;

            BitStream.Write(static_cast<unsigned char>(resourceFile->GetType()));
            CChecksum checksum = resourceFile->GetLastChecksum();
            BitStream.Write(checksum.ulCRC);
            BitStream.Write((const char*)checksum.md5.data, sizeof(checksum.md5.data));
            BitStream.Write((double)resourceFile->GetSizeHint());  // Has to be double for bitstream format compatibility
            if (resourceFile->GetType() == CResourceScriptItem::RESOURCE_FILE_TYPE_CLIENT_FILE)
            {
                CResourceClientFileItem* pRCFItem = reinterpret_cast<CResourceClientFileItem*>(resourceFile);
                // write bool whether to download or not
                BitStream.WriteBit(pRCFItem->IsAutoDownload());
            }
        }
    };

    // Each format requires its exact capability. Tagged files remain omitted
    // below when that capability is absent, so an older client cannot mistake
    // a generic pack for ordinary resource content. The legacy N layout stays
    // byte-for-byte unchanged.
    const bool canWriteNativeWorldPack = nativeWorldPack.format == 1 ? BitStream.Can(eBitStreamVersion::NativeWorldPackTransport)
                                         : nativeWorldPack.format == 2
                                             ? BitStream.Can(eBitStreamVersion::NativeWorldStaticWorldV2Transport)
                                             : nativeWorldPack.format == 3 && BitStream.Can(eBitStreamVersion::NativeWorldStaticWorldV3LodTransport);
    if (nativeWorldPack.present && m_pResource->IsClientFilesOn() && canWriteNativeWorldPack)
    {
        // Legacy formats retain their publish-only downgrade. The format 3 set
        // was rejected above unless its exact authorization tuple is writable.
        const bool writeStartupAuthorization =
            nativeWorldPack.startupAuthorization &&
            ((nativeWorldPack.format == 1 && BitStream.Can(eBitStreamVersion::NativeWorldStartupAuthorization)) ||
             (nativeWorldPack.format == 2 && BitStream.Can(eBitStreamVersion::NativeWorldStaticWorldV2StartupAuthorization)) ||
             (nativeWorldPack.format == 3 && BitStream.Can(eBitStreamVersion::NativeWorldStaticWorldV3StartupAuthorization)));
        BitStream.Write(static_cast<unsigned char>(writeStartupAuthorization ? 'A' : 'N'));
        BitStream.Write(nativeWorldPack.format);
        BitStream.Write(static_cast<unsigned char>(nativeWorldPack.files.size()));
        BitStream.Write(static_cast<unsigned char>(nativeWorldPack.manifestPath.size()));
        BitStream.Write(nativeWorldPack.manifestPath.c_str(), static_cast<int>(nativeWorldPack.manifestPath.size()));

        if (writeStartupAuthorization)
        {
            BitStream.Write(nativeWorldPack.authorizationVersion);
            BitStream.Write(static_cast<unsigned char>(1));  // one-shot startup mode
            BitStream.Write(nativeWorldPack.authorizationPolicy);
        }

        for (CResourceFile* resourceFile : nativeWorldPack.files)
            writeResourceFile(resourceFile);
    }

    // Send ordinary resource files after the engine-owned native world group. Tagged files are omitted here for both capable and legacy clients.
    for (CResourceFile* resourceFile : m_pResource->GetFiles())
    {
        if (!isNativeWorldFile(resourceFile))
            writeResourceFile(resourceFile);
    }

    // Loop through the exported functions
    std::list<CExportedFunction>::iterator iterExportedFunction = m_pResource->IterBeginExportedFunctions();
    for (; iterExportedFunction != m_pResource->IterEndExportedFunctions(); iterExportedFunction++)
    {
        // Check to see if the exported function is 'client'
        if (iterExportedFunction->GetType() == CExportedFunction::EXPORTED_FUNCTION_TYPE_CLIENT)
        {
            // Write the Type of chunk to read (F - File, E - Exported Function)
            BitStream.Write(static_cast<unsigned char>('E'));

            // Write the exported function
            std::string strFunctionName = iterExportedFunction->GetFunctionName();
            size_t      sizeFunctionName = strFunctionName.length();

            BitStream.Write(static_cast<unsigned char>(sizeFunctionName));
            if (sizeFunctionName > 0)
            {
                BitStream.Write(strFunctionName.c_str(), sizeFunctionName);
            }
        }
    }

    return true;
}
