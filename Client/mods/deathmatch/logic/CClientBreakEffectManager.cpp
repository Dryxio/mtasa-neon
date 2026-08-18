/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBreakEffectManager.cpp
 *  PURPOSE:     Generic RenderWare mesh fracture cache, physics and rendering
 *
 *****************************************************************************/

#include <StdInc.h>
#include <core/CGraphicsInterface.h>
#include <game/CRenderWare.h>
#include <game/RenderWare.h>
#include <game/RenderWareD3D.h>
#include "CClientBreakEffectManager.h"
#include "CClientBreakEffect.h"
#include "CClientObject.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr std::uint16_t UNASSIGNED_CLUSTER = std::numeric_limits<std::uint16_t>::max();
    constexpr std::uint32_t FREE_SPIN_FRAMES = 5;

    struct SChunkSettleState
    {
        CVector       restAxis{0.0f, 0.0f, 1.0f};
        float         halfThickness = 0.05f;
        std::uint32_t framesActive = 0;
        bool          touchedGround = false;
    };

    struct SQuaternion
    {
        float w = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    std::unordered_map<const SBreakEffectChunk*, SChunkSettleState> g_ChunkSettleStates;

    float LengthSq(const CVector& value)
    {
        return value.fX * value.fX + value.fY * value.fY + value.fZ * value.fZ;
    }

    float Dot(const CVector& a, const CVector& b)
    {
        return a.fX * b.fX + a.fY * b.fY + a.fZ * b.fZ;
    }

    CVector Cross(const CVector& a, const CVector& b)
    {
        return CVector(a.fY * b.fZ - a.fZ * b.fY, a.fZ * b.fX - a.fX * b.fZ, a.fX * b.fY - a.fY * b.fX);
    }

    CVector NormalizeOr(const CVector& value, const CVector& fallback)
    {
        const float lengthSq = LengthSq(value);
        if (lengthSq <= 0.000001f)
            return fallback;
        const float invLength = 1.0f / std::sqrt(lengthSq);
        return CVector(value.fX * invLength, value.fY * invLength, value.fZ * invLength);
    }

    CVector TransformPoint(const RwMatrix& matrix, const RwV3d& point)
    {
        return CVector(matrix.right.x * point.x + matrix.up.x * point.y + matrix.at.x * point.z + matrix.pos.x,
                       matrix.right.y * point.x + matrix.up.y * point.y + matrix.at.y * point.z + matrix.pos.y,
                       matrix.right.z * point.x + matrix.up.z * point.y + matrix.at.z * point.z + matrix.pos.z);
    }

    CVector RotateAxisAngle(const CVector& point, const CVector& axis, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const float dot = point.fX * axis.fX + point.fY * axis.fY + point.fZ * axis.fZ;
        return CVector(point.fX * c + (axis.fY * point.fZ - axis.fZ * point.fY) * s + axis.fX * dot * (1.0f - c),
                       point.fY * c + (axis.fZ * point.fX - axis.fX * point.fZ) * s + axis.fY * dot * (1.0f - c),
                       point.fZ * c + (axis.fX * point.fY - axis.fY * point.fX) * s + axis.fZ * dot * (1.0f - c));
    }

    SQuaternion NormalizeQuaternion(const SQuaternion& value)
    {
        const float lengthSq = value.w * value.w + value.x * value.x + value.y * value.y + value.z * value.z;
        if (lengthSq <= 0.000001f)
            return {};
        const float invLength = 1.0f / std::sqrt(lengthSq);
        return {value.w * invLength, value.x * invLength, value.y * invLength, value.z * invLength};
    }

    SQuaternion QuaternionFromAxisAngle(const CVector& axis, float angle)
    {
        if (std::fabs(angle) <= 0.000001f)
            return {};
        const CVector normalizedAxis = NormalizeOr(axis, CVector(0.0f, 0.0f, 1.0f));
        const float half = angle * 0.5f;
        const float s = std::sin(half);
        return NormalizeQuaternion({std::cos(half), normalizedAxis.fX * s, normalizedAxis.fY * s, normalizedAxis.fZ * s});
    }

    SQuaternion MultiplyQuaternion(const SQuaternion& a, const SQuaternion& b)
    {
        return NormalizeQuaternion({a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
                                    a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                                    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w});
    }

    void QuaternionToAxisAngle(SQuaternion value, CVector& axis, float& angle)
    {
        value = NormalizeQuaternion(value);
        if (value.w < 0.0f)
        {
            value.w = -value.w;
            value.x = -value.x;
            value.y = -value.y;
            value.z = -value.z;
        }

        const float w = std::clamp(value.w, -1.0f, 1.0f);
        angle = 2.0f * std::acos(w);
        const float sinHalf = std::sqrt(std::max(0.0f, 1.0f - w * w));
        if (sinHalf <= 0.00001f || angle <= 0.00001f)
        {
            axis = CVector(0.0f, 0.0f, 1.0f);
            angle = 0.0f;
            return;
        }

        axis = NormalizeOr(CVector(value.x / sinHalf, value.y / sinHalf, value.z / sinHalf), CVector(0.0f, 0.0f, 1.0f));
    }

    void ApplyOrientationCorrection(SBreakEffectChunk& chunk, const CVector& axis, float angle)
    {
        if (std::fabs(angle) <= 0.000001f || LengthSq(axis) <= 0.000001f)
            return;

        const SQuaternion current = QuaternionFromAxisAngle(chunk.rotationAxis, chunk.rotation);
        const SQuaternion correction = QuaternionFromAxisAngle(axis, angle);
        const SQuaternion updated = MultiplyQuaternion(correction, current);
        QuaternionToAxisAngle(updated, chunk.rotationAxis, chunk.rotation);
    }

    SChunkSettleState BuildSettleState(const SBreakEffectChunk& chunk)
    {
        CVector minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        CVector maximum(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
        bool hasVertices = false;

        for (const SBreakEffectBatch& batch : chunk.batches)
            for (const SBreakEffectVertex& vertex : batch.vertices)
            {
                minimum.fX = std::min(minimum.fX, vertex.localPosition.fX);
                minimum.fY = std::min(minimum.fY, vertex.localPosition.fY);
                minimum.fZ = std::min(minimum.fZ, vertex.localPosition.fZ);
                maximum.fX = std::max(maximum.fX, vertex.localPosition.fX);
                maximum.fY = std::max(maximum.fY, vertex.localPosition.fY);
                maximum.fZ = std::max(maximum.fZ, vertex.localPosition.fZ);
                hasVertices = true;
            }

        SChunkSettleState state;
        if (!hasVertices)
            return state;

        const float width = std::max(0.001f, maximum.fX - minimum.fX);
        const float length = std::max(0.001f, maximum.fY - minimum.fY);
        const float height = std::max(0.001f, maximum.fZ - minimum.fZ);

        if (width <= length && width <= height)
        {
            state.restAxis = CVector(1.0f, 0.0f, 0.0f);
            state.halfThickness = width * 0.5f;
        }
        else if (length <= width && length <= height)
        {
            state.restAxis = CVector(0.0f, 1.0f, 0.0f);
            state.halfThickness = length * 0.5f;
        }
        else
        {
            state.restAxis = CVector(0.0f, 0.0f, 1.0f);
            state.halfThickness = height * 0.5f;
        }

        state.halfThickness = std::max(state.halfThickness, 0.01f);
        return state;
    }

    SChunkSettleState& GetSettleState(SBreakEffectChunk& chunk)
    {
        auto [iter, inserted] = g_ChunkSettleStates.try_emplace(&chunk);
        if (inserted)
            iter->second = BuildSettleState(chunk);
        return iter->second;
    }

    float GetRestAlignment(const SBreakEffectChunk& chunk, const SChunkSettleState& state)
    {
        CVector facing = NormalizeOr(RotateAxisAngle(state.restAxis, chunk.rotationAxis, chunk.rotation), state.restAxis);
        return std::fabs(std::clamp(facing.fZ, -1.0f, 1.0f));
    }

    void AdvanceVanillaRestOrientation(SBreakEffectChunk& chunk, SChunkSettleState& state, float dt)
    {
        ++state.framesActive;
        if (state.framesActive <= FREE_SPIN_FRAMES)
        {
            chunk.rotation += chunk.rotationSpeed * dt;
            return;
        }

        const CVector groundNormal(0.0f, 0.0f, 1.0f);
        CVector facing = NormalizeOr(RotateAxisAngle(state.restAxis, chunk.rotationAxis, chunk.rotation), state.restAxis);
        float facingDot = std::clamp(Dot(facing, groundNormal), -1.0f, 1.0f);

        // Either side of a thin fragment can face upward. Choosing the nearer
        // hemisphere avoids a pointless 180-degree flip while preserving the
        // vanilla rule that the thinnest axis is driven toward the ground normal.
        if (facingDot < 0.0f)
        {
            facing.fX = -facing.fX;
            facing.fY = -facing.fY;
            facing.fZ = -facing.fZ;
            facingDot = -facingDot;
        }

        if (facingDot >= 0.99999f)
            return;

        CVector correctionAxis = Cross(facing, groundNormal);
        const float axisLengthSq = LengthSq(correctionAxis);
        if (axisLengthSq <= 0.000001f)
            return;
        correctionAxis = NormalizeOr(correctionAxis, CVector(1.0f, 0.0f, 0.0f));

        const float angle = std::acos(std::clamp(facingDot, -1.0f, 1.0f));
        // GTA applies roughly angle * timeStep / 20 every update. With our dt
        // expressed in seconds, 2.5/sec gives the same ~5% correction at 50 Hz.
        const float rate = state.touchedGround ? 4.0f : 2.5f;
        const float step = angle * std::clamp(dt * rate, 0.0f, 1.0f);
        ApplyOrientationCorrection(chunk, correctionAxis, step);
    }

    std::uint32_t NextRandom(std::uint32_t& state)
    {
        if (!state)
            state = 0xA341316Cu;
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float RandomRange(std::uint32_t& state, float minimum, float maximum)
    {
        const float t = static_cast<float>(NextRandom(state) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
        return minimum + (maximum - minimum) * t;
    }

    struct SQuantizedVertex
    {
        int x;
        int y;
        int z;
        bool operator==(const SQuantizedVertex& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z; }
    };

    struct SQuantizedVertexHash
    {
        std::size_t operator()(const SQuantizedVertex& value) const
        {
            std::size_t h = static_cast<std::size_t>(value.x) * 73856093u;
            h ^= static_cast<std::size_t>(value.y) * 19349663u;
            h ^= static_cast<std::size_t>(value.z) * 83492791u;
            return h;
        }
    };

    struct SAtomicSource
    {
        RpAtomic*   atomic = nullptr;
        RpGeometry* geometry = nullptr;
        int         triangles = 0;
    };

    void CollectAtomic(RpAtomic* atomic, std::vector<SAtomicSource>& sources)
    {
        if (!atomic || !atomic->geometry || !atomic->geometry->morph_target || !atomic->geometry->morph_target->verts ||
            !atomic->geometry->triangles || atomic->geometry->triangles_size <= 0 || atomic->geometry->vertices_size <= 0)
            return;
        sources.push_back({atomic, atomic->geometry, atomic->geometry->triangles_size});
    }

    void CollectAtomics(void* rwObject, std::vector<SAtomicSource>& sources)
    {
        if (!rwObject)
            return;
        RwObject* object = static_cast<RwObject*>(rwObject);
        if (object->type == RP_TYPE_ATOMIC)
        {
            CollectAtomic(reinterpret_cast<RpAtomic*>(object), sources);
            return;
        }
        if (object->type != RP_TYPE_CLUMP)
            return;

        RpClump* clump = reinterpret_cast<RpClump*>(object);
        RwListEntry* root = &clump->atomics.root;
        for (RwListEntry* entry = root->next; entry && entry != root; entry = entry->next)
        {
            auto* atomic = reinterpret_cast<RpAtomic*>(reinterpret_cast<unsigned char*>(entry) - offsetof(RpAtomic, globalClumps));
            CollectAtomic(atomic, sources);
        }
    }

    RwMatrix IdentityRwMatrix()
    {
        RwMatrix matrix{};
        matrix.right = {1.0f, 0.0f, 0.0f};
        matrix.up = {0.0f, 1.0f, 0.0f};
        matrix.at = {0.0f, 0.0f, 1.0f};
        matrix.pos = {0.0f, 0.0f, 0.0f};
        return matrix;
    }

    D3DMATRIX IdentityD3DMatrix()
    {
        D3DMATRIX matrix{};
        matrix._11 = matrix._22 = matrix._33 = matrix._44 = 1.0f;
        return matrix;
    }

    std::uint32_t PackColor(const RwColor* vertexColor, const RpMaterial* material)
    {
        const RwColor white{255, 255, 255, 255};
        const RwColor& v = vertexColor ? *vertexColor : white;
        const RwColor& m = material ? material->color : white;
        const auto mul = [](unsigned char a, unsigned char b) -> unsigned char
        {
            return static_cast<unsigned char>((static_cast<unsigned int>(a) * b) / 255u);
        };
        const unsigned char r = mul(v.r, m.r);
        const unsigned char g = mul(v.g, m.g);
        const unsigned char b = mul(v.b, m.b);
        const unsigned char a = mul(v.a, m.a);
        return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
    }

    IDirect3DTexture9* GetD3DTexture(RwTexture* texture)
    {
        if (!texture || !texture->raster || !texture->raster->renderResource)
            return nullptr;
        return reinterpret_cast<RwD3D9Raster*>(texture->raster->renderResource)->texture;
    }

    std::size_t ChooseAutomaticFragmentCount(std::size_t triangles)
    {
        if (triangles <= 8)
            return std::max<std::size_t>(1, triangles);
        std::size_t count = static_cast<std::size_t>(std::round(std::sqrt(static_cast<float>(triangles)) * 0.72f));
        if (triangles > 5000)
            count = std::max<std::size_t>(count, 40);
        return std::clamp<std::size_t>(count, 6, CClientBreakEffectManager::MAX_FRAGMENTS_PER_EFFECT);
    }
}

CClientBreakEffectManager& CClientBreakEffectManager::GetSingleton()
{
    static CClientBreakEffectManager manager;
    return manager;
}

void CClientBreakEffectManager::AddToList(CClientBreakEffect* effect) { m_List.push_back(effect); }

void CClientBreakEffectManager::RemoveFromList(CClientBreakEffect* effect)
{
    if (effect)
        for (SBreakEffectChunk& chunk : effect->m_Chunks)
            g_ChunkSettleStates.erase(&chunk);
    m_List.remove(effect);
}

CClientBreakEffect* CClientBreakEffectManager::Get(ElementID ID)
{
    for (CClientBreakEffect* effect : m_List)
        if (effect && effect->GetID() == ID)
            return effect;
    return nullptr;
}

std::size_t CClientBreakEffectManager::GetActiveFragmentCount() const
{
    std::size_t count = 0;
    for (const CClientBreakEffect* effect : m_List)
        if (effect && !effect->IsBeingDeleted())
            count += effect->m_Chunks.size();
    return count;
}

void CClientBreakEffectManager::ClearCache() { m_Cache.clear(); }

std::uint64_t CClientBreakEffectManager::ComputeGeometrySignature(const RpGeometry* geometry) const
{
    if (!geometry || !geometry->morph_target || !geometry->morph_target->verts)
        return 0;
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](std::uint32_t value)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix(static_cast<std::uint32_t>(geometry->vertices_size));
    mix(static_cast<std::uint32_t>(geometry->triangles_size));
    const int vertexStep = std::max(1, geometry->vertices_size / 16);
    for (int i = 0; i < geometry->vertices_size; i += vertexStep)
    {
        const RwV3d& v = geometry->morph_target->verts[i];
        mix(static_cast<std::uint32_t>(std::lround(v.x * 1000.0f)));
        mix(static_cast<std::uint32_t>(std::lround(v.y * 1000.0f)));
        mix(static_cast<std::uint32_t>(std::lround(v.z * 1000.0f)));
    }
    const int triangleStep = std::max(1, geometry->triangles_size / 16);
    for (int i = 0; i < geometry->triangles_size; i += triangleStep)
    {
        const RpTriangle& t = geometry->triangles[i];
        mix(t.verts[0]); mix(t.verts[1]); mix(t.verts[2]); mix(t.materialId);
    }
    return hash;
}

bool CClientBreakEffectManager::BuildClusterAssignments(const RpGeometry* geometry, std::size_t fragments, std::uint32_t seed,
                                                         std::vector<std::uint16_t>& outAssignments, std::size_t& outClusterCount,
                                                         bool& outCacheHit)
{
    outCacheHit = false;
    outClusterCount = 0;
    if (!geometry || !geometry->morph_target || !geometry->morph_target->verts || !geometry->triangles || geometry->triangles_size <= 0 ||
        geometry->vertices_size <= 0)
        return false;

    fragments = std::clamp<std::size_t>(fragments, 1, std::min<std::size_t>(MAX_FRAGMENTS_PER_EFFECT, geometry->triangles_size));
    const std::uint64_t signature = ComputeGeometrySignature(geometry);
    for (const SClusterCacheEntry& entry : m_Cache)
    {
        if (entry.geometry == geometry && entry.triangleCount == geometry->triangles_size && entry.vertexCount == geometry->vertices_size &&
            entry.fragments == fragments && entry.seed == seed && entry.signature == signature)
        {
            outAssignments = entry.assignments;
            for (std::uint16_t assignment : outAssignments)
                outClusterCount = std::max(outClusterCount, static_cast<std::size_t>(assignment) + 1);
            outCacheHit = true;
            return outClusterCount != 0;
        }
    }

    const int triangleCount = geometry->triangles_size;
    const int vertexCount = geometry->vertices_size;
    const RwV3d* vertices = geometry->morph_target->verts;

    std::unordered_map<SQuantizedVertex, std::uint32_t, SQuantizedVertexHash> weldMap;
    std::vector<std::uint32_t> welded(vertexCount);
    weldMap.reserve(vertexCount * 2);
    std::uint32_t nextWeld = 0;
    for (int i = 0; i < vertexCount; ++i)
    {
        const SQuantizedVertex key{static_cast<int>(std::lround(vertices[i].x * 10000.0f)), static_cast<int>(std::lround(vertices[i].y * 10000.0f)),
                                   static_cast<int>(std::lround(vertices[i].z * 10000.0f))};
        auto [it, inserted] = weldMap.emplace(key, nextWeld);
        if (inserted)
            ++nextWeld;
        welded[i] = it->second;
    }

    std::unordered_map<std::uint64_t, std::vector<int>> edgeTriangles;
    edgeTriangles.reserve(static_cast<std::size_t>(triangleCount) * 3);
    std::vector<CVector> centroids(triangleCount);
    for (int i = 0; i < triangleCount; ++i)
    {
        const RpTriangle& triangle = geometry->triangles[i];
        if (triangle.verts[0] >= vertexCount || triangle.verts[1] >= vertexCount || triangle.verts[2] >= vertexCount)
            return false;
        const RwV3d& a = vertices[triangle.verts[0]];
        const RwV3d& b = vertices[triangle.verts[1]];
        const RwV3d& c = vertices[triangle.verts[2]];
        centroids[i] = CVector((a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f);
        const std::uint32_t ids[3] = {welded[triangle.verts[0]], welded[triangle.verts[1]], welded[triangle.verts[2]]};
        for (int edge = 0; edge < 3; ++edge)
        {
            const std::uint32_t lo = std::min(ids[edge], ids[(edge + 1) % 3]);
            const std::uint32_t hi = std::max(ids[edge], ids[(edge + 1) % 3]);
            edgeTriangles[(static_cast<std::uint64_t>(lo) << 32) | hi].push_back(i);
        }
    }

    std::vector<std::vector<int>> adjacency(triangleCount);
    for (const auto& item : edgeTriangles)
    {
        const auto& triangles = item.second;
        for (std::size_t i = 0; i < triangles.size(); ++i)
            for (std::size_t j = i + 1; j < triangles.size(); ++j)
            {
                adjacency[triangles[i]].push_back(triangles[j]);
                adjacency[triangles[j]].push_back(triangles[i]);
            }
    }

    std::vector<int> componentOf(triangleCount, -1);
    std::vector<std::vector<int>> components;
    for (int triangle = 0; triangle < triangleCount; ++triangle)
    {
        if (componentOf[triangle] != -1)
            continue;
        const int componentIndex = static_cast<int>(components.size());
        components.emplace_back();
        std::deque<int> queue{triangle};
        componentOf[triangle] = componentIndex;
        while (!queue.empty())
        {
            const int current = queue.front();
            queue.pop_front();
            components.back().push_back(current);
            for (int neighbor : adjacency[current])
                if (componentOf[neighbor] == -1)
                {
                    componentOf[neighbor] = componentIndex;
                    queue.push_back(neighbor);
                }
        }
    }

    outAssignments.assign(triangleCount, UNASSIGNED_CLUSTER);
    if (components.size() >= fragments)
    {
        for (std::size_t component = 0; component < components.size(); ++component)
        {
            const std::uint16_t cluster = static_cast<std::uint16_t>(component % fragments);
            for (int triangle : components[component])
                outAssignments[triangle] = cluster;
        }
        outClusterCount = fragments;
    }
    else
    {
        std::vector<std::size_t> allocation(components.size(), 1);
        std::size_t remaining = fragments - components.size();
        while (remaining > 0)
        {
            std::size_t best = components.size();
            float bestScore = -1.0f;
            for (std::size_t component = 0; component < components.size(); ++component)
            {
                if (allocation[component] >= components[component].size())
                    continue;
                const float score = static_cast<float>(components[component].size()) / static_cast<float>(allocation[component]);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = component;
                }
            }
            if (best == components.size())
                break;
            ++allocation[best];
            --remaining;
        }

        std::uint16_t clusterBase = 0;
        for (std::size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex)
        {
            const std::vector<int>& component = components[componentIndex];
            const std::size_t clusterCount = std::min(allocation[componentIndex], component.size());
            std::vector<int> seeds;
            seeds.reserve(clusterCount);
            seeds.push_back(component[seed % component.size()]);
            while (seeds.size() < clusterCount)
            {
                int bestTriangle = component.front();
                float bestDistance = -1.0f;
                for (int candidate : component)
                {
                    float minDistance = std::numeric_limits<float>::max();
                    for (int existingSeed : seeds)
                    {
                        const CVector delta(centroids[candidate].fX - centroids[existingSeed].fX, centroids[candidate].fY - centroids[existingSeed].fY,
                                            centroids[candidate].fZ - centroids[existingSeed].fZ);
                        minDistance = std::min(minDistance, LengthSq(delta));
                    }
                    if (minDistance > bestDistance)
                    {
                        bestDistance = minDistance;
                        bestTriangle = candidate;
                    }
                }
                if (std::find(seeds.begin(), seeds.end(), bestTriangle) != seeds.end())
                    break;
                seeds.push_back(bestTriangle);
            }

            std::deque<std::pair<int, std::uint16_t>> queue;
            for (std::size_t localCluster = 0; localCluster < seeds.size(); ++localCluster)
            {
                const std::uint16_t cluster = static_cast<std::uint16_t>(clusterBase + localCluster);
                outAssignments[seeds[localCluster]] = cluster;
                queue.emplace_back(seeds[localCluster], cluster);
            }
            while (!queue.empty())
            {
                const auto current = queue.front();
                queue.pop_front();
                for (int neighbor : adjacency[current.first])
                    if (componentOf[neighbor] == static_cast<int>(componentIndex) && outAssignments[neighbor] == UNASSIGNED_CLUSTER)
                    {
                        outAssignments[neighbor] = current.second;
                        queue.emplace_back(neighbor, current.second);
                    }
            }
            for (int triangle : component)
                if (outAssignments[triangle] == UNASSIGNED_CLUSTER)
                    outAssignments[triangle] = clusterBase;
            clusterBase = static_cast<std::uint16_t>(clusterBase + seeds.size());
        }
        outClusterCount = clusterBase;
    }

    if (!outClusterCount)
        return false;
    if (m_Cache.size() >= 128)
        m_Cache.erase(m_Cache.begin());
    m_Cache.push_back({geometry, geometry->triangles_size, geometry->vertices_size, fragments, seed, signature, outAssignments});
    return true;
}

CClientBreakEffect* CClientBreakEffectManager::CreateFromObject(CClientManager* pManager, CClientObject* pObject, ElementID ID,
                                                                 const SManagedBreakOptions& requestedOptions)
{
    if (!pManager || !pObject || !pObject->GetGameObject() || m_List.size() >= MAX_ACTIVE_EFFECTS)
        return nullptr;
    if (ID != INVALID_ELEMENT_ID && Get(ID))
        return nullptr;

    SManagedBreakOptions options = requestedOptions;
    if (!std::isfinite(options.force) || !std::isfinite(options.randomness) || !std::isfinite(options.gravity) || !std::isfinite(options.bounce) ||
        !std::isfinite(options.drag) || !std::isfinite(options.renderDistance) || options.force < 0.0f || options.randomness < 0.0f ||
        options.gravity < 0.0f || options.bounce < 0.0f || options.bounce > 1.5f || options.drag < 0.0f || options.renderDistance <= 0.0f ||
        options.lifetimeMs == 0)
        return nullptr;

    pObject->ApplySAMPObjectMaterialsForRender();
    void* rwObject = static_cast<void*>(pObject->GetGameObject()->GetRpClump());
    std::vector<SAtomicSource> sources;
    CollectAtomics(rwObject, sources);
    if (sources.empty() || sources.size() > MAX_FRAGMENTS_PER_EFFECT)
    {
        pObject->RestoreSAMPObjectMaterialsAfterRender();
        return nullptr;
    }

    std::size_t sourceTriangleCount = 0;
    for (const SAtomicSource& source : sources)
        sourceTriangleCount += static_cast<std::size_t>(source.triangles);

    const std::size_t activeFragments = GetActiveFragmentCount();
    const std::size_t freeFragments = activeFragments < MAX_ACTIVE_FRAGMENTS ? MAX_ACTIVE_FRAGMENTS - activeFragments : 0;
    if (freeFragments < sources.size())
    {
        pObject->RestoreSAMPObjectMaterialsAfterRender();
        return nullptr;
    }

    std::size_t targetFragments = options.fragments ? options.fragments : ChooseAutomaticFragmentCount(sourceTriangleCount);
    targetFragments = std::clamp<std::size_t>(targetFragments, sources.size(), MAX_FRAGMENTS_PER_EFFECT);
    targetFragments = std::min(targetFragments, freeFragments);

    std::vector<std::size_t> allocations(sources.size(), 1);
    std::size_t remaining = targetFragments - sources.size();
    while (remaining > 0)
    {
        std::size_t best = sources.size();
        float bestScore = -1.0f;
        for (std::size_t i = 0; i < sources.size(); ++i)
        {
            if (allocations[i] >= static_cast<std::size_t>(sources[i].triangles))
                continue;
            const float score = static_cast<float>(sources[i].triangles) / static_cast<float>(allocations[i]);
            if (score > bestScore)
            {
                bestScore = score;
                best = i;
            }
        }
        if (best == sources.size())
            break;
        ++allocations[best];
        --remaining;
    }

    std::vector<SBreakEffectChunk> chunks;
    chunks.reserve(targetFragments);
    std::unordered_set<RwTexture*> uniqueTextures;
    bool allCacheHits = true;
    std::size_t emittedTriangles = 0;

    for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex)
    {
        SAtomicSource& source = sources[sourceIndex];
        std::vector<std::uint16_t> assignments;
        std::size_t clusterCount = 0;
        bool cacheHit = false;
        if (!BuildClusterAssignments(source.geometry, allocations[sourceIndex], options.seed + static_cast<std::uint32_t>(sourceIndex * 977u), assignments,
                                     clusterCount, cacheHit))
        {
            pObject->RestoreSAMPObjectMaterialsAfterRender();
            return nullptr;
        }
        allCacheHits = allCacheHits && cacheHit;

        const std::size_t chunkOffset = chunks.size();
        chunks.resize(chunkOffset + clusterCount);
        RwFrame* frame = RpAtomicGetFrame(source.atomic);
        const RwMatrix matrix = frame ? frame->ltm : IdentityRwMatrix();
        RpGeometry* geometry = source.geometry;
        const RwV3d* vertices = geometry->morph_target->verts;

        for (int triangleIndex = 0; triangleIndex < geometry->triangles_size; ++triangleIndex)
        {
            const RpTriangle& triangle = geometry->triangles[triangleIndex];
            const std::size_t cluster = assignments[triangleIndex];
            if (cluster >= clusterCount)
                continue;
            RpMaterial* material = nullptr;
            if (geometry->materials.materials && triangle.materialId < geometry->materials.entries)
                material = geometry->materials.materials[triangle.materialId];
            RwTexture* texture = material ? material->texture : nullptr;
            if (texture)
                uniqueTextures.insert(texture);

            SBreakEffectChunk& chunk = chunks[chunkOffset + cluster];
            auto batchIt = std::find_if(chunk.batches.begin(), chunk.batches.end(), [texture](const SBreakEffectBatch& batch) { return batch.texture == texture; });
            if (batchIt == chunk.batches.end())
            {
                chunk.batches.push_back({});
                batchIt = std::prev(chunk.batches.end());
                batchIt->texture = texture;
            }
            for (int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
            {
                const unsigned short index = triangle.verts[vertexIndex];
                SBreakEffectVertex vertex;
                vertex.localPosition = TransformPoint(matrix, vertices[index]);
                if (geometry->texcoords[0])
                {
                    vertex.u = geometry->texcoords[0][index].u;
                    vertex.v = geometry->texcoords[0][index].v;
                }
                vertex.color = PackColor(geometry->colors ? &geometry->colors[index] : nullptr, material);
                batchIt->vertices.push_back(vertex);
            }
            ++emittedTriangles;
        }
    }

    pObject->RestoreSAMPObjectMaterialsAfterRender();
    if (chunks.empty() || emittedTriangles != sourceTriangleCount)
        return nullptr;

    CVector origin;
    pObject->GetPosition(origin);
    const CVector impact = options.hasImpactPosition ? options.impactPosition : origin;
    std::uint32_t randomState = options.seed ^ static_cast<std::uint32_t>(pObject->GetModel() * 2654435761u);

    for (SBreakEffectChunk& chunk : chunks)
    {
        CVector sum(0.0f, 0.0f, 0.0f);
        std::size_t samples = 0;
        for (const SBreakEffectBatch& batch : chunk.batches)
            for (const SBreakEffectVertex& vertex : batch.vertices)
            {
                sum.fX += vertex.localPosition.fX;
                sum.fY += vertex.localPosition.fY;
                sum.fZ += vertex.localPosition.fZ;
                ++samples;
            }
        if (!samples)
            continue;

        chunk.position = CVector(sum.fX / samples, sum.fY / samples, sum.fZ / samples);
        chunk.radius = 0.05f;
        for (SBreakEffectBatch& batch : chunk.batches)
            for (SBreakEffectVertex& vertex : batch.vertices)
            {
                vertex.localPosition.fX -= chunk.position.fX;
                vertex.localPosition.fY -= chunk.position.fY;
                vertex.localPosition.fZ -= chunk.position.fZ;
                chunk.radius = std::max(chunk.radius, std::sqrt(LengthSq(vertex.localPosition)));
            }

        CVector outward(chunk.position.fX - impact.fX, chunk.position.fY - impact.fY, chunk.position.fZ - impact.fZ);
        outward = NormalizeOr(outward, CVector(RandomRange(randomState, -1.0f, 1.0f), RandomRange(randomState, -1.0f, 1.0f),
                                              RandomRange(randomState, 0.15f, 1.0f)));
        chunk.velocity = CVector(options.velocity.fX + outward.fX * options.force + RandomRange(randomState, -options.randomness, options.randomness),
                                 options.velocity.fY + outward.fY * options.force + RandomRange(randomState, -options.randomness, options.randomness),
                                 options.velocity.fZ + outward.fZ * options.force + RandomRange(randomState, -options.randomness, options.randomness));
        chunk.rotationAxis = NormalizeOr(CVector(RandomRange(randomState, -1.0f, 1.0f), RandomRange(randomState, -1.0f, 1.0f),
                                                  RandomRange(randomState, -1.0f, 1.0f)), CVector(0.0f, 0.0f, 1.0f));
        chunk.rotationSpeed = RandomRange(randomState, 2.5f, 6.5f);
        CVector probe = chunk.position;
        chunk.groundZ = g_pGame && g_pGame->GetWorld() ? g_pGame->GetWorld()->FindGroundZFor3DPosition(&probe) : -1000.0f;
    }

    std::vector<RwTexture*> referencedTextures;
    referencedTextures.reserve(uniqueTextures.size());
    for (RwTexture* texture : uniqueTextures)
        if (texture)
        {
            ++texture->refs;
            referencedTextures.push_back(texture);
        }

    CClientBreakEffect* effect = new CClientBreakEffect(pManager, ID);
    if (!effect->Initialize(std::move(chunks), std::move(referencedTextures), origin, sourceTriangleCount, options.lifetimeMs, options.gravity,
                            options.bounce, options.drag, options.renderDistance, allCacheHits, pObject->GetDimension(), pObject->GetInterior()))
    {
        delete effect;
        return nullptr;
    }

    for (SBreakEffectChunk& chunk : effect->m_Chunks)
        g_ChunkSettleStates[&chunk] = BuildSettleState(chunk);

    if (options.hideOriginal)
        pObject->SetVisible(false);
    if (options.disableOriginalCollision)
        pObject->SetCollisionEnabled(false);
    return effect;
}

void CClientBreakEffectManager::DoPulse(CClientManager* pManager)
{
    if (!pManager || m_List.empty())
        return;
    const unsigned long long now = GetTickCount64_();
    if (!m_ullLastPulse)
    {
        m_ullLastPulse = now;
        return;
    }
    const float dt = std::min(static_cast<float>(now - m_ullLastPulse) / 1000.0f, 0.05f);
    m_ullLastPulse = now;
    ++m_uiPhysicsFrame;
    if (dt <= 0.0f)
        return;

    std::vector<CClientBreakEffect*> expired;
    for (CClientBreakEffect* effect : m_List)
    {
        if (!effect || effect->IsBeingDeleted())
            continue;
        if (now - effect->m_ullCreatedAt >= effect->m_uiLifetimeMs)
        {
            expired.push_back(effect);
            continue;
        }
        if (effect->m_bPaused)
            continue;

        for (std::size_t chunkIndex = 0; chunkIndex < effect->m_Chunks.size(); ++chunkIndex)
        {
            SBreakEffectChunk& chunk = effect->m_Chunks[chunkIndex];
            if (chunk.sleeping)
                continue;

            SChunkSettleState& settle = GetSettleState(chunk);
            const float damping = 1.0f / (1.0f + effect->m_fDrag * dt);
            chunk.velocity.fX *= damping;
            chunk.velocity.fY *= damping;
            chunk.velocity.fZ -= effect->m_fGravity * dt;
            chunk.position.fX += chunk.velocity.fX * dt;
            chunk.position.fY += chunk.velocity.fY * dt;
            chunk.position.fZ += chunk.velocity.fZ * dt;

            AdvanceVanillaRestOrientation(chunk, settle, dt);

            if (((m_uiPhysicsFrame + chunkIndex) & 7u) == 0u && g_pGame && g_pGame->GetWorld())
            {
                CVector probe = chunk.position;
                const float ground = g_pGame->GetWorld()->FindGroundZFor3DPosition(&probe);
                if (std::isfinite(ground))
                    chunk.groundZ = ground;
            }

            constexpr float GROUND_CONTACT_TOLERANCE = 0.02f;
            if (chunk.position.fZ - chunk.radius <= chunk.groundZ + GROUND_CONTACT_TOLERANCE)
            {
                float lowestWorldZ = std::numeric_limits<float>::max();
                for (const SBreakEffectBatch& batch : chunk.batches)
                    for (const SBreakEffectVertex& vertex : batch.vertices)
                    {
                        const CVector rotated = RotateAxisAngle(vertex.localPosition, chunk.rotationAxis, chunk.rotation);
                        lowestWorldZ = std::min(lowestWorldZ, chunk.position.fZ + rotated.fZ);
                    }

                if (lowestWorldZ <= chunk.groundZ + GROUND_CONTACT_TOLERANCE)
                {
                    settle.touchedGround = true;
                    if (lowestWorldZ < chunk.groundZ)
                        chunk.position.fZ += chunk.groundZ - lowestWorldZ;

                    // Vanilla BreakObject_c kills free rotation on collision and
                    // damps the reflected velocity hard. Keep the configurable
                    // restitution for the normal component, then apply GTA's
                    // ~0.8 post-collision velocity scale.
                    if (chunk.velocity.fZ < 0.0f)
                        chunk.velocity.fZ = -chunk.velocity.fZ * effect->m_fBounce;
                    chunk.velocity.fX *= 0.8f;
                    chunk.velocity.fY *= 0.8f;
                    chunk.velocity.fZ *= 0.8f;
                    chunk.rotationSpeed = 0.0f;

                    const float alignment = GetRestAlignment(chunk, settle);
                    const float speedSq = LengthSq(chunk.velocity);
                    if (speedSq < 0.12f)
                    {
                        chunk.velocity = CVector();
                        if (alignment >= 0.985f)
                        {
                            chunk.sleeping = true;
                        }
                    }
                }
            }
        }
    }
    for (CClientBreakEffect* effect : expired)
        if (effect && !effect->IsBeingDeleted())
            g_pClientGame->GetElementDeleter()->Delete(effect);
}

void CClientBreakEffectManager::DoRender(CClientManager* pManager)
{
    if (!pManager || m_List.empty() || !g_pCore || !g_pCore->GetGraphics())
        return;
    CClientPlayer* localPlayer = pManager->GetPlayerManager()->GetLocalPlayer();
    if (!localPlayer)
        return;
    IDirect3DDevice9* device = g_pCore->GetGraphics()->GetDevice();
    if (!device)
        return;

    IDirect3DStateBlock9* stateBlock = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &stateBlock)) || !stateBlock)
        return;
    stateBlock->Capture();

    const D3DMATRIX identity = IdentityD3DMatrix();
    device->SetTransform(D3DTS_WORLD, &identity);
    device->SetVertexShader(nullptr);
    device->SetPixelShader(nullptr);
    device->SetFVF(PrimitiveMaterialVertice::FNV);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ZENABLE, TRUE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    CVector cameraPosition;
    pManager->GetCamera()->GetPosition(cameraPosition);
    const unsigned short dimension = localPlayer->GetDimension();
    const unsigned char interior = localPlayer->GetInterior();
    const unsigned long long now = GetTickCount64_();
    std::vector<PrimitiveMaterialVertice> vertices;

    for (CClientBreakEffect* effect : m_List)
    {
        if (!effect || effect->IsBeingDeleted() || effect->GetDimension() != dimension || effect->GetInterior() != interior)
            continue;
        const CVector delta(effect->m_vecPosition.fX - cameraPosition.fX, effect->m_vecPosition.fY - cameraPosition.fY,
                            effect->m_vecPosition.fZ - cameraPosition.fZ);
        if (LengthSq(delta) > effect->m_fRenderDistance * effect->m_fRenderDistance)
            continue;

        const unsigned long long age = now - effect->m_ullCreatedAt;
        float fade = 1.0f;
        if (effect->m_uiLifetimeMs > 1000 && age + 1000 > effect->m_uiLifetimeMs)
            fade = std::clamp(static_cast<float>(effect->m_uiLifetimeMs - age) / 1000.0f, 0.0f, 1.0f);

        for (const SBreakEffectChunk& chunk : effect->m_Chunks)
            for (const SBreakEffectBatch& batch : chunk.batches)
            {
                if (batch.vertices.empty())
                    continue;
                vertices.clear();
                vertices.reserve(batch.vertices.size());
                for (const SBreakEffectVertex& source : batch.vertices)
                {
                    const CVector rotated = RotateAxisAngle(source.localPosition, chunk.rotationAxis, chunk.rotation);
                    std::uint32_t color = source.color;
                    const unsigned char originalAlpha = static_cast<unsigned char>((color >> 24) & 0xFF);
                    const unsigned char alpha = static_cast<unsigned char>(static_cast<float>(originalAlpha) * fade);
                    color = (color & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha) << 24);
                    vertices.push_back({chunk.position.fX + rotated.fX, chunk.position.fY + rotated.fY, chunk.position.fZ + rotated.fZ, color, source.u, source.v});
                }
                IDirect3DTexture9* texture = GetD3DTexture(batch.texture);
                device->SetTexture(0, texture);
                device->SetTextureStageState(0, D3DTSS_COLOROP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
                device->SetTextureStageState(0, D3DTSS_ALPHAOP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
                device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertices.size() / 3), vertices.data(), sizeof(PrimitiveMaterialVertice));
            }
    }

    stateBlock->Apply();
    stateBlock->Release();
}
