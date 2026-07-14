/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/CBuilding.h
 *  PURPOSE:     Object entity class
 *
 *  Multi Theft Auto is available from http://www.multitheftauto.com/
 *
 *****************************************************************************/
#pragma once

#include "CElement.h"
#include "CEvents.h"

#include "CEasingCurve.h"
#include "TInterpolation.h"
#include "CPositionRotationAnimation.h"

class CBuildingManager;

class CBuilding final : public CElement
{
    friend class CPlayer;

public:
    explicit CBuilding(CElement* pParent, class CBuildingManager* pObjectManager);
    explicit CBuilding(const CBuilding& Copy);
    ~CBuilding();
    CElement* Clone(bool* bAddEntity, CResource* pResource) override;

    bool IsEntity() override { return true; }

    void Unlink() override;

    const CVector& GetPosition() override;
    void           SetPosition(const CVector& vecPosition) override;

    void GetRotation(CVector& vecRotation) override;
    void SetRotation(const CVector& vecRotation);

    void GetMatrix(CMatrix& matrix) override;
    void SetMatrix(const CMatrix& matrix) override;

    std::uint16_t GetModel() const noexcept { return m_model; }
    std::uint16_t GetSyncModel() const noexcept { return m_customModel != 0xFFFF ? m_customModel : m_model; }
    bool          HasCustomModel() const noexcept { return m_customModel != 0xFFFF; }
    void          SetModel(std::uint16_t model) noexcept
    {
        m_model = model;
        m_customModel = 0xFFFF;
    }
    void SetCustomModel(std::uint16_t model, std::uint16_t parentModel) noexcept
    {
        // Buildings retain the native parent for server-side IDE/world rules,
        // while clients synchronize the registry identity used for replacement.
        m_model = parentModel;
        m_customModel = model;
    }

    bool GetCollisionEnabled() const noexcept { return m_bCollisionsEnabled; }
    void SetCollisionEnabled(bool bCollisionEnabled) noexcept { m_bCollisionsEnabled = bCollisionEnabled; }

    bool       SetLowLodBuilding(CBuilding* pLowLodBuilding) noexcept;
    CBuilding* GetLowLodElement() const noexcept { return m_pLowLodBuilding; }

    CBuilding* GetHighLodBuilding() const { return m_pHighLodBuilding; }
    void       SetHighLodObject(CBuilding* pHighLodObject) { m_pHighLodBuilding = pHighLodObject; }

protected:
    bool ReadSpecialData(const int iLine) override;

private:
    CBuildingManager* m_pBuildingManager;
    CVector           m_vecRotation;
    std::uint16_t     m_model;
    std::uint16_t     m_customModel = 0xFFFF;

protected:
    bool m_bCollisionsEnabled;

    CBuilding* m_pLowLodBuilding;
    CBuilding* m_pHighLodBuilding;
};
