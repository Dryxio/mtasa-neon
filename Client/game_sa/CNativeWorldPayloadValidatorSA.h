/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeWorldPayloadValidatorSA.h
 *  PURPOSE:     Closed payload validator for trusted native world-pack policy
 *
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>

struct SNativeWorldPayloadBudgetSA
{
    uint32_t renderWareLibraryId;
    uint32_t maximumRenderWareDepth;
    uint32_t maximumRenderWareChunks;
    uint64_t maximumRenderWareBytes;

    uint32_t maximumFramesPerClump;
    uint32_t maximumGeometriesPerClump;
    uint32_t maximumAtomicsPerClump;
    uint32_t maximumLightsPerClump;
    uint32_t maximumGeometryVertices;
    uint32_t maximumGeometryTriangles;
    uint32_t maximumGeometryMaterials;
    uint64_t maximumTotalGeometryVertices;
    uint64_t maximumTotalGeometryTriangles;
    uint64_t maximumTotalGeometryMaterials;
    uint32_t maximumBinMeshes;
    uint32_t maximumBinMeshIndices;
    uint64_t maximumTotalBinMeshIndices;
    uint32_t maximum2dEffects;
    uint64_t maximumTotal2dEffects;
    uint32_t maximumBreakableVertices;
    uint32_t maximumBreakableTriangles;
    uint32_t maximumBreakableMaterials;
    uint64_t maximumTotalBreakableVertices;
    uint64_t maximumTotalBreakableTriangles;
    uint64_t maximumTotalBreakableMaterials;
    uint32_t maximumNativeTextures;
    uint32_t maximumNativeTextureWidth;
    uint32_t maximumNativeTextureHeight;
    uint32_t maximumNativeTextureLevels;
    uint64_t maximumNativeTextureGpuBytes;
    uint64_t maximumTotalNativeTextureGpuBytes;
    uint64_t maximumNativeTextureDecodedBytes;
    uint64_t maximumTotalNativeTextureDecodedBytes;

    uint32_t maximumColRecords;
    uint32_t maximumColRecordBytes;
    uint32_t maximumColSpheres;
    uint32_t maximumColBoxes;
    uint32_t maximumColLines;
    uint32_t maximumColVertices;
    uint32_t maximumColFaces;
    uint32_t maximumColFaceGroups;
    uint32_t maximumColShadowFaces;
    uint64_t maximumTotalColBytes;
    uint64_t maximumTotalColSpheres;
    uint64_t maximumTotalColBoxes;
    uint64_t maximumTotalColLines;
    uint64_t maximumTotalColVertices;
    uint64_t maximumTotalColFaces;
    uint64_t maximumTotalColFaceGroups;
};

struct SNativeWorldPayloadImgEntrySA
{
    uint32_t offsetBlocks;
    uint32_t sizeBlocks;
};

struct SNativeWorldPayloadPlanSA
{
    std::map<std::string, SNativeWorldPayloadImgEntrySA> imgEntries;
    std::map<uint32_t, std::string>                      modelNames;
    std::set<std::string>                                txdNames;
    std::string                                          colFileName;
};

struct SNativeWorldPayloadSummarySA
{
    uint32_t dffCount{};
    uint32_t txdCount{};
    uint32_t renderWareChunkCount{};
    uint32_t maximumRenderWareDepth{};
    uint64_t renderWareBytes{};
    uint64_t geometryVertices{};
    uint64_t geometryTriangles{};
    uint64_t geometryMaterials{};
    uint64_t binMeshIndices{};
    uint64_t effects2d{};
    uint64_t breakableVertices{};
    uint64_t breakableTriangles{};
    uint64_t breakableMaterials{};
    uint32_t nativeTextureCount{};
    uint64_t nativeTextureGpuBytes{};
    uint64_t nativeTextureDecodedBytes{};

    uint32_t colRecordCount{};
    uint32_t collRecordCount{};
    uint32_t col3RecordCount{};
    uint32_t maximumColRecordBytes{};
    uint32_t maximumColVertices{};
    uint32_t maximumColFaces{};
    uint32_t maximumColFaceGroups{};
    uint64_t colBytes{};
    uint64_t colSpheres{};
    uint64_t colBoxes{};
    uint64_t colLines{};
    uint64_t colVertices{};
    uint64_t colFaces{};
    uint64_t colFaceGroups{};
};

class CNativeWorldPayloadValidatorSA
{
public:
    // Callers must first validate the IMG directory and its compiled per-entry
    // block cap. This parser rechecks byte arithmetic and semantic budgets, but
    // deliberately relies on that upstream cap before allocating a member.
    static bool Validate(const wchar_t* imgPath, const SNativeWorldPayloadPlanSA& plan, const SNativeWorldPayloadBudgetSA& budget,
                         SNativeWorldPayloadSummarySA& summary, std::string& error);
};
