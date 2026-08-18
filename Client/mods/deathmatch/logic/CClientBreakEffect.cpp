/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBreakEffect.cpp
 *  PURPOSE:     Client managed object fracture effect element
 *
 *****************************************************************************/

#include <StdInc.h>
#include <game/CRenderWare.h>
#include "CClientBreakEffect.h"
#include "CClientBreakEffectManager.h"

CClientBreakEffect::CClientBreakEffect(CClientManager* pManager, ElementID ID) : CClientEntity(ID)
{
    m_pManager = pManager;
    m_pBreakEffectManager = &CClientBreakEffectManager::GetSingleton();
    SetTypeName("break-effect");
    m_pBreakEffectManager->AddToList(this);
}

CClientBreakEffect::~CClientBreakEffect()
{
    for (RwTexture* texture : m_ReferencedTextures)
    {
        if (texture && g_pGame && g_pGame->GetRenderWare())
            g_pGame->GetRenderWare()->ReleaseTextureReference(texture);
    }
    m_ReferencedTextures.clear();
    Unlink();
}

bool CClientBreakEffect::Initialize(std::vector<SBreakEffectChunk>&& chunks, std::vector<RwTexture*>&& referencedTextures, const CVector& origin,
                                    std::size_t sourceTriangleCount, std::uint32_t lifetimeMs, float gravity, float bounce, float drag,
                                    float renderDistance, bool cacheHit, unsigned short dimension, unsigned char interior)
{
    if (chunks.empty() || !sourceTriangleCount || !lifetimeMs || !std::isfinite(gravity) || !std::isfinite(bounce) || !std::isfinite(drag) ||
        !std::isfinite(renderDistance) || gravity < 0.0f || bounce < 0.0f || bounce > 1.5f || drag < 0.0f || renderDistance <= 0.0f)
        return false;

    m_Chunks = std::move(chunks);
    m_ReferencedTextures = std::move(referencedTextures);
    m_vecPosition = origin;
    m_uiSourceTriangleCount = sourceTriangleCount;
    m_uiLifetimeMs = lifetimeMs;
    m_fGravity = gravity;
    m_fBounce = bounce;
    m_fDrag = drag;
    m_fRenderDistance = renderDistance;
    m_bCacheHit = cacheHit;
    m_ullCreatedAt = GetTickCount64_();
    SetDimension(dimension);
    SetInterior(interior);
    return true;
}

void CClientBreakEffect::SetPosition(const CVector& position)
{
    if (!std::isfinite(position.fX) || !std::isfinite(position.fY) || !std::isfinite(position.fZ))
        return;

    const CVector delta(position.fX - m_vecPosition.fX, position.fY - m_vecPosition.fY, position.fZ - m_vecPosition.fZ);
    for (SBreakEffectChunk& chunk : m_Chunks)
    {
        chunk.position.fX += delta.fX;
        chunk.position.fY += delta.fY;
        chunk.position.fZ += delta.fZ;
        chunk.groundZ += delta.fZ;
    }
    m_vecPosition = position;
}

std::size_t CClientBreakEffect::GetSleepingFragmentCount() const
{
    std::size_t count = 0;
    for (const SBreakEffectChunk& chunk : m_Chunks)
    {
        if (chunk.sleeping)
            ++count;
    }
    return count;
}

void CClientBreakEffect::Unlink()
{
    if (m_pBreakEffectManager)
    {
        m_pBreakEffectManager->RemoveFromList(this);
        m_pBreakEffectManager = nullptr;
    }
}
