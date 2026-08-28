/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto: Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Authenticated encrypted resource asset format
 *
 *****************************************************************************/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace NeonAsset
{
    constexpr std::array<std::uint8_t, 8> MAGIC{{'N', 'E', 'O', 'N', 'A', 'S', 'T', '1'}};
    constexpr std::uint8_t                FORMAT_VERSION = 1;
    constexpr std::size_t                 PACKAGE_ID_SIZE = 16;
    constexpr std::size_t                 CONTENT_KEY_SIZE = 32;
    constexpr std::size_t                 NONCE_SIZE = 12;
    constexpr std::size_t                 TAG_SIZE = 16;
    constexpr std::size_t                 HEADER_SIZE = 48;
    constexpr std::uint64_t               MAX_PLAINTEXT_SIZE = 64ULL * 1024ULL * 1024ULL;

    using PackageId = std::array<std::uint8_t, PACKAGE_ID_SIZE>;
    using ContentKey = std::array<std::uint8_t, CONTENT_KEY_SIZE>;

    enum class Type : std::uint8_t
    {
        Dff = 1,
        Txd = 2,
        Col = 3,
    };

    struct Header
    {
        Type                                 type{};
        std::uint64_t                        plaintextSize{};
        PackageId                            packageId{};
        std::array<std::uint8_t, NONCE_SIZE> nonce{};
    };

    bool DecodePackageId(std::string_view hex, PackageId& output);
    bool DecodeContentKey(std::string_view hex, ContentKey& output);
    bool IsCanonicalRelativePath(std::string_view path);
    bool ParseHeader(std::string_view container, Header& output, std::string& error);
    bool Decrypt(std::string_view container, const ContentKey& key, const PackageId& expectedPackageId, std::string_view resourceName,
                 std::string_view relativePath, std::string& plaintext, Type& type, std::string& error);
    void SecureWipe(void* data, std::size_t size) noexcept;
    void SecureClear(std::string& value) noexcept;
}
