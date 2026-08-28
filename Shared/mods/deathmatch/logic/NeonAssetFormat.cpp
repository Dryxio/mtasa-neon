/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto: Neon
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Authenticated encrypted resource asset format
 *
 *****************************************************************************/

#include "StdInc.h"
#include "NeonAssetFormat.h"

#include <cryptopp/aes.h>
#include <cryptopp/filters.h>
#include <cryptopp/gcm.h>
#include <cryptopp/misc.h>
#include <filesystem>
#include <limits>

namespace
{
    int DecodeHexNibble(char value)
    {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        if (value >= 'A' && value <= 'F')
            return value - 'A' + 10;
        return -1;
    }

    template <std::size_t Size>
    bool DecodeHex(std::string_view input, std::array<std::uint8_t, Size>& output)
    {
        if (input.size() != Size * 2)
            return false;

        for (std::size_t index = 0; index < Size; ++index)
        {
            const int high = DecodeHexNibble(input[index * 2]);
            const int low = DecodeHexNibble(input[index * 2 + 1]);
            if (high < 0 || low < 0)
                return false;
            output[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
        return true;
    }

    std::uint64_t ReadLittleEndian64(const std::uint8_t* data)
    {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < sizeof(value); ++index)
            value |= static_cast<std::uint64_t>(data[index]) << (index * 8);
        return value;
    }

    void AppendLittleEndian16(std::string& output, std::uint16_t value)
    {
        output.push_back(static_cast<char>(value & 0xff));
        output.push_back(static_cast<char>((value >> 8) & 0xff));
    }

    bool IsSupportedType(NeonAsset::Type type)
    {
        return type == NeonAsset::Type::Dff || type == NeonAsset::Type::Txd || type == NeonAsset::Type::Col;
    }
}

bool NeonAsset::DecodePackageId(std::string_view hex, PackageId& output)
{
    return DecodeHex(hex, output);
}

bool NeonAsset::DecodeContentKey(std::string_view hex, ContentKey& output)
{
    return DecodeHex(hex, output);
}

bool NeonAsset::IsCanonicalRelativePath(std::string_view path)
{
    if (path.empty() || path.size() > std::numeric_limits<std::uint16_t>::max() || path.front() == '/' || path.back() == '/' ||
        path.find('\\') != std::string_view::npos || path.find_first_of(":*?\"<>|") != std::string_view::npos)
    {
        return false;
    }

    const std::filesystem::path filesystemPath{std::string(path)};
    if (filesystemPath.has_root_path() || filesystemPath.lexically_normal().generic_string() != path)
        return false;
    return std::none_of(filesystemPath.begin(), filesystemPath.end(),
                        [](const std::filesystem::path& part) { return part.empty() || part == "." || part == ".."; });
}

bool NeonAsset::ParseHeader(std::string_view container, Header& output, std::string& error)
{
    if (container.size() < HEADER_SIZE + TAG_SIZE)
    {
        error = "container is shorter than the authenticated header and tag";
        return false;
    }

    const auto* data = reinterpret_cast<const std::uint8_t*>(container.data());
    if (!std::equal(MAGIC.begin(), MAGIC.end(), data))
    {
        error = "invalid container magic";
        return false;
    }
    if (data[8] != FORMAT_VERSION)
    {
        error = "unsupported container version";
        return false;
    }

    output.type = static_cast<Type>(data[9]);
    if (!IsSupportedType(output.type) || data[10] != 0 || data[11] != 0)
    {
        error = "invalid asset type or reserved header bits";
        return false;
    }

    output.plaintextSize = ReadLittleEndian64(data + 12);
    if (output.plaintextSize == 0 || output.plaintextSize > MAX_PLAINTEXT_SIZE || container.size() != HEADER_SIZE + output.plaintextSize + TAG_SIZE)
    {
        error = "invalid plaintext length";
        return false;
    }

    std::copy_n(data + 20, PACKAGE_ID_SIZE, output.packageId.begin());
    std::copy_n(data + 36, NONCE_SIZE, output.nonce.begin());
    return true;
}

bool NeonAsset::Decrypt(std::string_view container, const ContentKey& key, const PackageId& expectedPackageId, std::string_view resourceName,
                        std::string_view relativePath, std::string& plaintext, Type& type, std::string& error)
{
    Header header;
    if (!ParseHeader(container, header, error))
        return false;
    if (header.packageId != expectedPackageId)
    {
        error = "container package id does not match the resource capability";
        return false;
    }
    if (resourceName.empty() || resourceName.size() > std::numeric_limits<std::uint16_t>::max() || !IsCanonicalRelativePath(relativePath))
    {
        error = "invalid resource or asset path binding";
        return false;
    }

    std::string associatedData(container.substr(0, HEADER_SIZE));
    AppendLittleEndian16(associatedData, static_cast<std::uint16_t>(resourceName.size()));
    associatedData.append(resourceName);
    AppendLittleEndian16(associatedData, static_cast<std::uint16_t>(relativePath.size()));
    associatedData.append(relativePath);

    try
    {
        CryptoPP::GCM<CryptoPP::AES>::Decryption decryptor;
        decryptor.SetKeyWithIV(key.data(), key.size(), header.nonce.data(), header.nonce.size());

        std::string                             candidate;
        CryptoPP::AuthenticatedDecryptionFilter filter(decryptor, new CryptoPP::StringSink(candidate), CryptoPP::AuthenticatedDecryptionFilter::THROW_EXCEPTION,
                                                       TAG_SIZE);
        filter.ChannelPut(CryptoPP::AAD_CHANNEL, reinterpret_cast<const CryptoPP::byte*>(associatedData.data()), associatedData.size());
        filter.ChannelMessageEnd(CryptoPP::AAD_CHANNEL);
        filter.ChannelPut(CryptoPP::DEFAULT_CHANNEL, reinterpret_cast<const CryptoPP::byte*>(container.data() + HEADER_SIZE), container.size() - HEADER_SIZE);
        filter.ChannelMessageEnd(CryptoPP::DEFAULT_CHANNEL);

        if (!filter.GetLastResult() || candidate.size() != header.plaintextSize)
        {
            SecureClear(candidate);
            error = "asset authentication failed";
            return false;
        }

        SecureClear(plaintext);
        plaintext = std::move(candidate);
        type = header.type;
        return true;
    }
    catch (const CryptoPP::Exception& exception)
    {
        SecureClear(plaintext);
        error = std::string("asset authentication failed: ") + exception.what();
        return false;
    }
}

void NeonAsset::SecureWipe(void* data, std::size_t size) noexcept
{
    if (data && size)
        CryptoPP::SecureWipeBuffer(reinterpret_cast<CryptoPP::byte*>(data), size);
}

void NeonAsset::SecureClear(std::string& value) noexcept
{
    SecureWipe(value.data(), value.size());
    std::string().swap(value);
}
