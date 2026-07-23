/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CNativeWorldAuthorizationStore.cpp
 *  PURPOSE:     DPAPI-backed native-world authorization store
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeWorldAuthorizationStore.h"

#include "SharedUtil.File.h"
#include "SharedUtil.Misc.h"

#include <Aclapi.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace
{
    constexpr unsigned short     RECORD_FORMAT = 1;
    constexpr unsigned short     ENVELOPE_FORMAT = 1;
    constexpr unsigned long long RECORD_LIFETIME_SECONDS = 900;
    constexpr unsigned long long CLOCK_ROLLBACK_TOLERANCE_SECONDS = 120;
    constexpr size_t             MAX_RECORD_BYTES = 4096;
    constexpr char               RECORD_MAGIC[] = "MTANWAR1";
    constexpr char               ENVELOPE_MAGIC[] = "MTADPAPI";
    constexpr char               DPAPI_PURPOSE[] = "mta-native-world-activation-record-v1";
    constexpr wchar_t            STORE_DIRECTORY[] = L"native-world-activation";
    constexpr wchar_t            STORE_VERSION_DIRECTORY[] = L"v1";
    constexpr wchar_t            TRANSACTION_LOCK[] = L"transaction.lock";
    constexpr wchar_t            PENDING_FILE[] = L"pending.bin";
    constexpr wchar_t            TEMP_PREFIX[] = L".pending.tmp.";
    constexpr wchar_t            REVOKED_PREFIX[] = L".revoked.";
    constexpr wchar_t            SPENT_PREFIX[] = L".spent.";

    struct SRecord
    {
        SNativeWorldStartupAuthorization authorization;
        std::array<unsigned char, 32>    offerId{};
        std::array<unsigned char, 32>    contentId{};
        std::array<unsigned char, 16>    ticketId{};
        unsigned long long               issuedAt{};
        unsigned long long               expiresAt{};
    };

    class CScopedHandles
    {
    public:
        ~CScopedHandles()
        {
            for (HANDLE handle : m_handles)
                CloseHandle(handle);
        }

        void Add(HANDLE handle) { m_handles.push_back(handle); }

    private:
        std::vector<HANDLE> m_handles;
    };

    struct STransaction
    {
        CScopedHandles            guards;
        std::wstring              directory;
        std::wstring              pending;
        std::vector<std::wstring> temporaryFiles;
        std::vector<std::wstring> revokedFiles;
        std::set<std::string>     spentTickets;
    };

    struct SStartupTransaction
    {
        STransaction transaction;
        SRecord      record;
    };

    std::unique_ptr<SStartupTransaction> g_startupTransaction;
    std::atomic_bool                     g_startupCancelled{};
    std::mutex                           g_startupStateMutex;
    bool                                 g_startupBeginning{};
    bool                                 g_startupFinishing{};

    class CStartupBeginScope
    {
    public:
        bool Enter(std::string& error)
        {
            std::lock_guard<std::mutex> stateLock(g_startupStateMutex);
            if (g_startupBeginning || g_startupTransaction)
            {
                error = "a native-world startup transaction is already active";
                return false;
            }

            // This is the beginning of a distinct startup attempt. Clear the
            // previous attempt only while publishing the in-progress state so
            // shutdown cannot be lost between construction and publication.
            g_startupCancelled.store(false, std::memory_order_release);
            g_startupBeginning = true;
            m_entered = true;
            return true;
        }

        void Publish(std::unique_ptr<SStartupTransaction> startup)
        {
            std::lock_guard<std::mutex> stateLock(g_startupStateMutex);
            g_startupTransaction = std::move(startup);
            g_startupBeginning = false;
            m_entered = false;
        }

        ~CStartupBeginScope()
        {
            if (!m_entered)
                return;

            std::lock_guard<std::mutex> stateLock(g_startupStateMutex);
            g_startupBeginning = false;
        }

    private:
        bool m_entered{};
    };

    void AppendU8(std::vector<unsigned char>& output, unsigned char value)
    {
        output.push_back(value);
    }

    void AppendU16(std::vector<unsigned char>& output, unsigned short value)
    {
        output.push_back(static_cast<unsigned char>(value));
        output.push_back(static_cast<unsigned char>(value >> 8));
    }

    void AppendU32(std::vector<unsigned char>& output, unsigned int value)
    {
        for (unsigned int shift = 0; shift < 32; shift += 8)
            output.push_back(static_cast<unsigned char>(value >> shift));
    }

    void AppendU64(std::vector<unsigned char>& output, unsigned long long value)
    {
        for (unsigned int shift = 0; shift < 64; shift += 8)
            output.push_back(static_cast<unsigned char>(value >> shift));
    }

    template <size_t Size>
    void AppendArray(std::vector<unsigned char>& output, const std::array<unsigned char, Size>& value)
    {
        output.insert(output.end(), value.begin(), value.end());
    }

    class CReader
    {
    public:
        explicit CReader(const std::vector<unsigned char>& bytes) : m_bytes(bytes) {}

        bool ReadU8(unsigned char& value)
        {
            if (m_offset >= m_bytes.size())
                return false;
            value = m_bytes[m_offset++];
            return true;
        }

        bool ReadU16(unsigned short& value)
        {
            unsigned char low = 0;
            unsigned char high = 0;
            if (!ReadU8(low) || !ReadU8(high))
                return false;
            value = static_cast<unsigned short>(low | static_cast<unsigned short>(high << 8));
            return true;
        }

        bool ReadU32(unsigned int& value)
        {
            value = 0;
            for (unsigned int shift = 0; shift < 32; shift += 8)
            {
                unsigned char byte = 0;
                if (!ReadU8(byte))
                    return false;
                value |= static_cast<unsigned int>(byte) << shift;
            }
            return true;
        }

        bool ReadU64(unsigned long long& value)
        {
            value = 0;
            for (unsigned int shift = 0; shift < 64; shift += 8)
            {
                unsigned char byte = 0;
                if (!ReadU8(byte))
                    return false;
                value |= static_cast<unsigned long long>(byte) << shift;
            }
            return true;
        }

        template <size_t Size>
        bool ReadArray(std::array<unsigned char, Size>& value)
        {
            if (m_bytes.size() - m_offset < Size)
                return false;
            std::copy_n(m_bytes.begin() + m_offset, Size, value.begin());
            m_offset += Size;
            return true;
        }

        bool ReadString(std::string& value)
        {
            unsigned char length = 0;
            if (!ReadU8(length) || length == 0 || length > 64 || m_bytes.size() - m_offset < length)
                return false;
            value.assign(reinterpret_cast<const char*>(m_bytes.data() + m_offset), length);
            m_offset += length;
            return std::all_of(value.begin(), value.end(),
                               [](unsigned char character)
                               {
                                   return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                          (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
                               });
        }

        bool ReadMagic(const char* magic, size_t length)
        {
            if (m_bytes.size() - m_offset < length || memcmp(m_bytes.data() + m_offset, magic, length) != 0)
                return false;
            m_offset += length;
            return true;
        }

        bool AtEnd() const { return m_offset == m_bytes.size(); }

    private:
        const std::vector<unsigned char>& m_bytes;
        size_t                            m_offset{};
    };

    bool IsLowerHex(const std::string& value, size_t length)
    {
        return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char character)
                                                     { return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'); });
    }

    bool IsLowerHex(const std::wstring& value, size_t length)
    {
        return value.size() == length && std::all_of(value.begin(), value.end(), [](wchar_t character)
                                                     { return (character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f'); });
    }

    template <size_t Size>
    bool DecodeHex(const std::string& value, std::array<unsigned char, Size>& output)
    {
        if (!IsLowerHex(value, Size * 2))
            return false;
        const auto nibble = [](char character) -> unsigned char
        { return character <= '9' ? static_cast<unsigned char>(character - '0') : static_cast<unsigned char>(character - 'a' + 10); };
        for (size_t index = 0; index < Size; ++index)
            output[index] = static_cast<unsigned char>((nibble(value[index * 2]) << 4) | nibble(value[index * 2 + 1]));
        return true;
    }

    template <size_t Size>
    std::string EncodeHex(const std::array<unsigned char, Size>& value)
    {
        static constexpr char HEX[] = "0123456789abcdef";
        std::string           output(Size * 2, '0');
        for (size_t index = 0; index < Size; ++index)
        {
            output[index * 2] = HEX[value[index] >> 4];
            output[index * 2 + 1] = HEX[value[index] & 0x0F];
        }
        return output;
    }

    bool IsCanonicalResourceName(const std::string& value)
    {
        if (value.empty() || value.size() > 64)
            return false;
        return std::all_of(value.begin(), value.end(),
                           [](unsigned char character)
                           {
                               return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                      (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
                           });
    }

    bool ValidateAuthorization(const SNativeWorldStartupAuthorization& authorization, std::string& error)
    {
        const unsigned short minimumBitstreamVersion = static_cast<unsigned short>(
            authorization.packFormat == NATIVE_WORLD_BULLWORTH_FORMAT   ? eBitStreamVersion::NativeWorldStartupAuthorization
            : authorization.packFormat == NATIVE_WORLD_STATIC_V1_FORMAT ? eBitStreamVersion::NativeWorldStaticWorldV2StartupAuthorization
                                                                        : eBitStreamVersion::NativeWorldStaticWorldV3StartupAuthorization);
        if (!authorization.present ||
            !IsClosedNativeWorldStartupAuthorization(authorization.wireVersion, authorization.startupMode, authorization.policy, authorization.packFormat) ||
            authorization.serverPort == 0 || authorization.connectionGeneration == 0 || authorization.authorizationEpoch == 0 ||
            authorization.resourceNetId == 0xFFFF || authorization.resourceStartCounter == 0 || authorization.bitstreamVersion < minimumBitstreamVersion ||
            authorization.bitstreamVersion > static_cast<unsigned short>(eBitStreamVersion::Latest) || !IsCanonicalResourceName(authorization.resourceName) ||
            std::all_of(authorization.serverIdDigest.begin(), authorization.serverIdDigest.end(), [](unsigned char byte) { return byte == 0; }) ||
            std::all_of(authorization.serverIpv4.begin(), authorization.serverIpv4.end(), [](unsigned char byte) { return byte == 0; }))
        {
            error = "authorization snapshot is outside the closed v1 contract";
            return false;
        }
        return true;
    }

    std::vector<unsigned char> EncodeRecord(const SRecord& record)
    {
        std::vector<unsigned char> output;
        output.reserve(256);
        output.insert(output.end(), RECORD_MAGIC, RECORD_MAGIC + sizeof(RECORD_MAGIC) - 1);
        AppendU16(output, RECORD_FORMAT);
        AppendU8(output, record.authorization.wireVersion);
        AppendU8(output, record.authorization.startupMode);
        AppendU8(output, record.authorization.packFormat);
        AppendU8(output, record.authorization.policy);
        AppendArray(output, record.contentId);
        AppendArray(output, record.offerId);
        AppendArray(output, record.authorization.serverIdDigest);
        AppendArray(output, record.authorization.serverIpv4);
        AppendU16(output, record.authorization.serverPort);
        AppendU8(output, static_cast<unsigned char>(record.authorization.resourceName.size()));
        output.insert(output.end(), record.authorization.resourceName.begin(), record.authorization.resourceName.end());
        AppendU16(output, record.authorization.resourceNetId);
        AppendU32(output, record.authorization.resourceStartCounter);
        AppendU16(output, record.authorization.bitstreamVersion);
        AppendU64(output, record.authorization.connectionGeneration);
        AppendU64(output, record.authorization.authorizationEpoch);
        AppendArray(output, record.ticketId);
        AppendU64(output, record.issuedAt);
        AppendU64(output, record.expiresAt);
        return output;
    }

    bool DecodeRecord(const std::vector<unsigned char>& bytes, SRecord& record, std::string& error)
    {
        CReader        reader(bytes);
        unsigned short format = 0;
        if (!reader.ReadMagic(RECORD_MAGIC, sizeof(RECORD_MAGIC) - 1) || !reader.ReadU16(format) || format != RECORD_FORMAT ||
            !reader.ReadU8(record.authorization.wireVersion) || !reader.ReadU8(record.authorization.startupMode) ||
            !reader.ReadU8(record.authorization.packFormat) || !reader.ReadU8(record.authorization.policy) || !reader.ReadArray(record.contentId) ||
            !reader.ReadArray(record.offerId) || !reader.ReadArray(record.authorization.serverIdDigest) || !reader.ReadArray(record.authorization.serverIpv4) ||
            !reader.ReadU16(record.authorization.serverPort) || !reader.ReadString(record.authorization.resourceName) ||
            !reader.ReadU16(record.authorization.resourceNetId) || !reader.ReadU32(record.authorization.resourceStartCounter) ||
            !reader.ReadU16(record.authorization.bitstreamVersion) || !reader.ReadU64(record.authorization.connectionGeneration) ||
            !reader.ReadU64(record.authorization.authorizationEpoch) || !reader.ReadArray(record.ticketId) || !reader.ReadU64(record.issuedAt) ||
            !reader.ReadU64(record.expiresAt) || !reader.AtEnd())
        {
            error = "authorization record is truncated, non-canonical, or has trailing bytes";
            return false;
        }
        record.authorization.present = true;
        if (!ValidateAuthorization(record.authorization, error) || record.expiresAt < record.issuedAt ||
            record.expiresAt - record.issuedAt != RECORD_LIFETIME_SECONDS)
        {
            if (error.empty())
                error = "authorization record lifetime is invalid";
            return false;
        }
        if (std::all_of(record.contentId.begin(), record.contentId.end(), [](unsigned char byte) { return byte == 0; }) ||
            std::all_of(record.offerId.begin(), record.offerId.end(), [](unsigned char byte) { return byte == 0; }) ||
            std::all_of(record.ticketId.begin(), record.ticketId.end(), [](unsigned char byte) { return byte == 0; }))
        {
            error = "authorization record contains a zero identity";
            return false;
        }
        return true;
    }

    bool RecordsEqual(const SRecord& left, const SRecord& right)
    {
        return left.authorization.present == right.authorization.present && left.authorization.wireVersion == right.authorization.wireVersion &&
               left.authorization.startupMode == right.authorization.startupMode && left.authorization.policy == right.authorization.policy &&
               left.authorization.packFormat == right.authorization.packFormat && left.authorization.serverIdDigest == right.authorization.serverIdDigest &&
               left.authorization.serverIpv4 == right.authorization.serverIpv4 && left.authorization.serverPort == right.authorization.serverPort &&
               left.authorization.resourceNetId == right.authorization.resourceNetId &&
               left.authorization.resourceStartCounter == right.authorization.resourceStartCounter &&
               left.authorization.bitstreamVersion == right.authorization.bitstreamVersion &&
               left.authorization.connectionGeneration == right.authorization.connectionGeneration &&
               left.authorization.authorizationEpoch == right.authorization.authorizationEpoch &&
               left.authorization.resourceName == right.authorization.resourceName && left.offerId == right.offerId && left.contentId == right.contentId &&
               left.ticketId == right.ticketId && left.issuedAt == right.issuedAt && left.expiresAt == right.expiresAt;
    }

    bool SameSemanticAuthorization(const SRecord& record, const SNativeWorldStartupAuthorization& authorization,
                                   const SNativeWorldAuthorizationPublication& publication)
    {
        std::array<unsigned char, 32> offerId{};
        std::array<unsigned char, 32> contentId{};
        return DecodeHex(publication.offerId, offerId) && DecodeHex(publication.contentId, contentId) && record.offerId == offerId &&
               record.contentId == contentId && record.authorization.wireVersion == authorization.wireVersion &&
               record.authorization.startupMode == authorization.startupMode && record.authorization.policy == authorization.policy &&
               record.authorization.packFormat == authorization.packFormat && record.authorization.serverIdDigest == authorization.serverIdDigest &&
               record.authorization.serverIpv4 == authorization.serverIpv4 && record.authorization.serverPort == authorization.serverPort &&
               record.authorization.resourceNetId == authorization.resourceNetId &&
               record.authorization.resourceStartCounter == authorization.resourceStartCounter &&
               record.authorization.bitstreamVersion == authorization.bitstreamVersion &&
               record.authorization.connectionGeneration == authorization.connectionGeneration &&
               record.authorization.authorizationEpoch == authorization.authorizationEpoch && record.authorization.resourceName == authorization.resourceName;
    }

    bool SameDurableAuthorization(const SRecord& record, const SNativeWorldStartupAuthorization& authorization,
                                  const SNativeWorldAuthorizationPublication& publication)
    {
        std::array<unsigned char, 32> offerId{};
        std::array<unsigned char, 32> contentId{};
        return DecodeHex(publication.offerId, offerId) && DecodeHex(publication.contentId, contentId) && record.offerId == offerId &&
               record.contentId == contentId && record.authorization.wireVersion == authorization.wireVersion &&
               record.authorization.startupMode == authorization.startupMode && record.authorization.policy == authorization.policy &&
               record.authorization.packFormat == authorization.packFormat && record.authorization.serverIdDigest == authorization.serverIdDigest &&
               record.authorization.serverIpv4 == authorization.serverIpv4 && record.authorization.serverPort == authorization.serverPort &&
               record.authorization.resourceNetId == authorization.resourceNetId &&
               record.authorization.resourceStartCounter == authorization.resourceStartCounter &&
               record.authorization.bitstreamVersion == authorization.bitstreamVersion && record.authorization.resourceName == authorization.resourceName;
    }

    bool MatchesRevocation(const SRecord& record, const SNativeWorldStartupAuthorization& authorization, const std::string& contentId)
    {
        SNativeWorldAuthorizationPublication publication;
        publication.success = true;
        publication.offerId = EncodeHex(record.offerId);
        publication.contentId = contentId;
        return SameDurableAuthorization(record, authorization, publication);
    }

    enum class EFreshness
    {
        Fresh,
        Expired,
        ClockRollback,
    };

    EFreshness EvaluateFreshness(const SRecord& record, unsigned long long now)
    {
        if (now > record.expiresAt)
            return EFreshness::Expired;
        if (now <= std::numeric_limits<unsigned long long>::max() - CLOCK_ROLLBACK_TOLERANCE_SECONDS &&
            now + CLOCK_ROLLBACK_TOLERANCE_SECONDS < record.issuedAt)
            return EFreshness::ClockRollback;
        return EFreshness::Fresh;
    }

    bool FillRandom(void* output, size_t bytes, std::string& error)
    {
        if (bytes > std::numeric_limits<ULONG>::max() ||
            BCryptGenRandom(nullptr, static_cast<PUCHAR>(output), static_cast<ULONG>(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        {
            error = "authorization CSPRNG failed";
            return false;
        }
        return true;
    }

    std::wstring JoinPath(const std::wstring& directory, const std::wstring& leaf)
    {
        return directory + L"\\" + leaf;
    }

    bool CanonicalizeLocalPath(const std::wstring& input, std::wstring& output, std::string& error)
    {
        if (input.size() < 3 || input.size() >= 32760 || input[1] != L':' || (input[2] != L'\\' && input[2] != L'/') || input.rfind(L"\\\\", 0) == 0 ||
            input.rfind(L"\\\\?\\", 0) == 0)
        {
            error = "authorization store path is not a bounded absolute drive path";
            return false;
        }
        const wchar_t driveRoot[] = {input[0], L':', L'\\', L'\0'};
        if (GetDriveTypeW(driveRoot) != DRIVE_FIXED)
        {
            error = "authorization store is not on a fixed local drive";
            return false;
        }
        std::vector<wchar_t> buffer(32768);
        const DWORD          length = GetFullPathNameW(input.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (!length || length >= buffer.size())
        {
            error = SString("authorization path canonicalization failed win32=%u", GetLastError());
            return false;
        }
        output.assign(buffer.data(), length);
        return true;
    }

    bool GetCurrentUserSid(std::vector<unsigned char>& tokenBytes, PSID& userSid, std::string& error)
    {
        userSid = nullptr;
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            error = SString("authorization owner token open failed win32=%u", GetLastError());
            return false;
        }
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        tokenBytes.resize(bytes);
        const bool tokenRead = bytes && GetTokenInformation(token, TokenUser, tokenBytes.data(), bytes, &bytes);
        CloseHandle(token);
        if (!tokenRead)
        {
            error = SString("authorization owner token read failed win32=%u", GetLastError());
            return false;
        }

        userSid = reinterpret_cast<TOKEN_USER*>(tokenBytes.data())->User.Sid;
        if (!userSid || !IsValidSid(userSid))
        {
            error = "authorization user SID is invalid";
            return false;
        }
        return true;
    }

    bool SetOwnerToCurrentUser(HANDLE handle, std::string& error)
    {
        std::vector<unsigned char> tokenBytes;
        PSID                       userSid = nullptr;
        if (!GetCurrentUserSid(tokenBytes, userSid, error))
            return false;

        const DWORD securityError = SetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, userSid, nullptr, nullptr, nullptr);
        if (securityError != ERROR_SUCCESS)
        {
            error = SString("authorization owner assignment failed win32=%u", securityError);
            return false;
        }
        return true;
    }

    bool IsOwnedByCurrentUser(HANDLE handle, std::string& error)
    {
        std::vector<unsigned char> tokenBytes;
        PSID                       userSid = nullptr;
        if (!GetCurrentUserSid(tokenBytes, userSid, error))
            return false;

        PSID                 owner = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const DWORD securityError = GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor);
        const bool  matches = securityError == ERROR_SUCCESS && owner && EqualSid(owner, userSid);
        if (descriptor)
            LocalFree(descriptor);
        if (!matches)
        {
            if (securityError == ERROR_SUCCESS)
                error = "authorization store owner differs from the current user";
            else
                error = SString("authorization owner query failed win32=%u", securityError);
            return false;
        }
        return true;
    }

    bool HandleMatchesPath(HANDLE handle, const std::wstring& expectedPath, bool directory, std::string& error)
    {
        BY_HANDLE_FILE_INFORMATION information{};
        if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &information) ||
            !!(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            error = "authorization handle type or attributes are invalid";
            return false;
        }

        wchar_t     finalPath[32768]{};
        const DWORD length = GetFinalPathNameByHandleW(handle, finalPath, static_cast<DWORD>(std::size(finalPath)), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!length || length >= std::size(finalPath))
        {
            error = SString("authorization final-path query failed win32=%u", GetLastError());
            return false;
        }
        const wchar_t* normalized = finalPath;
        if (wcsncmp(normalized, L"\\\\?\\", 4) == 0)
            normalized += 4;
        std::wstring canonicalExpected;
        if (wcsncmp(normalized, L"UNC\\", 4) == 0 || !CanonicalizeLocalPath(expectedPath, canonicalExpected, error) ||
            _wcsicmp(normalized, canonicalExpected.c_str()) != 0 || !IsOwnedByCurrentUser(handle, error))
        {
            if (error.empty())
                error = "authorization handle escaped its expected local path";
            return false;
        }
        return true;
    }

    bool EnsureAndLockDirectory(const std::wstring& path, CScopedHandles& guards, std::string& error)
    {
        std::wstring canonical;
        if (!CanonicalizeLocalPath(path, canonical, error))
            return false;
        DWORD attributes = GetFileAttributesW(canonical.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            if (!CreateDirectoryW(canonical.c_str(), nullptr))
            {
                error = SString("authorization directory creation failed win32=%u", GetLastError());
                return false;
            }
        }
        const HANDLE handle = CreateFileW(canonical.c_str(), FILE_LIST_DIRECTORY | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = SString("authorization directory lock failed win32=%u", GetLastError());
            return false;
        }
        if (!HandleMatchesPath(handle, canonical, true, error))
        {
            CloseHandle(handle);
            return false;
        }
        guards.Add(handle);
        return true;
    }

    bool ValidateClosedDirectory(STransaction& transaction, std::string& error)
    {
        WIN32_FIND_DATAW   data{};
        const std::wstring pattern = JoinPath(transaction.directory, L"*");
        HANDLE             find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
        {
            error = SString("authorization directory enumeration failed win32=%u", GetLastError());
            return false;
        }
        bool success = true;
        do
        {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            const bool knownFixed = name == TRANSACTION_LOCK || name == PENDING_FILE;
            const bool knownTemp = name.rfind(TEMP_PREFIX, 0) == 0 && IsLowerHex(name.substr(std::size(TEMP_PREFIX) - 1), 32);
            const bool knownRevoked = (name.rfind(REVOKED_PREFIX, 0) == 0 && IsLowerHex(name.substr(std::size(REVOKED_PREFIX) - 1), 32)) ||
                                      (name.rfind(SPENT_PREFIX, 0) == 0 && IsLowerHex(name.substr(std::size(SPENT_PREFIX) - 1), 32));
            if ((!knownFixed && !knownTemp && !knownRevoked) || (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            {
                error = "authorization store contains an unknown or unsafe sibling";
                success = false;
                break;
            }
            if (knownTemp)
                transaction.temporaryFiles.push_back(JoinPath(transaction.directory, name));
            if (knownRevoked)
                transaction.revokedFiles.push_back(JoinPath(transaction.directory, name));
        } while (FindNextFileW(find, &data));
        const DWORD findError = GetLastError();
        FindClose(find);
        if (success && findError != ERROR_NO_MORE_FILES)
        {
            error = SString("authorization directory enumeration failed win32=%u", findError);
            return false;
        }
        return success;
    }

    bool BeginTransaction(STransaction& transaction, std::string& error)
    {
        const SString localDataUtf8 = SharedUtil::GetSystemLocalAppDataPath();
        if (localDataUtf8.empty())
        {
            error = "LocalAppData is unavailable";
            return false;
        }
        const std::wstring localData = SharedUtil::FromUTF8(localDataUtf8).c_str();
        const std::wstring product = JoinPath(localData, SharedUtil::FromUTF8(SharedUtil::GetProductCommonDataDir()).c_str());
        const std::wstring major = JoinPath(product, SharedUtil::FromUTF8(SharedUtil::GetMajorVersionString()).c_str());
        const std::wstring store = JoinPath(major, STORE_DIRECTORY);
        transaction.directory = JoinPath(store, STORE_VERSION_DIRECTORY);
        transaction.pending = JoinPath(transaction.directory, PENDING_FILE);
        if (!EnsureAndLockDirectory(localData, transaction.guards, error) || !EnsureAndLockDirectory(product, transaction.guards, error) ||
            !EnsureAndLockDirectory(major, transaction.guards, error) || !EnsureAndLockDirectory(store, transaction.guards, error) ||
            !EnsureAndLockDirectory(transaction.directory, transaction.guards, error))
            return false;

        const std::wstring lockPath = JoinPath(transaction.directory, TRANSACTION_LOCK);
        SetLastError(ERROR_SUCCESS);
        const HANDLE lock = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL | WRITE_OWNER, 0, nullptr, OPEN_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        const DWORD  lockOpenStatus = GetLastError();
        if (lock == INVALID_HANDLE_VALUE)
        {
            error = SString("authorization transaction lock failed win32=%u", GetLastError());
            return false;
        }
        BY_HANDLE_FILE_INFORMATION information{};
        // Elevated administrator tokens default new objects to the
        // Administrators group even though TokenUser remains the interactive
        // account. Normalize only the file this CREATE just produced; an
        // existing foreign-owned lock must still fail closed below.
        if ((lockOpenStatus != ERROR_ALREADY_EXISTS && !SetOwnerToCurrentUser(lock, error)) || !HandleMatchesPath(lock, lockPath, false, error) ||
            !GetFileInformationByHandle(lock, &information) || information.nFileSizeHigh != 0 || information.nFileSizeLow != 0)
        {
            CloseHandle(lock);
            if (error.empty())
                error = "authorization transaction lock has invalid content";
            return false;
        }
        transaction.guards.Add(lock);
        return ValidateClosedDirectory(transaction, error);
    }

    bool ProtectRecord(const std::vector<unsigned char>& plaintext, std::vector<unsigned char>& envelope, std::string& error)
    {
        DATA_BLOB input{static_cast<DWORD>(plaintext.size()), const_cast<BYTE*>(plaintext.data())};
        DATA_BLOB entropy{static_cast<DWORD>(sizeof(DPAPI_PURPOSE) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(DPAPI_PURPOSE))};
        DATA_BLOB protectedBlob{};
        if (!CryptProtectData(&input, L"MTA native-world startup authorization", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &protectedBlob))
        {
            error = SString("authorization DPAPI protect failed win32=%u", GetLastError());
            return false;
        }
        envelope.insert(envelope.end(), ENVELOPE_MAGIC, ENVELOPE_MAGIC + sizeof(ENVELOPE_MAGIC) - 1);
        AppendU16(envelope, ENVELOPE_FORMAT);
        AppendU32(envelope, protectedBlob.cbData);
        envelope.insert(envelope.end(), protectedBlob.pbData, protectedBlob.pbData + protectedBlob.cbData);
        LocalFree(protectedBlob.pbData);
        if (envelope.size() > MAX_RECORD_BYTES)
        {
            error = "authorization DPAPI envelope exceeds its bound";
            return false;
        }
        return true;
    }

    bool UnprotectRecord(const std::vector<unsigned char>& envelope, SRecord& record, std::string& error)
    {
        CReader        reader(envelope);
        unsigned short format = 0;
        unsigned int   protectedBytes = 0;
        if (!reader.ReadMagic(ENVELOPE_MAGIC, sizeof(ENVELOPE_MAGIC) - 1) || !reader.ReadU16(format) || format != ENVELOPE_FORMAT ||
            !reader.ReadU32(protectedBytes) || protectedBytes == 0 || protectedBytes > MAX_RECORD_BYTES ||
            envelope.size() != sizeof(ENVELOPE_MAGIC) - 1 + 2 + 4 + protectedBytes)
        {
            error = "authorization DPAPI envelope is non-canonical";
            return false;
        }
        DATA_BLOB input{protectedBytes, const_cast<BYTE*>(envelope.data() + sizeof(ENVELOPE_MAGIC) - 1 + 2 + 4)};
        DATA_BLOB entropy{static_cast<DWORD>(sizeof(DPAPI_PURPOSE) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(DPAPI_PURPOSE))};
        DATA_BLOB plaintext{};
        LPWSTR    description = nullptr;
        if (!CryptUnprotectData(&input, &description, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plaintext))
        {
            error = SString("authorization DPAPI unprotect failed win32=%u", GetLastError());
            return false;
        }
        if (description)
            LocalFree(description);
        std::vector<unsigned char> plaintextBytes(plaintext.pbData, plaintext.pbData + plaintext.cbData);
        if (plaintext.pbData)
            LocalFree(plaintext.pbData);
        return DecodeRecord(plaintextBytes, record, error);
    }

    enum class EReadResult
    {
        Missing,
        Success,
        Failure,
    };

    EReadResult ReadRecord(const std::wstring& path, SRecord& record, std::string& error)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND)
                return EReadResult::Missing;
            error = SString("authorization record open failed win32=%u", GetLastError());
            return EReadResult::Failure;
        }
        BY_HANDLE_FILE_INFORMATION information{};
        if (!HandleMatchesPath(file, path, false, error) || !GetFileInformationByHandle(file, &information) || information.nFileSizeHigh != 0 ||
            information.nFileSizeLow == 0 || information.nFileSizeLow > MAX_RECORD_BYTES)
        {
            CloseHandle(file);
            if (error.empty())
                error = "authorization record byte length is invalid";
            return EReadResult::Failure;
        }
        std::vector<unsigned char> bytes(information.nFileSizeLow);
        DWORD                      read = 0;
        const bool                 success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
        const DWORD                readError = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!success)
        {
            error = SString("authorization record read failed win32=%u", readError);
            return EReadResult::Failure;
        }
        return UnprotectRecord(bytes, record, error) ? EReadResult::Success : EReadResult::Failure;
    }

    bool DeleteExactFile(const std::wstring& path, bool allowMissing, std::string& error)
    {
        const HANDLE file = CreateFileW(path.c_str(), DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES, 0, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            if (allowMissing && GetLastError() == ERROR_FILE_NOT_FOUND)
                return true;
            error = SString("authorization file delete-open failed win32=%u", GetLastError());
            return false;
        }
        FILE_DISPOSITION_INFO disposition{TRUE};
        const bool            success =
            HandleMatchesPath(file, path, false, error) && SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
        const DWORD deleteError = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!success && error.empty())
            error = SString("authorization file deletion failed win32=%u", deleteError);
        return success;
    }

    bool WriteAndFlush(const std::wstring& path, const std::vector<unsigned char>& bytes, std::string& error)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | READ_CONTROL | WRITE_OWNER, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = SString("authorization temporary creation failed win32=%u", GetLastError());
            return false;
        }
        DWORD written = 0;
        // CREATE_NEW plus share mode zero proves this handle names the object
        // created by this transaction. Assign TokenUser before validation and
        // before publishing any bytes so elevated launches do not strand an
        // Administrators-owned zero-byte fail-closed marker.
        const bool success = SetOwnerToCurrentUser(file, error) && HandleMatchesPath(file, path, false, error) &&
                             WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size() &&
                             FlushFileBuffers(file);
        const DWORD writeError = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(file);
        if (!success && error.empty())
            error = SString("authorization temporary write/flush failed win32=%u", writeError);
        return success;
    }

    SNativeWorldAuthorizationRecordResult MakeResult(const SRecord& record, const char* state)
    {
        SNativeWorldAuthorizationRecordResult result;
        result.success = true;
        result.found = true;
        result.ticketId = EncodeHex(record.ticketId);
        result.issuedAt = record.issuedAt;
        result.expiresAt = record.expiresAt;
        const std::string ticketCorrelation = result.ticketId.substr(0, 8);
        result.diagnostic =
            SString("state=%s resource=%s endpoint=%u.%u.%u.%u:%u format=%u policy=%s contentId=%s ticket=%s issued=%llu expires=%llu activation=no lease=no",
                    state, record.authorization.resourceName.c_str(), record.authorization.serverIpv4[0], record.authorization.serverIpv4[1],
                    record.authorization.serverIpv4[2], record.authorization.serverIpv4[3], record.authorization.serverPort, record.authorization.packFormat,
                    GetNativeWorldStartupPolicyName(record.authorization.packFormat), EncodeHex(record.contentId).c_str(), ticketCorrelation.c_str(),
                    result.issuedAt, result.expiresAt);
        return result;
    }

    bool CurrentTime(unsigned long long& now, std::string& error);
    bool ValidateSpentLedger(STransaction& transaction, unsigned long long now, std::string& error);

    bool ReadInspectablePending(STransaction& transaction, SRecord& record, EReadResult& read, unsigned long long& now, std::string& error)
    {
        if (!BeginTransaction(transaction, error))
            return false;
        if (!transaction.temporaryFiles.empty())
        {
            error = "authorization store contains an unproven temporary remnant";
            return false;
        }
        if (!CurrentTime(now, error) || !ValidateSpentLedger(transaction, now, error))
            return false;
        read = ReadRecord(transaction.pending, record, error);
        if (read == EReadResult::Failure)
            return false;
        if (read == EReadResult::Success && transaction.spentTickets.count(EncodeHex(record.ticketId)))
        {
            error = "the pending authorization ticket is already spent";
            return false;
        }
        return true;
    }

    bool TemporaryRecordsMatch(const STransaction& transaction, const SRecord& expected, std::string& error)
    {
        for (const std::wstring& temporary : transaction.temporaryFiles)
        {
            SRecord record;
            if (ReadRecord(temporary, record, error) != EReadResult::Success || !RecordsEqual(record, expected))
            {
                if (error.empty())
                    error = "authorization temporary remnant does not match the record being terminalized";
                return false;
            }
        }
        return true;
    }

    bool DeleteTemporaryRecords(const STransaction& transaction, std::string& error)
    {
        for (const std::wstring& temporary : transaction.temporaryFiles)
        {
            if (!DeleteExactFile(temporary, false, error))
                return false;
        }
        return true;
    }

    bool CreateTerminalizationMarker(STransaction& transaction, const SRecord& record, const std::wstring& marker, std::string& error)
    {
        std::vector<unsigned char> envelope;
        if (!ProtectRecord(EncodeRecord(record), envelope, error))
            return false;

        if (!WriteAndFlush(marker, envelope, error))
        {
            // ResourceStop has begun. Keep even a partial recognized marker
            // fail-closed so a later startup cannot consume the pending
            // record after this terminalization attempt became ambiguous.
            return false;
        }
        SRecord reopened;
        if (ReadRecord(marker, reopened, error) != EReadResult::Success || !RecordsEqual(record, reopened))
        {
            if (error.empty())
                error = "authorization terminalization marker differs from the pending record";
            // The exact marker remains as a durable blocker. Explicit clear
            // is the only safe recovery when verification is inconclusive.
            return false;
        }
        transaction.temporaryFiles.push_back(marker);
        return true;
    }

    bool CurrentTime(unsigned long long& now, std::string& error)
    {
        const time_t current = time(nullptr);
        if (current <= 0)
        {
            error = "authorization wall clock is unavailable";
            return false;
        }
        now = static_cast<unsigned long long>(current);
        return true;
    }

    bool ValidateSpentLedger(STransaction& transaction, unsigned long long now, std::string& error)
    {
        // Bound hostile enumeration work, but always prune recognized expired
        // receipts before enforcing the smaller live-ledger capacity. This
        // keeps a formerly full store recoverable as its 15-minute window ends.
        if (transaction.revokedFiles.size() > 256)
        {
            error = "authorization spent ledger enumeration exceeds its hard bound";
            return false;
        }
        for (const std::wstring& path : transaction.revokedFiles)
        {
            SRecord record;
            if (ReadRecord(path, record, error) != EReadResult::Success)
                return false;
            const std::string  ticket = EncodeHex(record.ticketId);
            const std::wstring expectedSuffix = SharedUtil::FromUTF8(ticket).c_str();
            if (path.size() < expectedSuffix.size() || _wcsicmp(path.c_str() + path.size() - expectedSuffix.size(), expectedSuffix.c_str()) != 0)
            {
                error = "authorization spent filename does not match its ticket";
                return false;
            }
            const EFreshness freshness = EvaluateFreshness(record, now);
            if (freshness == EFreshness::Expired)
            {
                if (!DeleteExactFile(path, false, error))
                    return false;
                continue;
            }
            if (freshness == EFreshness::ClockRollback || !transaction.spentTickets.emplace(ticket).second)
            {
                error = freshness == EFreshness::ClockRollback ? "authorization spent record appears issued in the future"
                                                               : "authorization spent ledger contains a duplicate ticket";
                return false;
            }
        }
        if (transaction.spentTickets.size() > 64)
        {
            error = "authorization live spent ledger exceeds its bound";
            return false;
        }
        return true;
    }

    SNativeWorldStartupSelection MakeStartupSelection(const SRecord& record)
    {
        SNativeWorldStartupSelection selection;
        selection.success = true;
        selection.found = true;
        selection.wireVersion = record.authorization.wireVersion;
        selection.startupMode = record.authorization.startupMode;
        selection.policy = record.authorization.policy;
        selection.packFormat = record.authorization.packFormat;
        selection.serverIdDigest = record.authorization.serverIdDigest;
        selection.serverIpv4 = record.authorization.serverIpv4;
        selection.serverPort = record.authorization.serverPort;
        selection.bitstreamVersion = record.authorization.bitstreamVersion;
        selection.issuedAt = record.issuedAt;
        selection.expiresAt = record.expiresAt;
        selection.resourceName = record.authorization.resourceName;
        selection.offerId = EncodeHex(record.offerId);
        selection.contentId = EncodeHex(record.contentId);
        selection.ticketId = EncodeHex(record.ticketId);
        return selection;
    }

    SNativeWorldAuthorizationRecordResult TerminalizeStartup(SStartupTransaction& startup, bool claim, bool cancelledAtCommit, const std::string& refusalReason)
    {
        SNativeWorldAuthorizationRecordResult result;
        std::string                           effectiveRefusalReason = refusalReason;
        SRecord                               pending;
        const EReadResult                     read = ReadRecord(startup.transaction.pending, pending, result.error);
        if (read != EReadResult::Success || !RecordsEqual(pending, startup.record))
        {
            if (result.error.empty())
                result.error = read == EReadResult::Missing ? "selected authorization disappeared before terminalization"
                                                            : "selected authorization changed before terminalization";
            return result;
        }
        if (!TemporaryRecordsMatch(startup.transaction, startup.record, result.error))
            return result;

        unsigned long long now = 0;
        if (!CurrentTime(now, result.error))
            return result;
        const EFreshness freshness = EvaluateFreshness(startup.record, now);
        if (claim && cancelledAtCommit)
        {
            claim = false;
            effectiveRefusalReason = "cancelled-at-claim";
        }
        else if (claim && freshness != EFreshness::Fresh)
        {
            claim = false;
            effectiveRefusalReason = freshness == EFreshness::Expired ? "expired-at-claim" : "clock-refused-at-claim";
        }

        const std::string  ticket = EncodeHex(startup.record.ticketId);
        const std::wstring marker = JoinPath(startup.transaction.directory, std::wstring(TEMP_PREFIX) + SharedUtil::FromUTF8(ticket).c_str());
        const std::wstring spent = JoinPath(startup.transaction.directory, std::wstring(SPENT_PREFIX) + SharedUtil::FromUTF8(ticket).c_str());
        // A claim is exactly one write-through rename: a crash before it
        // leaves pending and a crash after it leaves spent. Only refusal uses
        // the extra blocker required for fail-closed terminalization.
        if (!claim && startup.transaction.temporaryFiles.empty() && !CreateTerminalizationMarker(startup.transaction, startup.record, marker, result.error))
        {
            const std::string markerError = result.error;
            if (MoveFileExW(startup.transaction.pending.c_str(), spent.c_str(), MOVEFILE_WRITE_THROUGH))
            {
                SRecord     reopened;
                std::string reopenError;
                if (ReadRecord(spent, reopened, reopenError) == EReadResult::Success && RecordsEqual(reopened, startup.record))
                {
                    std::string cleanupError;
                    if (DeleteExactFile(marker, true, cleanupError))
                    {
                        result = MakeResult(reopened, "terminal-refused");
                        result.diagnostic += SString(" refusal=%s fallback=direct-spent", effectiveRefusalReason.c_str());
                        return result;
                    }
                    result.error = markerError + "; direct spent rename succeeded but blocker cleanup failed: " + cleanupError;
                }
                else
                    result.error = markerError + "; direct spent rename verification failed: " + reopenError;
            }
            else
            {
                const DWORD renameError = GetLastError();
                std::string deleteError;
                if (DeleteExactFile(startup.transaction.pending, false, deleteError))
                {
                    result.error = SString("%s; direct spent rename failed win32=%u; pending was handle-verified and removed without a spent receipt",
                                           markerError.c_str(), renameError);
                }
                else
                    result.error = SString("%s; direct spent rename failed win32=%u; verified pending removal failed: %s", markerError.c_str(), renameError,
                                           deleteError.c_str());
            }
            result.publicationAmbiguous = true;
            return result;
        }
        if (!TemporaryRecordsMatch(startup.transaction, startup.record, result.error))
            return result;

        if (!MoveFileExW(startup.transaction.pending.c_str(), spent.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            result.publicationAmbiguous = true;
            result.error = SString("authorization startup terminalization rename failed win32=%u", GetLastError());
            return result;
        }
        SRecord reopened;
        if (ReadRecord(spent, reopened, result.error) != EReadResult::Success || !RecordsEqual(reopened, startup.record))
        {
            result.publicationAmbiguous = true;
            if (result.error.empty())
                result.error = "spent authorization differs after durable startup rename";
            return result;
        }
        if (!DeleteTemporaryRecords(startup.transaction, result.error))
        {
            result.publicationAmbiguous = true;
            return result;
        }

        result = MakeResult(reopened, claim ? "claimed" : "terminal-refused");
        result.claimed = claim;
        if (!claim && !effectiveRefusalReason.empty())
            result.diagnostic += SString(" refusal=%s", effectiveRefusalReason.c_str());
        return result;
    }
}

SNativeWorldAuthorizationRecordResult NativeWorldAuthorizationStore::Persist(const SNativeWorldStartupAuthorization&     authorization,
                                                                             const SNativeWorldAuthorizationPublication& publication)
{
    SNativeWorldAuthorizationRecordResult result;
    if (!publication.success || !ValidateAuthorization(authorization, result.error) || !IsLowerHex(publication.offerId, 64) ||
        !IsLowerHex(publication.contentId, 64))
    {
        if (result.error.empty())
            result.error = "authorization publication identity is invalid";
        return result;
    }

    STransaction transaction;
    if (!BeginTransaction(transaction, result.error))
        return result;
    if (!transaction.temporaryFiles.empty())
    {
        result.error = "authorization store contains an unproven temporary remnant; use explicit clear";
        return result;
    }

    unsigned long long now = 0;
    if (!CurrentTime(now, result.error))
        return result;
    if (!ValidateSpentLedger(transaction, now, result.error))
        return result;

    SRecord           existing;
    const EReadResult existingRead = ReadRecord(transaction.pending, existing, result.error);
    if (existingRead == EReadResult::Failure)
        return result;
    if (existingRead == EReadResult::Success)
    {
        if (transaction.spentTickets.count(EncodeHex(existing.ticketId)))
        {
            result.error = "the pending authorization ticket is already spent";
            return result;
        }
        const EFreshness freshness = EvaluateFreshness(existing, now);
        if (freshness == EFreshness::ClockRollback)
        {
            result.error = "authorization record appears issued in the future";
            return result;
        }
        if (freshness == EFreshness::Fresh)
        {
            if (!SameSemanticAuthorization(existing, authorization, publication))
            {
                if (!SameDurableAuthorization(existing, authorization, publication))
                {
                    result.error = "a different unexpired authorization is already pending";
                    return result;
                }

                // A reconnect may reproduce the exact durable authorization
                // with new process-lifetime provenance. Attach the live
                // resource for stop-time revocation without refreshing the
                // original ticket or its fixed expiry.
                result = MakeResult(existing, "pending");
                result.attached = true;
                return result;
            }
            result = MakeResult(existing, "pending");
            result.idempotent = true;
            return result;
        }
        if (!DeleteExactFile(transaction.pending, false, result.error))
            return result;
    }
    if (transaction.spentTickets.size() >= 64)
    {
        result.error = "authorization live spent ledger is full";
        return result;
    }

    SRecord record;
    record.authorization = authorization;
    if (!DecodeHex(publication.offerId, record.offerId) || !DecodeHex(publication.contentId, record.contentId) ||
        !FillRandom(record.ticketId.data(), record.ticketId.size(), result.error))
        return result;
    record.issuedAt = now;
    record.expiresAt = now + RECORD_LIFETIME_SECONDS;

    const std::vector<unsigned char> plaintext = EncodeRecord(record);
    std::vector<unsigned char>       envelope;
    if (!ProtectRecord(plaintext, envelope, result.error))
        return result;

    std::array<unsigned char, 16> temporaryToken{};
    std::array<unsigned char, 16> markerToken{};
    if (!FillRandom(temporaryToken.data(), temporaryToken.size(), result.error) || !FillRandom(markerToken.data(), markerToken.size(), result.error))
        return result;
    const std::wstring temporary = JoinPath(transaction.directory, std::wstring(TEMP_PREFIX) + SharedUtil::FromUTF8(EncodeHex(temporaryToken)).c_str());
    const std::wstring marker = JoinPath(transaction.directory, std::wstring(TEMP_PREFIX) + SharedUtil::FromUTF8(EncodeHex(markerToken)).c_str());
    if (!WriteAndFlush(temporary, envelope, result.error))
    {
        std::string cleanupError;
        DeleteExactFile(temporary, true, cleanupError);
        return result;
    }

    SRecord reopened;
    if (ReadRecord(temporary, reopened, result.error) != EReadResult::Success || !RecordsEqual(record, reopened))
    {
        if (result.error.empty())
            result.error = "authorization temporary reopen differs from the canonical record";
        std::string cleanupError;
        DeleteExactFile(temporary, true, cleanupError);
        return result;
    }

    // Keep a second, verified copy until the final pending record has been
    // reopened. If an antivirus or filesystem filter makes terminalization
    // ambiguous, this marker blocks later activation instead of allowing an
    // apparently refused authorization to survive unnoticed.
    if (!WriteAndFlush(marker, envelope, result.error))
    {
        std::string cleanupError;
        DeleteExactFile(marker, true, cleanupError);
        DeleteExactFile(temporary, true, cleanupError);
        return result;
    }
    SRecord markerRecord;
    if (ReadRecord(marker, markerRecord, result.error) != EReadResult::Success || !RecordsEqual(record, markerRecord))
    {
        if (result.error.empty())
            result.error = "authorization ambiguity marker differs from the canonical record";
        std::string cleanupError;
        DeleteExactFile(marker, true, cleanupError);
        DeleteExactFile(temporary, true, cleanupError);
        return result;
    }

    if (!MoveFileExW(temporary.c_str(), transaction.pending.c_str(), MOVEFILE_WRITE_THROUGH))
    {
        const DWORD publishError = GetLastError();
        std::string cleanupError;
        DeleteExactFile(temporary, true, cleanupError);
        DeleteExactFile(marker, true, cleanupError);
        result.error = SString("authorization atomic publication failed win32=%u", publishError);
        return result;
    }
    result.found = true;
    result.publicationAmbiguous = true;

    SRecord published;
    if (ReadRecord(transaction.pending, published, result.error) != EReadResult::Success || !RecordsEqual(record, published))
    {
        const std::string  reopenError = result.error.empty() ? "authorization final reopen differs from the canonical record" : result.error;
        const std::wstring revoked = JoinPath(transaction.directory, std::wstring(REVOKED_PREFIX) + SharedUtil::FromUTF8(EncodeHex(record.ticketId)).c_str());
        if (MoveFileExW(transaction.pending.c_str(), revoked.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            SRecord     burned;
            std::string burnedError;
            const bool  burnedVerified = ReadRecord(revoked, burned, burnedError) == EReadResult::Success && RecordsEqual(record, burned);
            if (burnedVerified)
            {
                std::string markerDeleteError;
                if (DeleteExactFile(marker, false, markerDeleteError))
                    result.publicationAmbiguous = false;
                result.error = markerDeleteError.empty() ? reopenError + "; ticket was durably revoked"
                                                         : reopenError + "; ticket was revoked but its ambiguity marker remains: " + markerDeleteError;
            }
            else
                result.error = reopenError + "; ticket was revoked but its final verification failed: " + burnedError;
        }
        else
            result.error = SString("%s; pending terminalization failed win32=%u", reopenError.c_str(), GetLastError());
        return result;
    }
    if (!DeleteExactFile(marker, false, result.error))
    {
        const std::string  markerDeleteError = result.error;
        const std::wstring revoked = JoinPath(transaction.directory, std::wstring(REVOKED_PREFIX) + SharedUtil::FromUTF8(EncodeHex(record.ticketId)).c_str());
        if (MoveFileExW(transaction.pending.c_str(), revoked.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            SRecord     burned;
            std::string burnedError;
            if (ReadRecord(revoked, burned, burnedError) == EReadResult::Success && RecordsEqual(record, burned))
                result.error = markerDeleteError + "; ticket was durably revoked but its ambiguity marker remains";
            else
                result.error = markerDeleteError + "; ticket was revoked but its final verification failed: " + burnedError;
        }
        else
            result.error = SString("%s; pending terminalization failed win32=%u", markerDeleteError.c_str(), GetLastError());
        return result;
    }
    result.publicationAmbiguous = false;
    return MakeResult(published, "pending");
}

SNativeWorldAuthorizationRecordResult NativeWorldAuthorizationStore::Inspect()
{
    SNativeWorldAuthorizationRecordResult result;
    STransaction                          transaction;
    unsigned long long                    now = 0;
    SRecord                               record;
    EReadResult                           read = EReadResult::Failure;
    if (!ReadInspectablePending(transaction, record, read, now, result.error))
        return result;
    if (read == EReadResult::Missing)
    {
        result.success = true;
        result.diagnostic = "state=absent activation=no lease=no";
        return result;
    }
    const EFreshness freshness = EvaluateFreshness(record, now);
    return MakeResult(record, freshness == EFreshness::Fresh ? "pending" : freshness == EFreshness::Expired ? "expired" : "clock-refused");
}

SNativeWorldAuthorizationRecordResult NativeWorldAuthorizationStore::InspectFreshRestartTarget(SRestartTarget& target)
{
    target = {};
    SNativeWorldAuthorizationRecordResult result;
    STransaction                          transaction;
    unsigned long long                    now = 0;
    SRecord                               record;
    EReadResult                           read = EReadResult::Failure;
    if (!ReadInspectablePending(transaction, record, read, now, result.error))
        return result;
    if (read == EReadResult::Missing)
    {
        result.error = "no pending native-world authorization is available for restart";
        return result;
    }

    const EFreshness freshness = EvaluateFreshness(record, now);
    if (freshness != EFreshness::Fresh)
    {
        result.error =
            freshness == EFreshness::Expired ? "native-world authorization expired before restart" : "native-world authorization clock check refused restart";
        return result;
    }
    if (record.expiresAt - now < 60)
    {
        result.error = "native-world authorization has insufficient time remaining for restart";
        return result;
    }

    target.serverIpv4 = record.authorization.serverIpv4;
    target.serverPort = record.authorization.serverPort;
    return MakeResult(record, "restart-ready");
}

SNativeWorldAuthorizationRecordResult NativeWorldAuthorizationStore::Clear()
{
    SNativeWorldAuthorizationRecordResult result;
    STransaction                          transaction;
    if (!BeginTransaction(transaction, result.error))
        return result;
    const DWORD pendingAttributes = GetFileAttributesW(transaction.pending.c_str());
    const bool  pendingExists = pendingAttributes != INVALID_FILE_ATTRIBUTES;
    if (!pendingExists)
    {
        const DWORD attributeError = GetLastError();
        if (attributeError != ERROR_FILE_NOT_FOUND && attributeError != ERROR_PATH_NOT_FOUND)
        {
            result.error = SString("authorization pending status failed win32=%u", attributeError);
            return result;
        }
    }
    if (pendingExists && !DeleteExactFile(transaction.pending, false, result.error))
        return result;
    for (const std::wstring& temporary : transaction.temporaryFiles)
    {
        if (!DeleteExactFile(temporary, false, result.error))
            return result;
    }
    result.success = true;
    result.found = pendingExists || !transaction.temporaryFiles.empty();
    result.diagnostic = SString("state=cleared removed=%s activation=no lease=no", result.found ? "yes" : "no");
    return result;
}

SNativeWorldAuthorizationRecordResult NativeWorldAuthorizationStore::Revoke(const SNativeWorldStartupAuthorization& authorization, const std::string& contentId)
{
    SNativeWorldAuthorizationRecordResult result;
    if (!ValidateAuthorization(authorization, result.error) || !IsLowerHex(contentId, 64))
        return result;
    STransaction transaction;
    if (!BeginTransaction(transaction, result.error))
        return result;
    unsigned long long now = 0;
    if (!CurrentTime(now, result.error) || !ValidateSpentLedger(transaction, now, result.error))
        return result;
    SRecord           record;
    const EReadResult read = ReadRecord(transaction.pending, record, result.error);
    if (read == EReadResult::Failure)
        return result;
    if (read == EReadResult::Missing)
    {
        for (const std::wstring& revokedPath : transaction.revokedFiles)
        {
            SRecord     revokedRecord;
            std::string revokedError;
            if (ReadRecord(revokedPath, revokedRecord, revokedError) == EReadResult::Success && MatchesRevocation(revokedRecord, authorization, contentId))
            {
                if (!TemporaryRecordsMatch(transaction, revokedRecord, result.error) || !DeleteTemporaryRecords(transaction, result.error))
                    return result;
                result = MakeResult(revokedRecord, "revoked");
                result.idempotent = true;
                return result;
            }
        }
        if (!transaction.temporaryFiles.empty())
        {
            result.error = "authorization store contains an unproven temporary remnant";
            return result;
        }
        result.success = true;
        result.diagnostic = "state=absent activation=no lease=no";
        return result;
    }
    if (!MatchesRevocation(record, authorization, contentId))
    {
        result.error = "pending authorization does not belong to the stopped resource session";
        return result;
    }
    if (transaction.spentTickets.count(EncodeHex(record.ticketId)))
    {
        result.error = "the pending authorization ticket is already spent";
        return result;
    }
    const std::wstring revoked = JoinPath(transaction.directory, std::wstring(REVOKED_PREFIX) + SharedUtil::FromUTF8(EncodeHex(record.ticketId)).c_str());
    if (transaction.temporaryFiles.empty())
    {
        const std::wstring marker = JoinPath(transaction.directory, std::wstring(TEMP_PREFIX) + SharedUtil::FromUTF8(EncodeHex(record.ticketId)).c_str());
        if (!CreateTerminalizationMarker(transaction, record, marker, result.error))
        {
            const std::string markerError = result.error;
            if (MoveFileExW(transaction.pending.c_str(), revoked.c_str(), MOVEFILE_WRITE_THROUGH))
            {
                SRecord     fallbackReopened;
                std::string fallbackError;
                if (ReadRecord(revoked, fallbackReopened, fallbackError) == EReadResult::Success && RecordsEqual(record, fallbackReopened))
                {
                    std::string cleanupError;
                    if (DeleteExactFile(marker, true, cleanupError))
                        return MakeResult(fallbackReopened, "revoked");
                    result.error = markerError + "; direct revocation succeeded but blocker cleanup failed: " + cleanupError;
                }
                else
                    result.error = markerError + "; direct revocation final verification failed: " + fallbackError;
            }
            else
            {
                const DWORD renameError = GetLastError();
                std::string deleteError;
                if (DeleteExactFile(transaction.pending, false, deleteError))
                    result.error = SString("%s; direct revocation rename failed win32=%u; pending was handle-verified and removed without a spent receipt",
                                           markerError.c_str(), renameError);
                else
                    result.error = SString("%s; direct revocation rename failed win32=%u; pending removal also failed: %s", markerError.c_str(), renameError,
                                           deleteError.c_str());
            }
            result.found = true;
            result.publicationAmbiguous = true;
            return result;
        }
    }
    if (!TemporaryRecordsMatch(transaction, record, result.error))
        return result;
    if (!MoveFileExW(transaction.pending.c_str(), revoked.c_str(), MOVEFILE_WRITE_THROUGH))
    {
        result.found = true;
        result.publicationAmbiguous = true;
        result.error = SString("authorization revocation rename failed win32=%u", GetLastError());
        return result;
    }
    SRecord reopened;
    if (ReadRecord(revoked, reopened, result.error) != EReadResult::Success || !RecordsEqual(record, reopened))
    {
        result.found = true;
        result.publicationAmbiguous = true;
        if (result.error.empty())
            result.error = "revoked authorization differs after durable rename";
        return result;
    }
    if (!DeleteTemporaryRecords(transaction, result.error))
    {
        result.found = true;
        result.publicationAmbiguous = true;
        return result;
    }
    result = MakeResult(reopened, "revoked");
    return result;
}

SNativeWorldStartupSelection NativeWorldAuthorizationStore::BeginStartup(const std::array<unsigned char, 4>* endpointIpv4, unsigned short endpointPort,
                                                                         bool legacySelectorEnabled)
{
    SNativeWorldStartupSelection selection;
    CStartupBeginScope           beginScope;
    if (!beginScope.Enter(selection.error))
        return selection;

    auto startup = std::make_unique<SStartupTransaction>();
    if (!BeginTransaction(startup->transaction, selection.error))
        return selection;
    if (!startup->transaction.temporaryFiles.empty())
    {
        selection.error = "authorization store contains an unproven temporary remnant";
        return selection;
    }

    unsigned long long now = 0;
    if (!CurrentTime(now, selection.error) || !ValidateSpentLedger(startup->transaction, now, selection.error))
        return selection;
    const EReadResult read = ReadRecord(startup->transaction.pending, startup->record, selection.error);
    if (read == EReadResult::Failure)
        return selection;
    if (read == EReadResult::Missing)
    {
        selection.success = true;
        selection.diagnostic = "state=absent activation=no lease=no";
        return selection;
    }

    selection = MakeStartupSelection(startup->record);
    const std::string ticket = selection.ticketId;
    if (startup->transaction.spentTickets.count(ticket))
    {
        selection.success = false;
        selection.error = "the pending startup authorization ticket is already spent";
        return selection;
    }
    const EFreshness freshness = EvaluateFreshness(startup->record, now);
    if (freshness != EFreshness::Fresh)
    {
        selection.diagnostic =
            SString("state=%s ticket=%s activation=no lease=no", freshness == EFreshness::Expired ? "expired" : "clock-refused", ticket.substr(0, 8).c_str());
        return selection;
    }

    const bool endpointMatches =
        endpointIpv4 && *endpointIpv4 == startup->record.authorization.serverIpv4 && endpointPort == startup->record.authorization.serverPort;
    if (legacySelectorEnabled)
    {
        selection.terminalRefusalRequired = true;
        selection.diagnostic = SString("state=selector-ambiguous ticket=%s activation=no lease=no", ticket.substr(0, 8).c_str());
        beginScope.Publish(std::move(startup));
        return selection;
    }
    if (!endpointMatches)
    {
        selection.diagnostic =
            SString("state=pending target=%s ticket=%s activation=no lease=no", endpointIpv4 ? "mismatch" : "non-canonical", ticket.substr(0, 8).c_str());
        return selection;
    }

    selection.ready = true;
    selection.diagnostic =
        SString("state=selected endpoint=%u.%u.%u.%u:%u format=%u policy=%s contentId=%s ticket=%s activation=no lease=no", selection.serverIpv4[0],
                selection.serverIpv4[1], selection.serverIpv4[2], selection.serverIpv4[3], selection.serverPort, selection.packFormat,
                GetNativeWorldStartupPolicyName(startup->record.authorization.packFormat), selection.contentId.c_str(), ticket.substr(0, 8).c_str());
    beginScope.Publish(std::move(startup));
    return selection;
}

SNativeWorldAuthorizationRecordResult NativeWorldAuthorizationStore::FinishStartup(const std::string& ticketId, bool claim, const std::string& refusalReason)
{
    SNativeWorldAuthorizationRecordResult result;
    bool                                  cancelledAtCommit = false;
    if ((!claim && (refusalReason.empty() || refusalReason.size() > 64)) ||
        !std::all_of(refusalReason.begin(), refusalReason.end(),
                     [](unsigned char character) { return (character >= 'a' && character <= 'z') || character == '-'; }))
    {
        result.error = "native-world startup refusal reason is non-canonical";
        return result;
    }
    {
        std::lock_guard<std::mutex> stateLock(g_startupStateMutex);
        if (!g_startupTransaction || g_startupFinishing)
        {
            result.error = "no available native-world startup transaction is active";
            return result;
        }
        if (!IsLowerHex(ticketId, 32) || EncodeHex(g_startupTransaction->record.ticketId) != ticketId)
        {
            result.error = "native-world startup transaction ticket mismatch";
            return result;
        }

        // This lock acquisition is the claim/refusal commit boundary.
        // Cancellation before it is terminalized as a refusal; shutdown after
        // it is deliberately too late to alter the already chosen outcome.
        cancelledAtCommit = g_startupCancelled.load(std::memory_order_acquire);
        g_startupFinishing = true;
    }

    result = TerminalizeStartup(*g_startupTransaction, claim, cancelledAtCommit, refusalReason);
    {
        std::lock_guard<std::mutex> stateLock(g_startupStateMutex);
        g_startupTransaction.reset();
        g_startupFinishing = false;
    }
    return result;
}

void NativeWorldAuthorizationStore::CancelStartup(const std::string& ticketId)
{
    std::lock_guard<std::mutex> stateLock(g_startupStateMutex);
    if (!g_startupFinishing && g_startupTransaction && EncodeHex(g_startupTransaction->record.ticketId) == ticketId)
        g_startupCancelled.store(true, std::memory_order_release);
}

bool NativeWorldAuthorizationStore::IsStartupCancelled(const std::string& ticketId)
{
    std::lock_guard<std::mutex> stateLock(g_startupStateMutex);
    return g_startupTransaction && EncodeHex(g_startupTransaction->record.ticketId) == ticketId && g_startupCancelled.load(std::memory_order_acquire);
}

void NativeWorldAuthorizationStore::CancelActiveStartup()
{
    std::lock_guard<std::mutex> stateLock(g_startupStateMutex);
    if (!g_startupFinishing && (g_startupBeginning || g_startupTransaction))
        g_startupCancelled.store(true, std::memory_order_release);
}
