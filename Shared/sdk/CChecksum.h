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
        std::uint64_t size, mtime;
        unsigned long crc;
        MD5           md5;
        std::uint64_t lastAccess;
    };

    // This cache deliberately survives resource-manager teardown so reconnects
    // can reuse verified files. Keep it bounded because server-controlled
    // manifests can otherwise grow process memory for the lifetime of MTA.
    static constexpr std::size_t MAX_CHECKSUM_CACHE_ENTRIES = 8192;

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
            Cache().erase(victim);
    }

public:
    static void ClearChecksumCache()
    {
        std::lock_guard<std::mutex> l(CacheMtx());
        Cache().clear();
        ++CacheMutationSequence();
    }

    static void InvalidateChecksum(const SString& strFilename)
    {
        std::lock_guard<std::mutex> lock(CacheMtx());
        Cache().erase(NormalizeCacheKey(strFilename));
        // The sequence also prevents an in-flight checksum from repopulating an
        // entry after a script or downloader invalidated it.
        ++CacheMutationSequence();
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
        bool          hasMeta = !wide.empty() && SharedUtil::GetFileAttributesExWithTimeout(wide.c_str(), attr, 500);
        std::uint64_t sz = 0, mt = 0;
        std::uint64_t checksumMutationSequence = 0;
        if (hasMeta)
        {
            sz = (std::uint64_t(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
            mt = (std::uint64_t(attr.ftLastWriteTime.dwHighDateTime) << 32) | attr.ftLastWriteTime.dwLowDateTime;
        }

        {
            std::lock_guard<std::mutex> lock(CacheMtx());
            auto                        iter = Cache().find(key);
            if (cachePolicy == CachePolicy::UseCache && hasMeta && iter != Cache().end() && iter->second.size == sz && iter->second.mtime == mt)
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

        if (hasMeta && SharedUtil::GetFileAttributesExWithTimeout(wide.c_str(), attr, 500) &&
            sz == ((std::uint64_t(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow) &&
            mt == ((std::uint64_t(attr.ftLastWriteTime.dwHighDateTime) << 32) | attr.ftLastWriteTime.dwLowDateTime))
        {
            std::lock_guard<std::mutex> lock(CacheMtx());
            // Do not resurrect a value invalidated while the file was being read.
            if (checksumMutationSequence == CacheMutationSequence())
            {
                if (Cache().find(key) == Cache().end() && Cache().size() >= MAX_CHECKSUM_CACHE_ENTRIES)
                    EvictLeastRecentlyUsedEntry();
                Cache()[key] = {sz, mt, r.ulCRC, r.md5, ++CacheAccessSequence()};
            }
        }
        return r;
    }
#else
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
