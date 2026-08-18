/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBreakEffectRender.cpp
 *  PURPOSE:     Safe queued rendering path for managed object fracture effects
 *
 *****************************************************************************/

#include <StdInc.h>
#include <core/CGraphicsInterface.h>
#include "CClientBreakEffectManager.h"
#include "CClientBreakEffect.h"

#include <algorithm>
#include <cmath>

namespace
{
    float LengthSq(const CVector& value)
    {
        return value.fX * value.fX + value.fY * value.fY + value.fZ * value.fZ;
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
}

void CClientBreakEffectManager::DoRenderQueued(CClientManager* pManager)
{
    if (!pManager || m_List.empty() || !g_pCore || !g_pCore->GetGraphics())
        return;

    CClientPlayer* localPlayer = pManager->GetPlayerManager()->GetLocalPlayer();
    if (!localPlayer)
        return;

    CVector cameraPosition;
    pManager->GetCamera()->GetPosition(cameraPosition);
    const unsigned short dimension = localPlayer->GetDimension();
    const unsigned char interior = localPlayer->GetInterior();
    const unsigned long long now = GetTickCount64_();

    auto* vertices = new std::vector<PrimitiveVertice>();

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
        {
            for (const SBreakEffectBatch& batch : chunk.batches)
            {
                for (const SBreakEffectVertex& source : batch.vertices)
                {
                    const CVector rotated = RotateAxisAngle(source.localPosition, chunk.rotationAxis, chunk.rotation);
                    std::uint32_t color = source.color;
                    const unsigned char sourceAlpha = static_cast<unsigned char>((color >> 24) & 0xFFu);
                    const unsigned char alpha = static_cast<unsigned char>(static_cast<float>(sourceAlpha) * fade);
                    color = (color & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha) << 24);
                    vertices->push_back({chunk.position.fX + rotated.fX, chunk.position.fY + rotated.fY, chunk.position.fZ + rotated.fZ, color});
                }
            }
        }
    }

    if (vertices->empty())
    {
        delete vertices;
        return;
    }

    g_pCore->GetGraphics()->DrawPrimitive3DQueued(vertices, D3DPT_TRIANGLELIST, eRenderStage::PRE_FX);
}
