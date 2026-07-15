/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeBullworthPackSA.cpp
 *  PURPOSE:     Trusted Bullworth native world-pack policy
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeBullworthPackSA.h"
#include "CNativeWorldPackSA.h"

namespace
{
    constexpr SNativeTxdPoolProfileSA BULLWORTH_TXD_POOL_PROFILES[] = {
        {"hoodlum-raw", "standalone-3607", 3607, 3606, -1, {true}},
        {"mta-programdata",
         "mta-runtime-3608",
         3608,
         3607,
         3607,
         {true, 0x01, 0x00000000, 0, 0xFFFF, 0xEA5A8E45, 0xFFFF, 0xFFFF, 0xFFFF, 0x00, 4, 13153, 5, 0}},
    };

    // These are closed safety budgets, not format compatibility promises.
    // They cover the audited Bullworth maxima while bounding parser work and
    // every allocation/count GTA will later consume from the native archive.
    constexpr SNativeWorldPayloadBudgetSA MakeBullworthPayloadBudget()
    {
        SNativeWorldPayloadBudgetSA budget{};
        budget.renderWareLibraryId = 0x1803FFFF;
        budget.maximumRenderWareDepth = 16;
        budget.maximumRenderWareChunks = 100000;
        budget.maximumRenderWareBytes = 268435456;
        budget.maximumFramesPerClump = 16;
        budget.maximumGeometriesPerClump = 4;
        budget.maximumAtomicsPerClump = 16;
        budget.maximumLightsPerClump = 16;
        budget.maximumGeometryVertices = 32768;
        budget.maximumGeometryTriangles = 32768;
        budget.maximumGeometryMaterials = 64;
        budget.maximumTotalGeometryVertices = 1100000;
        budget.maximumTotalGeometryTriangles = 800000;
        budget.maximumTotalGeometryMaterials = 6000;
        budget.maximumBinMeshes = 64;
        budget.maximumBinMeshIndices = 65536;
        budget.maximumTotalBinMeshIndices = 2100000;
        budget.maximum2dEffects = 64;
        budget.maximumTotal2dEffects = 64;
        budget.maximumBreakableVertices = 256;
        budget.maximumBreakableTriangles = 512;
        budget.maximumBreakableMaterials = 16;
        budget.maximumTotalBreakableVertices = 2048;
        budget.maximumTotalBreakableTriangles = 2048;
        budget.maximumTotalBreakableMaterials = 64;
        budget.maximumNativeTextures = 5000;
        budget.maximumNativeTextureWidth = 1024;
        budget.maximumNativeTextureHeight = 1024;
        budget.maximumNativeTextureLevels = 12;
        budget.maximumNativeTextureGpuBytes = 1048576;
        budget.maximumTotalNativeTextureGpuBytes = 134217728;
        budget.maximumNativeTextureDecodedBytes = 2097152;
        budget.maximumTotalNativeTextureDecodedBytes = 1073741824;
        budget.maximumColRecords = 2000;
        budget.maximumColRecordBytes = 327680;
        budget.maximumColSpheres = 8;
        budget.maximumColBoxes = 16;
        budget.maximumColLines = 0;
        budget.maximumColVertices = 32768;
        budget.maximumColFaces = 32768;
        budget.maximumColFaceGroups = 1024;
        budget.maximumColShadowFaces = 0;
        budget.maximumTotalColBytes = 9437184;
        budget.maximumTotalColSpheres = 32;
        budget.maximumTotalColBoxes = 128;
        budget.maximumTotalColLines = 0;
        budget.maximumTotalColVertices = 550000;
        budget.maximumTotalColFaces = 600000;
        budget.maximumTotalColFaceGroups = 20000;
        return budget;
    }

    constexpr SNativeWorldPayloadBudgetSA BULLWORTH_PAYLOAD_BUDGET = MakeBullworthPayloadBudget();

    constexpr SNativeWorldPackPolicySA BULLWORTH_POLICY = {
        "bullworth",
        "Bullworth",
        "[NativeBW]",
        "MTA_NATIVE_BW_MODEL_STORES",
        "MTA\\data\\extended-world\\bullworth",
        "native-world.json",
        4096,
        1048576,
        131072,
        4096,
        8192,
        19999,
        2000,
        1000,
        10000,
        5000,
        252,
        255,
        191,
        256,
        6,
        {13984, 69, 160},
        {15000, 160, 200},
        BULLWORTH_PAYLOAD_BUDGET,
        BULLWORTH_TXD_POOL_PROFILES,
        static_cast<unsigned int>(std::size(BULLWORTH_TXD_POOL_PROFILES)),
    };
}  // namespace

const SNativeWorldPackPolicySA& GetNativeBullworthPackPolicy()
{
    return BULLWORTH_POLICY;
}
