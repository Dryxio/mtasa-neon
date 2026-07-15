/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeWorldPackSA.h
 *  PURPOSE:     Native GTA streaming registration for reviewed world packs
 *
 *****************************************************************************/

#pragma once

class CStreamingSA;

struct SNativeTxdSlotFingerprintSA
{
    bool           configured;
    unsigned char  poolFlag;
    unsigned int   dictionary;
    unsigned short usages;
    unsigned short parent;
    unsigned int   hash;
    unsigned short prev;
    unsigned short next;
    unsigned short nextInImg;
    unsigned char  streamingFlags;
    unsigned char  archive;
    unsigned int   offset;
    unsigned int   size;
    unsigned int   loadState;
};

struct SNativeTxdPoolProfileSA
{
    const char*                 executableIdentity;
    const char*                 name;
    unsigned int                occupied;
    int                         firstFree;
    int                         fingerprintSlot;
    SNativeTxdSlotFingerprintSA fingerprint;
};

struct SNativeModelStoreUsageSA
{
    unsigned int atomic;
    unsigned int damageAtomic;
    unsigned int time;
};

struct SNativeWorldPackDescriptorSA
{
    const char* key;
    const char* displayName;
    const char* logPrefix;
    const char* featureEnvironment;
    const char* relativeDirectory;
    const char* ideFileName;
    const char* imgFileName;
    const char* colFileName;
    const char* ideSha256;
    const char* imgSha256;

    unsigned int       modelFirst;
    unsigned int       modelLast;
    unsigned int       modelCount;
    unsigned int       txdCount;
    unsigned int       txdPoolCapacity;
    unsigned int       stockColOccupied;
    unsigned int       colPoolCapacity;
    unsigned int       stockIplOccupied;
    unsigned int       iplPoolCapacity;
    const char* const* iplNames;
    unsigned int       iplCount;
    unsigned int       imgEntryCount;
    unsigned int       imgSectorCount;
    unsigned int       largestImgEntryBlocks;
    unsigned char      expectedArchiveId;

    SNativeModelStoreUsageSA       stockModelStores;
    SNativeModelStoreUsageSA       addedModelStores;
    const SNativeTxdPoolProfileSA* txdPoolProfiles;
    unsigned int                   txdPoolProfileCount;
};

class CNativeWorldPackManagerSA
{
public:
    // Installs only the startup call hook. The pack itself is validated and
    // registered after GTA has loaded all stock CD directories.
    static void InstallFromEnvironment(CStreamingSA* streaming);

    // Returns zero unless the native pack completed registration. Once active,
    // the pack remains registered for the GTA process lifetime, including MTA
    // disconnect/reconnect cycles.
    static unsigned int GetRequiredStreamingBufferSizeBlocks();

    // Keeps lifecycle-sensitive diagnostics in one place and prefixes them
    // with the active descriptor's stable log tag.
    static void LogStreamingBufferClamp(unsigned int requestedBlocks, unsigned int effectiveBlocks, unsigned int requiredBlocks);
};
