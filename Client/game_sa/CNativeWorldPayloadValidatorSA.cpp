/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CNativeWorldPayloadValidatorSA.cpp
 *  PURPOSE:     Closed payload validator for trusted native world-pack policy
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CNativeWorldPayloadValidatorSA.h"

#include <cmath>
#include <limits>
#include <sstream>
#include <vector>

namespace
{
    constexpr uint64_t IMG_BLOCK_BYTES = 2048;
    constexpr float    MAX_ABSOLUTE_PAYLOAD_FLOAT = 1000000.0f;

    constexpr uint32_t RW_STRUCT = 0x01;
    constexpr uint32_t RW_STRING = 0x02;
    constexpr uint32_t RW_EXTENSION = 0x03;
    constexpr uint32_t RW_TEXTURE = 0x06;
    constexpr uint32_t RW_MATERIAL = 0x07;
    constexpr uint32_t RW_MATERIAL_LIST = 0x08;
    constexpr uint32_t RW_FRAME_LIST = 0x0E;
    constexpr uint32_t RW_GEOMETRY = 0x0F;
    constexpr uint32_t RW_CLUMP = 0x10;
    constexpr uint32_t RW_LIGHT = 0x12;
    constexpr uint32_t RW_ATOMIC = 0x14;
    constexpr uint32_t RW_TEXTURE_NATIVE = 0x15;
    constexpr uint32_t RW_TEXTURE_DICTIONARY = 0x16;
    constexpr uint32_t RW_GEOMETRY_LIST = 0x1A;

    struct SByteReader
    {
        const std::vector<uint8_t>& bytes;

        bool Contains(uint64_t offset, uint64_t length) const { return offset <= bytes.size() && length <= bytes.size() - offset; }

        bool U8(uint64_t offset, uint8_t& value) const
        {
            if (!Contains(offset, sizeof(value)))
                return false;
            value = bytes[static_cast<size_t>(offset)];
            return true;
        }

        bool U16(uint64_t offset, uint16_t& value) const
        {
            if (!Contains(offset, sizeof(value)))
                return false;
            memcpy(&value, bytes.data() + offset, sizeof(value));
            return true;
        }

        bool U32(uint64_t offset, uint32_t& value) const
        {
            if (!Contains(offset, sizeof(value)))
                return false;
            memcpy(&value, bytes.data() + offset, sizeof(value));
            return true;
        }

        bool Float(uint64_t offset, float& value) const
        {
            if (!Contains(offset, sizeof(value)))
                return false;
            memcpy(&value, bytes.data() + offset, sizeof(value));
            return true;
        }

        bool Zero(uint64_t offset, uint64_t length) const
        {
            return Contains(offset, length) && std::all_of(bytes.begin() + static_cast<size_t>(offset), bytes.begin() + static_cast<size_t>(offset + length),
                                                           [](uint8_t value) { return value == 0; });
        }
    };

    struct SRwChunk
    {
        uint32_t type{};
        uint32_t payloadBytes{};
        uint32_t libraryId{};
        uint64_t begin{};
        uint64_t payloadBegin{};
        uint64_t end{};
    };

    bool Fail(std::string& error, const std::string& member, const char* reason)
    {
        error = member + ": " + reason;
        return false;
    }

    bool CheckedAdd(uint64_t left, uint64_t right, uint64_t& result)
    {
        if (right > std::numeric_limits<uint64_t>::max() - left)
            return false;
        result = left + right;
        return true;
    }

    bool CheckedMul(uint64_t left, uint64_t right, uint64_t& result)
    {
        if (left && right > std::numeric_limits<uint64_t>::max() / left)
            return false;
        result = left * right;
        return true;
    }

    bool ReadRwChunk(const SByteReader& reader, uint64_t begin, uint64_t parentEnd, SRwChunk& chunk)
    {
        chunk.begin = begin;
        chunk.payloadBegin = begin + 12;
        if (!reader.Contains(begin, 12) || !reader.U32(begin, chunk.type) || !reader.U32(begin + 4, chunk.payloadBytes) ||
            !reader.U32(begin + 8, chunk.libraryId) || !CheckedAdd(chunk.payloadBegin, chunk.payloadBytes, chunk.end))
            return false;
        return chunk.end <= parentEnd;
    }

    bool IsRwContainer(uint32_t type)
    {
        switch (type)
        {
            case RW_EXTENSION:
            case RW_TEXTURE:
            case RW_MATERIAL:
            case RW_MATERIAL_LIST:
            case RW_FRAME_LIST:
            case RW_GEOMETRY:
            case RW_CLUMP:
            case RW_LIGHT:
            case RW_ATOMIC:
            case RW_TEXTURE_NATIVE:
            case RW_TEXTURE_DICTIONARY:
            case RW_GEOMETRY_LIST:
                return true;
            default:
                return false;
        }
    }

    bool IsAuditedRwPlugin(uint32_t type)
    {
        switch (type)
        {
            case 0x0000050E:
            case 0x0253F2F8:
            case 0x0253F2F9:
            case 0x0253F2FC:
            case 0x0253F2FD:
            case 0x0253F2FE:
                return true;
            default:
                return false;
        }
    }

    bool DirectChildren(const SByteReader& reader, const SRwChunk& parent, uint32_t expectedLibraryId, std::vector<SRwChunk>& children)
    {
        uint64_t offset = parent.payloadBegin;
        while (offset < parent.end)
        {
            SRwChunk child;
            if (!ReadRwChunk(reader, offset, parent.end, child) || child.libraryId != expectedLibraryId)
                return false;
            children.push_back(child);
            offset = child.end;
        }
        return offset == parent.end;
    }

    bool ValidateRwTree(const SByteReader& reader, const SRwChunk& root, const std::string& member, const SNativeWorldPayloadBudgetSA& budget,
                        SNativeWorldPayloadSummarySA& summary, std::string& error)
    {
        struct SFrame
        {
            uint64_t begin;
            uint64_t end;
            uint32_t parentType;
            uint32_t depth;
        };
        std::vector<SFrame> stack{{root.payloadBegin, root.end, root.type, 1}};
        ++summary.renderWareChunkCount;
        while (!stack.empty())
        {
            const SFrame frame = stack.back();
            stack.pop_back();
            if (frame.depth > budget.maximumRenderWareDepth)
                return Fail(error, member, "RenderWare nesting exceeds the compiled budget");
            summary.maximumRenderWareDepth = std::max(summary.maximumRenderWareDepth, frame.depth);

            uint64_t offset = frame.begin;
            while (offset < frame.end)
            {
                SRwChunk child;
                if (!ReadRwChunk(reader, offset, frame.end, child) || child.libraryId != budget.renderWareLibraryId)
                    return Fail(error, member, "RenderWare child header, library ID, or boundary is invalid");
                if (++summary.renderWareChunkCount > budget.maximumRenderWareChunks)
                    return Fail(error, member, "RenderWare chunk count exceeds the compiled budget");

                if (IsRwContainer(child.type))
                    stack.push_back({child.payloadBegin, child.end, child.type, frame.depth + 1});
                else if (child.type == RW_STRUCT || child.type == RW_STRING)
                {
                    // These core chunks are binary leaves. Their layouts are
                    // checked below where GTA uses their counts and indices.
                }
                else if (frame.parentType != RW_EXTENSION || !IsAuditedRwPlugin(child.type))
                    return Fail(error, member, "RenderWare chunk type is outside the audited Bullworth profile");
                offset = child.end;
            }
            if (offset != frame.end)
                return Fail(error, member, "RenderWare container is not consumed exactly");
        }
        return true;
    }

    bool CheckedBudgetAdd(uint64_t& value, uint64_t amount, uint64_t maximum)
    {
        uint64_t next = 0;
        if (!CheckedAdd(value, amount, next) || next > maximum)
            return false;
        value = next;
        return true;
    }

    bool FiniteFloats(const SByteReader& reader, uint64_t begin, uint64_t count)
    {
        uint64_t bytes = 0;
        if (!CheckedMul(count, sizeof(float), bytes) || !reader.Contains(begin, bytes))
            return false;
        for (uint64_t index = 0; index < count; ++index)
        {
            float value = 0.0f;
            if (!reader.Float(begin + index * sizeof(float), value) || !std::isfinite(value) || std::abs(value) > MAX_ABSOLUTE_PAYLOAD_FLOAT)
                return false;
        }
        return true;
    }

    bool PaddedAscii(const SByteReader& reader, uint64_t begin, uint64_t bytes, bool allowEmpty = false)
    {
        if (!bytes || !reader.Contains(begin, bytes))
            return false;
        uint64_t terminator = 0;
        while (terminator < bytes && reader.bytes[static_cast<size_t>(begin + terminator)] != 0)
        {
            const uint8_t value = reader.bytes[static_cast<size_t>(begin + terminator++)];
            if (value < 0x20 || value > 0x7E)
                return false;
        }
        return (allowEmpty || terminator) && terminator < bytes && reader.Zero(begin + terminator, bytes - terminator);
    }

    bool PaddedAsciiName(const SByteReader& reader, uint64_t begin, uint64_t bytes, std::string& name)
    {
        if (!PaddedAscii(reader, begin, bytes))
            return false;
        name.clear();
        for (uint64_t index = 0; index < bytes; ++index)
        {
            uint8_t value = reader.bytes[static_cast<size_t>(begin + index)];
            if (value == 0)
                return true;
            if (value >= 'A' && value <= 'Z')
                value += 'a' - 'A';
            name.push_back(static_cast<char>(value));
        }
        return false;
    }

    bool RawAscii(const SByteReader& reader, const SRwChunk& chunk, uint32_t maximumBytes)
    {
        if (!chunk.payloadBytes || chunk.payloadBytes > maximumBytes || !reader.Contains(chunk.payloadBegin, chunk.payloadBytes))
            return false;
        for (uint32_t index = 0; index < chunk.payloadBytes; ++index)
        {
            const uint8_t value = reader.bytes[static_cast<size_t>(chunk.payloadBegin + index)];
            if (value < 0x20 || value > 0x7E)
                return false;
        }
        return true;
    }

    bool TerminatedAscii(const SByteReader& reader, uint64_t begin, uint64_t bytes)
    {
        if (!bytes || !reader.Contains(begin, bytes))
            return false;
        for (uint64_t index = 0; index < bytes; ++index)
        {
            const uint8_t value = reader.bytes[static_cast<size_t>(begin + index)];
            if (value == 0)
                return index != 0;
            if (value < 0x20 || value > 0x7E)
                return false;
        }
        return false;
    }

    bool EmptyExtension(const SByteReader& reader, const SRwChunk& extension, uint32_t libraryId)
    {
        std::vector<SRwChunk> plugins;
        return extension.type == RW_EXTENSION && DirectChildren(reader, extension, libraryId, plugins) && plugins.empty();
    }

    bool ValidateFrameList(const SByteReader& reader, const SRwChunk& frameList, uint32_t libraryId, const SNativeWorldPayloadBudgetSA& budget,
                           uint32_t& frameCount)
    {
        std::vector<SRwChunk> children;
        if (!DirectChildren(reader, frameList, libraryId, children) || children.empty() || children[0].type != RW_STRUCT ||
            !reader.U32(children[0].payloadBegin, frameCount) || !frameCount || frameCount > budget.maximumFramesPerClump)
            return false;
        uint64_t frameBytes = 0;
        if (!CheckedMul(frameCount, 56, frameBytes) || children[0].payloadBytes != 4 + frameBytes || children.size() != frameCount + 1)
            return false;

        uint32_t roots = 0;
        for (uint32_t index = 0; index < frameCount; ++index)
        {
            const uint64_t frameBegin = children[0].payloadBegin + 4 + index * 56;
            uint32_t       parent = 0, flags = 0;
            float          matrix[12]{};
            if (!FiniteFloats(reader, frameBegin, 12) || !reader.U32(frameBegin + 48, parent) || !reader.U32(frameBegin + 52, flags) ||
                (flags != 0 && flags != 3 && flags != 0x00020003))
                return false;
            for (uint32_t component = 0; component < std::size(matrix); ++component)
                if (!reader.Float(frameBegin + component * sizeof(float), matrix[component]) ||
                    std::abs(matrix[component]) > (component < 9 ? 10000.0f : 1000000.0f))
                    return false;
            const float determinant = matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
                                      matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) + matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
            if (std::abs(determinant) < 0.000001f || std::abs(determinant) > 1000000.0f)
                return false;
            if (parent == UINT32_MAX)
                ++roots;
            else if (parent >= index)
                return false;

            std::vector<SRwChunk> plugins;
            if (children[index + 1].type != RW_EXTENSION || !DirectChildren(reader, children[index + 1], libraryId, plugins) || plugins.size() != 1 ||
                plugins[0].type != 0x0253F2FE || !RawAscii(reader, plugins[0], 23))
                return false;
        }
        return roots == 1;
    }

    bool ValidateTexture(const SByteReader& reader, const SRwChunk& texture, uint32_t libraryId)
    {
        std::vector<SRwChunk> children;
        if (!DirectChildren(reader, texture, libraryId, children) || children.size() != 4 || children[0].type != RW_STRUCT || children[0].payloadBytes != 4 ||
            children[1].type != RW_STRING || children[2].type != RW_STRING || children[3].type != RW_EXTENSION ||
            !PaddedAscii(reader, children[1].payloadBegin, children[1].payloadBytes) ||
            !PaddedAscii(reader, children[2].payloadBegin, children[2].payloadBytes, true) || !EmptyExtension(reader, children[3], libraryId))
            return false;
        uint32_t flags = 0;
        return reader.U32(children[0].payloadBegin, flags) && (flags == 0x00010106 || flags == 0x00011106);
    }

    bool ValidateMaterial(const SByteReader& reader, const SRwChunk& material, uint32_t libraryId)
    {
        std::vector<SRwChunk> children;
        if (!DirectChildren(reader, material, libraryId, children) || (children.size() != 2 && children.size() != 3) || children[0].type != RW_STRUCT ||
            children[0].payloadBytes != 28 || children.back().type != RW_EXTENSION)
            return false;

        uint32_t flags = 0, unused = 0, textured = 0;
        if (!reader.U32(children[0].payloadBegin, flags) || !reader.U32(children[0].payloadBegin + 8, unused) ||
            !reader.U32(children[0].payloadBegin + 12, textured) || flags != 0 || (unused != 0 && unused != 16688092 && unused != 406430684) || textured > 1 ||
            !FiniteFloats(reader, children[0].payloadBegin + 16, 3) || children.size() != static_cast<size_t>(textured) + 2)
            return false;
        if (textured && (children[1].type != RW_TEXTURE || !ValidateTexture(reader, children[1], libraryId)))
            return false;

        std::vector<SRwChunk> plugins;
        if (!DirectChildren(reader, children.back(), libraryId, plugins) || plugins.size() > 1)
            return false;
        if (plugins.empty())
            return true;
        if (plugins[0].type != 0x0253F2FC || plugins[0].payloadBytes != 24 || !FiniteFloats(reader, plugins[0].payloadBegin, 5))
            return false;
        uint32_t serializedPointer = 1;
        return reader.U32(plugins[0].payloadBegin + 20, serializedPointer) && serializedPointer == 0;
    }

    bool ValidateMaterialList(const SByteReader& reader, const SRwChunk& materialList, uint32_t libraryId, const SNativeWorldPayloadBudgetSA& budget,
                              uint32_t& materialCount)
    {
        std::vector<SRwChunk> children;
        if (!DirectChildren(reader, materialList, libraryId, children) || children.empty() || children[0].type != RW_STRUCT ||
            !reader.U32(children[0].payloadBegin, materialCount) || !materialCount || materialCount > budget.maximumGeometryMaterials)
            return false;
        uint64_t referenceBytes = 0;
        if (!CheckedMul(materialCount, 4, referenceBytes) || children[0].payloadBytes != 4 + referenceBytes || children.size() != materialCount + 1)
            return false;
        for (uint32_t index = 0; index < materialCount; ++index)
        {
            uint32_t reference = 0;
            if (!reader.U32(children[0].payloadBegin + 4 + index * 4, reference) || reference != UINT32_MAX || children[index + 1].type != RW_MATERIAL ||
                !ValidateMaterial(reader, children[index + 1], libraryId))
                return false;
        }
        return true;
    }

    bool ValidateBinMesh(const SByteReader& reader, const SRwChunk& plugin, uint32_t vertices, uint32_t triangles, uint32_t materials,
                         const SNativeWorldPayloadBudgetSA& budget, SNativeWorldPayloadSummarySA& summary)
    {
        uint32_t flags = 0, meshes = 0, indices = 0;
        if (plugin.payloadBytes < 12 || !reader.U32(plugin.payloadBegin, flags) || !reader.U32(plugin.payloadBegin + 4, meshes) ||
            !reader.U32(plugin.payloadBegin + 8, indices) || flags != 0 || meshes != materials || meshes > budget.maximumBinMeshes ||
            indices > budget.maximumBinMeshIndices || indices != triangles * 3 ||
            !CheckedBudgetAdd(summary.binMeshIndices, indices, budget.maximumTotalBinMeshIndices))
            return false;
        uint64_t           cursor = plugin.payloadBegin + 12;
        std::set<uint32_t> seenMaterials;
        uint64_t           countedIndices = 0;
        for (uint32_t mesh = 0; mesh < meshes; ++mesh)
        {
            uint32_t meshIndices = 0, material = 0;
            if (!reader.U32(cursor, meshIndices) || !reader.U32(cursor + 4, material) || meshIndices > budget.maximumBinMeshIndices || meshIndices % 3 != 0 ||
                material >= materials || !seenMaterials.insert(material).second)
                return false;
            cursor += 8;
            uint64_t indexBytes = 0, next = 0;
            if (!CheckedMul(meshIndices, 4, indexBytes) || !CheckedAdd(cursor, indexBytes, next) || next > plugin.end ||
                !CheckedAdd(countedIndices, meshIndices, countedIndices))
                return false;
            for (uint32_t index = 0; index < meshIndices; ++index)
            {
                uint32_t vertex = 0;
                if (!reader.U32(cursor + index * 4, vertex) || vertex >= vertices)
                    return false;
            }
            cursor = next;
        }
        return cursor == plugin.end && countedIndices == indices;
    }

    bool ValidateBreakable(const SByteReader& reader, const SRwChunk& plugin, const SNativeWorldPayloadBudgetSA& budget, SNativeWorldPayloadSummarySA& summary)
    {
        uint32_t section = 0;
        if (plugin.payloadBytes < 4 || !reader.U32(plugin.payloadBegin, section))
            return false;
        if (section == 0)
            return plugin.payloadBytes == 4;
        if (section != 1 && section != 0x64646464)
            return false;

        uint32_t positionRule = 0;
        uint16_t vertices = 0, triangles = 0, materials = 0, padding = 1;
        if (plugin.payloadBytes < 56 || !reader.U32(plugin.payloadBegin + 4, positionRule) || positionRule > 1 ||
            !reader.U16(plugin.payloadBegin + 8, vertices) || !reader.U16(plugin.payloadBegin + 10, padding) || padding != 0 ||
            !reader.U16(plugin.payloadBegin + 24, triangles) || !reader.U16(plugin.payloadBegin + 26, padding) || padding != 0 ||
            !reader.U16(plugin.payloadBegin + 36, materials) || !reader.U16(plugin.payloadBegin + 38, padding) || padding != 0 || !vertices ||
            vertices > budget.maximumBreakableVertices || !triangles || triangles > budget.maximumBreakableTriangles || !materials ||
            materials > budget.maximumBreakableMaterials)
            return false;
        constexpr uint32_t POINTER_OFFSETS[] = {12, 16, 20, 28, 32, 40, 44, 48, 52};
        for (uint32_t offset : POINTER_OFFSETS)
        {
            uint32_t value = 0;
            if (!reader.U32(plugin.payloadBegin + offset, value) || value != 0)
                return false;
        }

        uint64_t vertexBytes = 0, triangleBytes = 0, materialBytes = 0, expected = 56;
        if (!CheckedMul(vertices, 24, vertexBytes) || !CheckedMul(triangles, 8, triangleBytes) || !CheckedMul(materials, 76, materialBytes) ||
            !CheckedAdd(expected, vertexBytes, expected) || !CheckedAdd(expected, triangleBytes, expected) || !CheckedAdd(expected, materialBytes, expected) ||
            expected != plugin.payloadBytes)
            return false;
        uint64_t cursor = plugin.payloadBegin + 56;
        if (!FiniteFloats(reader, cursor, static_cast<uint64_t>(vertices) * 3))
            return false;
        cursor += static_cast<uint64_t>(vertices) * 12;
        if (!FiniteFloats(reader, cursor, static_cast<uint64_t>(vertices) * 2))
            return false;
        cursor += static_cast<uint64_t>(vertices) * 8 + static_cast<uint64_t>(vertices) * 4;
        for (uint32_t triangle = 0; triangle < triangles; ++triangle)
        {
            for (uint32_t component = 0; component < 3; ++component)
            {
                uint16_t vertex = 0;
                if (!reader.U16(cursor + triangle * 6 + component * 2, vertex) || vertex >= vertices)
                    return false;
            }
        }
        cursor += static_cast<uint64_t>(triangles) * 6;
        for (uint32_t triangle = 0; triangle < triangles; ++triangle)
        {
            uint16_t material = 0;
            if (!reader.U16(cursor + triangle * 2, material) || material >= materials)
                return false;
        }
        cursor += static_cast<uint64_t>(triangles) * 2;
        for (uint32_t material = 0; material < materials; ++material)
            if (!PaddedAscii(reader, cursor + material * 32, 32))
                return false;
        cursor += static_cast<uint64_t>(materials) * 32;
        for (uint32_t material = 0; material < materials; ++material)
        {
            // An empty mask name is meaningful, but the fixed field must still
            // be zero-padded so GTA cannot read beyond it.
            if (!reader.Contains(cursor + material * 32, 32))
                return false;
            const uint8_t first = reader.bytes[static_cast<size_t>(cursor + material * 32)];
            if (first != 0 && !PaddedAscii(reader, cursor + material * 32, 32))
                return false;
            if (first == 0 && !reader.Zero(cursor + material * 32, 32))
                return false;
        }
        cursor += static_cast<uint64_t>(materials) * 32;
        if (!FiniteFloats(reader, cursor, static_cast<uint64_t>(materials) * 3))
            return false;
        return CheckedBudgetAdd(summary.breakableVertices, vertices, budget.maximumTotalBreakableVertices) &&
               CheckedBudgetAdd(summary.breakableTriangles, triangles, budget.maximumTotalBreakableTriangles) &&
               CheckedBudgetAdd(summary.breakableMaterials, materials, budget.maximumTotalBreakableMaterials);
    }

    bool Validate2dEffects(const SByteReader& reader, const SRwChunk& plugin, const SNativeWorldPayloadBudgetSA& budget, SNativeWorldPayloadSummarySA& summary)
    {
        uint32_t effects = 0;
        if (plugin.payloadBytes < 4 || !reader.U32(plugin.payloadBegin, effects) || !effects || effects > budget.maximum2dEffects)
            return false;
        uint64_t expected = 4, effectBytes = 0;
        if (!CheckedMul(effects, 100, effectBytes) || !CheckedAdd(expected, effectBytes, expected) || expected != plugin.payloadBytes)
            return false;
        uint64_t cursor = plugin.payloadBegin + 4;
        for (uint32_t effect = 0; effect < effects; ++effect, cursor += 100)
        {
            uint32_t type = 0, bytes = 0;
            float    x = 0.0f, y = 0.0f, z = 0.0f, farClip = 0.0f, pointRange = 0.0f, coronaSize = 0.0f, shadowSize = 0.0f;
            if (!FiniteFloats(reader, cursor, 3) || !reader.Float(cursor, x) || !reader.Float(cursor + 4, y) || !reader.Float(cursor + 8, z) ||
                std::abs(x) > 100000.0f || std::abs(y) > 100000.0f || std::abs(z) > 100000.0f || !reader.U32(cursor + 12, type) ||
                !reader.U32(cursor + 16, bytes) || type != 0 || bytes != 80 || !FiniteFloats(reader, cursor + 24, 4) || !reader.Float(cursor + 24, farClip) ||
                !reader.Float(cursor + 28, pointRange) || !reader.Float(cursor + 32, coronaSize) || !reader.Float(cursor + 36, shadowSize) || farClip < 0.0f ||
                farClip > 100000.0f || pointRange < 0.0f || pointRange > 100000.0f || coronaSize < 0.0f || coronaSize > 10000.0f || shadowSize < 0.0f ||
                shadowSize > 10000.0f)
                return false;
            const uint64_t light = cursor + 20;
            uint8_t        flash = 0, reflection = 0, flare = 0, shadow = 0, flagsLow = 0, flagsHigh = 0;
            if (!reader.U8(light + 20, flash) || !reader.U8(light + 21, reflection) || !reader.U8(light + 22, flare) || !reader.U8(light + 23, shadow) ||
                !reader.U8(light + 24, flagsLow) || !reader.U8(light + 74, flagsHigh) || (flash != 0 && flash != 5) || reflection > 1 || flare != 0 ||
                (shadow != 40 && shadow != 80) || (flagsLow != 64 && flagsLow != 66 && flagsLow != 96) || (flagsHigh != 0 && flagsHigh != 4) ||
                !TerminatedAscii(reader, light + 25, 24) || !TerminatedAscii(reader, light + 49, 24) || !reader.Zero(light + 78, 2))
                return false;
        }
        return CheckedBudgetAdd(summary.effects2d, effects, budget.maximumTotal2dEffects);
    }

    bool ValidateGeometryPlugins(const SByteReader& reader, const SRwChunk& extension, uint32_t libraryId, uint32_t vertices, uint32_t triangles,
                                 uint32_t materials, const SNativeWorldPayloadBudgetSA& budget, SNativeWorldPayloadSummarySA& summary)
    {
        std::vector<SRwChunk> plugins;
        if (!DirectChildren(reader, extension, libraryId, plugins))
            return false;
        std::set<uint32_t> seen;
        bool               binMesh = false, breakable = false;
        for (const SRwChunk& plugin : plugins)
        {
            if (!seen.insert(plugin.type).second)
                return false;
            switch (plugin.type)
            {
                case 0x0000050E:
                    binMesh = ValidateBinMesh(reader, plugin, vertices, triangles, materials, budget, summary);
                    if (!binMesh)
                        return false;
                    break;
                case 0x0253F2F8:
                    if (!Validate2dEffects(reader, plugin, budget, summary))
                        return false;
                    break;
                case 0x0253F2F9:
                {
                    uint32_t marker = 0;
                    uint64_t expected = 4 + static_cast<uint64_t>(vertices) * 4;
                    if (plugin.payloadBytes != expected || !reader.U32(plugin.payloadBegin, marker) || marker == 0)
                        return false;
                    break;
                }
                case 0x0253F2FD:
                    breakable = ValidateBreakable(reader, plugin, budget, summary);
                    if (!breakable)
                        return false;
                    break;
                default:
                    return false;
            }
        }
        return binMesh && breakable;
    }

    bool ValidateGeometry(const SByteReader& reader, const SRwChunk& geometry, uint32_t libraryId, const SNativeWorldPayloadBudgetSA& budget,
                          SNativeWorldPayloadSummarySA& summary)
    {
        std::vector<SRwChunk> children;
        if (!DirectChildren(reader, geometry, libraryId, children) || children.size() != 3 || children[0].type != RW_STRUCT ||
            children[1].type != RW_MATERIAL_LIST || children[2].type != RW_EXTENSION || children[0].payloadBytes < 40)
            return false;
        uint32_t flags = 0, triangles = 0, vertices = 0, morphTargets = 0;
        if (!reader.U32(children[0].payloadBegin, flags) || !reader.U32(children[0].payloadBegin + 4, triangles) ||
            !reader.U32(children[0].payloadBegin + 8, vertices) || !reader.U32(children[0].payloadBegin + 12, morphTargets) ||
            (flags != 0x0001002E && flags != 0x00010076 && flags != 0x0001007E) || !vertices || vertices > budget.maximumGeometryVertices || !triangles ||
            triangles > budget.maximumGeometryTriangles || morphTargets != 1 ||
            !CheckedBudgetAdd(summary.geometryVertices, vertices, budget.maximumTotalGeometryVertices) ||
            !CheckedBudgetAdd(summary.geometryTriangles, triangles, budget.maximumTotalGeometryTriangles))
            return false;

        uint64_t cursor = children[0].payloadBegin + 16;
        if (flags & 8)
            cursor += static_cast<uint64_t>(vertices) * 4;
        if (!FiniteFloats(reader, cursor, static_cast<uint64_t>(vertices) * 2))
            return false;
        cursor += static_cast<uint64_t>(vertices) * 8;
        const uint64_t triangleBegin = cursor;
        cursor += static_cast<uint64_t>(triangles) * 8;

        float    radius = 0.0f;
        uint32_t hasVertices = 0, hasNormals = 0;
        if (!FiniteFloats(reader, cursor, 4) || !reader.Float(cursor + 12, radius) || radius < 0.0f || !reader.U32(cursor + 16, hasVertices) ||
            !reader.U32(cursor + 20, hasNormals) || hasVertices != 1 || hasNormals != ((flags & 0x10) ? 1u : 0u))
            return false;
        cursor += 24;
        if (!FiniteFloats(reader, cursor, static_cast<uint64_t>(vertices) * 3))
            return false;
        cursor += static_cast<uint64_t>(vertices) * 12;
        if (hasNormals)
        {
            if (!FiniteFloats(reader, cursor, static_cast<uint64_t>(vertices) * 3))
                return false;
            cursor += static_cast<uint64_t>(vertices) * 12;
        }
        if (cursor != children[0].end)
            return false;

        uint32_t materials = 0;
        if (!ValidateMaterialList(reader, children[1], libraryId, budget, materials) ||
            !CheckedBudgetAdd(summary.geometryMaterials, materials, budget.maximumTotalGeometryMaterials))
            return false;
        for (uint32_t triangle = 0; triangle < triangles; ++triangle)
        {
            uint16_t       first = 0, second = 0, material = 0, third = 0;
            const uint64_t begin = triangleBegin + triangle * 8;
            if (!reader.U16(begin, first) || !reader.U16(begin + 2, second) || !reader.U16(begin + 4, material) || !reader.U16(begin + 6, third) ||
                first >= vertices || second >= vertices || third >= vertices || material >= materials)
                return false;
        }
        return ValidateGeometryPlugins(reader, children[2], libraryId, vertices, triangles, materials, budget, summary);
    }

    bool ValidateGeometryList(const SByteReader& reader, const SRwChunk& geometryList, uint32_t libraryId, const SNativeWorldPayloadBudgetSA& budget,
                              SNativeWorldPayloadSummarySA& summary, uint32_t& geometryCount)
    {
        std::vector<SRwChunk> children;
        if (!DirectChildren(reader, geometryList, libraryId, children) || children.empty() || children[0].type != RW_STRUCT || children[0].payloadBytes != 4 ||
            !reader.U32(children[0].payloadBegin, geometryCount) || !geometryCount || geometryCount > budget.maximumGeometriesPerClump ||
            children.size() != geometryCount + 1)
            return false;
        for (uint32_t index = 0; index < geometryCount; ++index)
            if (children[index + 1].type != RW_GEOMETRY || !ValidateGeometry(reader, children[index + 1], libraryId, budget, summary))
                return false;
        return true;
    }

    bool ValidateDffContract(const SByteReader& reader, const SRwChunk& root, uint32_t libraryId, const SNativeWorldPayloadBudgetSA& budget,
                             SNativeWorldPayloadSummarySA& summary)
    {
        std::vector<SRwChunk> children;
        if (!DirectChildren(reader, root, libraryId, children) || children.size() < 4 || children[0].type != RW_STRUCT || children[0].payloadBytes != 12 ||
            children[1].type != RW_FRAME_LIST || children[2].type != RW_GEOMETRY_LIST || children.back().type != RW_EXTENSION ||
            !EmptyExtension(reader, children.back(), libraryId))
            return false;
        uint32_t atomics = 0, lights = 0, cameras = 0;
        if (!reader.U32(children[0].payloadBegin, atomics) || !reader.U32(children[0].payloadBegin + 4, lights) ||
            !reader.U32(children[0].payloadBegin + 8, cameras) || !atomics || atomics > budget.maximumAtomicsPerClump ||
            lights > budget.maximumLightsPerClump || cameras != 0 || children.size() != static_cast<size_t>(atomics) + lights * 2 + 4)
            return false;
        uint32_t frameCount = 0, geometryCount = 0;
        if (!ValidateFrameList(reader, children[1], libraryId, budget, frameCount) ||
            !ValidateGeometryList(reader, children[2], libraryId, budget, summary, geometryCount))
            return false;
        size_t childIndex = 3;
        for (uint32_t atomic = 0; atomic < atomics; ++atomic, ++childIndex)
        {
            std::vector<SRwChunk> atomicChildren;
            uint32_t              frame = 0, geometry = 0, flags = 0, unused = 1;
            if (children[childIndex].type != RW_ATOMIC || !DirectChildren(reader, children[childIndex], libraryId, atomicChildren) ||
                atomicChildren.size() != 2 || atomicChildren[0].type != RW_STRUCT || atomicChildren[0].payloadBytes != 16 ||
                atomicChildren[1].type != RW_EXTENSION || !reader.U32(atomicChildren[0].payloadBegin, frame) ||
                !reader.U32(atomicChildren[0].payloadBegin + 4, geometry) || !reader.U32(atomicChildren[0].payloadBegin + 8, flags) ||
                !reader.U32(atomicChildren[0].payloadBegin + 12, unused) || frame >= frameCount || geometry >= geometryCount || flags != 5 || unused != 0 ||
                !EmptyExtension(reader, atomicChildren[1], libraryId))
                return false;
        }
        for (uint32_t light = 0; light < lights; ++light, ++childIndex)
        {
            uint32_t frame = 0;
            if (children[childIndex].type != RW_STRUCT || children[childIndex].payloadBytes != 4 || !reader.U32(children[childIndex].payloadBegin, frame) ||
                frame >= frameCount)
                return false;
            ++childIndex;
            std::vector<SRwChunk> lightChildren;
            uint32_t              typeAndFlags = 0;
            float                 radius = 0.0f, red = 0.0f, green = 0.0f, blue = 0.0f, minusCosAngle = 0.0f;
            if (children[childIndex].type != RW_LIGHT || !DirectChildren(reader, children[childIndex], libraryId, lightChildren) || lightChildren.size() != 2 ||
                lightChildren[0].type != RW_STRUCT || lightChildren[0].payloadBytes != 24 || lightChildren[1].type != RW_EXTENSION ||
                !FiniteFloats(reader, lightChildren[0].payloadBegin, 5) || !reader.Float(lightChildren[0].payloadBegin, radius) ||
                !reader.Float(lightChildren[0].payloadBegin + 4, red) || !reader.Float(lightChildren[0].payloadBegin + 8, green) ||
                !reader.Float(lightChildren[0].payloadBegin + 12, blue) || !reader.Float(lightChildren[0].payloadBegin + 16, minusCosAngle) || radius < 0.0f ||
                red < 0.0f || red > 1.0f || green < 0.0f || green > 1.0f || blue < 0.0f || blue > 1.0f || minusCosAngle < -1.0f || minusCosAngle > 1.0f ||
                !reader.U32(lightChildren[0].payloadBegin + 20, typeAndFlags) || typeAndFlags != 0x00800003 ||
                !EmptyExtension(reader, lightChildren[1], libraryId))
                return false;
        }
        return childIndex == children.size() - 1;
    }

    bool ValidateNativeTexture(const SByteReader& reader, const SRwChunk& texture, uint32_t libraryId, const SNativeWorldPayloadBudgetSA& budget,
                               SNativeWorldPayloadSummarySA& summary, std::string& textureName)
    {
        std::vector<SRwChunk> children;
        if (!DirectChildren(reader, texture, libraryId, children) || children.size() != 2 || children[0].type != RW_STRUCT || children[0].payloadBytes < 92 ||
            children[1].type != RW_EXTENSION || !EmptyExtension(reader, children[1], libraryId))
            return false;
        const uint64_t begin = children[0].payloadBegin;
        uint32_t       platform = 0, filter = 0, rasterFormat = 0, d3dFormat = 0;
        uint16_t       width = 0, height = 0;
        uint8_t        depth = 0, levels = 0, rasterType = 0, flags = 0;
        if (!reader.U32(begin, platform) || !reader.U32(begin + 4, filter) || !PaddedAsciiName(reader, begin + 8, 32, textureName) ||
            !PaddedAscii(reader, begin + 40, 32, true) || !reader.U32(begin + 72, rasterFormat) || !reader.U32(begin + 76, d3dFormat) ||
            !reader.U16(begin + 80, width) || !reader.U16(begin + 82, height) || !reader.U8(begin + 84, depth) || !reader.U8(begin + 85, levels) ||
            !reader.U8(begin + 86, rasterType) || !reader.U8(begin + 87, flags) || platform != 9 || (filter != 0x00001102 && filter != 0x00001106) || !width ||
            width > budget.maximumNativeTextureWidth || !height || height > budget.maximumNativeTextureHeight || (width & (width - 1)) ||
            (height & (height - 1)) || !levels || levels > budget.maximumNativeTextureLevels || rasterType != 4 || (flags != 8 && flags != 9) ||
            (depth != 4 && depth != 8 && depth != 16))
            return false;
        const bool dxt1 = d3dFormat == 0x31545844;
        const bool dxt5 = d3dFormat == 0x35545844;
        uint32_t   maximumLevels = 1;
        for (uint32_t dimension = std::max<uint32_t>(width, height); dimension > 1; dimension >>= 1)
            ++maximumLevels;
        if ((!dxt1 && !dxt5) || (dxt1 && (rasterFormat & 0x7F00) != 0x0100 && (rasterFormat & 0x7F00) != 0x0200) ||
            (dxt5 && (rasterFormat & 0x7F00) != 0x0300) || (rasterFormat & ~0xFF00) || ((rasterFormat & 0x8000) != 0) != (levels > 1) ||
            levels > maximumLevels || (dxt5 && !(flags & 1)))
            return false;

        uint64_t cursor = begin + 88, gpuBytes = 0, decodedBytes = 0;
        for (uint32_t level = 0; level < levels; ++level)
        {
            const uint32_t levelWidth = std::max<uint32_t>(1, width >> level);
            const uint32_t levelHeight = std::max<uint32_t>(1, height >> level);
            const uint64_t blocksWide = (levelWidth + 3) / 4;
            const uint64_t blocksHigh = (levelHeight + 3) / 4;
            uint64_t       expectedBytes = 0, levelDecoded = 0, next = 0;
            uint32_t       serializedBytes = 0;
            if (!CheckedMul(blocksWide, blocksHigh, expectedBytes) || !CheckedMul(expectedBytes, dxt1 ? 8 : 16, expectedBytes) ||
                !reader.U32(cursor, serializedBytes) || serializedBytes != expectedBytes || !CheckedAdd(cursor, 4, cursor) ||
                !CheckedAdd(cursor, expectedBytes, next) || next > children[0].end ||
                !CheckedBudgetAdd(gpuBytes, expectedBytes, budget.maximumNativeTextureGpuBytes) || !CheckedMul(levelWidth, levelHeight, levelDecoded) ||
                !CheckedMul(levelDecoded, 4, levelDecoded) || !CheckedBudgetAdd(decodedBytes, levelDecoded, budget.maximumNativeTextureDecodedBytes))
                return false;
            cursor = next;
        }
        if (cursor != children[0].end || ++summary.nativeTextureCount > budget.maximumNativeTextures ||
            !CheckedBudgetAdd(summary.nativeTextureGpuBytes, gpuBytes, budget.maximumTotalNativeTextureGpuBytes) ||
            !CheckedBudgetAdd(summary.nativeTextureDecodedBytes, decodedBytes, budget.maximumTotalNativeTextureDecodedBytes))
            return false;
        return true;
    }

    bool ValidateTxdContract(const SByteReader& reader, const SRwChunk& root, uint32_t libraryId, const SNativeWorldPayloadBudgetSA& budget,
                             SNativeWorldPayloadSummarySA& summary)
    {
        std::vector<SRwChunk> children;
        uint16_t              textures = 0;
        uint16_t              deviceId = 1;
        if (!DirectChildren(reader, root, libraryId, children) || children.size() < 2 || children[0].type != RW_STRUCT || children[0].payloadBytes != 4 ||
            children.back().type != RW_EXTENSION || !reader.U16(children[0].payloadBegin, textures) || !reader.U16(children[0].payloadBegin + 2, deviceId) ||
            deviceId != 0 || children.size() != static_cast<size_t>(textures) + 2 || !EmptyExtension(reader, children.back(), libraryId))
            return false;
        std::set<std::string> textureNames;
        for (uint32_t index = 0; index < textures; ++index)
        {
            std::string textureName;
            if (children[index + 1].type != RW_TEXTURE_NATIVE || !ValidateNativeTexture(reader, children[index + 1], libraryId, budget, summary, textureName) ||
                !textureNames.insert(textureName).second)
                return false;
        }
        return true;
    }

    bool ValidateRwMember(const std::vector<uint8_t>& data, const std::string& member, uint32_t expectedRoot, const SNativeWorldPayloadBudgetSA& budget,
                          SNativeWorldPayloadSummarySA& summary, std::string& error)
    {
        const SByteReader reader{data};
        SRwChunk          root;
        if (!ReadRwChunk(reader, 0, data.size(), root) || root.type != expectedRoot || root.libraryId != budget.renderWareLibraryId)
            return Fail(error, member, "RenderWare root type, library ID, or boundary is invalid");
        if (!reader.Zero(root.end, data.size() - root.end))
            return Fail(error, member, "RenderWare sector padding is not zero");
        if (!ValidateRwTree(reader, root, member, budget, summary, error))
            return false;
        const bool contractValid = expectedRoot == RW_CLUMP ? ValidateDffContract(reader, root, budget.renderWareLibraryId, budget, summary)
                                                            : ValidateTxdContract(reader, root, budget.renderWareLibraryId, budget, summary);
        if (!contractValid)
            return Fail(error, member, "RenderWare core counts, ordering, or indices differ from the Bullworth profile");
        return true;
    }

    bool ValidateFiniteBounds(const SByteReader& reader, uint64_t begin, bool versionOne)
    {
        float values[10]{};
        for (uint32_t index = 0; index < std::size(values); ++index)
            if (!reader.Float(begin + index * sizeof(float), values[index]) || !std::isfinite(values[index]) ||
                std::abs(values[index]) > MAX_ABSOLUTE_PAYLOAD_FLOAT)
                return false;
        if (versionOne)
            return values[0] >= 0.0f && values[4] <= values[7] && values[5] <= values[8] && values[6] <= values[9];
        return values[9] >= 0.0f && values[0] <= values[3] && values[1] <= values[4] && values[2] <= values[5];
    }

    bool ValidateFloatBox(const SByteReader& reader, uint64_t begin)
    {
        float values[6]{};
        for (uint32_t index = 0; index < std::size(values); ++index)
            if (!reader.Float(begin + index * sizeof(float), values[index]) || !std::isfinite(values[index]) ||
                std::abs(values[index]) > MAX_ABSOLUTE_PAYLOAD_FLOAT)
                return false;
        return values[0] <= values[3] && values[1] <= values[4] && values[2] <= values[5];
    }

    bool ValidateCollRecord(const SByteReader& reader, uint64_t begin, uint64_t end, const SNativeWorldPayloadBudgetSA& budget,
                            SNativeWorldPayloadSummarySA& summary)
    {
        if (end - begin < 72 || !ValidateFiniteBounds(reader, begin + 32, true))
            return false;
        uint64_t   cursor = begin + 72;
        uint32_t   spheres = 0, lines = 0, boxes = 0, vertices = 0, faces = 0;
        const auto readCount = [&](uint32_t& count)
        {
            if (!reader.U32(cursor, count))
                return false;
            cursor += 4;
            return true;
        };
        const auto consume = [&](uint32_t count, uint64_t itemBytes)
        {
            uint64_t bytes = 0, next = 0;
            if (!CheckedMul(count, itemBytes, bytes) || !CheckedAdd(cursor, bytes, next) || next > end)
                return false;
            cursor = next;
            return true;
        };
        if (!readCount(spheres) || spheres > budget.maximumColSpheres)
            return false;
        const uint64_t spheresBegin = cursor;
        if (!consume(spheres, 20) || !readCount(lines) || lines > budget.maximumColLines || !consume(lines, 24) || !readCount(boxes) ||
            boxes > budget.maximumColBoxes)
            return false;
        const uint64_t boxesBegin = cursor;
        if (!consume(boxes, 28) || !readCount(vertices) || vertices > budget.maximumColVertices)
            return false;
        const uint64_t verticesBegin = cursor;
        if (!consume(vertices, 12) || !readCount(faces) || faces > budget.maximumColFaces)
            return false;
        const uint64_t facesBegin = cursor;
        if (!consume(faces, 16) || cursor != end)
            return false;

        for (uint32_t index = 0; index < spheres; ++index)
        {
            float values[4]{};
            for (uint32_t component = 0; component < 4; ++component)
                if (!reader.Float(spheresBegin + index * 20 + component * 4, values[component]) || !std::isfinite(values[component]) ||
                    std::abs(values[component]) > MAX_ABSOLUTE_PAYLOAD_FLOAT)
                    return false;
            if (values[0] < 0.0f)
                return false;
        }
        for (uint32_t index = 0; index < boxes; ++index)
            if (!ValidateFloatBox(reader, boxesBegin + index * 28))
                return false;
        for (uint32_t index = 0; index < vertices; ++index)
            for (uint32_t component = 0; component < 3; ++component)
            {
                float value = 0.0f;
                if (!reader.Float(verticesBegin + index * 12 + component * 4, value) || !std::isfinite(value) || std::abs(value) > MAX_ABSOLUTE_PAYLOAD_FLOAT)
                    return false;
            }
        for (uint32_t index = 0; index < faces; ++index)
            for (uint32_t component = 0; component < 3; ++component)
            {
                uint32_t vertex = 0;
                if (!reader.U32(facesBegin + index * 16 + component * 4, vertex) || vertex >= vertices || vertex > UINT16_MAX)
                    return false;
            }
        if (!CheckedBudgetAdd(summary.colSpheres, spheres, budget.maximumTotalColSpheres) ||
            !CheckedBudgetAdd(summary.colBoxes, boxes, budget.maximumTotalColBoxes) ||
            !CheckedBudgetAdd(summary.colLines, lines, budget.maximumTotalColLines) ||
            !CheckedBudgetAdd(summary.colVertices, vertices, budget.maximumTotalColVertices) ||
            !CheckedBudgetAdd(summary.colFaces, faces, budget.maximumTotalColFaces))
            return false;
        summary.maximumColVertices = std::max(summary.maximumColVertices, vertices);
        summary.maximumColFaces = std::max(summary.maximumColFaces, faces);
        return true;
    }

    bool ResolveCol3Offset(uint64_t recordBegin, uint32_t rawOffset, uint64_t& resolved)
    {
        uint64_t base = 0;
        return rawOffset && CheckedAdd(recordBegin, 4, base) && CheckedAdd(base, rawOffset, resolved);
    }

    bool ValidateCol3Record(const SByteReader& reader, uint64_t begin, uint64_t end, const SNativeWorldPayloadBudgetSA& budget,
                            SNativeWorldPayloadSummarySA& summary)
    {
        if (end - begin < 120 || !ValidateFiniteBounds(reader, begin + 32, false))
            return false;
        uint16_t spheres = 0, boxes = 0, faces = 0;
        uint8_t  lines = 0;
        uint32_t flags = 0, sphereRaw = 0, boxRaw = 0, lineRaw = 0, vertexRaw = 0, faceRaw = 0, planeRaw = 0;
        uint32_t shadowFaces = 0, shadowVertexRaw = 0, shadowFaceRaw = 0;
        if (!reader.U16(begin + 72, spheres) || !reader.U16(begin + 74, boxes) || !reader.U16(begin + 76, faces) || !reader.U8(begin + 78, lines) ||
            !reader.U32(begin + 80, flags) || !reader.U32(begin + 84, sphereRaw) || !reader.U32(begin + 88, boxRaw) || !reader.U32(begin + 92, lineRaw) ||
            !reader.U32(begin + 96, vertexRaw) || !reader.U32(begin + 100, faceRaw) || !reader.U32(begin + 104, planeRaw) ||
            !reader.U32(begin + 108, shadowFaces) || !reader.U32(begin + 112, shadowVertexRaw) || !reader.U32(begin + 116, shadowFaceRaw))
            return false;
        if (spheres > budget.maximumColSpheres || boxes > budget.maximumColBoxes || lines > budget.maximumColLines || faces > budget.maximumColFaces ||
            shadowFaces > budget.maximumColShadowFaces || (flags != 0 && flags != 2 && flags != 10) || lineRaw || planeRaw || shadowVertexRaw || shadowFaceRaw)
            return false;

        const bool hasContents = spheres || boxes || lines || faces || shadowFaces;
        if (((flags & 2) != 0) != hasContents || ((flags & 8) != 0) && !faces)
            return false;

        uint64_t   cursor = begin + 120;
        const auto consumeSection = [&](uint32_t count, uint32_t rawOffset, uint64_t itemBytes)
        {
            if (!count)
                return rawOffset == 0;
            uint64_t resolved = 0, bytes = 0, next = 0;
            if (!ResolveCol3Offset(begin, rawOffset, resolved) || resolved != cursor || !CheckedMul(count, itemBytes, bytes) ||
                !CheckedAdd(cursor, bytes, next) || next > end)
                return false;
            cursor = next;
            return true;
        };
        uint64_t sphereBegin = cursor;
        if (!consumeSection(spheres, sphereRaw, 20))
            return false;
        uint64_t boxBegin = cursor;
        if (!consumeSection(boxes, boxRaw, 28) || lines || lineRaw)
            return false;
        for (uint32_t index = 0; index < spheres; ++index)
        {
            float values[4]{};
            for (uint32_t component = 0; component < 4; ++component)
                if (!reader.Float(sphereBegin + index * 20 + component * 4, values[component]) || !std::isfinite(values[component]) ||
                    std::abs(values[component]) > MAX_ABSOLUTE_PAYLOAD_FLOAT)
                    return false;
            if (values[3] < 0.0f)
                return false;
        }
        for (uint32_t index = 0; index < boxes; ++index)
            if (!ValidateFloatBox(reader, boxBegin + index * 28))
                return false;
        if (!CheckedBudgetAdd(summary.colSpheres, spheres, budget.maximumTotalColSpheres) ||
            !CheckedBudgetAdd(summary.colBoxes, boxes, budget.maximumTotalColBoxes) || !CheckedBudgetAdd(summary.colLines, lines, budget.maximumTotalColLines))
            return false;

        if (!faces)
        {
            if (vertexRaw || faceRaw || (flags & 8) || cursor != end)
                return false;
            return true;
        }

        uint64_t vertexBegin = 0, faceBegin = 0;
        if (!ResolveCol3Offset(begin, vertexRaw, vertexBegin) || !ResolveCol3Offset(begin, faceRaw, faceBegin) || vertexBegin != cursor || faceBegin > end)
            return false;
        uint64_t vertexEnd = faceBegin;
        uint32_t faceGroups = 0;
        if (flags & 8)
        {
            if (faceBegin < begin + 4 || !reader.U32(faceBegin - 4, faceGroups) || faceGroups > budget.maximumColFaceGroups)
                return false;
            uint64_t groupBytes = 0, groupAndCount = 0;
            if (!CheckedMul(faceGroups, 28, groupBytes) || !CheckedAdd(groupBytes, 4, groupAndCount) || groupAndCount > faceBegin - vertexBegin)
                return false;
            vertexEnd = faceBegin - groupAndCount;
            uint16_t previousLast = 0;
            for (uint32_t index = 0; index < faceGroups; ++index)
            {
                const uint64_t groupBegin = vertexEnd + index * 28;
                uint16_t       first = 0, last = 0;
                if (!ValidateFloatBox(reader, groupBegin) || !reader.U16(groupBegin + 24, first) || !reader.U16(groupBegin + 26, last) || first > last ||
                    last >= faces || (index == 0 ? first != 0 : first != static_cast<uint32_t>(previousLast) + 1))
                    return false;
                previousLast = last;
            }
            if (!faceGroups || previousLast != faces - 1)
                return false;
        }
        const uint64_t vertexBytes = vertexEnd - vertexBegin;
        const uint64_t vertexPadding = vertexBytes % 6;
        const uint64_t vertices = vertexBytes / 6;
        if (!vertices || vertices > budget.maximumColVertices || (vertexPadding != 0 && vertexPadding != 2) ||
            !reader.Zero(vertexBegin + vertices * 6, vertexPadding))
            return false;

        uint64_t faceBytes = 0, faceEnd = 0;
        if (!CheckedMul(faces, 8, faceBytes) || !CheckedAdd(faceBegin, faceBytes, faceEnd) || faceEnd != end)
            return false;
        for (uint32_t index = 0; index < faces; ++index)
            for (uint32_t component = 0; component < 3; ++component)
            {
                uint16_t vertex = 0;
                if (!reader.U16(faceBegin + index * 8 + component * 2, vertex) || vertex >= vertices)
                    return false;
            }
        if (!CheckedBudgetAdd(summary.colVertices, vertices, budget.maximumTotalColVertices) ||
            !CheckedBudgetAdd(summary.colFaces, faces, budget.maximumTotalColFaces) ||
            !CheckedBudgetAdd(summary.colFaceGroups, faceGroups, budget.maximumTotalColFaceGroups))
            return false;
        summary.maximumColVertices = std::max(summary.maximumColVertices, static_cast<uint32_t>(vertices));
        summary.maximumColFaces = std::max(summary.maximumColFaces, static_cast<uint32_t>(faces));
        summary.maximumColFaceGroups = std::max(summary.maximumColFaceGroups, faceGroups);
        return true;
    }

    bool ValidateColMember(const std::vector<uint8_t>& data, const SNativeWorldPayloadPlanSA& plan, const SNativeWorldPayloadBudgetSA& budget,
                           SNativeWorldPayloadSummarySA& summary, std::string& error)
    {
        const SByteReader               reader{data};
        std::map<std::string, uint32_t> expectedByName;
        std::set<uint32_t>              seenIds;
        std::set<std::string>           seenNames;
        for (const auto& [id, name] : plan.modelNames)
            if (!expectedByName.emplace(name, id).second)
                return Fail(error, plan.colFileName, "IDE model names are not unique");

        uint64_t offset = 0;
        while (offset < data.size())
        {
            if (reader.Zero(offset, data.size() - offset))
                break;
            if (!reader.Contains(offset, 32) || ++summary.colRecordCount > budget.maximumColRecords)
                return Fail(error, plan.colFileName, "COL record header or count exceeds the compiled budget");

            const bool coll = memcmp(data.data() + offset, "COLL", 4) == 0;
            const bool col3 = memcmp(data.data() + offset, "COL3", 4) == 0;
            uint32_t   payloadBytes = 0;
            uint64_t   recordBytes = 0, recordEnd = 0;
            if ((!coll && !col3) || !reader.U32(offset + 4, payloadBytes) || payloadBytes < 24 || !CheckedAdd(8, payloadBytes, recordBytes) ||
                recordBytes > budget.maximumColRecordBytes || !CheckedAdd(offset, recordBytes, recordEnd) || recordEnd > data.size())
                return Fail(error, plan.colFileName, "COL magic, record size, or boundary is invalid");

            const uint8_t* nameBytes = data.data() + offset + 8;
            const uint8_t* terminator = static_cast<const uint8_t*>(memchr(nameBytes, 0, 22));
            uint16_t       modelId = 0;
            if (!terminator || terminator == nameBytes || !reader.U16(offset + 30, modelId) ||
                !std::all_of(terminator, nameBytes + 22, [](uint8_t value) { return value == 0; }))
                return Fail(error, plan.colFileName, "COL model name or compiled ID field is invalid");
            const std::string modelName(reinterpret_cast<const char*>(nameBytes), reinterpret_cast<const char*>(terminator));
            const auto        expected = expectedByName.find(modelName);
            if (expected == expectedByName.end() || expected->second != modelId || !seenIds.insert(modelId).second || !seenNames.insert(modelName).second)
                return Fail(error, plan.colFileName, "COL model ID/name mapping is not one-to-one with the IDE");

            const bool validBody =
                coll ? ValidateCollRecord(reader, offset, recordEnd, budget, summary) : ValidateCol3Record(reader, offset, recordEnd, budget, summary);
            if (!validBody)
                return Fail(error, plan.colFileName, "COL internal counts, offsets, indices, or finite bounds are invalid");
            summary.collRecordCount += coll ? 1 : 0;
            summary.col3RecordCount += col3 ? 1 : 0;
            summary.maximumColRecordBytes = std::max(summary.maximumColRecordBytes, static_cast<uint32_t>(recordBytes));
            if (!CheckedBudgetAdd(summary.colBytes, recordBytes, budget.maximumTotalColBytes))
                return Fail(error, plan.colFileName, "COL byte work exceeds the compiled budget");
            offset = recordEnd;
        }
        if (!reader.Zero(offset, data.size() - offset) || seenIds.size() != plan.modelNames.size() || seenNames.size() != plan.modelNames.size())
            return Fail(error, plan.colFileName, "COL final padding or model inventory is incomplete");
        return true;
    }

    bool ReadMember(HANDLE file, uint64_t fileBytes, const std::string& name, const SNativeWorldPayloadImgEntrySA& entry, std::vector<uint8_t>& data,
                    std::string& error)
    {
        uint64_t begin = 0, bytes = 0, end = 0;
        if (!CheckedMul(entry.offsetBlocks, IMG_BLOCK_BYTES, begin) || !CheckedMul(entry.sizeBlocks, IMG_BLOCK_BYTES, bytes) ||
            !CheckedAdd(begin, bytes, end) || end > fileBytes || bytes > MAXDWORD)
            return Fail(error, name, "IMG member byte range is invalid");
        data.resize(static_cast<size_t>(bytes));
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(begin);
        DWORD read = 0;
        if (!SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) || !ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) ||
            read != data.size())
            return Fail(error, name, "IMG member read is truncated");
        return true;
    }
}  // namespace

bool CNativeWorldPayloadValidatorSA::Validate(const wchar_t* imgPath, const SNativeWorldPayloadPlanSA& plan, const SNativeWorldPayloadBudgetSA& budget,
                                              SNativeWorldPayloadSummarySA& summary, std::string& error)
{
    summary = {};
    HANDLE file = CreateFileW(imgPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = "native payload IMG cannot be opened";
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0)
    {
        CloseHandle(file);
        error = "native payload IMG byte length cannot be read";
        return false;
    }

    const auto validateRwEntry = [&](const std::string& name, uint32_t rootType)
    {
        const auto found = plan.imgEntries.find(name);
        if (found == plan.imgEntries.end())
            return Fail(error, name, "payload entry is absent from the IMG plan");
        uint64_t nextBytes = 0;
        if (!CheckedAdd(summary.renderWareBytes, static_cast<uint64_t>(found->second.sizeBlocks) * IMG_BLOCK_BYTES, nextBytes) ||
            nextBytes > budget.maximumRenderWareBytes)
            return Fail(error, name, "RenderWare byte work exceeds the compiled budget");
        std::vector<uint8_t> data;
        if (!ReadMember(file, static_cast<uint64_t>(size.QuadPart), name, found->second, data, error) ||
            !ValidateRwMember(data, name, rootType, budget, summary, error))
            return false;
        summary.renderWareBytes = nextBytes;
        return true;
    };

    bool valid = true;
    for (const auto& [id, modelName] : plan.modelNames)
    {
        if (!validateRwEntry(modelName + ".dff", RW_CLUMP))
        {
            valid = false;
            break;
        }
        ++summary.dffCount;
    }
    for (auto iterator = plan.txdNames.begin(); valid && iterator != plan.txdNames.end(); ++iterator)
    {
        if (!validateRwEntry(*iterator + ".txd", RW_TEXTURE_DICTIONARY))
        {
            valid = false;
            break;
        }
        ++summary.txdCount;
    }
    if (valid)
    {
        const auto           col = plan.imgEntries.find(plan.colFileName);
        std::vector<uint8_t> data;
        valid = col != plan.imgEntries.end() && ReadMember(file, static_cast<uint64_t>(size.QuadPart), plan.colFileName, col->second, data, error) &&
                ValidateColMember(data, plan, budget, summary, error);
        if (col == plan.imgEntries.end())
            Fail(error, plan.colFileName, "COL payload entry is absent from the IMG plan");
    }
    CloseHandle(file);
    return valid;
}
