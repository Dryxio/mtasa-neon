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
#include <locale>
#include <sstream>

namespace
{
    constexpr const char* CACHE_ROOT_DIRECTORY = "native-world-cache";
    constexpr const char* CACHE_FORMAT_DIRECTORY = "v1";
    constexpr const char* CONTENT_ID_DOMAIN = "mta-native-world-cache-content-v1";
    constexpr const char* CACHED_MANIFEST_FILE = "native-world.json";
    constexpr const char* CACHED_IDE_FILE = "world.ide";
    constexpr const char* CACHED_IMG_FILE = "world.img";

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

    bool LockRegularFile(const SString& path, unsigned int minimumBytes, unsigned int maximumBytes, CScopedHandles& handles, std::string& error)
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
        if (!HandleMatchesPath(handle, path, false, error) || !GetFileInformationByHandle(handle, &information) || information.nFileSizeHigh != 0 ||
            information.nFileSizeLow < minimumBytes || information.nFileSizeLow > maximumBytes)
        {
            CloseHandle(handle);
            if (error.empty())
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
        manifest << "{\n"
                 << "  \"format\": " << request.format << ",\n"
                 << "  \"pack_id\": \"" << request.packId << "\",\n"
                 << "  \"files\": {\n"
                 << "    \"ide\": {\n"
                 << "      \"name\": \"" << CACHED_IDE_FILE << "\",\n"
                 << "      \"bytes\": " << request.ide.bytes << ",\n"
                 << "      \"sha256\": \"" << request.ide.sha256 << "\"\n"
                 << "    },\n"
                 << "    \"img\": {\n"
                 << "      \"name\": \"" << CACHED_IMG_FILE << "\",\n"
                 << "      \"bytes\": " << request.img.bytes << ",\n"
                 << "      \"sha256\": \"" << request.img.sha256 << "\"\n"
                 << "    }\n"
                 << "  }\n"
                 << "}\n";
        return manifest.str();
    }

    struct SCachePaths
    {
        SString dataRoot;
        SString root;
        SString format;
        SString pack;
        SString published;
        SString manifest;
        SString ide;
        SString img;
    };

    SCachePaths MakeCachePaths(const SNativeWorldCacheRequestSA& request, const SString& publishedDirectory)
    {
        SCachePaths paths;
        paths.dataRoot = SharedUtil::GetMTADataPath();
        paths.root = JoinPath(paths.dataRoot, CACHE_ROOT_DIRECTORY);
        paths.format = JoinPath(paths.root, CACHE_FORMAT_DIRECTORY);
        paths.pack = JoinPath(paths.format, request.packId);
        paths.published = publishedDirectory;
        paths.manifest = JoinPath(paths.published, CACHED_MANIFEST_FILE);
        paths.ide = JoinPath(paths.published, CACHED_IDE_FILE);
        paths.img = JoinPath(paths.published, CACHED_IMG_FILE);
        return paths;
    }

    bool LockAndValidatePublishedFiles(const SNativeWorldCacheRequestSA& request, const SCachePaths& paths, CScopedHandles& handles, std::string& error)
    {
        const std::string canonicalManifest = BuildCanonicalManifest(request);
        const std::string canonicalManifestHash = SharedUtil::GenerateSha256HexString(canonicalManifest);
        if (!LockRegularFile(paths.manifest, static_cast<unsigned int>(canonicalManifest.size()), static_cast<unsigned int>(canonicalManifest.size()), handles,
                             error) ||
            !LockRegularFile(paths.ide, request.ide.bytes, request.ide.bytes, handles, error) ||
            !LockRegularFile(paths.img, request.img.bytes, request.img.bytes, handles, error))
            return false;
        if (!HasExactHash(paths.manifest, canonicalManifestHash) || !HasExactHash(paths.ide, request.ide.sha256) ||
            !HasExactHash(paths.img, request.img.sha256))
        {
            error = "published cache SHA-256 differs from its semantic content address";
            return false;
        }
        return true;
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
                error = SString("cache quarantine stream identity failed file=%s bytes=%llu expected=%u win32=%u", identity.name.c_str(), total, identity.bytes,
                                copyError);
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

    void RemoveVerifiedDirectory(const SCachePaths& paths, CScopedHandles& directoryGuard)
    {
        // Resolve and delete each known child by its own verified handle while
        // the parent guard prevents directory replacement. This cannot follow
        // a raced junction or delete through an unverified child path.
        const bool manifestRemoved = DeleteVerifiedChild(paths.manifest);
        const bool ideRemoved = DeleteVerifiedChild(paths.ide);
        const bool imgRemoved = DeleteVerifiedChild(paths.img);
        directoryGuard.Close();
        if (manifestRemoved && ideRemoved && imgRemoved)
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
        constexpr size_t   MAX_OBJECTS = 4;
        constexpr uint64_t MAX_TOTAL_BYTES = 1024ULL * 1024ULL * 1024ULL;
        constexpr uint32_t MAX_IDE_BYTES = 1024U * 1024U;
        constexpr uint32_t MAX_IMG_BYTES = 256U * 1024U * 1024U;
        constexpr uint64_t MINIMUM_FREE_MARGIN = 64ULL * 1024ULL * 1024ULL;

        const uint64_t requestedBytes = static_cast<uint64_t>(request.maximumManifestBytes) + request.ide.bytes + request.img.bytes;
        if (request.ide.bytes > MAX_IDE_BYTES || request.img.bytes > MAX_IMG_BYTES || requestedBytes > MAX_TOTAL_BYTES)
        {
            error = "native world cache request exceeds the transport byte policy";
            return false;
        }

        ULARGE_INTEGER freeBytes{};
        if (!GetDiskFreeSpaceExW(SharedUtil::FromUTF8(paths.pack).c_str(), &freeBytes, nullptr, nullptr) ||
            freeBytes.QuadPart < requestedBytes + MINIMUM_FREE_MARGIN)
        {
            error = SString("native world cache has insufficient free space required=%llu margin=%llu available=%llu", requestedBytes, MINIMUM_FREE_MARGIN,
                            freeBytes.QuadPart);
            return false;
        }

        WIN32_FIND_DATAW data{};
        const WString    pattern = SharedUtil::FromUTF8(JoinPath(paths.pack, "*"));
        const HANDLE     search = FindFirstFileW(pattern.c_str(), &data);
        size_t           objects = 0;
        uint64_t         bytes = request.sourceManifestBytes + request.ide.bytes + request.img.bytes;
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
                RemoveVerifiedDirectory(remnant, remnantGuard);
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
                !LockRegularFile(object.ide, 1, MAX_IDE_BYTES, locks, error) || !LockRegularFile(object.img, 1, MAX_IMG_BYTES, locks, error))
            {
                valid = false;
                break;
            }
            bytes += FileSize(object.manifest) + FileSize(object.ide) + FileSize(object.img);
            if (objects >= MAX_OBJECTS || bytes > MAX_TOTAL_BYTES)
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
    identity << CONTENT_ID_DOMAIN << '\n'
             << "format=" << request.format << '\n'
             << "policy=" << request.packId << '\n'
             << "ide.bytes=" << request.ide.bytes << '\n'
             << "ide.sha256=" << request.ide.sha256 << '\n'
             << "img.bytes=" << request.img.bytes << '\n'
             << "img.sha256=" << request.img.sha256 << '\n';
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
        if (request.format != 1 || request.manifestFileName != CACHED_MANIFEST_FILE || !IsSafeLeafName(request.packId, 32) ||
            !IsSafeLeafName(request.manifestFileName, 63) || !IsSafeLeafName(request.ide.name, 63) || !IsSafeLeafName(request.img.name, 63) ||
            request.manifestFileName == request.ide.name || request.manifestFileName == request.img.name || request.ide.name == request.img.name ||
            !IsLowerSha256(request.sourceManifestSha256) || !IsLowerSha256(request.ide.sha256) || !IsLowerSha256(request.img.sha256) ||
            !IsLowerSha256(request.contentId) || !request.sourceManifestBytes || request.sourceManifestBytes > request.maximumManifestBytes ||
            !request.ide.bytes || !request.img.bytes || GenerateNativeWorldContentId(request) != request.contentId)
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
        const SString format = JoinPath(root, CACHE_FORMAT_DIRECTORY);
        const SString pack = JoinPath(format, request.packId);
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
            if (LockDirectory(paths.published, existingLocks, existingError) && LockAndValidatePublishedFiles(request, paths, existingLocks, existingError))
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
        sourcePaths.img = JoinPath(sourceDirectory, request.img.name);
        CScopedHandles sourceLocks;
        if (!IsSafeLocalPath(sourceDirectory) || !LockDirectory(sourceDirectory, sourceLocks, error) ||
            !LockRegularFile(sourcePaths.manifest, request.sourceManifestBytes, request.sourceManifestBytes, sourceLocks, error) ||
            !LockRegularFile(sourcePaths.ide, request.ide.bytes, request.ide.bytes, sourceLocks, error) ||
            !LockRegularFile(sourcePaths.img, request.img.bytes, request.img.bytes, sourceLocks, error) ||
            !HasExactHash(sourcePaths.manifest, request.sourceManifestSha256) || !HasExactHash(sourcePaths.ide, request.ide.sha256) ||
            !HasExactHash(sourcePaths.img, request.img.sha256))
        {
            if (error.empty())
                error = "local cache seed SHA-256 differs from its parsed manifest";
            error = "local cache seed is invalid: " + error;
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
        const bool        filled = WriteAndFlushFile(quarantine.manifest, canonicalManifest, error) &&
                            CopyHashAndFlushFile(sourcePaths.ide, quarantine.ide, request, request.ide, error) &&
                            CopyHashAndFlushFile(sourcePaths.img, quarantine.img, request, request.img, error);
        if (!filled)
        {
            RemoveVerifiedDirectory(quarantine, quarantineDirectoryGuard);
            return false;
        }

        CScopedHandles publishedFileLocks;
        if (!LockAndValidatePublishedFiles(request, quarantine, publishedFileLocks, error))
        {
            publishedFileLocks.Close();
            RemoveVerifiedDirectory(quarantine, quarantineDirectoryGuard);
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
            RemoveVerifiedDirectory(quarantine, quarantineDirectoryGuard);
            error = "cache quarantine semantic audit failed: " + error;
            return false;
        }
        publishedFileLocks.Close();
        quarantineDirectoryGuard.Close();

        if (IsCancelled(request))
        {
            CScopedHandles cancelledGuard;
            if (LockDirectory(quarantine.published, cancelledGuard, error))
                RemoveVerifiedDirectory(quarantine, cancelledGuard);
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
                !LockDirectory(paths.published, concurrentLocks, error) || !LockAndValidatePublishedFiles(request, paths, concurrentLocks, error))
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
        if (!LockDirectory(paths.published, publishedLocks, error) || !LockAndValidatePublishedFiles(request, paths, publishedLocks, error))
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
    return PrepareNativeWorldCacheImpl(request, publishedDirectory, cacheHit, error, false, false, &audit);
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
