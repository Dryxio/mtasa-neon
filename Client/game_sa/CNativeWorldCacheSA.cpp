/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeWorldCacheSA.cpp
 *  PURPOSE:     Immutable content-addressed cache for native world packs
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeWorldCacheSA.h"

#include "SharedUtil.File.h"
#include "SharedUtil.Hash.h"
#include "SharedUtil.Misc.h"
#include "sha2.h"

#include <array>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace
{
    constexpr const char*   CACHE_ROOT_DIRECTORY = "native-world-cache";
    constexpr const char*   CONTENT_ID_DOMAIN_V1 = "mta-native-world-cache-content-v1";
    constexpr const char*   CONTENT_ID_DOMAIN_V2 = "mta-native-world-cache-content-v2";
    constexpr const char*   CONTENT_ID_DOMAIN_V3 = "mta-native-world-cache-content-v3";
    constexpr const char*   CACHED_MANIFEST_FILE = "native-world.json";
    constexpr const char*   CACHED_IDE_FILE = "world.ide";
    constexpr const char*   CACHED_LOD_FILE = "world.lod";
    constexpr const char*   CACHED_IMG_FILE = "world.img";
    constexpr std::uint64_t MAX_V3_IMG_BYTES = 256ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t MAX_V3_TOTAL_BYTES = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    constexpr size_t        MAX_V3_IMAGES = 32;

    // Pending locks are explicit transaction ownership: refusal closes them,
    // while successful native activation promotes them to process lifetime.
    std::vector<HANDLE> g_pendingLocks;
    std::vector<HANDLE> g_processLocks;
    bool                g_cachePrepared = false;

    class CScopedHandles
    {
    public:
        ~CScopedHandles() { Close(); }

        void Add(HANDLE handle) { m_handles.push_back(handle); }

        void Close()
        {
            for (HANDLE handle : m_handles)
                CloseHandle(handle);
            m_handles.clear();
        }

        void TransferTo(std::vector<HANDLE>& destination)
        {
            destination.insert(destination.end(), m_handles.begin(), m_handles.end());
            m_handles.clear();
        }

    private:
        std::vector<HANDLE> m_handles;
    };

    bool IsLowerSha256(const std::string& value)
    {
        return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character)
                                                 { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
    }

    bool IsLowerHex(const std::string& value, size_t length)
    {
        return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char character)
                                                     { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
    }

    bool IsPrivateCacheSibling(const std::string& value)
    {
        if (value.size() < 1 + 64 + 1 + 7 + 1 + 32 || value.front() != '.' || !IsLowerSha256(value.substr(1, 64)))
            return false;
        const size_t separator = value.find('.', 66);
        if (separator == std::string::npos || !IsLowerHex(value.substr(separator + 1), 32))
            return false;
        const std::string kind = value.substr(66, separator - 66);
        return kind == "invalid" || kind == "quarantine";
    }

    bool IsCancelled(const SNativeWorldCacheRequestSA& request)
    {
        return request.cancellation && request.cancellation->load(std::memory_order_acquire);
    }

    bool IsSafeLeafName(const std::string& value, size_t maximumLength)
    {
        if (value.empty() || value.size() > maximumLength || value == "." || value == "..")
            return false;
        return std::all_of(value.begin(), value.end(),
                           [](unsigned char character)
                           {
                               return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' ||
                                      character == '-' || character == '.';
                           });
    }

    bool IsCanonicalV3ImageName(const std::string& value, size_t expectedIndex)
    {
        char expected[9]{};
        _snprintf_s(expected, sizeof(expected), _TRUNCATE, "w%03u.img", static_cast<unsigned int>(expectedIndex));
        return value == expected;
    }

    bool IsValidRequestIdentity(const SNativeWorldCacheRequestSA& request)
    {
        if (request.format == 1)
            return request.policyKey == "bullworth" && request.packId == "bullworth";
        if (request.format == 2)
            return request.policyKey == "static-world-v1" && IsSafeLeafName(request.packId, 15) && request.packId.find('.') == std::string::npos;
        return request.format == 3 && request.policyKey == "static-world-v3" && IsSafeLeafName(request.packId, 15) &&
               request.packId.find('.') == std::string::npos;
    }

    const char* CacheFormatDirectory(const SNativeWorldCacheRequestSA& request)
    {
        return request.format == 1 ? "v1" : request.format == 2 ? "v2" : "v3";
    }

    const std::string& CachePolicyDirectory(const SNativeWorldCacheRequestSA& request)
    {
        return request.format == 1 ? request.packId : request.policyKey;
    }

    bool IsSafeLocalPath(const SString& path)
    {
        if (path.empty() || path.length() >= MAX_PATH || path.length() < 3 || path[1] != ':' || (path[2] != '\\' && path[2] != '/'))
            return false;
        if (!std::all_of(path.begin(), path.end(), [](unsigned char character) { return character > 0 && character <= 0x7F; }))
            return false;
        const wchar_t driveRoot[] = {static_cast<wchar_t>(path[0]), L':', L'\\', L'\0'};
        return GetDriveTypeW(driveRoot) == DRIVE_FIXED;
    }

    SString JoinPath(const SString& directory, const std::string& leaf)
    {
        return SString("%s\\%s", directory.c_str(), leaf.c_str());
    }

    bool HandleMatchesPath(HANDLE handle, const SString& expectedPath, bool directory, std::string& error)
    {
        BY_HANDLE_FILE_INFORMATION information{};
        if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &information) ||
            !!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            error = "cache handle type or attributes are invalid";
            return false;
        }

        wchar_t     finalPath[32768]{};
        const DWORD length = GetFinalPathNameByHandleW(handle, finalPath, static_cast<DWORD>(std::size(finalPath)), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!length || length >= std::size(finalPath))
        {
            error = SString("cache final-path query failed win32=%u", GetLastError());
            return false;
        }
        const wchar_t* normalized = finalPath;
        if (wcsncmp(normalized, L"\\\\?\\", 4) == 0)
            normalized += 4;
        if (wcsncmp(normalized, L"UNC\\", 4) == 0 || _wcsicmp(normalized, SharedUtil::FromUTF8(expectedPath).c_str()) != 0)
        {
            error = "cache handle final path escaped its expected local path";
            return false;
        }
        return true;
    }

    bool GetPlainAttributes(const SString& path, bool directory, std::string& error)
    {
        const DWORD attributes = GetFileAttributesW(SharedUtil::FromUTF8(path).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || !!(attributes & FILE_ATTRIBUTE_DIRECTORY) != directory || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            error = directory ? "cache path is missing, not a directory, or is a reparse point" : "cache file is missing, not regular, or is a reparse point";
            return false;
        }
        return true;
    }

    bool EnsurePlainDirectory(const SString& path, std::string& error)
    {
        if (!IsSafeLocalPath(path))
        {
            error = "cache directory is not a safe local path";
            return false;
        }
        if (GetFileAttributesW(SharedUtil::FromUTF8(path).c_str()) == INVALID_FILE_ATTRIBUTES &&
            !CreateDirectoryW(SharedUtil::FromUTF8(path).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            error = SString("cache directory creation failed win32=%u", GetLastError());
            return false;
        }
        return GetPlainAttributes(path, true, error);
    }

    bool LockDirectory(const SString& path, CScopedHandles& handles, std::string& error)
    {
        if (!IsSafeLocalPath(path) || !GetPlainAttributes(path, true, error))
            return false;
        const HANDLE handle = CreateFileW(SharedUtil::FromUTF8(path).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = SString("cache directory lock failed win32=%u", GetLastError());
            return false;
        }
        if (!HandleMatchesPath(handle, path, true, error))
        {
            CloseHandle(handle);
            return false;
        }
        handles.Add(handle);
        return true;
    }

    bool LockRegularFile(const SString& path, std::uint64_t minimumBytes, std::uint64_t maximumBytes, CScopedHandles& handles, std::string& error)
    {
        if (!IsSafeLocalPath(path) || !GetPlainAttributes(path, false, error))
            return false;
        const HANDLE handle = CreateFileW(SharedUtil::FromUTF8(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = SString("cache file lock failed win32=%u", GetLastError());
            return false;
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (!HandleMatchesPath(handle, path, false, error) || !GetFileInformationByHandle(handle, &information))
        {
            CloseHandle(handle);
            if (error.empty())
                error = "cache file byte length is invalid";
            return false;
        }
        const std::uint64_t bytes = (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32) | information.nFileSizeLow;
        if (bytes < minimumBytes || bytes > maximumBytes)
        {
            CloseHandle(handle);
            error = "cache file byte length is invalid";
            return false;
        }
        handles.Add(handle);
        return true;
    }

    bool HasExactHash(const SString& path, const std::string& expected)
    {
        const SString actual = SharedUtil::GenerateSha256HexStringFromFile(path);
        return actual.length() == 64 && _stricmp(actual.c_str(), expected.c_str()) == 0;
    }

    std::string BuildCanonicalManifest(const SNativeWorldCacheRequestSA& request)
    {
        std::ostringstream manifest;
        manifest.imbue(std::locale::classic());
        manifest << "{\n" << "  \"format\": " << request.format << ",\n";
        if (request.format != 1)
            manifest << "  \"policy\": \"" << request.policyKey << "\",\n";
        manifest << "  \"pack_id\": \"" << request.packId << "\",\n"
                 << "  \"files\": {\n"
                 << "    \"ide\": {\n"
                 << "      \"name\": \"" << CACHED_IDE_FILE << "\",\n"
                 << "      \"bytes\": " << request.ide.bytes << ",\n"
                 << "      \"sha256\": \"" << request.ide.sha256 << "\"\n"
                 << "    }";
        if (request.format == 3)
        {
            manifest << ",\n"
                     << "    \"lod\": {\n"
                     << "      \"name\": \"" << CACHED_LOD_FILE << "\",\n"
                     << "      \"bytes\": " << request.lod.bytes << ",\n"
                     << "      \"sha256\": \"" << request.lod.sha256 << "\"\n"
                     << "    },\n"
                     << "    \"images\": [\n";
            for (size_t index = 0; index < request.images.size(); ++index)
            {
                const SNativeWorldCacheFileSA& image = request.images[index];
                manifest << "      {\n"
                         << "        \"name\": \"" << image.name << "\",\n"
                         << "        \"bytes\": " << image.bytes << ",\n"
                         << "        \"sha256\": \"" << image.sha256 << "\"\n"
                         << "      }" << (index + 1 == request.images.size() ? "\n" : ",\n");
            }
            manifest << "    ]\n";
        }
        else
        {
            manifest << ",\n"
                     << "    \"img\": {\n"
                     << "      \"name\": \"" << CACHED_IMG_FILE << "\",\n"
                     << "      \"bytes\": " << request.img.bytes << ",\n"
                     << "      \"sha256\": \"" << request.img.sha256 << "\"\n"
                     << "    }\n";
        }
        manifest << "  }\n"
                 << "}\n";
        return manifest.str();
    }

    bool HasValidCacheRequestFiles(const SNativeWorldCacheRequestSA& request)
    {
        const bool isV3 = request.format == 3;
        if (request.manifestFileName != CACHED_MANIFEST_FILE || !IsSafeLeafName(request.manifestFileName, 63) || !IsSafeLeafName(request.ide.name, 63) ||
            request.manifestFileName == request.ide.name || !IsLowerSha256(request.sourceManifestSha256) || !IsLowerSha256(request.ide.sha256) ||
            !request.sourceManifestBytes || !request.ide.bytes || request.sourceManifestBytes > request.maximumManifestBytes ||
            request.maximumManifestBytes > (isV3 ? 64ULL * 1024ULL : 4096ULL) || request.ide.bytes > (isV3 ? 8ULL * 1024ULL * 1024ULL : 1024ULL * 1024ULL))
        {
            return false;
        }

        if (!isV3)
        {
            return request.images.empty() && IsSafeLeafName(request.img.name, 63) && request.manifestFileName != request.img.name &&
                   request.ide.name != request.img.name && IsLowerSha256(request.img.sha256) && request.img.bytes && request.img.bytes <= MAX_V3_IMG_BYTES &&
                   request.img.bytes % 2048 == 0;
        }

        if (!request.img.name.empty() || !request.img.sha256.empty() || request.img.bytes || request.lod.name != CACHED_LOD_FILE ||
            !IsLowerSha256(request.lod.sha256) || !request.lod.bytes || request.lod.bytes > 256ULL * 1024ULL || request.images.empty() ||
            request.images.size() > MAX_V3_IMAGES)
            return false;
        std::uint64_t totalBytes = request.ide.bytes + request.lod.bytes;
        for (size_t index = 0; index < request.images.size(); ++index)
        {
            const SNativeWorldCacheFileSA& image = request.images[index];
            if (!IsCanonicalV3ImageName(image.name, index) || !IsLowerSha256(image.sha256) || !image.bytes || image.bytes > MAX_V3_IMG_BYTES ||
                image.bytes % 2048 != 0 || totalBytes > MAX_V3_TOTAL_BYTES - image.bytes)
            {
                return false;
            }
            totalBytes += image.bytes;
        }
        return totalBytes <= MAX_V3_TOTAL_BYTES;
    }

    struct SCachePaths
    {
        SString              dataRoot;
        SString              root;
        SString              format;
        SString              pack;
        SString              published;
        SString              manifest;
        SString              ide;
        SString              lod;
        SString              img;
        std::vector<SString> images;
    };

    SCachePaths MakeCachePaths(const SNativeWorldCacheRequestSA& request, const SString& publishedDirectory)
    {
        SCachePaths paths;
        paths.dataRoot = SharedUtil::GetMTADataPath();
        paths.root = JoinPath(paths.dataRoot, CACHE_ROOT_DIRECTORY);
        paths.format = JoinPath(paths.root, CacheFormatDirectory(request));
        paths.pack = JoinPath(paths.format, CachePolicyDirectory(request));
        paths.published = publishedDirectory;
        paths.manifest = JoinPath(paths.published, CACHED_MANIFEST_FILE);
        paths.ide = JoinPath(paths.published, CACHED_IDE_FILE);
        paths.lod = JoinPath(paths.published, CACHED_LOD_FILE);
        paths.img = JoinPath(paths.published, CACHED_IMG_FILE);
        for (const SNativeWorldCacheFileSA& image : request.images)
            paths.images.push_back(JoinPath(paths.published, image.name));
        return paths;
    }

    bool LockAndValidatePublishedFiles(const SNativeWorldCacheRequestSA& request, const SCachePaths& paths, CScopedHandles& handles, std::string& error)
    {
        const std::string canonicalManifest = BuildCanonicalManifest(request);
        const std::string canonicalManifestHash = SharedUtil::GenerateSha256HexString(canonicalManifest);
        if (!LockRegularFile(paths.manifest, canonicalManifest.size(), canonicalManifest.size(), handles, error) ||
            !LockRegularFile(paths.ide, request.ide.bytes, request.ide.bytes, handles, error))
            return false;
        if (request.format == 3)
        {
            if (!LockRegularFile(paths.lod, request.lod.bytes, request.lod.bytes, handles, error))
                return false;
            for (size_t index = 0; index < request.images.size(); ++index)
            {
                if (!LockRegularFile(paths.images[index], request.images[index].bytes, request.images[index].bytes, handles, error))
                    return false;
            }
        }
        else if (!LockRegularFile(paths.img, request.img.bytes, request.img.bytes, handles, error))
            return false;
        if (!HasExactHash(paths.manifest, canonicalManifestHash) || !HasExactHash(paths.ide, request.ide.sha256) ||
            (request.format == 3 && !HasExactHash(paths.lod, request.lod.sha256)))
        {
            error = "published cache SHA-256 differs from its semantic content address";
            return false;
        }
        if (request.format == 3)
        {
            for (size_t index = 0; index < request.images.size(); ++index)
            {
                if (!HasExactHash(paths.images[index], request.images[index].sha256))
                {
                    error = "published cache IMG SHA-256 differs from its semantic content address";
                    return false;
                }
            }
        }
        else if (!HasExactHash(paths.img, request.img.sha256))
        {
            error = "published cache SHA-256 differs from its semantic content address";
            return false;
        }
        return true;
    }

    std::set<std::wstring> ExpectedCachedFileNames(const SNativeWorldCacheRequestSA& request)
    {
        std::set<std::wstring> names{L"native-world.json", L"world.ide"};
        if (request.format == 3)
        {
            names.emplace(L"world.lod");
            for (const SNativeWorldCacheFileSA& image : request.images)
                names.emplace(SharedUtil::FromUTF8(image.name).c_str());
        }
        else
            names.emplace(L"world.img");
        return names;
    }

    bool ValidateClosedPublishedDirectory(const SString& directory, const std::set<std::wstring>& expectedNames, std::string& error)
    {
        WIN32_FIND_DATAW data{};
        const SString    pattern = SString("%s\\*", directory.c_str());
        HANDLE           find = FindFirstFileW(SharedUtil::FromUTF8(pattern).c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
        {
            error = SString("cache directory enumeration failed win32=%u", GetLastError());
            return false;
        }
        std::set<std::wstring> remaining = expectedNames;
        bool                   valid = true;
        do
        {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            if (remaining.erase(name) != 1 || (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            {
                valid = false;
                error = "published cache object is not the exact closed file directory";
                break;
            }
        } while (FindNextFileW(find, &data));
        const DWORD findError = GetLastError();
        FindClose(find);
        if (valid && findError != ERROR_NO_MORE_FILES)
        {
            error = SString("cache directory enumeration failed win32=%u", findError);
            return false;
        }
        if (valid && !remaining.empty())
        {
            error = "published cache object is missing a declared file";
            return false;
        }
        return valid;
    }

    bool FillRandomToken(std::string& token, std::string& error)
    {
        using RtlGenRandomFunction = BOOLEAN(WINAPI*)(PVOID, ULONG);
        const HMODULE advapi = GetModuleHandleW(L"advapi32.dll");
#pragma warning(suppress : 4191)  // GetProcAddress is the Win32 contract for resolving SystemFunction036.
        const auto    rtlGenRandom = advapi ? reinterpret_cast<RtlGenRandomFunction>(GetProcAddress(advapi, "SystemFunction036")) : nullptr;
        unsigned char bytes[16]{};
        if (!rtlGenRandom || !rtlGenRandom(bytes, sizeof(bytes)))
        {
            error = "secure quarantine-name generation failed";
            return false;
        }
        static constexpr char HEX[] = "0123456789abcdef";
        token.resize(sizeof(bytes) * 2);
        for (size_t index = 0; index < sizeof(bytes); ++index)
        {
            token[index * 2] = HEX[bytes[index] >> 4];
            token[index * 2 + 1] = HEX[bytes[index] & 0x0F];
        }
        return true;
    }

    bool WriteAndFlushFile(const SString& path, const std::string& bytes, std::string& error)
    {
        const HANDLE file = CreateFileW(SharedUtil::FromUTF8(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = SString("cache canonical-manifest creation failed win32=%u", GetLastError());
            return false;
        }
        DWORD      written = 0;
        const bool success =
            WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(file);
        const DWORD writeError = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!success)
        {
            error = SString("cache canonical-manifest write failed win32=%u", writeError);
            return false;
        }
        return true;
    }

    bool CopyHashAndFlushFile(const SString& source, const SString& destination, const SNativeWorldCacheRequestSA& request,
                              const SNativeWorldCacheFileSA& identity, std::string& error)
    {
        const HANDLE input = CreateFileW(SharedUtil::FromUTF8(source).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        const HANDLE output = CreateFileW(SharedUtil::FromUTF8(destination).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE)
        {
            const DWORD openError = GetLastError();
            if (input != INVALID_HANDLE_VALUE)
                CloseHandle(input);
            if (output != INVALID_HANDLE_VALUE)
                CloseHandle(output);
            error = SString("cache quarantine stream open failed file=%s win32=%u", identity.name.c_str(), openError);
            return false;
        }

        sha256_ctx hash;
        // Keep worker stack use modest: publication runs on an implementation-
        // defined std::async stack and the copy is intentionally streaming.
        std::array<unsigned char, 64 * 1024> buffer{};
        uint64_t                             total = 0;
        bool                                 success = true;
        sha256_init(&hash);
        while (success)
        {
            if (IsCancelled(request))
            {
                error = "native world cache publication was cancelled";
                success = false;
                break;
            }
            DWORD read = 0;
            if (!ReadFile(input, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
            {
                success = false;
                break;
            }
            if (!read)
                break;
            total += read;
            if (total > identity.bytes)
            {
                success = false;
                break;
            }
            sha256_update(&hash, buffer.data(), read);
            DWORD written = 0;
            success = WriteFile(output, buffer.data(), read, &written, nullptr) && written == read;
        }
        unsigned char digest[SHA256_DIGEST_SIZE]{};
        sha256_final(&hash, digest);
        const SString actualHash = SharedUtil::ConvertDataToHexString(digest, sizeof(digest)).ToLower();
        success = success && total == identity.bytes && actualHash == identity.sha256.c_str() && FlushFileBuffers(output);
        const DWORD copyError = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(input);
        CloseHandle(output);
        if (!success)
        {
            if (error.empty())
                error = SString("cache quarantine stream identity failed file=%s bytes=%llu expected=%llu win32=%u", identity.name.c_str(), total,
                                identity.bytes, copyError);
            return false;
        }
        return true;
    }

    bool DeleteVerifiedChild(const SString& path)
    {
        const HANDLE file =
            CreateFileW(SharedUtil::FromUTF8(path).c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;

        std::string           error;
        FILE_DISPOSITION_INFO disposition{TRUE};
        const bool            safe =
            HandleMatchesPath(file, path, false, error) && SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
        CloseHandle(file);
        return safe;
    }

    void RemoveVerifiedDirectory(const SNativeWorldCacheRequestSA& request, const SCachePaths& paths, CScopedHandles& directoryGuard)
    {
        // Enumerate only canonical leaf names while the verified parent handle
        // is held. This collects interrupted format-3 publications even when a
        // prior request used a different bounded IMG count, without traversing
        // an unknown child.
        WIN32_FIND_DATAW     data{};
        const HANDLE         search = FindFirstFileW(SharedUtil::FromUTF8(JoinPath(paths.published, "*")).c_str(), &data);
        std::vector<SString> children;
        bool                 safe = search != INVALID_HANDLE_VALUE || GetLastError() == ERROR_FILE_NOT_FOUND;
        if (search != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::string name = SharedUtil::ToUTF8(data.cFileName);
                if (name == "." || name == "..")
                    continue;
                bool known = name == CACHED_MANIFEST_FILE || name == CACHED_IDE_FILE || (request.format == 3 && name == CACHED_LOD_FILE);
                if (request.format == 3 && name.size() == 8 && name.front() == 'w' && name.compare(4, 4, ".img") == 0 &&
                    std::all_of(name.begin() + 1, name.begin() + 4, [](unsigned char c) { return c >= '0' && c <= '9'; }))
                {
                    const unsigned int index = static_cast<unsigned int>((name[1] - '0') * 100 + (name[2] - '0') * 10 + name[3] - '0');
                    known = index < MAX_V3_IMAGES;
                }
                else if (request.format != 3)
                    known = known || name == CACHED_IMG_FILE;
                if (!known || (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                {
                    safe = false;
                    break;
                }
                children.push_back(JoinPath(paths.published, name));
            } while (FindNextFileW(search, &data));
            const DWORD findError = GetLastError();
            FindClose(search);
            safe = safe && findError == ERROR_NO_MORE_FILES;
        }

        bool childrenRemoved = safe;
        if (safe)
        {
            for (const SString& child : children)
                childrenRemoved = DeleteVerifiedChild(child) && childrenRemoved;
        }
        directoryGuard.Close();
        if (childrenRemoved)
            RemoveDirectoryW(SharedUtil::FromUTF8(paths.published).c_str());
    }

    SString MakePrivateSibling(const SCachePaths& paths, const char* kind, std::string& error)
    {
        std::string token;
        if (!FillRandomToken(token, error))
            return {};
        return JoinPath(paths.pack, SString(".%s.%s.%s", paths.published.SubStr(paths.published.length() - 64).c_str(), kind, token.c_str()));
    }

    bool CheckTransportCacheQuota(const SCachePaths& paths, const SNativeWorldCacheRequestSA& request, std::string& error)
    {
        constexpr size_t        LEGACY_MAX_OBJECTS = 4;
        constexpr size_t        V3_MAX_OBJECTS = 8;
        constexpr std::uint64_t LEGACY_MAX_TOTAL_BYTES = 1024ULL * 1024ULL * 1024ULL;
        // Eight objects provide an active and replacement bank for four city
        // packs. Keep the independent 32 GiB aggregate byte ceiling: the
        // object count permits transactional rollover but does not authorize
        // eight worst-case 8 GiB payloads.
        constexpr std::uint64_t V3_MAX_CACHE_BYTES = 32ULL * 1024ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t LEGACY_MAX_IDE_BYTES = 1024ULL * 1024ULL;
        constexpr std::uint64_t V3_MAX_IDE_BYTES = 8ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t MINIMUM_V3_FREE_MARGIN = 512ULL * 1024ULL * 1024ULL;
        constexpr std::uint64_t MINIMUM_LEGACY_FREE_MARGIN = 64ULL * 1024ULL * 1024ULL;
        const bool              isV3 = request.format == 3;
        const size_t            maximumObjects = isV3 ? V3_MAX_OBJECTS : LEGACY_MAX_OBJECTS;
        const std::uint64_t     maximumIdeBytes = isV3 ? V3_MAX_IDE_BYTES : LEGACY_MAX_IDE_BYTES;
        const std::uint64_t     maximumCacheBytes = isV3 ? V3_MAX_CACHE_BYTES : LEGACY_MAX_TOTAL_BYTES;

        std::uint64_t payloadBytes = request.ide.bytes + (isV3 ? request.lod.bytes : 0);
        if (isV3)
        {
            for (const SNativeWorldCacheFileSA& image : request.images)
            {
                if (image.bytes > MAX_V3_IMG_BYTES || payloadBytes > MAX_V3_TOTAL_BYTES - image.bytes)
                {
                    error = "native world cache request exceeds the format-3 transport byte policy";
                    return false;
                }
                payloadBytes += image.bytes;
            }
        }
        else
        {
            if (request.maximumManifestBytes + payloadBytes > LEGACY_MAX_TOTAL_BYTES - request.img.bytes)
            {
                error = "native world cache request exceeds the legacy transport byte policy";
                return false;
            }
            payloadBytes += request.img.bytes;
        }
        if (request.ide.bytes > maximumIdeBytes || payloadBytes > (isV3 ? MAX_V3_TOTAL_BYTES : LEGACY_MAX_TOTAL_BYTES))
        {
            error = "native world cache request exceeds the transport byte policy";
            return false;
        }

        const std::uint64_t requestedDiskBytes = request.maximumManifestBytes + payloadBytes;
        const std::uint64_t freeMargin = isV3 ? std::max(MINIMUM_V3_FREE_MARGIN, requestedDiskBytes / 8) : MINIMUM_LEGACY_FREE_MARGIN;
        ULARGE_INTEGER      freeBytes{};
        if (!GetDiskFreeSpaceExW(SharedUtil::FromUTF8(paths.pack).c_str(), &freeBytes, nullptr, nullptr) ||
            requestedDiskBytes > std::numeric_limits<std::uint64_t>::max() - freeMargin || freeBytes.QuadPart < requestedDiskBytes + freeMargin)
        {
            error = SString("native world cache has insufficient free space required=%llu margin=%llu available=%llu", requestedDiskBytes, freeMargin,
                            freeBytes.QuadPart);
            return false;
        }

        WIN32_FIND_DATAW data{};
        const WString    pattern = SharedUtil::FromUTF8(JoinPath(paths.pack, "*"));
        const HANDLE     search = FindFirstFileW(pattern.c_str(), &data);
        size_t           objects = 0;
        std::uint64_t    bytes = request.sourceManifestBytes + request.ide.bytes + (isV3 ? request.lod.bytes : 0);
        if (isV3)
        {
            for (const SNativeWorldCacheFileSA& image : request.images)
                bytes += image.bytes;
        }
        else
            bytes += request.img.bytes;
        if (search == INVALID_HANDLE_VALUE)
        {
            const DWORD findError = GetLastError();
            if (findError == ERROR_FILE_NOT_FOUND)
                return true;
            error = SString("native world cache quota enumeration failed win32=%u", findError);
            return false;
        }

        bool valid = true;
        do
        {
            const std::string name = SharedUtil::ToUTF8(data.cFileName);
            if (name == "." || name == "..")
                continue;
            if (!name.empty() && name.front() == '.')
            {
                if (!IsPrivateCacheSibling(name) || !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                {
                    error = "native world cache contains an unsafe private sibling";
                    valid = false;
                    break;
                }

                SCachePaths    remnant = MakeCachePaths(request, JoinPath(paths.pack, name));
                CScopedHandles remnantGuard;
                if (!LockDirectory(remnant.published, remnantGuard, error))
                {
                    valid = false;
                    break;
                }
                RemoveVerifiedDirectory(request, remnant, remnantGuard);
                if (GetFileAttributesW(SharedUtil::FromUTF8(remnant.published).c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    error = "native world cache private sibling could not be safely collected";
                    valid = false;
                    break;
                }
                continue;
            }
            if (!IsLowerSha256(name) || !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            {
                error = "native world cache contains an unsafe quota sibling";
                valid = false;
                break;
            }
            if (name == request.contentId)
                continue;

            ++objects;
            SCachePaths    object = MakeCachePaths(request, JoinPath(paths.pack, name));
            CScopedHandles locks;
            if (!LockDirectory(object.published, locks, error) || !LockRegularFile(object.manifest, 1, request.maximumManifestBytes, locks, error) ||
                !LockRegularFile(object.ide, 1, maximumIdeBytes, locks, error))
            {
                valid = false;
                break;
            }

            std::vector<SString> objectImages;
            bool                 objectHasLod = false;
            if (isV3)
            {
                WIN32_FIND_DATAW child{};
                const HANDLE     children = FindFirstFileW(SharedUtil::FromUTF8(JoinPath(object.published, "*")).c_str(), &child);
                if (children == INVALID_HANDLE_VALUE)
                {
                    error = "native world cache object enumeration failed";
                    valid = false;
                    break;
                }
                std::set<unsigned int> indices;
                bool                   childrenValid = true;
                do
                {
                    const std::string childName = SharedUtil::ToUTF8(child.cFileName);
                    if (childName == "." || childName == ".." || childName == "native-world.json" || childName == "world.ide")
                        continue;
                    if (childName == "world.lod")
                    {
                        objectHasLod = true;
                        continue;
                    }
                    if ((child.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || childName.size() != 8 ||
                        childName.front() != 'w' || childName.compare(4, 4, ".img") != 0 ||
                        !std::all_of(childName.begin() + 1, childName.begin() + 4, [](unsigned char c) { return c >= '0' && c <= '9'; }))
                    {
                        childrenValid = false;
                        break;
                    }
                    const unsigned int index = static_cast<unsigned int>((childName[1] - '0') * 100 + (childName[2] - '0') * 10 + childName[3] - '0');
                    if (index >= MAX_V3_IMAGES || !indices.emplace(index).second)
                    {
                        childrenValid = false;
                        break;
                    }
                    objectImages.push_back(JoinPath(object.published, childName));
                } while (FindNextFileW(children, &child));
                const DWORD childrenError = GetLastError();
                FindClose(children);
                if (!childrenValid || childrenError != ERROR_NO_MORE_FILES || objectImages.empty() || objectImages.size() != indices.size())
                {
                    error = "native world cache object contains an invalid format-3 file set";
                    valid = false;
                    break;
                }
                if (objectHasLod && !LockRegularFile(object.lod, 1, 256ULL * 1024ULL, locks, error))
                {
                    valid = false;
                    break;
                }
                for (size_t index = 0; index < objectImages.size(); ++index)
                {
                    const SString imageName("w%03u.img", static_cast<unsigned int>(index));
                    if (!indices.count(static_cast<unsigned int>(index)) ||
                        !LockRegularFile(JoinPath(object.published, imageName.c_str()), 1, MAX_V3_IMG_BYTES, locks, error))
                    {
                        valid = false;
                        break;
                    }
                }
                if (!valid)
                    break;
            }
            else if (!LockRegularFile(object.img, 1, MAX_V3_IMG_BYTES, locks, error))
            {
                valid = false;
                break;
            }

            const auto addObjectBytes = [&bytes, maximumCacheBytes](std::uint64_t amount)
            {
                if (amount > maximumCacheBytes - bytes)
                    return false;
                bytes += amount;
                return true;
            };
            if (!addObjectBytes(FileSize(object.manifest)) || !addObjectBytes(FileSize(object.ide)) ||
                (isV3 && objectHasLod && !addObjectBytes(FileSize(object.lod))))
                valid = false;
            if (valid && isV3)
            {
                for (const SString& image : objectImages)
                {
                    if (!addObjectBytes(FileSize(image)))
                    {
                        valid = false;
                        break;
                    }
                }
            }
            else if (valid && !addObjectBytes(FileSize(object.img)))
                valid = false;
            if (!valid || objects >= maximumObjects || bytes > maximumCacheBytes)
            {
                error = "native world cache transport quota is exhausted";
                valid = false;
                break;
            }
        } while (FindNextFileW(search, &data));
        FindClose(search);
        return valid;
    }
}  // namespace

std::string GenerateNativeWorldContentId(const SNativeWorldCacheRequestSA& request)
{
    // The domain-separated semantic tuple stays stable across JSON whitespace
    // and key order while still binding compiled policy identity and payload.
    std::ostringstream identity;
    identity.imbue(std::locale::classic());
    identity << (request.format == 1   ? CONTENT_ID_DOMAIN_V1
                 : request.format == 2 ? CONTENT_ID_DOMAIN_V2
                                       : CONTENT_ID_DOMAIN_V3)
             << '\n'
             << "format=" << request.format << '\n';
    if (request.format == 1)
        identity << "policy=" << request.packId << '\n';
    else
        identity << "policy=" << request.policyKey << '\n' << "pack_id=" << request.packId << '\n';
    identity << "ide.bytes=" << request.ide.bytes << '\n' << "ide.sha256=" << request.ide.sha256 << '\n';
    if (request.format == 3)
    {
        identity << "lod.bytes=" << request.lod.bytes << '\n' << "lod.sha256=" << request.lod.sha256 << '\n';
        for (const SNativeWorldCacheFileSA& image : request.images)
            identity << "img.name=" << image.name << '\n' << "img.bytes=" << image.bytes << '\n' << "img.sha256=" << image.sha256 << '\n';
    }
    else
        identity << "img.bytes=" << request.img.bytes << '\n' << "img.sha256=" << request.img.sha256 << '\n';
    // SharedUtil renders digests in uppercase. Cache paths and manifests use
    // the closed lowercase SHA-256 spelling so C++ and transport tooling share
    // one canonical content ID.
    return SharedUtil::GenerateSha256HexString(identity.str()).ToLower();
}

namespace
{
    bool PrepareNativeWorldCacheImpl(const SNativeWorldCacheRequestSA& request, std::string& publishedDirectory, bool& cacheHit, std::string& error,
                                     bool retainLease, bool recoverInvalid, const NativeWorldCacheAuditSA* audit)
    {
        if (IsCancelled(request))
        {
            error = "native world cache publication was cancelled";
            return false;
        }
        if (retainLease && (g_cachePrepared || !g_pendingLocks.empty()))
        {
            error = "native world cache can be prepared only once per startup transaction";
            return false;
        }
        if (retainLease && request.format != 1)
        {
            error = request.format == 2 ? "format-2 static-world publication cannot acquire an activation lease"
                                        : "format-3 static-world publication is transport-only and cannot acquire an activation lease";
            return false;
        }
        if (!IsValidRequestIdentity(request) || !HasValidCacheRequestFiles(request) || !IsLowerSha256(request.contentId) ||
            GenerateNativeWorldContentId(request) != request.contentId)
        {
            error = "native world cache request identity is invalid";
            return false;
        }
        if (BuildCanonicalManifest(request).size() > request.maximumManifestBytes)
        {
            error = "canonical native world manifest exceeds the compiled policy";
            return false;
        }

        const SString dataRoot = SharedUtil::GetMTADataPath();
        const SString root = JoinPath(dataRoot, CACHE_ROOT_DIRECTORY);
        const SString format = JoinPath(root, CacheFormatDirectory(request));
        const SString pack = JoinPath(format, CachePolicyDirectory(request));
        const SString published = JoinPath(pack, request.contentId);
        publishedDirectory = published.c_str();
        SCachePaths paths = MakeCachePaths(request, published);
        if (!EnsurePlainDirectory(paths.dataRoot, error))
            return false;

        CScopedHandles parentLocks;
        // Lock each verified parent before creating or opening its child. This
        // prevents a raced ancestor rename from redirecting cache writes.
        if (!LockDirectory(paths.dataRoot, parentLocks, error) || !EnsurePlainDirectory(paths.root, error) || !LockDirectory(paths.root, parentLocks, error) ||
            !EnsurePlainDirectory(paths.format, error) || !LockDirectory(paths.format, parentLocks, error) || !EnsurePlainDirectory(paths.pack, error) ||
            !LockDirectory(paths.pack, parentLocks, error))
            return false;

        bool existingNeedsRecovery = GetFileAttributesW(SharedUtil::FromUTF8(paths.published).c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!retainLease && !existingNeedsRecovery && !CheckTransportCacheQuota(paths, request, error))
            return false;
        if (existingNeedsRecovery)
        {
            CScopedHandles existingLocks;
            std::string    existingError;
            if (LockDirectory(paths.published, existingLocks, existingError) &&
                ValidateClosedPublishedDirectory(paths.published, ExpectedCachedFileNames(request), existingError) &&
                LockAndValidatePublishedFiles(request, paths, existingLocks, existingError))
            {
                if (IsCancelled(request))
                {
                    error = "native world cache publication was cancelled";
                    return false;
                }
                if (retainLease)
                {
                    parentLocks.TransferTo(g_pendingLocks);
                    existingLocks.TransferTo(g_pendingLocks);
                }
                cacheHit = true;
                g_cachePrepared = retainLease;
                return true;
            }
            existingLocks.Close();

            if (!recoverInvalid)
            {
                error = "conflicting immutable cache content already occupies the semantic address: " + existingError;
                return false;
            }
            // A cache hit must not require spare disk, but rebuilding an
            // invalid transport object temporarily needs a complete second
            // copy. Recheck the same object-count, byte and free-margin policy
            // before moving the failed object away from its semantic address.
            if (!retainLease && !CheckTransportCacheQuota(paths, request, error))
                return false;

            // A power-loss remnant or corrupt object is never loaded. Move it away
            // from the semantic address atomically, then rebuild from the locked
            // local seed. Unknown extra files keep the invalid sibling nonempty but
            // cannot block or alias the final address.
            const SString invalidPath = MakePrivateSibling(paths, "invalid", error);
            if (invalidPath.empty())
                return false;
            if (!MoveFileExW(SharedUtil::FromUTF8(paths.published).c_str(), SharedUtil::FromUTF8(invalidPath).c_str(), MOVEFILE_WRITE_THROUGH))
            {
                error = SString("invalid cache object recovery failed prior=%s win32=%u", existingError.c_str(), GetLastError());
                return false;
            }
            // Never traverse an object that failed handle validation: it may have
            // been a junction. A later verified GC may remove this inert sibling.
        }

        SString sourceDirectory;
        if (request.sourceAbsoluteDirectory.empty())
            sourceDirectory = SharedUtil::CalcMTASAPath(request.sourceRelativeDirectory.c_str());
        else
            sourceDirectory = request.sourceAbsoluteDirectory.c_str();
        SCachePaths sourcePaths = MakeCachePaths(request, sourceDirectory);
        sourcePaths.manifest = JoinPath(sourceDirectory, request.manifestFileName);
        sourcePaths.ide = JoinPath(sourceDirectory, request.ide.name);
        sourcePaths.lod = JoinPath(sourceDirectory, request.lod.name);
        sourcePaths.img = JoinPath(sourceDirectory, request.img.name);
        CScopedHandles sourceLocks;
        if (!IsSafeLocalPath(sourceDirectory) || !LockDirectory(sourceDirectory, sourceLocks, error) ||
            !LockRegularFile(sourcePaths.manifest, request.sourceManifestBytes, request.sourceManifestBytes, sourceLocks, error) ||
            !LockRegularFile(sourcePaths.ide, request.ide.bytes, request.ide.bytes, sourceLocks, error))
        {
            if (error.empty())
                error = "local cache seed SHA-256 differs from its parsed manifest";
            error = "local cache seed is invalid: " + error;
            return false;
        }
        if (request.format == 3)
        {
            if (!LockRegularFile(sourcePaths.lod, request.lod.bytes, request.lod.bytes, sourceLocks, error))
            {
                error = "local cache seed is invalid: " + error;
                return false;
            }
            for (size_t index = 0; index < request.images.size(); ++index)
            {
                if (!LockRegularFile(sourcePaths.images[index], request.images[index].bytes, request.images[index].bytes, sourceLocks, error))
                {
                    error = "local cache seed is invalid: " + error;
                    return false;
                }
            }
        }
        else if (!LockRegularFile(sourcePaths.img, request.img.bytes, request.img.bytes, sourceLocks, error))
        {
            error = "local cache seed is invalid: " + error;
            return false;
        }
        if (!HasExactHash(sourcePaths.manifest, request.sourceManifestSha256) || !HasExactHash(sourcePaths.ide, request.ide.sha256) ||
            (request.format == 3 && !HasExactHash(sourcePaths.lod, request.lod.sha256)))
        {
            error = "local cache seed SHA-256 differs from its parsed manifest";
            return false;
        }
        if (request.format == 3)
        {
            for (size_t index = 0; index < request.images.size(); ++index)
            {
                if (!HasExactHash(sourcePaths.images[index], request.images[index].sha256))
                {
                    error = "local cache seed IMG SHA-256 differs from its parsed manifest";
                    return false;
                }
            }
        }
        else if (!HasExactHash(sourcePaths.img, request.img.sha256))
        {
            error = "local cache seed SHA-256 differs from its parsed manifest";
            return false;
        }

        const SString quarantinePath = MakePrivateSibling(paths, "quarantine", error);
        if (quarantinePath.empty())
            return false;
        if (!CreateDirectoryW(SharedUtil::FromUTF8(quarantinePath).c_str(), nullptr))
        {
            error = SString("cache quarantine directory creation failed win32=%u", GetLastError());
            return false;
        }
        SCachePaths    quarantine = MakeCachePaths(request, quarantinePath);
        CScopedHandles quarantineDirectoryGuard;
        if (!LockDirectory(quarantine.published, quarantineDirectoryGuard, error))
        {
            // The unverified path is never traversed. Its random, non-addressable
            // name cannot become an activation candidate.
            return false;
        }
        const std::string canonicalManifest = BuildCanonicalManifest(request);
        bool              filled = WriteAndFlushFile(quarantine.manifest, canonicalManifest, error) &&
                      CopyHashAndFlushFile(sourcePaths.ide, quarantine.ide, request, request.ide, error);
        if (filled && request.format == 3)
        {
            filled = CopyHashAndFlushFile(sourcePaths.lod, quarantine.lod, request, request.lod, error);
            for (size_t index = 0; filled && index < request.images.size(); ++index)
            {
                if (!CopyHashAndFlushFile(sourcePaths.images[index], quarantine.images[index], request, request.images[index], error))
                {
                    filled = false;
                    break;
                }
            }
        }
        else if (filled)
            filled = CopyHashAndFlushFile(sourcePaths.img, quarantine.img, request, request.img, error);
        if (!filled)
        {
            RemoveVerifiedDirectory(request, quarantine, quarantineDirectoryGuard);
            return false;
        }

        CScopedHandles publishedFileLocks;
        if (!ValidateClosedPublishedDirectory(quarantine.published, ExpectedCachedFileNames(request), error) ||
            !LockAndValidatePublishedFiles(request, quarantine, publishedFileLocks, error))
        {
            publishedFileLocks.Close();
            RemoveVerifiedDirectory(request, quarantine, quarantineDirectoryGuard);
            error = "cache quarantine validation failed: " + error;
            return false;
        }
        // Transport bytes become addressable only after the caller has performed
        // its closed semantic audit while the verified quarantine directory and
        // all three files are held without write/delete sharing.
        if (IsCancelled(request) || (audit && !(*audit)(quarantine.published.c_str(), error)))
        {
            if (error.empty())
                error = "native world cache publication was cancelled";
            publishedFileLocks.Close();
            RemoveVerifiedDirectory(request, quarantine, quarantineDirectoryGuard);
            error = "cache quarantine semantic audit failed: " + error;
            return false;
        }
        publishedFileLocks.Close();
        quarantineDirectoryGuard.Close();

        if (IsCancelled(request))
        {
            CScopedHandles cancelledGuard;
            if (LockDirectory(quarantine.published, cancelledGuard, error))
                RemoveVerifiedDirectory(request, quarantine, cancelledGuard);
            error = "native world cache publication was cancelled";
            return false;
        }

        // Windows refuses to rename a directory containing children opened without
        // delete sharing. Close the private quarantine guards, publish by one
        // same-volume rename, then acquire final guards and repeat the complete
        // validation before the directory is returned to the registrar.
        if (!MoveFileExW(SharedUtil::FromUTF8(quarantine.published).c_str(), SharedUtil::FromUTF8(paths.published).c_str(), MOVEFILE_WRITE_THROUGH))
        {
            const DWORD publishError = GetLastError();
            // Guards were closed for publication, so this path is no longer safe
            // to traverse. Leave the random sibling inert for verified GC.

            // Windows may report ACCESS_DENIED rather than ALREADY_EXISTS for a
            // racing directory publication. Converge only if the final object is
            // independently lockable and exact.
            CScopedHandles concurrentLocks;
            if ((publishError != ERROR_ALREADY_EXISTS && publishError != ERROR_FILE_EXISTS && publishError != ERROR_ACCESS_DENIED) ||
                !LockDirectory(paths.published, concurrentLocks, error) ||
                !ValidateClosedPublishedDirectory(paths.published, ExpectedCachedFileNames(request), error) ||
                !LockAndValidatePublishedFiles(request, paths, concurrentLocks, error))
            {
                error = SString("cache atomic publication failed win32=%u detail=%s", publishError, error.c_str());
                return false;
            }
            if (retainLease)
            {
                parentLocks.TransferTo(g_pendingLocks);
                concurrentLocks.TransferTo(g_pendingLocks);
            }
            cacheHit = true;
            g_cachePrepared = retainLease;
            return true;
        }

        CScopedHandles publishedLocks;
        if (!LockDirectory(paths.published, publishedLocks, error) ||
            !ValidateClosedPublishedDirectory(paths.published, ExpectedCachedFileNames(request), error) ||
            !LockAndValidatePublishedFiles(request, paths, publishedLocks, error))
        {
            error = "published cache object cannot be locked and revalidated: " + error;
            return false;
        }
        if (retainLease)
        {
            parentLocks.TransferTo(g_pendingLocks);
            publishedLocks.TransferTo(g_pendingLocks);
        }
        cacheHit = false;
        g_cachePrepared = retainLease;
        return true;
    }
}  // namespace

struct CNativeWorldCacheLeaseSA::SImpl
{
    ~SImpl()
    {
        for (HANDLE handle : handles)
            CloseHandle(handle);
    }

    std::vector<HANDLE>    handles;
    unsigned int           format{};
    std::string            policy;
    std::string            contentId;
    std::string            ticketId;
    std::string            directory;
    std::set<std::wstring> expectedFileNames;
};

CNativeWorldCacheLeaseSA::CNativeWorldCacheLeaseSA() = default;
CNativeWorldCacheLeaseSA::~CNativeWorldCacheLeaseSA() = default;
CNativeWorldCacheLeaseSA::CNativeWorldCacheLeaseSA(CNativeWorldCacheLeaseSA&&) noexcept = default;
CNativeWorldCacheLeaseSA& CNativeWorldCacheLeaseSA::operator=(CNativeWorldCacheLeaseSA&&) noexcept = default;

bool CNativeWorldCacheLeaseSA::IsValid() const
{
    return m_impl && !m_impl->handles.empty();
}

bool CNativeWorldCacheLeaseSA::RevalidateClosedObject(std::string& error) const
{
    if (!IsValid())
    {
        error = "native-world cache lease is absent or already completed";
        return false;
    }
    return ValidateClosedPublishedDirectory(m_impl->directory.c_str(), m_impl->expectedFileNames, error);
}

bool CNativeWorldCacheLeaseSA::Commit(unsigned int format, const std::string& policy, const std::string& contentId, const std::string& ticketId,
                                      std::string& error)
{
    if (!IsValid())
    {
        error = "native-world cache lease is absent or already completed";
        return false;
    }
    if (m_impl->format != format || m_impl->policy != policy || m_impl->contentId != contentId || m_impl->ticketId != ticketId)
    {
        error = "native-world cache lease transaction token mismatch";
        return false;
    }
    g_processLocks.insert(g_processLocks.end(), m_impl->handles.begin(), m_impl->handles.end());
    m_impl->handles.clear();
    m_impl.reset();
    return true;
}

void CNativeWorldCacheLeaseSA::Release()
{
    m_impl.reset();
}

bool AcquireExistingNativeWorldCacheLease(const SNativeWorldCacheRequestSA& request, const std::string& ticketId, const NativeWorldCacheAuditSA& audit,
                                          CNativeWorldCacheLeaseSA& lease, std::string& publishedDirectory, std::string& error)
{
    if (lease.IsValid())
    {
        error = "native-world cache lease output is already active";
        return false;
    }
    const bool isV3 = request.format == 3;
    if (!audit || !IsValidRequestIdentity(request) || !HasValidCacheRequestFiles(request) || request.manifestFileName != CACHED_MANIFEST_FILE ||
        request.ide.name != CACHED_IDE_FILE || (isV3 && request.lod.name != CACHED_LOD_FILE) || (!isV3 && request.img.name != CACHED_IMG_FILE) ||
        !IsLowerSha256(request.contentId) || !IsLowerHex(ticketId, 32) || GenerateNativeWorldContentId(request) != request.contentId ||
        BuildCanonicalManifest(request).size() > request.maximumManifestBytes)
    {
        error = "existing native-world cache selection identity is invalid";
        return false;
    }
    if (IsCancelled(request))
    {
        error = "existing native-world cache selection was cancelled";
        return false;
    }

    const SString dataRoot = SharedUtil::GetMTADataPath();
    const SString root = JoinPath(dataRoot, CACHE_ROOT_DIRECTORY);
    const SString format = JoinPath(root, CacheFormatDirectory(request));
    const SString pack = JoinPath(format, CachePolicyDirectory(request));
    const SString published = JoinPath(pack, request.contentId);
    publishedDirectory = published.c_str();
    const SCachePaths paths = MakeCachePaths(request, published);

    CScopedHandles locks;
    if (!LockDirectory(paths.dataRoot, locks, error) || !LockDirectory(paths.root, locks, error) || !LockDirectory(paths.format, locks, error) ||
        !LockDirectory(paths.pack, locks, error) || !LockDirectory(paths.published, locks, error) ||
        !ValidateClosedPublishedDirectory(paths.published, ExpectedCachedFileNames(request), error) ||
        !LockAndValidatePublishedFiles(request, paths, locks, error))
        return false;
    if (IsCancelled(request) || !audit(publishedDirectory, error) || IsCancelled(request) ||
        !ValidateClosedPublishedDirectory(paths.published, ExpectedCachedFileNames(request), error))
    {
        if (error.empty())
            error = "existing native-world cache audit was cancelled";
        return false;
    }

    auto impl = std::make_unique<CNativeWorldCacheLeaseSA::SImpl>();
    impl->format = request.format;
    impl->policy = request.policyKey;
    impl->contentId = request.contentId;
    impl->ticketId = ticketId;
    impl->directory = publishedDirectory;
    impl->expectedFileNames = ExpectedCachedFileNames(request);
    locks.TransferTo(impl->handles);
    lease.m_impl = std::move(impl);
    return true;
}

namespace
{
    constexpr const char* STATIC_WORLD_V3_SET_POLICY = "static-world-v3-set";
    constexpr const char* STATIC_WORLD_V3_SET_MANIFEST = "static-world-v3-set.json";
    constexpr const char* STATIC_WORLD_V3_SET_ID_DOMAIN = "mta-native-world-static-world-v3-set-v1";
    constexpr const char* STATIC_WORLD_V3_SET_PACKS[] = {"bullworth", "vice-city", "liberty-city", "carcer-city"};

    std::string BuildCanonicalNativeWorldV3Set(const SNativeWorldV3SetRequestSA& request)
    {
        std::ostringstream output;
        output << "{\n  \"format\": 3,\n  \"policy\": \"static-world-v3-set\",\n  \"set_id\": \"" << request.setId << "\",\n  \"packs\": [\n";
        for (size_t index = 0; index < request.packs.size(); ++index)
        {
            output << "    {\n      \"pack_id\": \"" << request.packs[index].packId << "\",\n      \"content_id\": \"" << request.packs[index].contentId
                   << "\"\n    }" << (index + 1 == request.packs.size() ? "\n" : ",\n");
        }
        output << "  ]\n}\n";
        return output.str();
    }

    bool IsValidNativeWorldV3SetRequest(const SNativeWorldV3SetRequestSA& request)
    {
        if (request.manifestFileName != STATIC_WORLD_V3_SET_MANIFEST || !IsLowerSha256(request.sourceManifestSha256) || !IsLowerSha256(request.setId) ||
            !request.sourceManifestBytes || request.sourceManifestBytes > 16 * 1024)
            return false;
        for (size_t index = 0; index < request.packs.size(); ++index)
            if (request.packs[index].packId != STATIC_WORLD_V3_SET_PACKS[index] || !IsLowerSha256(request.packs[index].contentId))
                return false;
        return GenerateNativeWorldV3SetId(request) == request.setId;
    }

    bool ValidateNativeWorldV3SetObject(const SString& directory, const SNativeWorldV3SetRequestSA& request, CScopedHandles& locks, std::string& error)
    {
        const std::string canonical = BuildCanonicalNativeWorldV3Set(request);
        const SString     manifest = JoinPath(directory, STATIC_WORLD_V3_SET_MANIFEST);
        return LockDirectory(directory, locks, error) && ValidateClosedPublishedDirectory(directory, {L"static-world-v3-set.json"}, error) &&
               LockRegularFile(manifest, canonical.size(), canonical.size(), locks, error) &&
               HasExactHash(manifest, SharedUtil::GenerateSha256HexString(canonical));
    }

    void RemoveVerifiedNativeWorldV3SetDirectory(const SString& directory, CScopedHandles& directoryGuard)
    {
        // The set envelope has one closed child. Keep the verified directory
        // handle alive until that child has been opened without following
        // reparse points and marked for deletion by handle.
        WIN32_FIND_DATAW data{};
        const HANDLE     search = FindFirstFileW(SharedUtil::FromUTF8(JoinPath(directory, "*")).c_str(), &data);
        bool             safe = search != INVALID_HANDLE_VALUE || GetLastError() == ERROR_FILE_NOT_FOUND;
        bool             manifestPresent = false;
        if (search != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::wstring name = data.cFileName;
                if (name == L"." || name == L"..")
                    continue;
                if (manifestPresent || name != L"static-world-v3-set.json" ||
                    (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                {
                    safe = false;
                    break;
                }
                manifestPresent = true;
            } while (FindNextFileW(search, &data));
            const DWORD findError = GetLastError();
            FindClose(search);
            safe = safe && findError == ERROR_NO_MORE_FILES;
        }
        if (safe && manifestPresent)
            safe = DeleteVerifiedChild(JoinPath(directory, STATIC_WORLD_V3_SET_MANIFEST));
        directoryGuard.Close();
        if (safe)
            RemoveDirectoryW(SharedUtil::FromUTF8(directory).c_str());
    }
}

std::string GenerateNativeWorldV3SetId(const SNativeWorldV3SetRequestSA& request)
{
    std::ostringstream identity;
    identity << STATIC_WORLD_V3_SET_ID_DOMAIN << "\nformat=3\npolicy=" << STATIC_WORLD_V3_SET_POLICY << '\n';
    for (size_t index = 0; index < request.packs.size(); ++index)
        identity << "pack[" << index << "].pack_id=" << request.packs[index].packId << "\npack[" << index << "].content_id=" << request.packs[index].contentId
                 << '\n';
    return SharedUtil::GenerateSha256HexString(identity.str()).ToLower();
}

bool PublishNativeWorldV3Set(const SNativeWorldV3SetRequestSA& request, const NativeWorldCacheAuditSA& audit, std::string& publishedDirectory, bool& cacheHit,
                             std::string& error)
{
    cacheHit = false;
    publishedDirectory.clear();
    if (!audit || !IsValidNativeWorldV3SetRequest(request) || (request.cancellation && request.cancellation->load(std::memory_order_acquire)))
    {
        error = "static-world-v3-set publication request is invalid or cancelled";
        return false;
    }
    const SString     sourceDirectory = request.sourceAbsoluteDirectory.c_str();
    const SString     sourceManifest = JoinPath(sourceDirectory, request.manifestFileName);
    const std::string canonical = BuildCanonicalNativeWorldV3Set(request);
    CScopedHandles    sourceLocks;
    if (!IsSafeLocalPath(sourceDirectory) || !LockDirectory(sourceDirectory, sourceLocks, error) ||
        !LockRegularFile(sourceManifest, request.sourceManifestBytes, request.sourceManifestBytes, sourceLocks, error))
    {
        error = "static-world-v3-set source envelope cannot be locked: " + error;
        return false;
    }
    std::ifstream source(SharedUtil::FromUTF8(sourceManifest), std::ios::binary);
    std::string   sourceBytes((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    if (!source || sourceBytes != canonical || sourceBytes.size() != request.sourceManifestBytes ||
        SharedUtil::GenerateSha256HexString(sourceBytes).ToLower() != request.sourceManifestSha256.c_str())
    {
        error = "static-world-v3-set source envelope is not the exact canonical byte sequence";
        return false;
    }

    const SString  dataRoot = SharedUtil::GetMTADataPath();
    const SString  root = JoinPath(dataRoot, CACHE_ROOT_DIRECTORY);
    const SString  format = JoinPath(root, "v3");
    const SString  policy = JoinPath(format, STATIC_WORLD_V3_SET_POLICY);
    const SString  published = JoinPath(policy, request.setId);
    CScopedHandles parents;
    if (!EnsurePlainDirectory(dataRoot, error) || !LockDirectory(dataRoot, parents, error) || !EnsurePlainDirectory(root, error) ||
        !LockDirectory(root, parents, error) || !EnsurePlainDirectory(format, error) || !LockDirectory(format, parents, error) ||
        !EnsurePlainDirectory(policy, error) || !LockDirectory(policy, parents, error))
        return false;

    if (GetFileAttributesW(SharedUtil::FromUTF8(published).c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        CScopedHandles directoryGuard;
        CScopedHandles fileLocks;
        const SString  manifest = JoinPath(published, STATIC_WORLD_V3_SET_MANIFEST);
        const bool     directoryLocked = LockDirectory(published, directoryGuard, error);
        const bool     objectValid = directoryLocked && ValidateClosedPublishedDirectory(published, {L"static-world-v3-set.json"}, error) &&
                                 LockRegularFile(manifest, canonical.size(), canonical.size(), fileLocks, error) &&
                                 HasExactHash(manifest, SharedUtil::GenerateSha256HexString(canonical));
        if (!objectValid)
        {
            if (error.empty())
                error = "static-world-v3-set cached envelope differs from its canonical identity";
            fileLocks.Close();
            if (directoryLocked)
                RemoveVerifiedNativeWorldV3SetDirectory(published, directoryGuard);
            return false;
        }
        if (!audit(published, error) || (request.cancellation && request.cancellation->load(std::memory_order_acquire)))
            return false;
        publishedDirectory = published;
        cacheHit = true;
        return true;
    }

    WIN32_FIND_DATAW quotaData{};
    const HANDLE     quotaSearch = FindFirstFileW(SharedUtil::FromUTF8(JoinPath(policy, "*")).c_str(), &quotaData);
    size_t           objectCount = 0;
    bool             quotaValid = quotaSearch != INVALID_HANDLE_VALUE || GetLastError() == ERROR_FILE_NOT_FOUND;
    if (quotaSearch != INVALID_HANDLE_VALUE)
    {
        do
        {
            const std::string name = SharedUtil::ToUTF8(quotaData.cFileName);
            if (name == "." || name == "..")
                continue;
            if (!name.empty() && name.front() == '.')
            {
                if (!IsPrivateCacheSibling(name) || !(quotaData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (quotaData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                {
                    quotaValid = false;
                    break;
                }
                const SString  remnant = JoinPath(policy, name);
                CScopedHandles remnantGuard;
                if (!LockDirectory(remnant, remnantGuard, error))
                {
                    quotaValid = false;
                    break;
                }
                RemoveVerifiedNativeWorldV3SetDirectory(remnant, remnantGuard);
                if (GetFileAttributesW(SharedUtil::FromUTF8(remnant).c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    error = "static-world-v3-set private sibling could not be safely collected";
                    quotaValid = false;
                    break;
                }
                continue;
            }
            if (!IsLowerSha256(name) || !(quotaData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (quotaData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            {
                quotaValid = false;
                break;
            }
            ++objectCount;
        } while (FindNextFileW(quotaSearch, &quotaData));
        const DWORD quotaError = GetLastError();
        FindClose(quotaSearch);
        quotaValid = quotaValid && quotaError == ERROR_NO_MORE_FILES;
    }
    if (!quotaValid || objectCount >= 8)
    {
        error = "static-world-v3-set metadata cache is unsafe or its eight-object quota is exhausted";
        return false;
    }

    SCachePaths paths;
    paths.pack = policy;
    paths.published = published;
    const SString quarantine = MakePrivateSibling(paths, "quarantine", error);
    if (quarantine.empty() || !CreateDirectoryW(SharedUtil::FromUTF8(quarantine).c_str(), nullptr))
    {
        if (error.empty())
            error = SString("static-world-v3-set quarantine creation failed win32=%u", GetLastError());
        return false;
    }
    const SString  quarantineManifest = JoinPath(quarantine, STATIC_WORLD_V3_SET_MANIFEST);
    CScopedHandles quarantineDirectory;
    if (!LockDirectory(quarantine, quarantineDirectory, error))
    {
        // Never traverse an unverified random sibling. Verified GC will
        // collect it on a later publication if it remains a plain directory.
        return false;
    }
    if (!WriteAndFlushFile(quarantineManifest, canonical, error))
    {
        RemoveVerifiedNativeWorldV3SetDirectory(quarantine, quarantineDirectory);
        return false;
    }
    CScopedHandles files;
    if (!ValidateNativeWorldV3SetObject(quarantine, request, files, error) || !audit(quarantine, error) ||
        (request.cancellation && request.cancellation->load(std::memory_order_acquire)))
    {
        files.Close();
        RemoveVerifiedNativeWorldV3SetDirectory(quarantine, quarantineDirectory);
        return false;
    }
    files.Close();
    quarantineDirectory.Close();
    if (!MoveFileExW(SharedUtil::FromUTF8(quarantine).c_str(), SharedUtil::FromUTF8(published).c_str(), MOVEFILE_WRITE_THROUGH))
    {
        const DWORD moveError = GetLastError();
        // Guards had to be closed for the rename. Leave the random sibling
        // inert for verified GC instead of traversing a path that may have
        // been replaced.
        if (moveError == ERROR_ALREADY_EXISTS || moveError == ERROR_FILE_EXISTS)
        {
            CScopedHandles winner;
            if (ValidateNativeWorldV3SetObject(published, request, winner, error) && audit(published, error) &&
                !(request.cancellation && request.cancellation->load(std::memory_order_acquire)))
            {
                publishedDirectory = published;
                cacheHit = true;
                return true;
            }
        }
        error = SString("static-world-v3-set atomic publish failed win32=%u detail=%s", moveError, error.c_str());
        return false;
    }
    CScopedHandles publishedDirectoryGuard;
    CScopedHandles publishedFileLocks;
    const SString  publishedManifest = JoinPath(published, STATIC_WORLD_V3_SET_MANIFEST);
    const bool     publishedDirectoryLocked = LockDirectory(published, publishedDirectoryGuard, error);
    if (!publishedDirectoryLocked || !ValidateClosedPublishedDirectory(published, {L"static-world-v3-set.json"}, error) ||
        !LockRegularFile(publishedManifest, canonical.size(), canonical.size(), publishedFileLocks, error) ||
        !HasExactHash(publishedManifest, SharedUtil::GenerateSha256HexString(canonical)) || !audit(published, error) ||
        (request.cancellation && request.cancellation->load(std::memory_order_acquire)))
    {
        if (error.empty())
            error = "static-world-v3-set published envelope failed final validation or audit";
        publishedFileLocks.Close();
        if (publishedDirectoryLocked)
            RemoveVerifiedNativeWorldV3SetDirectory(published, publishedDirectoryGuard);
        return false;
    }
    publishedDirectory = published;
    return true;
}

bool AcquireExistingNativeWorldV3SetLease(const SNativeWorldV3SetRequestSA& request, const std::string& ticketId, const NativeWorldCacheAuditSA& audit,
                                          CNativeWorldCacheLeaseSA& lease, std::string& publishedDirectory, std::string& error)
{
    if (lease.IsValid() || !audit || !IsValidNativeWorldV3SetRequest(request) || !IsLowerHex(ticketId, 32))
    {
        error = "static-world-v3-set lease request is invalid";
        return false;
    }
    const SString  dataRoot = SharedUtil::GetMTADataPath();
    const SString  root = JoinPath(dataRoot, CACHE_ROOT_DIRECTORY);
    const SString  format = JoinPath(root, "v3");
    const SString  policy = JoinPath(format, STATIC_WORLD_V3_SET_POLICY);
    const SString  directory = JoinPath(policy, request.setId);
    CScopedHandles handles;
    if (!LockDirectory(dataRoot, handles, error) || !LockDirectory(root, handles, error) || !LockDirectory(format, handles, error) ||
        !LockDirectory(policy, handles, error) || !ValidateNativeWorldV3SetObject(directory, request, handles, error) || !audit(directory, error) ||
        (request.cancellation && request.cancellation->load(std::memory_order_acquire)))
        return false;
    auto impl = std::make_unique<CNativeWorldCacheLeaseSA::SImpl>();
    handles.TransferTo(impl->handles);
    impl->format = 3;
    impl->policy = STATIC_WORLD_V3_SET_POLICY;
    impl->contentId = request.setId;
    impl->ticketId = ticketId;
    impl->directory = directory;
    impl->expectedFileNames = {L"static-world-v3-set.json"};
    lease.m_impl = std::move(impl);
    publishedDirectory = directory;
    return true;
}

bool PrepareAndLockNativeWorldCache(const SNativeWorldCacheRequestSA& request, std::string& publishedDirectory, bool& cacheHit, std::string& error)
{
    return PrepareNativeWorldCacheImpl(request, publishedDirectory, cacheHit, error, true, true, nullptr);
}

bool PublishNativeWorldCache(const SNativeWorldCacheRequestSA& request, const NativeWorldCacheAuditSA& audit, std::string& publishedDirectory, bool& cacheHit,
                             std::string& error)
{
    if (!audit)
    {
        error = "transport cache publication requires a closed semantic audit";
        return false;
    }
    // Transport publication owns the trusted local seed and may therefore
    // quarantine a corrupt immutable object before rebuilding it. Startup
    // lease acquisition remains read-only and never repairs cache state.
    return PrepareNativeWorldCacheImpl(request, publishedDirectory, cacheHit, error, false, true, &audit);
}

void CommitNativeWorldCacheLease()
{
    g_processLocks.insert(g_processLocks.end(), g_pendingLocks.begin(), g_pendingLocks.end());
    g_pendingLocks.clear();
}

void ReleaseNativeWorldCacheLease()
{
    for (HANDLE handle : g_pendingLocks)
        CloseHandle(handle);
    g_pendingLocks.clear();
    g_cachePrepared = false;
}
