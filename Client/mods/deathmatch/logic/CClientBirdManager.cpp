/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.x
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/CClientBirdManager.cpp
 *  PURPOSE:     Managed bird simulation, rendering and gunshot policy
 *
 *****************************************************************************/

#include <StdInc.h>
#include "CClientBirdManager.h"
#include "CClientBird.h"
#include "CClientBreakEffectManager.h"
#include "CClientBreakEffect.h"
#include <core/CGraphicsInterface.h>
#include <cmath>
#include <limits>

namespace
{
    constexpr float TWO_PI = 6.28318530717958647692f;
    constexpr float HIT_RADIUS = 0.5f;

    float LengthSq(const CVector& value)
    {
        return value.fX * value.fX + value.fY * value.fY + value.fZ * value.fZ;
    }

    float DistanceSq(const CVector& a, const CVector& b)
    {
        const CVector d(a.fX - b.fX, a.fY - b.fY, a.fZ - b.fZ);
        return LengthSq(d);
    }

    CVector RotateBreakAxisAngle(const CVector& point, const CVector& axis, float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const float dot = point.fX * axis.fX + point.fY * axis.fY + point.fZ * axis.fZ;
        return CVector(point.fX * c + (axis.fY * point.fZ - axis.fZ * point.fY) * s + axis.fX * dot * (1.0f - c),
                       point.fY * c + (axis.fZ * point.fX - axis.fX * point.fZ) * s + axis.fY * dot * (1.0f - c),
                       point.fZ * c + (axis.fX * point.fY - axis.fY * point.fX) * s + axis.fZ * dot * (1.0f - c));
    }

    DWORD WithAlpha(DWORD color, unsigned char alpha)
    {
        return (color & 0x00FFFFFFu) | (static_cast<DWORD>(alpha) << 24);
    }

    void AddTriangle(std::vector<PrimitiveVertice>& vertices, const CVector& a, const CVector& b, const CVector& c, DWORD color)
    {
        vertices.push_back({a.fX, a.fY, a.fZ, color});
        vertices.push_back({b.fX, b.fY, b.fZ, color});
        vertices.push_back({c.fX, c.fY, c.fZ, color});
        vertices.push_back({c.fX, c.fY, c.fZ, color});
        vertices.push_back({b.fX, b.fY, b.fZ, color});
        vertices.push_back({a.fX, a.fY, a.fZ, color});
    }
}

CClientBirdManager& CClientBirdManager::GetSingleton()
{
    static CClientBirdManager manager;
    return manager;
}

CClientBirdManager::CClientBirdManager()
{
    if (g_pClientGame && g_pClientGame->GetEvents())
        g_pClientGame->GetEvents()->AddEvent("onClientBirdShot", "attacker, weapon, hitX, hitY, hitZ", nullptr, false);
}

CClientBird* CClientBirdManager::Create(CClientManager* pManager, ElementID ID)
{
    if (!pManager || m_List.size() >= MAX_MANAGED_BIRDS)
        return nullptr;

    if (ID != INVALID_ELEMENT_ID && Get(ID))
        return nullptr;

    return new CClientBird(pManager, ID);
}

CClientBird* CClientBirdManager::Get(ElementID ID)
{
    for (CClientBird* pBird : m_List)
    {
        if (pBird && pBird->GetID() == ID)
            return pBird;
    }
    return nullptr;
}

void CClientBirdManager::AddToList(CClientBird* pBird)
{
    m_List.push_back(pBird);
}

void CClientBirdManager::RemoveFromList(CClientBird* pBird)
{
    m_List.remove(pBird);
}

void CClientBirdManager::DoPulse(CClientManager* pManager)
{
    if (!pManager || m_List.empty())
        return;

    const unsigned long long now = GetTickCount64_();
    if (!m_ullLastPulse)
    {
        m_ullLastPulse = now;
        return;
    }

    const float dt = std::min(static_cast<float>(now - m_ullLastPulse) / 1000.0f, 0.1f);
    m_ullLastPulse = now;
    if (dt <= 0.0f)
        return;

    for (CClientBird* pBird : m_List)
    {
        if (!pBird || pBird->IsBeingDeleted() || !pBird->m_bMovementEnabled)
            continue;

        if (pBird->m_bCurvedFlight)
        {
            const float angle = dt / 10.0f;
            const float s = std::sin(angle);
            const float c = std::cos(angle);
            const float x = pBird->m_vecTargetVelocity.fX;
            const float y = pBird->m_vecTargetVelocity.fY;
            pBird->m_vecTargetVelocity.fX = c * x + s * y;
            pBird->m_vecTargetVelocity.fY = c * y - s * x;
        }

        const float blend = std::min(dt / 2.0f, 1.0f);
        pBird->m_vecVelocity.fX += (pBird->m_vecTargetVelocity.fX - pBird->m_vecVelocity.fX) * blend;
        pBird->m_vecVelocity.fY += (pBird->m_vecTargetVelocity.fY - pBird->m_vecVelocity.fY) * blend;
        pBird->m_vecVelocity.fZ += (pBird->m_vecTargetVelocity.fZ - pBird->m_vecVelocity.fZ) * blend;

        pBird->m_vecPosition.fX += pBird->m_vecVelocity.fX * dt;
        pBird->m_vecPosition.fY += pBird->m_vecVelocity.fY * dt;
        pBird->m_vecPosition.fZ += pBird->m_vecVelocity.fZ * dt;
    }
}

void CClientBirdManager::DoRender(CClientManager* pManager)
{
    if (!pManager || m_List.empty() || !g_pCore || !g_pCore->GetGraphics())
        return;

    CClientPlayer* pLocalPlayer = pManager->GetPlayerManager()->GetLocalPlayer();
    if (!pLocalPlayer)
        return;

    CVector cameraPosition;
    pManager->GetCamera()->GetPosition(cameraPosition);

    auto* vertices = new std::vector<PrimitiveVertice>();
    vertices->reserve(m_List.size() * 36);

    const unsigned short dimension = pLocalPlayer->GetDimension();
    const unsigned char interior = pLocalPlayer->GetInterior();
    const float now = static_cast<float>(GetTickCount64_());

    for (CClientBird* pBird : m_List)
    {
        if (!pBird || pBird->IsBeingDeleted() || pBird->GetDimension() != dimension || pBird->GetInterior() != interior)
            continue;

        const float distanceSq = DistanceSq(cameraPosition, pBird->m_vecPosition);
        const float maxDistance = pBird->m_fRenderDistance;
        if (distanceSq >= maxDistance * maxDistance)
            continue;

        const float distance = std::sqrt(distanceSq);
        unsigned char alpha = 255;
        const float fadeStart = maxDistance * 0.7f;
        if (distance > fadeStart)
        {
            const float fade = std::max(0.0f, 1.0f - (distance - fadeStart) / std::max(maxDistance - fadeStart, 0.001f));
            alpha = static_cast<unsigned char>(fade * 255.0f);
        }

        float dirX = pBird->m_vecTargetVelocity.fX;
        float dirY = pBird->m_vecTargetVelocity.fY;
        float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
        if (dirLen < 0.001f)
        {
            dirX = 0.0f;
            dirY = 1.0f;
            dirLen = 1.0f;
        }
        dirX /= dirLen;
        dirY /= dirLen;
        const float rightX = dirY;
        const float rightY = -dirX;

        const float size = pBird->m_fSize;
        const float phase = std::fmod(now, static_cast<float>(pBird->m_uiWingBeatTime)) / static_cast<float>(pBird->m_uiWingBeatTime) * TWO_PI;
        const float wingLift = std::sin(phase) * size * 0.55f;
        const float bob = std::sin(phase) * size * 0.1f;
        const CVector center(pBird->m_vecPosition.fX, pBird->m_vecPosition.fY, pBird->m_vecPosition.fZ + bob);

        const CVector nose(center.fX + dirX * size * 0.75f, center.fY + dirY * size * 0.75f, center.fZ);
        const CVector tail(center.fX - dirX * size * 0.75f, center.fY - dirY * size * 0.75f, center.fZ);
        const CVector leftWing(center.fX - rightX * size * 1.15f, center.fY - rightY * size * 1.15f, center.fZ + wingLift);
        const CVector rightWing(center.fX + rightX * size * 1.15f, center.fY + rightY * size * 1.15f, center.fZ + wingLift);
        const CVector leftMid(center.fX - rightX * size * 0.30f - dirX * size * 0.20f,
                              center.fY - rightY * size * 0.30f - dirY * size * 0.20f, center.fZ);
        const CVector rightMid(center.fX + rightX * size * 0.30f - dirX * size * 0.20f,
                               center.fY + rightY * size * 0.30f - dirY * size * 0.20f, center.fZ);

        const DWORD bodyColor = WithAlpha(pBird->m_ulBodyColor, alpha);
        const DWORD wingColor = WithAlpha(pBird->m_ulWingColor, alpha);
        AddTriangle(*vertices, nose, leftMid, tail, bodyColor);
        AddTriangle(*vertices, nose, tail, rightMid, bodyColor);
        AddTriangle(*vertices, center, leftWing, leftMid, wingColor);
        AddTriangle(*vertices, center, rightMid, rightWing, wingColor);
        AddTriangle(*vertices, tail, leftMid, rightMid, bodyColor);
    }

    if (vertices->empty())
    {
        delete vertices;
        return;
    }

    g_pCore->GetGraphics()->DrawPrimitive3DQueued(vertices, D3DPT_TRIANGLELIST, eRenderStage::PRE_FX);
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
                    const CVector rotated = RotateBreakAxisAngle(source.localPosition, chunk.rotationAxis, chunk.rotation);
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

bool CClientBirdManager::HandleGunShot(const CVector& start, const CVector& end, CClientEntity* pAttacker, int weapon)
{
    CClientManager* pManager = g_pClientGame ? g_pClientGame->GetManager() : nullptr;
    CClientPlayer* pLocalPlayer = pManager ? pManager->GetPlayerManager()->GetLocalPlayer() : nullptr;
    if (!pLocalPlayer)
        return false;

    const CVector segment(end.fX - start.fX, end.fY - start.fY, end.fZ - start.fZ);
    const float segmentLengthSq = LengthSq(segment);
    if (segmentLengthSq <= 0.000001f)
        return false;

    CClientBird* bestBird = nullptr;
    CVector bestHit;
    float bestT = std::numeric_limits<float>::max();

    for (CClientBird* pBird : m_List)
    {
        if (!pBird || pBird->IsBeingDeleted() || !pBird->m_bShootable || pBird->GetDimension() != pLocalPlayer->GetDimension() ||
            pBird->GetInterior() != pLocalPlayer->GetInterior())
            continue;

        const CVector toBird(pBird->m_vecPosition.fX - start.fX, pBird->m_vecPosition.fY - start.fY, pBird->m_vecPosition.fZ - start.fZ);
        float t = (toBird.fX * segment.fX + toBird.fY * segment.fY + toBird.fZ * segment.fZ) / segmentLengthSq;
        t = std::max(0.0f, std::min(t, 1.0f));
        const CVector closest(start.fX + segment.fX * t, start.fY + segment.fY * t, start.fZ + segment.fZ * t);
        if (DistanceSq(closest, pBird->m_vecPosition) > HIT_RADIUS * HIT_RADIUS || t >= bestT)
            continue;

        bestBird = pBird;
        bestHit = closest;
        bestT = t;
    }

    if (!bestBird)
        return false;

    CLuaArguments arguments;
    if (pAttacker)
        arguments.PushElement(pAttacker);
    else
        arguments.PushNil();
    arguments.PushNumber(weapon);
    arguments.PushNumber(bestHit.fX);
    arguments.PushNumber(bestHit.fY);
    arguments.PushNumber(bestHit.fZ);

    if (!bestBird->CallEvent("onClientBirdShot", arguments, true))
        return true;

    if (!bestBird->IsBeingDeleted())
        CStaticFunctionDefinitions::DestroyElement(*bestBird);
    return true;
}
