/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        core/CRenderItem.Shader.cpp
 *  PURPOSE:
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CRenderItem.EffectCloner.h"
#include "CRenderItem.EffectTemplate.h"

uint CShaderItem::ms_uiCreateTimeCounter = 0;

////////////////////////////////////////////////////////////////
//
// CShaderItem::PostConstruct
//
//
//
////////////////////////////////////////////////////////////////
void CShaderItem::PostConstruct(CRenderItemManager* pManager, const SString& strFile, const SString& strRootPath, bool bIsRawData, SString& strOutStatus,
                                float fPriority, float fMaxDistance, bool bLayered, bool bDebug, int iTypeMask, const EffectMacroList& macros)
{
    m_fPriority = fPriority;
    m_uiCreateTime = ms_uiCreateTimeCounter++;  // Priority tie breaker
    m_fMaxDistanceSq = fMaxDistance * fMaxDistance;
    m_bLayered = bLayered;
    m_iTypeMask = iTypeMask;
    if (bIsRawData)
        m_strSourceIdentifier = "<raw-data>";
    else
        m_strSourceIdentifier = strFile;

    Super::PostConstruct(pManager);

    // Initial creation of d3d data
    CreateUnderlyingData(strFile, strRootPath, bIsRawData, strOutStatus, bDebug, macros);
}

namespace
{
    SString EffectClassToString(D3DXPARAMETER_CLASS parameterClass)
    {
        switch (parameterClass)
        {
            case D3DXPC_SCALAR:
                return "scalar";
            case D3DXPC_VECTOR:
                return "vector";
            case D3DXPC_MATRIX_ROWS:
                return "matrix_rows";
            case D3DXPC_MATRIX_COLUMNS:
                return "matrix_columns";
            case D3DXPC_OBJECT:
                return "object";
            case D3DXPC_STRUCT:
                return "struct";
            default:
                return "unknown";
        }
    }

    SString EffectTypeToString(D3DXPARAMETER_TYPE parameterType)
    {
        switch (parameterType)
        {
            case D3DXPT_VOID:
                return "void";
            case D3DXPT_BOOL:
                return "bool";
            case D3DXPT_INT:
                return "int";
            case D3DXPT_FLOAT:
                return "float";
            case D3DXPT_STRING:
                return "string";
            case D3DXPT_TEXTURE:
                return "texture";
            case D3DXPT_TEXTURE1D:
                return "texture1d";
            case D3DXPT_TEXTURE2D:
                return "texture2d";
            case D3DXPT_TEXTURE3D:
                return "texture3d";
            case D3DXPT_TEXTURECUBE:
                return "texturecube";
            case D3DXPT_SAMPLER:
                return "sampler";
            case D3DXPT_SAMPLER1D:
                return "sampler1d";
            case D3DXPT_SAMPLER2D:
                return "sampler2d";
            case D3DXPT_SAMPLER3D:
                return "sampler3d";
            case D3DXPT_SAMPLERCUBE:
                return "samplercube";
            case D3DXPT_PIXELSHADER:
                return "pixelshader";
            case D3DXPT_VERTEXSHADER:
                return "vertexshader";
            case D3DXPT_PIXELFRAGMENT:
                return "pixelfragment";
            case D3DXPT_VERTEXFRAGMENT:
                return "vertexfragment";
            case D3DXPT_UNSUPPORTED:
                return "unsupported";
            default:
                return "unknown";
        }
    }

    SString ShaderVersionToString(DWORD version)
    {
        return SString("%d.%d", D3DSHADER_VERSION_MAJOR(version), D3DSHADER_VERSION_MINOR(version));
    }
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::GetDiagnostics
//
// Copies effect metadata into a pointer-free structure suitable for Lua.
//
////////////////////////////////////////////////////////////////
void CShaderItem::GetDiagnostics(SShaderDiagnostics& outDiagnostics) const
{
    outDiagnostics = SShaderDiagnostics();
    outDiagnostics.strSourceIdentifier = m_strSourceIdentifier;

    if (!m_pEffectWrap || !m_pEffectWrap->m_pEffectTemplate)
        return;

    CEffectTemplate* pTemplate = m_pEffectWrap->m_pEffectTemplate;
    ID3DXEffect*     pEffect = pTemplate->m_pD3DEffect;
    if (!pEffect)
        return;

    outDiagnostics.bCompiled = true;
    outDiagnostics.bUsesVertexShader = pTemplate->m_bUsesVertexShader;
    outDiagnostics.bUsesDepthBuffer = pTemplate->m_bUsesDepthBuffer;
    outDiagnostics.bUsesMultipleRenderTargets = !pTemplate->m_SecondaryRenderTargetList.empty();
    outDiagnostics.lCreateHResult = pTemplate->m_CreateHResult;
    outDiagnostics.strCompileLog = pTemplate->m_strCompileLog;
    outDiagnostics.strSelectedTechnique = pTemplate->m_strTechniqueName;
    outDiagnostics.strVertexShaderProfile = ShaderVersionToString(g_pDeviceState->DeviceCaps.VertexShaderVersion);
    outDiagnostics.strPixelShaderProfile = ShaderVersionToString(g_pDeviceState->DeviceCaps.PixelShaderVersion);

    D3DXEFFECT_DESC effectDesc{};
    if (FAILED(pEffect->GetDesc(&effectDesc)))
        return;

    outDiagnostics.techniques.reserve(effectDesc.Techniques);
    for (UINT i = 0; i < effectDesc.Techniques; ++i)
    {
        D3DXHANDLE         hTechnique = pEffect->GetTechnique(i);
        D3DXTECHNIQUE_DESC techniqueDesc{};
        if (!hTechnique || FAILED(pEffect->GetTechniqueDesc(hTechnique, &techniqueDesc)))
            continue;

        SShaderDiagnostics::STechnique technique;
        technique.strName = techniqueDesc.Name ? techniqueDesc.Name : "";
        technique.uiPassCount = techniqueDesc.Passes;
        technique.bValid = SUCCEEDED(pEffect->ValidateTechnique(hTechnique));
        outDiagnostics.techniques.push_back(std::move(technique));
    }

    outDiagnostics.parameters.reserve(effectDesc.Parameters);
    for (UINT i = 0; i < effectDesc.Parameters; ++i)
    {
        D3DXHANDLE         hParameter = pEffect->GetParameter(nullptr, i);
        D3DXPARAMETER_DESC parameterDesc{};
        if (!hParameter || FAILED(pEffect->GetParameterDesc(hParameter, &parameterDesc)))
            continue;

        SShaderDiagnostics::SParameter parameter;
        parameter.strName = parameterDesc.Name ? parameterDesc.Name : "";
        parameter.strSemantic = parameterDesc.Semantic ? parameterDesc.Semantic : "";
        for (uint annotationIndex = 0; annotationIndex < parameterDesc.Annotations; ++annotationIndex)
        {
            D3DXHANDLE         hAnnotation = pEffect->GetAnnotation(hParameter, annotationIndex);
            D3DXPARAMETER_DESC annotationDesc = {};
            if (!hAnnotation || FAILED(pEffect->GetParameterDesc(hAnnotation, &annotationDesc)) || !annotationDesc.Name ||
                !SStringX(annotationDesc.Name).CompareI("mtaSemantic"))
                continue;

            LPCSTR szValue = nullptr;
            if (SUCCEEDED(pEffect->GetString(hAnnotation, &szValue)) && szValue)
                parameter.strAutomaticSemantic = szValue;
            break;
        }
        parameter.strClass = EffectClassToString(parameterDesc.Class);
        parameter.strType = EffectTypeToString(parameterDesc.Type);
        parameter.uiRows = parameterDesc.Rows;
        parameter.uiColumns = parameterDesc.Columns;
        parameter.uiElements = parameterDesc.Elements;
        parameter.uiAnnotations = parameterDesc.Annotations;
        outDiagnostics.parameters.push_back(std::move(parameter));
    }
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::PreDestruct
//
//
//
////////////////////////////////////////////////////////////////
void CShaderItem::PreDestruct()
{
    ReleaseUnderlyingData();
    Super::PreDestruct();
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::IsValid
//
// Check underlying data is present
//
////////////////////////////////////////////////////////////////
bool CShaderItem::IsValid()
{
    return m_pEffectWrap;
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::OnLostDevice
//
// Release device stuff
//
////////////////////////////////////////////////////////////////
void CShaderItem::OnLostDevice()
{
    // Nothing required for CShaderItem
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::OnResetDevice
//
// Recreate device stuff
//
////////////////////////////////////////////////////////////////
void CShaderItem::OnResetDevice()
{
    // Nothing required for CShaderItem
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::CreateUnderlyingData
//
//
//
////////////////////////////////////////////////////////////////
void CShaderItem::CreateUnderlyingData(const SString& strFile, const SString& strRootPath, bool bIsRawData, SString& strOutStatus, bool bDebug,
                                       const EffectMacroList& macros)
{
    assert(!m_pEffectWrap);
    assert(!m_pShaderInstance);

    m_pEffectWrap = m_pManager->GetEffectCloner()->CreateD3DEffect(strFile, strRootPath, bIsRawData, strOutStatus, bDebug, macros);
    if (!m_pEffectWrap)
        return;

    m_pManager->NotifyShaderItemUsesDepthBuffer(this, m_pEffectWrap->m_pEffectTemplate->m_bUsesDepthBuffer);
    m_pManager->NotifyShaderItemUsesMultipleRenderTargets(this, !m_pEffectWrap->m_pEffectTemplate->m_SecondaryRenderTargetList.empty());

    // Create instance to store param values
    RenewShaderInstance();
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::ReleaseUnderlyingData
//
//
//
////////////////////////////////////////////////////////////////
void CShaderItem::ReleaseUnderlyingData()
{
    m_pManager->NotifyShaderItemUsesDepthBuffer(this, false);
    m_pManager->NotifyShaderItemUsesMultipleRenderTargets(this, false);
    if (m_pEffectWrap)
        m_pManager->GetEffectCloner()->ReleaseD3DEffect(m_pEffectWrap);
    SAFE_RELEASE(m_pShaderInstance);
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::SetValue
//
// Set one texture
//
////////////////////////////////////////////////////////////////
bool CShaderItem::SetValue(const SString& strName, CTextureItem* pTextureItem)
{
    if (D3DXHANDLE* phParameter = MapFind(m_pEffectWrap->m_pEffectTemplate->m_textureHandleMap, strName.ToUpper()))
    {
        // Check if value is changing
        if (!m_pShaderInstance->CmpTextureValue(*phParameter, pTextureItem))
        {
            // Check if we need a new shader instance
            MaybeRenewShaderInstance();

            if (*phParameter == m_pEffectWrap->m_pEffectTemplate->m_hFirstTexture)
            {
                // Mirror size of first texture declared in effect file
                m_uiSizeX = pTextureItem->m_uiSizeX;
                m_uiSizeY = pTextureItem->m_uiSizeY;
                m_pShaderInstance->m_uiSizeX = m_uiSizeX;
                m_pShaderInstance->m_uiSizeY = m_uiSizeY;
            }

            m_pShaderInstance->SetTextureValue(*phParameter, pTextureItem);
        }
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::SetValue
//
// Set one bool
//
////////////////////////////////////////////////////////////////
bool CShaderItem::SetValue(const SString& strName, bool bValue)
{
    if (D3DXHANDLE* phParameter = MapFind(m_pEffectWrap->m_pEffectTemplate->m_valueHandleMap, strName.ToUpper()))
    {
        // Check if value is changing
        if (!m_pShaderInstance->CmpBoolValue(*phParameter, bValue))
        {
            // Check if we need a new shader instance
            MaybeRenewShaderInstance();
            m_pShaderInstance->SetBoolValue(*phParameter, bValue);
        }
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::SetValue
//
// Set up to 16 floats
//
////////////////////////////////////////////////////////////////
bool CShaderItem::SetValue(const SString& strName, const float* pfValues, uint uiCount)
{
    if (D3DXHANDLE* phParameter = MapFind(m_pEffectWrap->m_pEffectTemplate->m_valueHandleMap, strName.ToUpper()))
    {
        // Check if value is changing
        if (!m_pShaderInstance->CmpFloatsValue(*phParameter, pfValues, uiCount))
        {
            // Check if we need a new shader instance
            MaybeRenewShaderInstance();
            m_pShaderInstance->SetFloatsValue(*phParameter, pfValues, uiCount);
        }
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::SetTessellation
//
//
//
////////////////////////////////////////////////////////////////
void CShaderItem::SetTessellation(uint uiTessellationX, uint uiTessellationY)
{
    // Check if value is changing
    if (uiTessellationX != m_pShaderInstance->m_uiTessellationX || uiTessellationY != m_pShaderInstance->m_uiTessellationY)
    {
        // Check if we need a new shader instance
        MaybeRenewShaderInstance();
        m_pShaderInstance->m_uiTessellationX = uiTessellationX;
        m_pShaderInstance->m_uiTessellationY = uiTessellationY;
    }
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::SetTransform
//
//
//
////////////////////////////////////////////////////////////////
void CShaderItem::SetTransform(const SShaderTransform& transform)
{
    // Check if value is changing
    if (memcmp(&m_pShaderInstance->m_Transform, &transform, sizeof(transform)) != 0)
    {
        // Check if we need a new shader instance
        MaybeRenewShaderInstance();
        m_pShaderInstance->m_Transform = transform;
        m_pShaderInstance->m_bHasModifiedTransform = true;
    }
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::MaybeRenewShaderInstance
//
// If current instance is in use by something else (i.e. in draw queue), we must create a new instance before changing parameter values
//
////////////////////////////////////////////////////////////////
void CShaderItem::MaybeRenewShaderInstance()
{
    if (m_pShaderInstance->m_iRefCount > 1)
        RenewShaderInstance();
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::RenewShaderInstance
//
// Create/clone a new instance
//
////////////////////////////////////////////////////////////////
void CShaderItem::RenewShaderInstance()
{
    CShaderInstance* pShaderInstance = new CShaderInstance();
    pShaderInstance->PostConstruct(m_pManager, this);
}

////////////////////////////////////////////////////////////////
//
// CShaderItem::GetUsesVertexShader
//
// Check if active technique uses a vertex shader
//
////////////////////////////////////////////////////////////////
bool CShaderItem::GetUsesVertexShader()
{
    return m_pEffectWrap->m_pEffectTemplate->m_bUsesVertexShader;
}
