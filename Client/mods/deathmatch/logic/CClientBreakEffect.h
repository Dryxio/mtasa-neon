/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBreakEffect.h
 *  PURPOSE:     Client managed object fracture effect element
 *
 *****************************************************************************/

#pragma once

#include "CClientEntity.h"
#include <cstddef>
#include <cstdint>
#include <vector>

struct RwTexture;
class CClientBreakEffectManager;

struct SBreakEffectColor
{
    std::uint32_t packed = 0xFFFFFFFFu;

    SBreakEffectColor() = default;
    SBreakEffectColor(std::uint32_t color) { *this = color; }

    SBreakEffectColor& operator=(std::uint32_t color)
    {
        // GTA's native BreakObject_c does not render raw prelight directly. It
        // bakes the current frame ambient colour into every fragment vertex in
        // SetBreakInfo before drawing with lighting disabled. Match that here so
        // managed fragments keep the same apparent lighting as the source mesh.
        // AmbientLightColourForFrame, GTA SA 1.0 US: 0xC886D4.
        const float* const ambient = reinterpret_cast<const float*>(0xC886D4);
        const auto addAmbient = [](std::uint32_t channel, float ambientChannel) -> std::uint32_t
        {
            const float lit = static_cast<float>(channel) + ambientChannel * 255.0f;
            if (lit <= 0.0f)
                return 0u;
            if (lit >= 255.0f)
                return 255u;
            return static_cast<std::uint32_t>(lit);
        };

        const std::uint32_t alpha = color & 0xFF000000u;
        const std::uint32_t red = addAmbient((color >> 16) & 0xFFu, ambient[0]);
        const std::uint32_t green = addAmbient((color >> 8) & 0xFFu, ambient[1]);
        const std::uint32_t blue = addAmbient(color & 0xFFu, ambient[2]);
        packed = alpha | (red << 16) | (green << 8) | blue;
        return *this;
    }

    operator std::uint32_t() const { return packed; }
};

struct SBreakEffectVertex
{
    CVector           localPosition;
    float             u = 0.0f;
    float             v = 0.0f;
    SBreakEffectColor color;
};

struct SBreakEffectBatch
{
    RwTexture*                       texture = nullptr;
    std::vector<SBreakEffectVertex> vertices;
};

struct SBreakEffectChunk
{
    CVector                        position;
    CVector                        velocity;
    CVector                        rotationAxis{0.0f, 0.0f, 1.0f};
    float                          rotation = 0.0f;
    float                          rotationSpeed = 0.0f;
    float                          radius = 0.1f;
    float                          groundZ = -1000.0f;
    bool                           sleeping = false;
    std::vector<SBreakEffectBatch> batches;
};

class CClientBreakEffect final : public CClientEntity
{
    friend class CClientBreakEffectManager;

public:
    CClientBreakEffect(CClientManager* pManager, ElementID ID);
    ~CClientBreakEffect();

    eClientEntityType GetType() const { return CCLIENTDUMMY; }

    void GetPosition(CVector& position) const override { position = m_vecPosition; }
    void SetPosition(const CVector& position) override;
    void Unlink();

    std::size_t GetFragmentCount() const { return m_Chunks.size(); }
    std::size_t GetSourceTriangleCount() const { return m_uiSourceTriangleCount; }
    std::size_t GetSleepingFragmentCount() const;
    bool        WasCacheHit() const { return m_bCacheHit; }
    bool        IsBeingDeleted() const { return const_cast<CClientBreakEffect*>(this)->CClientEntity::IsBeingDeleted(); }

    bool IsPaused() const { return m_bPaused; }
    void SetPaused(bool paused) { m_bPaused = paused; }

private:
    bool Initialize(std::vector<SBreakEffectChunk>&& chunks, std::vector<RwTexture*>&& referencedTextures, const CVector& origin,
                    std::size_t sourceTriangleCount, std::uint32_t lifetimeMs, float gravity, float bounce, float drag, float renderDistance,
                    bool cacheHit, unsigned short dimension, unsigned char interior);

    CClientBreakEffectManager*       m_pBreakEffectManager = nullptr;
    std::vector<SBreakEffectChunk>   m_Chunks;
    std::vector<RwTexture*>          m_ReferencedTextures;
    CVector                          m_vecPosition;
    std::size_t                      m_uiSourceTriangleCount = 0;
    unsigned long long               m_ullCreatedAt = 0;
    std::uint32_t                    m_uiLifetimeMs = 8000;
    float                            m_fGravity = 9.81f;
    float                            m_fBounce = 0.35f;
    float                            m_fDrag = 0.12f;
    float                            m_fRenderDistance = 350.0f;
    bool                             m_bCacheHit = false;
    bool                             m_bPaused = false;
};
