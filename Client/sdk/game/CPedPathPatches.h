#pragma once

#include <cstddef>
#include <string>

// Patches are client-global GTA state. The resource identity owns the lease;
// a revision is a content digest, never a caller-provided compatibility claim.
class CPedPathPatches
{
public:
    virtual ~CPedPathPatches() = default;
    virtual bool Load(void* owner, const void* data, std::size_t size, std::string& revision, std::string& error) = 0;
    virtual bool Unload(void* owner) = 0;
    virtual bool GetGraphRevision(std::string& revision, std::string& error) const = 0;
    virtual bool GetRevision(void* owner, std::string& revision, std::string& error) const = 0;
};
