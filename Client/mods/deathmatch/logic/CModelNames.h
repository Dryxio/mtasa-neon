/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *               (Shared logic for modifications)
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/CModelNames.h
 *  PURPOSE:     Model names class header
 *
 *****************************************************************************/

#include <cstdint>

#define CLOTHES_MODEL_ID_FIRST 30000
#define CLOTHES_MODEL_ID_LAST  30151
#define CLOTHES_TEX_ID_FIRST   30152
#define CLOTHES_TEX_ID_LAST    30541

// FileIDs and server model identities together occupy every uint16 value below
// 65535. Keep the shared name resolver sentinel at the sole unallocatable value.
constexpr std::uint32_t INVALID_MODEL_ID = 0xFFFFU;

class CModelNames
{
public:
    static uint32      GetModelID(const SString& strName);
    static uint32      GetClothesTexID(const SString& strName);
    static const char* GetModelName(uint32 usModelID);
    static uint32      ResolveModelID(const SString& strModelNameOrNumber);
    static uint32      ResolveClothesTexID(const SString& strTexNameOrNumber);

protected:
    static void InitializeMaps();

    static std::map<uint32, const char*> ms_ModelIDNameMap;
    static std::map<SString, uint32>     ms_NameModelIDMap;
    static std::map<uint32, const char*> ms_ClothesModelIDNameMap;
    static std::map<SString, uint32>     ms_NameClothesModelIDMap;
    static std::map<uint32, const char*> ms_ClothesTexIDNameMap;
    static std::map<SString, uint32>     ms_NameClothesTexIDMap;
};
