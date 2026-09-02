/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        sdk/CChecksum.h
 *  PURPOSE:     Checksum class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/
// Note: Cannot use #pragma once here, due to a duplicate existing in publicsdk
#ifndef __CChecksum_H
#define __CChecksum_H

#include <variant>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include "SharedUtil.Hash.h"
#include "SharedUtil.File.h"
#include "SString.h"
#include <bochs_internal/bochs_crc32.h>

#if defined(_WIN32) && defined(MTA_CLIENT)
    #include <unordered_map>
    #include <mutex>
#endif

// Depends on CMD5Hasher and CRCGenerator
class CChecksum
{
public:
    enum class CachePolicy
    {
        UseCache,
        BypassAndRefresh,
    };

    CChecksum() : ulCRC(0), md5{} {}

    // Comparison operators
    bool operator==(const CChecksum& other) const { return ulCRC == other.ulCRC && memcmp(md5.data, other.md5.data, sizeof(md5.data)) == 0; }

    bool operator!=(const CChecksum& other) const { return !operator==(other); }

#if defined(_WIN32) && defined(MTA_CLIENT)
private:
    struct CacheEntry
    {
        std::uint64_t size, mtime, changeTime, fileId;
        std::uint32_t volumeSerial;
        unsigned long crc;
        MD5           md5;
        std::uint64_t lastAccess;
    };

    struct FileMetadata
    {
        std::uint64_t size{};
        std::uint64_t mtime{};
        std::uint64_t changeTime{};
        std::uint64_t fileId{};
        std::uint32_t volumeSerial{};
        bool          strong{};
    };

    using DirectoryMetadataSnapshot = std::unordered_map<std::string, FileMetadata>;

    struct DirectoryEnumerationEntry
    {
        WString      filename;
        FileMetadata metadata;
    };

    struct DirectoryEnumerationParams
    {
        WString                                pattern;
        std::vector<DirectoryEnumerationEntry> entries;
        bool                                   success{};
        std::atomic<int>                       state{};
    };

    // This cache deliberately survives resource-manager teardown so reconnects
    // can reuse verified files. Keep it bounded because server-controlled
    // manifests can otherwise grow process memory for the lifetime of MTA.
    // The cache must be able to hold a complete large-server manifest. A cap
    // below that working set causes a sequential reconnect to evict every
    // useful entry before it can be reused.
    static constexpr std::size_t   MAX_CHECKSUM_CACHE_ENTRIES = 131072;
    static constexpr std::size_t   MAX_PERSISTENT_CACHE_BYTES = 64 * 1024 * 1024;
    static constexpr std::size_t   MAX_PERSISTENT_KEY_BYTES = 32768;
    static constexpr std::uint32_t PERSISTENT_CACHE_VERSION = 2;
    static constexpr char          PERSISTENT_CACHE_MAGIC[8] = {'M', 'T', 'A', 'C', 'S', 'U', 'M', '2'};

    static std::unordered_map<std::string, CacheEntry>& Cache()
    {
        static std::unordered_map<std::string, CacheEntry> c;
        return c;
    }
    static std::mutex& CacheMtx()
    {
        static std::mutex m;
        return m;
    }
    static std::unordered_map<std::string, DirectoryMetadataSnapshot>& MetadataSnapshots()
    {
        static std::unordered_map<std::string, DirectoryMetadataSnapshot> snapshots;
        return snapshots;
    }
    static std::mutex& MetadataSnapshotMtx()
    {
        static std::mutex mutex;
        return mutex;
    }
    static std::uint64_t& CacheAccessSequence()
    {
        static std::uint64_t sequence = 0;
        return sequence;
    }
    static std::uint64_t& CacheMutationSequence()
    {
        static std::uint64_t sequence = 0;
        return sequence;
    }
    static SString& PersistentCachePath()
    {
        static SString path;
        return path;
    }
    static bool& PersistentCacheDirty()
    {
        static bool dirty = false;
        return dirty;
    }
    static std::atomic<std::uint64_t>& PersistentCacheFlushDueAtMs()
    {
        static std::atomic<std::uint64_t> dueAtMs{};
        return dueAtMs;
    }
    static std::uint64_t MonotonicMilliseconds()
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    static void RequestPersistentCacheFlushLocked()
    {
        // Resource manifests can dirty thousands of entries in quick succession.
        // Debounce them into one atomic index replacement after the join settles.
        constexpr std::uint64_t FLUSH_DELAY_MS = 5000;
        PersistentCacheDirty() = true;
        PersistentCacheFlushDueAtMs().store(MonotonicMilliseconds() + FLUSH_DELAY_MS, std::memory_order_release);
    }
    static std::string NormalizeCacheKey(const SString& strFilename)
    {
        std::string key = strFilename;
        for (char& character : key)
        {
            if (character >= 'A' && character <= 'Z')
                character += 'a' - 'A';
            if (character == '\\')
                character = '/';
        }
        return key;
    }
    static std::string GetDirectoryKey(const std::string& key)
    {
        const std::size_t separator = key.find_last_of('/');
        return separator == std::string::npos ? std::string() : key.substr(0, separator);
    }
    static void ClearMetadataSnapshots()
    {
        std::lock_guard<std::mutex> lock(MetadataSnapshotMtx());
        MetadataSnapshots().clear();
    }
    static void InvalidateMetadataSnapshot(const std::string& key)
    {
        const std::string directoryKey = GetDirectoryKey(key);
        if (directoryKey.empty())
            return;

        std::lock_guard<std::mutex> lock(MetadataSnapshotMtx());
        MetadataSnapshots().erase(directoryKey);
    }
    static DWORD WINAPI EnumerateDirectoryThread(LPVOID parameter)
    {
        auto* params = static_cast<DirectoryEnumerationParams*>(parameter);
        try
        {
            WString directoryPath = params->pattern;
            if (directoryPath.EndsWith(L"\\*") || directoryPath.EndsWith(L"/*"))
                directoryPath.resize(directoryPath.size() - 2);

            HANDLE directory = CreateFileW(directoryPath.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (directory != INVALID_HANDLE_VALUE)
            {
                BY_HANDLE_FILE_INFORMATION directoryInfo{};
                bool                       complete = GetFileInformationByHandle(directory, &directoryInfo) != FALSE &&
                                (directoryInfo.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == FILE_ATTRIBUTE_DIRECTORY;
                bool                                           firstQuery = true;
                alignas(8) std::array<std::uint8_t, 64 * 1024> buffer{};
                while (complete)
                {
                    const FILE_INFO_BY_HANDLE_CLASS infoClass = firstQuery ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
                    firstQuery = false;
                    if (!GetFileInformationByHandleEx(directory, infoClass, buffer.data(), static_cast<DWORD>(buffer.size())))
                    {
                        if (GetLastError() != ERROR_NO_MORE_FILES)
                            complete = false;
                        break;
                    }

                    std::size_t offset = 0;
                    while (complete)
                    {
                        constexpr std::size_t FIXED_SIZE = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
                        if (offset > buffer.size() - FIXED_SIZE)
                        {
                            complete = false;
                            break;
                        }

                        const auto* info = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer.data() + offset);
                        if ((info->FileNameLength % sizeof(wchar_t)) != 0 || info->FileNameLength > buffer.size() - offset - FIXED_SIZE ||
                            info->EndOfFile.QuadPart < 0)
                        {
                            complete = false;
                            break;
                        }

                        if ((info->FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0)
                        {
                            DirectoryEnumerationEntry entry;
                            entry.filename = std::wstring(info->FileName, info->FileNameLength / sizeof(wchar_t));
                            entry.metadata.size = static_cast<std::uint64_t>(info->EndOfFile.QuadPart);
                            entry.metadata.mtime = static_cast<std::uint64_t>(info->LastWriteTime.QuadPart);
                            entry.metadata.changeTime = static_cast<std::uint64_t>(info->ChangeTime.QuadPart);
                            entry.metadata.fileId = static_cast<std::uint64_t>(info->FileId.QuadPart);
                            entry.metadata.volumeSerial = directoryInfo.dwVolumeSerialNumber;
                            entry.metadata.strong = entry.metadata.fileId != 0;
                            params->entries.emplace_back(std::move(entry));
                        }

                        if (info->NextEntryOffset == 0)
                            break;
                        if (info->NextEntryOffset < FIXED_SIZE || info->NextEntryOffset > buffer.size() - offset)
                        {
                            complete = false;
                            break;
                        }
                        offset += info->NextEntryOffset;
                    }
                }
                CloseHandle(directory);
                params->success = complete;
            }
        }
        catch (...)
        {
            params->success = false;
        }

        int expected = 0;
        if (!params->state.compare_exchange_strong(expected, 1, std::memory_order_release, std::memory_order_relaxed))
            delete params;
        return 0;
    }
    static bool EnumerateDirectoryWithTimeout(const SString& directory, std::vector<DirectoryEnumerationEntry>& entries)
    {
        auto* params = new (std::nothrow) DirectoryEnumerationParams;
        if (!params)
            return false;

        try
        {
            params->pattern = SharedUtil::FromUTF8(SharedUtil::PathJoin(directory, "*"));
        }
        catch (...)
        {
            delete params;
            return false;
        }
        if (params->pattern.empty())
        {
            delete params;
            return false;
        }

        HANDLE thread = CreateThread(nullptr, 0, EnumerateDirectoryThread, params, 0, nullptr);
        if (!thread)
        {
            delete params;
            return false;
        }

        constexpr DWORD DIRECTORY_ENUMERATION_TIMEOUT_MS = 5000;
        const DWORD     waitResult = WaitForSingleObject(thread, DIRECTORY_ENUMERATION_TIMEOUT_MS);
        if (waitResult != WAIT_OBJECT_0)
        {
            int expected = 0;
            if (params->state.compare_exchange_strong(expected, 2, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                CloseHandle(thread);
                return false;
            }

            // The worker completed while the timeout path was claiming
            // ownership. Join it before reading or releasing its result.
            WaitForSingleObject(thread, INFINITE);
        }

        CloseHandle(thread);
        const bool success = params->success;
        if (success)
            entries = std::move(params->entries);
        delete params;
        return success;
    }
    enum class MetadataLookupResult
    {
        Unavailable,
        Missing,
        Found,
    };
    static MetadataLookupResult GetMetadataFromDirectorySnapshot(const SString& filename, const std::string& key, FileMetadata& metadata)
    {
        const std::string directoryKey = GetDirectoryKey(key);
        const std::size_t separator = key.find_last_of('/');
        if (directoryKey.empty() || separator == std::string::npos || separator + 1 >= key.size())
            return MetadataLookupResult::Unavailable;

        const std::string basename = key.substr(separator + 1);
        {
            std::lock_guard<std::mutex> lock(MetadataSnapshotMtx());
            const auto                  directory = MetadataSnapshots().find(directoryKey);
            if (directory != MetadataSnapshots().end())
            {
                const auto file = directory->second.find(basename);
                if (file == directory->second.end())
                    return MetadataLookupResult::Missing;
                metadata = file->second;
                return MetadataLookupResult::Found;
            }
        }

        std::vector<DirectoryEnumerationEntry> entries;
        const bool                             success = EnumerateDirectoryWithTimeout(SharedUtil::ExtractPath(filename), entries);
        if (!success)
            return MetadataLookupResult::Unavailable;

        DirectoryMetadataSnapshot snapshot;
        snapshot.reserve(entries.size());
        for (const DirectoryEnumerationEntry& entry : entries)
        {
            try
            {
                snapshot[NormalizeCacheKey(SharedUtil::ToUTF8(entry.filename))] = entry.metadata;
            }
            catch (...)
            {
            }
        }

        std::lock_guard<std::mutex> lock(MetadataSnapshotMtx());
        auto [directory, inserted] = MetadataSnapshots().emplace(directoryKey, std::move(snapshot));
        const auto file = directory->second.find(basename);
        if (file == directory->second.end())
            return MetadataLookupResult::Missing;
        metadata = file->second;
        return MetadataLookupResult::Found;
    }
    static bool GetStrongFileMetadata(const WString& filename, FileMetadata& metadata)
    {
        if (filename.empty())
            return false;

        HANDLE file = CreateFileW(filename.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        BY_HANDLE_FILE_INFORMATION fileInfo{};
        FILE_BASIC_INFO            basicInfo{};
        const bool                 success = GetFileInformationByHandle(file, &fileInfo) != FALSE &&
                             GetFileInformationByHandleEx(file, FileBasicInfo, &basicInfo, sizeof(basicInfo)) != FALSE &&
                             (fileInfo.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
        CloseHandle(file);
        if (!success)
            return false;

        metadata.size = (static_cast<std::uint64_t>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
        metadata.mtime = static_cast<std::uint64_t>(basicInfo.LastWriteTime.QuadPart);
        metadata.changeTime = static_cast<std::uint64_t>(basicInfo.ChangeTime.QuadPart);
        metadata.fileId = (static_cast<std::uint64_t>(fileInfo.nFileIndexHigh) << 32) | fileInfo.nFileIndexLow;
        metadata.volumeSerial = fileInfo.dwVolumeSerialNumber;
        metadata.strong = metadata.fileId != 0;
        return metadata.strong;
    }
    static bool MetadataMatches(const CacheEntry& entry, const FileMetadata& metadata)
    {
        return metadata.strong && entry.size == metadata.size && entry.mtime == metadata.mtime && entry.changeTime == metadata.changeTime &&
               entry.fileId == metadata.fileId && entry.volumeSerial == metadata.volumeSerial;
    }
    static bool MetadataMatches(const FileMetadata& left, const FileMetadata& right)
    {
        return left.strong && right.strong && left.size == right.size && left.mtime == right.mtime && left.changeTime == right.changeTime &&
               left.fileId == right.fileId && left.volumeSerial == right.volumeSerial;
    }
    static void AppendU32(std::vector<std::uint8_t>& output, std::uint32_t value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8)
            output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    static void AppendU64(std::vector<std::uint8_t>& output, std::uint64_t value)
    {
        for (unsigned int shift = 0; shift < 64; shift += 8)
            output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
    static bool ReadU32(const std::uint8_t*& cursor, const std::uint8_t* end, std::uint32_t& value)
    {
        if (end - cursor < 4)
            return false;

        value = 0;
        for (unsigned int shift = 0; shift < 32; shift += 8)
            value |= static_cast<std::uint32_t>(*cursor++) << shift;
        return true;
    }
    static bool ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end, std::uint64_t& value)
    {
        if (end - cursor < 8)
            return false;

        value = 0;
        for (unsigned int shift = 0; shift < 64; shift += 8)
            value |= static_cast<std::uint64_t>(*cursor++) << shift;
        return true;
    }
    static bool LoadPersistentCacheLocked()
    {
        if (PersistentCachePath().empty() || !SharedUtil::FileExists(PersistentCachePath()))
            return true;

        SString fileData;
        if (!SharedUtil::FileLoad(PersistentCachePath(), fileData, static_cast<int>(MAX_PERSISTENT_CACHE_BYTES)))
            return false;

        constexpr std::size_t HEADER_SIZE = sizeof(PERSISTENT_CACHE_MAGIC) + 4 * sizeof(std::uint32_t);
        if (fileData.size() < HEADER_SIZE || fileData.size() > MAX_PERSISTENT_CACHE_BYTES)
        {
            return false;
        }

        const auto* begin = reinterpret_cast<const std::uint8_t*>(fileData.data());
        const auto* cursor = begin;
        const auto* end = begin + fileData.size();
        if (std::memcmp(cursor, PERSISTENT_CACHE_MAGIC, sizeof(PERSISTENT_CACHE_MAGIC)) != 0)
        {
            return false;
        }
        cursor += sizeof(PERSISTENT_CACHE_MAGIC);

        std::uint32_t version = 0;
        std::uint32_t entryCount = 0;
        std::uint32_t payloadSize = 0;
        std::uint32_t payloadCrc = 0;
        if (!ReadU32(cursor, end, version) || !ReadU32(cursor, end, entryCount) || !ReadU32(cursor, end, payloadSize) || !ReadU32(cursor, end, payloadCrc) ||
            version != PERSISTENT_CACHE_VERSION || entryCount > MAX_CHECKSUM_CACHE_ENTRIES || payloadSize != static_cast<std::uint32_t>(end - cursor) ||
            CRCGenerator::GetCRCFromBuffer(reinterpret_cast<const char*>(cursor), payloadSize) != payloadCrc)
        {
            return false;
        }

        std::unordered_map<std::string, CacheEntry> loadedEntries;
        loadedEntries.reserve(entryCount);
        for (std::uint32_t index = 0; index < entryCount; ++index)
        {
            std::uint32_t keySize = 0;
            if (!ReadU32(cursor, end, keySize) || keySize == 0 || keySize > MAX_PERSISTENT_KEY_BYTES || static_cast<std::size_t>(end - cursor) < keySize)
            {
                return false;
            }

            std::string key(reinterpret_cast<const char*>(cursor), keySize);
            cursor += keySize;
            CacheEntry    entry{};
            std::uint32_t crc = 0;
            if (key.find('\0') != std::string::npos || !ReadU64(cursor, end, entry.size) || !ReadU64(cursor, end, entry.mtime) ||
                !ReadU64(cursor, end, entry.changeTime) || !ReadU64(cursor, end, entry.fileId) || !ReadU32(cursor, end, entry.volumeSerial) ||
                !ReadU32(cursor, end, crc) || entry.fileId == 0 || end - cursor < static_cast<std::ptrdiff_t>(sizeof(entry.md5.data)))
            {
                return false;
            }
            entry.crc = crc;
            std::memcpy(entry.md5.data, cursor, sizeof(entry.md5.data));
            cursor += sizeof(entry.md5.data);
            entry.lastAccess = ++CacheAccessSequence();
            if (!loadedEntries.emplace(std::move(key), entry).second)
            {
                return false;
            }
        }

        if (cursor != end)
        {
            return false;
        }

        Cache() = std::move(loadedEntries);
        return true;
    }
    static bool SavePersistentCacheLocked()
    {
        if (!PersistentCacheDirty() || PersistentCachePath().empty())
            return true;

        std::vector<std::pair<std::string, CacheEntry>> entries(Cache().begin(), Cache().end());
        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) { return left.first < right.first; });

        std::vector<std::uint8_t> payload;
        std::uint32_t             serializedEntryCount = 0;
        for (const auto& [key, entry] : entries)
        {
            constexpr std::size_t FIXED_ENTRY_BYTES = sizeof(std::uint32_t) + 4 * sizeof(std::uint64_t) + 2 * sizeof(std::uint32_t) + sizeof(entry.md5.data);
            if (key.empty() || key.size() > MAX_PERSISTENT_KEY_BYTES || payload.size() + FIXED_ENTRY_BYTES + key.size() > MAX_PERSISTENT_CACHE_BYTES)
            {
                return false;
            }
            AppendU32(payload, static_cast<std::uint32_t>(key.size()));
            payload.insert(payload.end(), key.begin(), key.end());
            AppendU64(payload, entry.size);
            AppendU64(payload, entry.mtime);
            AppendU64(payload, entry.changeTime);
            AppendU64(payload, entry.fileId);
            AppendU32(payload, entry.volumeSerial);
            AppendU32(payload, static_cast<std::uint32_t>(entry.crc));
            payload.insert(payload.end(), std::begin(entry.md5.data), std::end(entry.md5.data));
            ++serializedEntryCount;
        }

        std::vector<std::uint8_t> output;
        output.insert(output.end(), std::begin(PERSISTENT_CACHE_MAGIC), std::end(PERSISTENT_CACHE_MAGIC));
        AppendU32(output, PERSISTENT_CACHE_VERSION);
        AppendU32(output, serializedEntryCount);
        AppendU32(output, static_cast<std::uint32_t>(payload.size()));
        AppendU32(output, static_cast<std::uint32_t>(CRCGenerator::GetCRCFromBuffer(reinterpret_cast<const char*>(payload.data()), payload.size())));
        output.insert(output.end(), payload.begin(), payload.end());

        const SString temporaryPath = PersistentCachePath() + ".tmp";
        SharedUtil::FileDelete(temporaryPath);
        bool saved =
            output.size() <= MAX_PERSISTENT_CACHE_BYTES &&
            SharedUtil::FileSave(temporaryPath, output.data(), static_cast<unsigned long>(output.size()), true) &&
            MoveFileExW(SharedUtil::FromUTF8(temporaryPath), SharedUtil::FromUTF8(PersistentCachePath()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!saved)
        {
            SharedUtil::FileDelete(temporaryPath);
        }
        else
        {
            PersistentCacheDirty() = false;
            PersistentCacheFlushDueAtMs().store(0, std::memory_order_release);
        }

        return saved;
    }
    static void EvictLeastRecentlyUsedEntry()
    {
        auto victim = Cache().end();
        for (auto iter = Cache().begin(); iter != Cache().end(); ++iter)
        {
            if (victim == Cache().end() || iter->second.lastAccess < victim->second.lastAccess ||
                (iter->second.lastAccess == victim->second.lastAccess && iter->first < victim->first))
            {
                victim = iter;
            }
        }
        if (victim != Cache().end())
        {
            Cache().erase(victim);
            RequestPersistentCacheFlushLocked();
        }
    }

public:
    static void ConfigurePersistentCache(const SString& path)
    {
        // Every connection gets a fresh metadata epoch. Reusing directory
        // snapshots across joins could hide an external cache modification.
        ClearMetadataSnapshots();
        std::lock_guard<std::mutex> lock(CacheMtx());
        if (PersistentCachePath() == path)
            return;

        SavePersistentCacheLocked();
        Cache().clear();
        CacheAccessSequence() = 0;
        ++CacheMutationSequence();
        PersistentCachePath() = path;
        PersistentCacheDirty() = false;
        PersistentCacheFlushDueAtMs().store(0, std::memory_order_release);
        LoadPersistentCacheLocked();
    }

    static void PulsePersistentCache()
    {
        const std::uint64_t now = MonotonicMilliseconds();
        const std::uint64_t dueAt = PersistentCacheFlushDueAtMs().load(std::memory_order_acquire);
        if (!dueAt || now < dueAt)
            return;

        std::lock_guard<std::mutex> lock(CacheMtx());
        const std::uint64_t         lockedDueAt = PersistentCacheFlushDueAtMs().load(std::memory_order_relaxed);
        if (!lockedDueAt || now < lockedDueAt)
            return;

        if (!SavePersistentCacheLocked())
        {
            // An unwritable cache is a performance degradation, not a reason to
            // retry disk I/O every frame or interrupt the connection.
            constexpr std::uint64_t RETRY_DELAY_MS = 30000;
            PersistentCacheFlushDueAtMs().store(now + RETRY_DELAY_MS, std::memory_order_release);
        }
    }

    static void ClearChecksumCache()
    {
        {
            std::lock_guard<std::mutex> lock(CacheMtx());
            Cache().clear();
            RequestPersistentCacheFlushLocked();
            ++CacheMutationSequence();
        }
        ClearMetadataSnapshots();
    }

    static void InvalidateChecksum(const SString& strFilename)
    {
        const std::string key = NormalizeCacheKey(strFilename);
        {
            std::lock_guard<std::mutex> lock(CacheMtx());
            if (Cache().erase(key) != 0)
                RequestPersistentCacheFlushLocked();
            // The sequence also prevents an in-flight checksum from repopulating an
            // entry after a script or downloader invalidated it.
            ++CacheMutationSequence();
        }
        InvalidateMetadataSnapshot(key);
    }

    static std::variant<CChecksum, std::string> GenerateChecksumFromFile(const SString& strFilename, CachePolicy cachePolicy = CachePolicy::UseCache)
    {
        const std::string key = NormalizeCacheKey(strFilename);

        WIN32_FILE_ATTRIBUTE_DATA attr;
        WString                   wide;
        try
        {
            wide = SharedUtil::FromUTF8(strFilename);
        }
        catch (...)
        {
        }
        FileMetadata               metadata;
        const MetadataLookupResult snapshotResult =
            cachePolicy == CachePolicy::UseCache ? GetMetadataFromDirectorySnapshot(strFilename, key, metadata) : MetadataLookupResult::Unavailable;
        bool hasMeta = snapshotResult == MetadataLookupResult::Found;
        if (snapshotResult == MetadataLookupResult::Unavailable)
        {
            if (cachePolicy == CachePolicy::BypassAndRefresh)
                hasMeta = GetStrongFileMetadata(wide, metadata);
            else
            {
                hasMeta = !wide.empty() && SharedUtil::GetFileAttributesExWithTimeout(wide.c_str(), attr, 500);
                if (hasMeta)
                {
                    metadata.size = (std::uint64_t(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
                    metadata.mtime = (std::uint64_t(attr.ftLastWriteTime.dwHighDateTime) << 32) | attr.ftLastWriteTime.dwLowDateTime;
                }
            }
        }
        std::uint64_t checksumMutationSequence = 0;

        {
            std::lock_guard<std::mutex> lock(CacheMtx());
            auto                        iter = Cache().find(key);
            if (cachePolicy == CachePolicy::UseCache && hasMeta && iter != Cache().end() && MetadataMatches(iter->second, metadata))
            {
                CChecksum cached;
                cached.ulCRC = iter->second.crc;
                cached.md5 = iter->second.md5;
                iter->second.lastAccess = ++CacheAccessSequence();
                return cached;
            }

            // A forced check must retire the previous value before touching the
            // file. Otherwise a failed download verification could leave a stale
            // entry available to the next connection.
            if (iter != Cache().end())
            {
                Cache().erase(iter);
                RequestPersistentCacheFlushLocked();
                ++CacheMutationSequence();
            }
            else if (cachePolicy == CachePolicy::BypassAndRefresh)
            {
                ++CacheMutationSequence();
            }
            checksumMutationSequence = CacheMutationSequence();
        }

        SString buf;
        if (!SharedUtil::FileLoadWithTimeout(strFilename, buf, 2000))
        {
            if (!hasMeta)
                return SString("File not found or inaccessible: %s", strFilename.c_str());
            return SString("Could not read: %s", strFilename.c_str());
        }
        CChecksum r;
        r.ulCRC = CRCGenerator::GetCRCFromBuffer(buf.data(), buf.size());
        CMD5Hasher().Calculate(buf.data(), buf.size(), r.md5);
        FileMetadata stableMetadata;
        const bool   hasStableMeta = hasMeta && metadata.strong && GetStrongFileMetadata(wide, stableMetadata) && MetadataMatches(metadata, stableMetadata);

        if (hasStableMeta)
        {
            std::lock_guard<std::mutex> lock(CacheMtx());
            // Do not resurrect a value invalidated while the file was being read.
            if (checksumMutationSequence == CacheMutationSequence())
            {
                if (Cache().find(key) == Cache().end() && Cache().size() >= MAX_CHECKSUM_CACHE_ENTRIES)
                    EvictLeastRecentlyUsedEntry();
                Cache()[key] = {
                    stableMetadata.size,    stableMetadata.mtime, stableMetadata.changeTime, stableMetadata.fileId, stableMetadata.volumeSerial, r.ulCRC, r.md5,
                    ++CacheAccessSequence()};
                RequestPersistentCacheFlushLocked();
            }
        }
        return r;
    }
#else
    static void ConfigurePersistentCache(const SString&) {}
    static void PulsePersistentCache() {}
    static void ClearChecksumCache() {}
    static void InvalidateChecksum(const SString&) {}

    // Server and non-Windows builds use the original implementation
    static std::variant<CChecksum, std::string> GenerateChecksumFromFile(const SString& strFilename, CachePolicy cachePolicy = CachePolicy::UseCache)
    {
        (void)cachePolicy;
        constexpr int maxRetries = 3;
        constexpr int retryDelayMs = 50;
        int           lastErrno = 0;
        int           attemptsMade = 0;

        for (int attempt = 0; attempt < maxRetries; ++attempt)
        {
            ++attemptsMade;

            if (attempt > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs * attempt));

            errno = 0;

            CChecksum result;
            result.ulCRC = CRCGenerator::GetCRCFromFile(strFilename);

            if (errno)
            {
                lastErrno = errno;
                if (errno == ENOENT)
                    break;
                continue;
            }

            errno = 0;
            bool success = CMD5Hasher().Calculate(strFilename, result.md5);

            if (!success)
            {
                lastErrno = errno ? errno : EIO;
                if (errno == ENOENT)
                    break;
                continue;
            }

            return result;
        }

        if (lastErrno == ENOENT)
            return SString("File not found: %s", strFilename.c_str());

        return SString("Could not checksum '%s' after %d attempt%s: %s", strFilename.c_str(), attemptsMade, attemptsMade == 1 ? "" : "s",
                       lastErrno ? std::strerror(lastErrno) : "Unknown error");
    }
#endif

    // GenerateChecksumFromFileUnsafe should never ever be used unless you are a bad person. Or unless you really know what you're doing.
    // If it's the latter, please leave a code comment somewhere explaining why. Otherwise we'll think it's just code that hasn't been migrated yet.
    static CChecksum GenerateChecksumFromFileUnsafe(const SString& strFilename, CachePolicy cachePolicy = CachePolicy::UseCache)
    {
        auto result = GenerateChecksumFromFile(strFilename, cachePolicy);

        // If it holds an error message, just return a default CChecksum
        if (std::holds_alternative<std::string>(result))
            return CChecksum();

        return std::get<CChecksum>(result);
    }

    static CChecksum GenerateChecksumFromBuffer(const char* cpBuffer, unsigned long ulLength)
    {
        CChecksum result;
        result.ulCRC = CRCGenerator::GetCRCFromBuffer(cpBuffer, ulLength);
        CMD5Hasher().Calculate(cpBuffer, ulLength, result.md5);
        return result;
    }

    unsigned long ulCRC;
    MD5           md5;
};

#endif
