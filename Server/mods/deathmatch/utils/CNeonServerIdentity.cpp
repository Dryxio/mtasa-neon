/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto: San Andreas
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/utils/CNeonServerIdentity.cpp
 *  PURPOSE:     Persistent cryptographic identity for automatic Neon onboarding
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNeonServerIdentity.h"
#include "CLogger.h"

#include <cryptopp/base64.h>
#include <cryptopp/osrng.h>
#include <cryptopp/xed25519.h>

#ifndef WIN32
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace
{
    constexpr const char* KEY_FILE_HEADER = "NEON_SERVER_IDENTITY_V1\n";
    constexpr const char* SIGNING_DOMAIN = "neon-server-heartbeat-v2\n";

    enum class ECreateResult
    {
        Created,
        AlreadyExists,
        Failed,
    };

    std::string Base64UrlEncode(const void* data, std::size_t size)
    {
        std::string            result;
        CryptoPP::StringSource source(static_cast<const CryptoPP::byte*>(data), size, true,
                                      new CryptoPP::Base64URLEncoder(new CryptoPP::StringSink(result), false));
        return result;
    }

    bool Base64UrlDecode(const std::string& encoded, std::string& decoded)
    {
        try
        {
            decoded.clear();
            CryptoPP::StringSource source(encoded, true, new CryptoPP::Base64URLDecoder(new CryptoPP::StringSink(decoded)));
            return Base64UrlEncode(decoded.data(), decoded.size()) == encoded;
        }
        catch (const CryptoPP::Exception&)
        {
            decoded.clear();
            return false;
        }
    }

    ECreateResult SaveNewIdentityFile(const SString& path, const std::string& serialized, const std::string& suffix)
    {
        const SString temporaryPath = path + ".new." + suffix;
#ifdef WIN32
        HANDLE file = CreateFileA(temporaryPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return ECreateResult::Failed;

        DWORD      written = 0;
        const bool saved = serialized.size() <= MAXDWORD && WriteFile(file, serialized.data(), static_cast<DWORD>(serialized.size()), &written, nullptr) &&
                           written == serialized.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!saved)
        {
            DeleteFileA(temporaryPath);
            return ECreateResult::Failed;
        }

        if (MoveFileExA(temporaryPath, path, MOVEFILE_WRITE_THROUGH))
            return ECreateResult::Created;
        const DWORD moveError = GetLastError();
        DeleteFileA(temporaryPath);
        return moveError == ERROR_FILE_EXISTS || moveError == ERROR_ALREADY_EXISTS ? ECreateResult::AlreadyExists : ECreateResult::Failed;
#else
        const int file = open(temporaryPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR);
        if (file < 0)
            return ECreateResult::Failed;

        std::size_t offset = 0;
        while (offset < serialized.size())
        {
            const ssize_t written = write(file, serialized.data() + offset, serialized.size() - offset);
            if (written <= 0)
                break;
            offset += static_cast<std::size_t>(written);
        }
        const bool saved = offset == serialized.size() && fsync(file) == 0;
        close(file);
        if (!saved)
        {
            unlink(temporaryPath.c_str());
            return ECreateResult::Failed;
        }

        const bool published = link(temporaryPath.c_str(), path.c_str()) == 0;
        const int  publishError = errno;
        unlink(temporaryPath.c_str());
        if (published)
            return ECreateResult::Created;
        return publishError == EEXIST ? ECreateResult::AlreadyExists : ECreateResult::Failed;
#endif
    }
}

bool CNeonServerIdentity::Initialize(const SString& path, std::string& error)
{
    m_path = path;
    if (FileExists(path))
        return Load(path, error);
    return Create(path, error);
}

bool CNeonServerIdentity::Load(const SString& path, std::string& error)
{
    SString contents;
    if (!FileLoad(std::nothrow, path, contents, 256))
    {
        error = "could not read the existing Neon server identity file";
        return false;
    }

    const std::string serialized(contents.data(), contents.size());
    const std::string header(KEY_FILE_HEADER);
    if (serialized.rfind(header, 0) != 0)
    {
        error = "the Neon server identity file has an unknown format";
        return false;
    }

    std::string encodedSeed = serialized.substr(header.size());
    while (!encodedSeed.empty() && (encodedSeed.back() == '\n' || encodedSeed.back() == '\r'))
        encodedSeed.pop_back();

    std::string seed;
    if (!Base64UrlDecode(encodedSeed, seed) || seed.size() != CryptoPP::ed25519Signer::SECRET_KEYLENGTH)
    {
        error = "the Neon server identity file contains an invalid private key";
        return false;
    }
    return InitializeFromSeed(seed, error);
}

bool CNeonServerIdentity::Create(const SString& path, std::string& error)
{
    try
    {
        CryptoPP::AutoSeededRandomPool random;
        std::string                    seed(CryptoPP::ed25519Signer::SECRET_KEYLENGTH, '\0');
        random.GenerateBlock(reinterpret_cast<CryptoPP::byte*>(seed.data()), seed.size());

        const std::string   serialized = std::string(KEY_FILE_HEADER) + Base64UrlEncode(seed.data(), seed.size()) + "\n";
        const std::string   suffix = Base64UrlEncode(seed.data(), 9);
        const ECreateResult createResult = SaveNewIdentityFile(path, serialized, suffix);
        if (createResult == ECreateResult::AlreadyExists)
            return Load(path, error);
        if (createResult != ECreateResult::Created)
        {
            error = "could not create the persistent Neon server identity file";
            return false;
        }
        if (!InitializeFromSeed(seed, error))
            return false;
        CLogger::LogPrintf("[Neon Identity] Created persistent server identity %s; keep neon-identity.keys in private backups\n", m_serverId.c_str());
        return true;
    }
    catch (const CryptoPP::Exception& exception)
    {
        error = SString("could not generate the Neon server identity: %s", exception.what());
        return false;
    }
}

bool CNeonServerIdentity::InitializeFromSeed(const std::string& seed, std::string& error)
{
    try
    {
        const CryptoPP::ed25519Signer   signer(reinterpret_cast<const CryptoPP::byte*>(seed.data()));
        const CryptoPP::ed25519Verifier verifier(signer);
        const auto&                     publicKeyObject = dynamic_cast<const CryptoPP::ed25519PublicKey&>(verifier.GetPublicKey());
        const CryptoPP::byte*           publicKey = publicKeyObject.GetPublicKeyBytePtr();
        uchar                           digest[32];
        SharedUtil::GenerateSha256(publicKey, CryptoPP::ed25519Signer::PUBLIC_KEYLENGTH, digest);

        m_seed = seed;
        m_publicKey = Base64UrlEncode(publicKey, CryptoPP::ed25519Signer::PUBLIC_KEYLENGTH);
        m_serverId = "nsrv_" + Base64UrlEncode(digest, sizeof(digest));
        error.clear();
        return true;
    }
    catch (const CryptoPP::Exception& exception)
    {
        error = SString("could not initialize the Neon server identity: %s", exception.what());
        return false;
    }
}

bool CNeonServerIdentity::SignHeartbeat(const std::string& body, std::string& timestamp, std::string& nonce, std::string& signature, std::string& error) const
{
    if (m_seed.empty())
    {
        error = "the Neon server identity is not initialized";
        return false;
    }

    try
    {
        CryptoPP::AutoSeededRandomPool random;
        CryptoPP::byte                 nonceBytes[16];
        random.GenerateBlock(nonceBytes, sizeof(nonceBytes));
        nonce = Base64UrlEncode(nonceBytes, sizeof(nonceBytes));
        timestamp = std::to_string(static_cast<long long>(std::time(nullptr)));
        const std::string message = std::string(SIGNING_DOMAIN) + timestamp + "\n" + nonce + "\n" +
                                    SharedUtil::GenerateSha256HexString(body.data(), static_cast<uint>(body.size())).ToLower();

        const CryptoPP::ed25519Signer signer(reinterpret_cast<const CryptoPP::byte*>(m_seed.data()));
        CryptoPP::byte                signatureBytes[CryptoPP::ed25519Signer::SIGNATURE_LENGTH];
        signer.SignMessage(random, reinterpret_cast<const CryptoPP::byte*>(message.data()), message.size(), signatureBytes);
        signature = Base64UrlEncode(signatureBytes, sizeof(signatureBytes));
        error.clear();
        return true;
    }
    catch (const CryptoPP::Exception& exception)
    {
        error = SString("could not sign the Neon registry heartbeat: %s", exception.what());
        return false;
    }
}
