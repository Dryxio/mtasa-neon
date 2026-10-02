/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/shared_logic/luadefs/CLuaDrawingDefs.cpp
 *  PURPOSE:     Lua drawing definitions class
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaDefs.h"
#include "lua/CLuaFunctionParser.h"

#include <SharedUtil.SysInfo.h>
#include <SharedUtil.SysInfo.hpp>

#define MIN_CLIENT_REQ_DXSETRENDERTARGET_CALL_RESTRICTIONS "1.3.0-9.04431"
extern bool g_bAllowAspectRatioAdjustment;

void CLuaDrawingDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"dxDrawLine", DxDrawLine},
        {"dxDrawMaterialLine3D", DxDrawMaterialLine3D},
        {"dxDrawMaterialSectionLine3D", DxDrawMaterialSectionLine3D},
        {"dxDrawLine3D", DxDrawLine3D},
        {"dxDrawText", DxDrawText},
        {"dxDrawRectangle", DxDrawRectangle},
        {"dxDrawCircle", DxDrawCircle},
        {"dxDrawImage", DxDrawImage},
        {"dxDrawImageSection", DxDrawImageSection},
        {"dxDrawPrimitive", DxDrawPrimitive},
        {"dxDrawPrimitive3D", DxDrawPrimitive3D},
        {"dxDrawMaterialPrimitive", DxDrawMaterialPrimitive},
        {"dxDrawMaterialPrimitive3D", DxDrawMaterialPrimitive3D},
        {"dxDrawWiredSphere", ArgumentParser<DxDrawWiredSphere>},
        {"dxDrawModel3D", ArgumentParser<DxDrawModel3D>},
        {"dxGetTextWidth", DxGetTextWidth},
        {"dxGetTextSize", ArgumentParser<DxGetTextSize>},
        {"dxGetFontHeight", DxGetFontHeight},
        {"dxCreateFont", DxCreateFont},
        {"dxCreateTexture", DxCreateTexture},
        {"dxCreateShader", DxCreateShader},
        {"dxGetShaderDiagnostics", DxGetShaderDiagnostics},
        {"dxGetRenderStatistics", DxGetRenderStatistics},
        {"dxCreateRenderTarget", DxCreateRenderTarget},
        {"dxCreateDepthStencilTarget", DxCreateDepthStencilTarget},
        {"dxCreateMrtSet", DxCreateMrtSet},
        {"dxCreateSceneView", DxCreateSceneView},
        {"dxSetSceneViewCamera", DxSetSceneViewCamera},
        {"dxSetSceneViewMatrix", DxSetSceneViewMatrix},
        {"dxSetSceneViewOrthographicProjection", DxSetSceneViewOrthographicProjection},
        {"dxSetSceneViewPerspectiveProjection", DxSetSceneViewPerspectiveProjection},
        {"dxRequestSceneViewRender", DxRequestSceneViewRender},
        {"dxSetSceneViewUpdateMode", DxSetSceneViewUpdateMode},
        {"engineApplyShaderToSceneViewWorldTexture", EngineApplyShaderToSceneViewWorldTexture},
        {"engineRemoveShaderFromSceneViewWorldTexture", EngineRemoveShaderFromSceneViewWorldTexture},
        {"dxSetSceneViewOutputShader", DxSetSceneViewOutputShader},
        {"dxRemoveSceneViewOutputShader", DxRemoveSceneViewOutputShader},
        {"dxGetSceneViewTexture", DxGetSceneViewTexture},
        {"dxGetSceneViewInfo", DxGetSceneViewInfo},
        {"dxCreateCubemapRenderTarget", DxCreateCubemapRenderTarget},
        {"dxSetCubemapRenderTargetCamera", DxSetCubemapRenderTargetCamera},
        {"dxRequestCubemapRenderTargetRender", DxRequestCubemapRenderTargetRender},
        {"dxGetCubemapRenderTargetTexture", DxGetCubemapRenderTargetTexture},
        {"dxGetCubemapRenderTargetInfo", DxGetCubemapRenderTargetInfo},
        {"dxCreateScreenSource", DxCreateScreenSource},
        {"dxGetMaterialSize", DxGetMaterialSize},
        {"dxSetShaderValue", DxSetShaderValue},
        {"dxSetShaderDepthTextureValue", DxSetShaderDepthTextureValue},
        {"dxSetShaderTessellation", DxSetShaderTessellation},
        {"dxSetShaderTransform", DxSetShaderTransform},
        {"dxSetRenderTarget", DxSetRenderTarget},
        {"dxBeginRenderPass", DxBeginRenderPass},
        {"dxEndRenderPass", DxEndRenderPass},
        {"dxUpdateScreenSource", DxUpdateScreenSource},
        {"dxGetStatus", DxGetStatus},
        {"dxGetRenderCapabilities", DxGetRenderCapabilities},
        {"dxSetTestMode", DxSetTestMode},
        {"dxGetTexturePixels", DxGetTexturePixels},
        {"dxSetTexturePixels", DxSetTexturePixels},
        {"dxGetPixelsSize", DxGetPixelsSize},
        {"dxGetPixelsFormat", DxGetPixelsFormat},
        {"dxConvertPixels", DxConvertPixels},
        {"dxGetPixelColor", DxGetPixelColor},
        {"dxSetPixelColor", DxSetPixelColor},
        {"dxSetBlendMode", DxSetBlendMode},
        {"dxGetBlendMode", DxGetBlendMode},
        {"dxSetAspectRatioAdjustmentEnabled", DxSetAspectRatioAdjustmentEnabled},
        {"dxIsAspectRatioAdjustmentEnabled", DxIsAspectRatioAdjustmentEnabled},
        {"dxSetTextureEdge", DxSetTextureEdge},
    };

    // Add functions
    for (const auto& [name, func] : functions)
        CLuaCFunctions::AddFunction(name, func);
}

void CLuaDrawingDefs::AddClass(lua_State* luaVM)
{
    AddDxMaterialClass(luaVM);
    AddDxTextureClass(luaVM);
    AddDxFontClass(luaVM);
    AddDxShaderClass(luaVM);
    AddDxScreenSourceClass(luaVM);
    AddDxRenderTargetClass(luaVM);
    AddDxDepthStencilTargetClass(luaVM);
    AddDxMrtSetClass(luaVM);
    AddDxSceneViewClass(luaVM);
    AddDxCubemapRenderTargetClass(luaVM);
}

void CLuaDrawingDefs::AddDxMaterialClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "getSize", "dxGetMaterialSize");

    lua_registerclass(luaVM, "DxMaterial", "Element");
}

void CLuaDrawingDefs::AddDxTextureClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "dxCreateTexture");

    lua_classfunction(luaVM, "setEdge", "dxSetTextureEdge");
    lua_classfunction(luaVM, "setPixels", "dxSetTexturePixels");
    lua_classfunction(luaVM, "getPixels", "dxGetTexturePixels");

    lua_registerclass(luaVM, "DxTexture", "DxMaterial");
}

void CLuaDrawingDefs::AddDxFontClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "dxCreateFont");
    lua_classfunction(luaVM, "destroy", "destroyElement");

    lua_classfunction(luaVM, "getHeight", OOP_DxGetFontHeight);
    lua_classfunction(luaVM, "getTextWidth", OOP_DxGetTextWidth);
    lua_classfunction(luaVM, "getTextSize", ArgumentParser<OOP_DxGetTextSize>);

    lua_registerclass(luaVM, "DxFont");
}

void CLuaDrawingDefs::AddDxShaderClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "dxCreateShader");
    lua_classfunction(luaVM, "applyToWorldTexture", "engineApplyShaderToWorldTexture");
    lua_classfunction(luaVM, "removeFromWorldTexture", "engineRemoveShaderFromWorldTexture");
    lua_classfunction(luaVM, "applyToSceneViewWorldTexture", "engineApplyShaderToSceneViewWorldTexture");
    lua_classfunction(luaVM, "removeFromSceneViewWorldTexture", "engineRemoveShaderFromSceneViewWorldTexture");

    lua_classfunction(luaVM, "setValue", "dxSetShaderValue");
    lua_classfunction(luaVM, "setDepthTextureValue", "dxSetShaderDepthTextureValue");
    lua_classfunction(luaVM, "setTessellation", "dxSetShaderTessellation");
    lua_classfunction(luaVM, "setTransform", "dxSetShaderTransform");
    lua_classfunction(luaVM, "getDiagnostics", "dxGetShaderDiagnostics");

    // lua_classvariable ( luaVM, "value", CLuaOOPDefs::SetShaderValue, NULL); // .value["param"] = value
    lua_classvariable(luaVM, "tessellation", "dxSetShaderTessellation", NULL);

    lua_registerclass(luaVM, "DxShader", "DxMaterial");
}

void CLuaDrawingDefs::AddDxScreenSourceClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "dxCreateScreenSource");
    lua_classfunction(luaVM, "update", "dxUpdateScreenSource");

    lua_registerclass(luaVM, "DxScreenSource", "DxTexture");
}

void CLuaDrawingDefs::AddDxRenderTargetClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "dxCreateRenderTarget");
    lua_classfunction(luaVM, "setAsTarget", "dxSetRenderTarget");

    lua_registerclass(luaVM, "DxRenderTarget", "DxTexture");
}

void CLuaDrawingDefs::AddDxDepthStencilTargetClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "dxCreateDepthStencilTarget");

    // Not a DxTexture/DxMaterial - a depth-stencil target isn't drawable via dxDrawImage
    lua_registerclass(luaVM, "DxDepthStencilTarget", "Element");
}

void CLuaDrawingDefs::AddDxMrtSetClass(lua_State* luaVM)
{
    lua_newclass(luaVM);

    lua_classfunction(luaVM, "create", "dxCreateMrtSet");

    lua_registerclass(luaVM, "DxMrtSet", "Element");
}

void CLuaDrawingDefs::AddDxSceneViewClass(lua_State* luaVM)
{
    lua_newclass(luaVM);
    lua_classfunction(luaVM, "create", "dxCreateSceneView");
    lua_classfunction(luaVM, "setCamera", "dxSetSceneViewCamera");
    lua_classfunction(luaVM, "setMatrix", "dxSetSceneViewMatrix");
    lua_classfunction(luaVM, "setOrthographicProjection", "dxSetSceneViewOrthographicProjection");
    lua_classfunction(luaVM, "setPerspectiveProjection", "dxSetSceneViewPerspectiveProjection");
    lua_classfunction(luaVM, "requestRender", "dxRequestSceneViewRender");
    lua_classfunction(luaVM, "setUpdateMode", "dxSetSceneViewUpdateMode");
    lua_classfunction(luaVM, "setOutputShader", "dxSetSceneViewOutputShader");
    lua_classfunction(luaVM, "removeOutputShader", "dxRemoveSceneViewOutputShader");
    lua_classfunction(luaVM, "getTexture", "dxGetSceneViewTexture");
    lua_classfunction(luaVM, "getInfo", "dxGetSceneViewInfo");
    lua_registerclass(luaVM, "DxSceneView", "DxRenderTarget");
}

void CLuaDrawingDefs::AddDxCubemapRenderTargetClass(lua_State* luaVM)
{
    lua_newclass(luaVM);
    lua_classfunction(luaVM, "create", "dxCreateCubemapRenderTarget");
    lua_classfunction(luaVM, "setCamera", "dxSetCubemapRenderTargetCamera");
    lua_classfunction(luaVM, "requestRender", "dxRequestCubemapRenderTargetRender");
    lua_classfunction(luaVM, "getTexture", "dxGetCubemapRenderTargetTexture");
    lua_classfunction(luaVM, "getInfo", "dxGetCubemapRenderTargetInfo");
    lua_registerclass(luaVM, "DxCubemapRenderTarget", "DxTexture");
}

int CLuaDrawingDefs::DxDrawLine(lua_State* luaVM)
{
    //  bool dxDrawLine ( int startX, int startY, int endX, int endY, int color, [float width=1, bool postGUI=false] )
    CVector2D vecStart;
    CVector2D vecEnd;
    SColor    color;
    float     fWidth;
    bool      bPostGUI;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(vecStart);
    argStream.ReadVector2D(vecEnd);
    argStream.ReadColor(color, 0xFFFFFFFF);
    argStream.ReadNumber(fWidth, 1);
    argStream.ReadBool(bPostGUI, false);

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->DrawLineQueued(vecStart.fX, vecStart.fY, vecEnd.fX, vecEnd.fY, fWidth, color, bPostGUI);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawLine3D(lua_State* luaVM)
{
    //  bool dxDrawLine3D ( float startX, float startY, float startZ, float endX, float endY, float endZ, int color[, int width, bool postGUI ] )
    CVector      vecBegin;
    CVector      vecEnd;
    SColor       color;
    float        fWidth;
    eRenderStage renderStage{eRenderStage::POST_FX};

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector3D(vecBegin);
    argStream.ReadVector3D(vecEnd);
    argStream.ReadColor(color, 0xFFFFFFFF);
    argStream.ReadNumber(fWidth, 1);
    if (argStream.NextIsBool())
        renderStage = argStream.ReadBool() ? eRenderStage::POST_GUI : eRenderStage::POST_FX;
    else
        argStream.ReadIfNextIsEnumString(renderStage, eRenderStage::POST_FX);

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->DrawLine3DQueued(vecBegin, vecEnd, fWidth, color, renderStage);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawMaterialLine3D(lua_State* luaVM)
{
    //  bool dxDrawMaterialLine3D ( float startX, float startY, float startZ, float endX, float endY, float endZ, [bool flipUV,] element material, int width [,
    //  int color = white,
    //                          float faceX, float faceY, float faceZ ] )
    CVector          vecBegin;
    CVector          vecEnd;
    bool             bFlipUV;
    CClientMaterial* pMaterial;
    float            fWidth;
    SColor           color;
    CVector          vecFaceToward;
    bool             bUseFaceToward = false;
    eRenderStage     renderStage{eRenderStage::POST_FX};

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector3D(vecBegin);
    argStream.ReadVector3D(vecEnd);
    argStream.ReadIfNextIsBool(bFlipUV, false);
    argStream.ReadUserData(pMaterial);
    argStream.ReadNumber(fWidth);
    argStream.ReadColor(color, 0xFFFFFFFF);
    if (argStream.NextIsBool())
        renderStage = argStream.ReadBool() ? eRenderStage::POST_GUI : eRenderStage::POST_FX;
    else
        argStream.ReadIfNextIsEnumString(renderStage, eRenderStage::POST_FX);

    if (argStream.NextIsVector3D())
    {
        argStream.ReadVector3D(vecFaceToward);
        bUseFaceToward = true;
    }

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->DrawMaterialLine3DQueued(vecBegin, vecEnd, fWidth, color, pMaterial->GetMaterialItem(), 0, 0, 1, 1, true, bFlipUV,
                                                         bUseFaceToward, vecFaceToward, renderStage);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawMaterialSectionLine3D(lua_State* luaVM)
{
    //  bool dxDrawMaterialSectionLine3D ( float startX, float startY, float startZ, float endX, float endY, float endZ, float u, float v, float usize, float
    //  vsize,
    //                                  [bool flipUV,] element material, int width, [int color = white, float faceX, float faceY, float faceZ ] )
    CVector          vecBegin;
    CVector          vecEnd;
    CVector2D        vecSectionPos;
    CVector2D        vecSectionSize;
    bool             bFlipUV;
    CClientMaterial* pMaterial;
    float            fWidth;
    SColor           color;
    CVector          vecFaceToward;
    bool             bUseFaceToward = false;
    eRenderStage     renderStage{eRenderStage::POST_FX};

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector3D(vecBegin);
    argStream.ReadVector3D(vecEnd);
    argStream.ReadVector2D(vecSectionPos);
    argStream.ReadVector2D(vecSectionSize);
    argStream.ReadIfNextIsBool(bFlipUV, false);
    argStream.ReadUserData(pMaterial);
    argStream.ReadNumber(fWidth);
    argStream.ReadColor(color, 0xFFFFFFFF);
    if (argStream.NextIsBool())
        renderStage = argStream.ReadBool() ? eRenderStage::POST_GUI : eRenderStage::POST_FX;
    else
        argStream.ReadIfNextIsEnumString(renderStage, eRenderStage::POST_FX);

    if (argStream.NextIsVector3D())
    {
        argStream.ReadVector3D(vecFaceToward);
        bUseFaceToward = true;
    }

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->DrawMaterialLine3DQueued(vecBegin, vecEnd, fWidth, color, pMaterial->GetMaterialItem(), vecSectionPos.fX, vecSectionPos.fY,
                                                         vecSectionSize.fX, vecSectionSize.fY, false, bFlipUV, bUseFaceToward, vecFaceToward, renderStage);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawText(lua_State* luaVM)
{
    //  bool dxDrawText ( string text, float left, float top [, float right=left, float bottom=top, int color=white, float scale=1, mixed font="default",
    //      string alignX="left", string alignY="top", bool clip=false, bool wordBreak=false, bool postGUI=false, bool colorCoded=false, bool
    //      subPixelPositioning=false, float rotation=0, float rotationCenterX=(left+right)/2, float rotationCenterY=(top+bottom)/2] )
    SString            strText;
    CVector2D          vecTopLeft;
    CVector2D          vecBottomRight;
    SColor             color;
    float              fScaleX;
    float              fScaleY;
    eFontType          fontType;
    CClientDxFont*     pDxFontElement;
    eDXHorizontalAlign alignX;
    eDXVerticalAlign   alignY;
    bool               bClip;
    bool               bWordBreak;
    bool               bPostGUI;
    bool               bColorCoded;
    bool               bSubPixelPositioning;
    float              fRotation;
    CVector2D          vecRotationOrigin;
    float              fLineHeight;

    CScriptArgReader argStream(luaVM);
    argStream.ReadString(strText);
    argStream.ReadVector2D(vecTopLeft);
    if (argStream.NextIsUserDataOfType<CLuaVector2D>())
        argStream.ReadVector2D(vecBottomRight);
    else
    {
        argStream.ReadNumber(vecBottomRight.fX, vecTopLeft.fX);
        argStream.ReadNumber(vecBottomRight.fY, vecTopLeft.fY);
    }
    argStream.ReadColor(color, 0xFFFFFFFF);
    if (argStream.NextIsUserDataOfType<CLuaVector2D>())
    {
        CVector2D vecScale;
        argStream.ReadVector2D(vecScale);
        fScaleX = vecScale.fX;
        fScaleY = vecScale.fY;
    }
    else
    {
        argStream.ReadNumber(fScaleX, 1);
        if (argStream.NextIsNumber())
            argStream.ReadNumber(fScaleY);
        else
            fScaleY = fScaleX;
    }
    MixedReadDxFontString(argStream, fontType, FONT_DEFAULT, pDxFontElement);
    argStream.ReadEnumString(alignX, DX_ALIGN_LEFT);
    argStream.ReadEnumString(alignY, DX_ALIGN_TOP);
    argStream.ReadBool(bClip, false);
    argStream.ReadBool(bWordBreak, false);
    argStream.ReadBool(bPostGUI, false);
    argStream.ReadBool(bColorCoded, false);
    argStream.ReadBool(bSubPixelPositioning, false);
    argStream.ReadNumber(fRotation, 0);
    argStream.ReadVector2D(vecRotationOrigin, CVector2D((vecTopLeft.fX + vecBottomRight.fX) * 0.5f, (vecTopLeft.fY + vecBottomRight.fY) * 0.5f));
    argStream.ReadNumber(fLineHeight, 0);

    if (!argStream.HasErrors())
    {
        // Get DX font
        ID3DXFont* pD3DXFont = CStaticFunctionDefinitions::ResolveD3DXFont(fontType, pDxFontElement);

        // Make format flag
        ulong ulFormat = alignX | alignY;
        // if ( ulFormat & DT_BOTTOM ) ulFormat |= DT_SINGLELINE;        MS says we should do this. Nobody tells me what to do.
        if (bWordBreak)
            ulFormat |= DT_WORDBREAK;
        if (!bClip)
            ulFormat |= DT_NOCLIP;

        g_pCore->GetGraphics()->DrawStringQueued(vecTopLeft.fX, vecTopLeft.fY, vecBottomRight.fX, vecBottomRight.fY, color, strText, fScaleX, fScaleY, ulFormat,
                                                 pD3DXFont, bPostGUI, bColorCoded, bSubPixelPositioning, fRotation, vecRotationOrigin.fX, vecRotationOrigin.fY,
                                                 fLineHeight);

        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawRectangle(lua_State* luaVM)
{
    //  bool dxDrawRectangle ( float startX, float startY, float width, float height [, int color = white, bool postGUI = false, bool subPixelPositioning=false]
    //  )
    CVector2D vecPosition;
    CVector2D vecSize;
    SColor    color;
    bool      bPostGUI;
    bool      bSubPixelPositioning;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(vecPosition);
    argStream.ReadVector2D(vecSize);
    argStream.ReadColor(color, 0xFFFFFFFF);
    argStream.ReadBool(bPostGUI, false);
    argStream.ReadBool(bSubPixelPositioning, false);

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->DrawRectQueued(vecPosition.fX, vecPosition.fY, vecSize.fX, vecSize.fY, color, bPostGUI, bSubPixelPositioning);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawCircle(lua_State* luaVM)
{
    CVector2D vecPosition;
    float     fRadius;
    float     fStartAngle;
    float     fStopAngle;
    SColor    color;
    SColor    colorCenter;
    short     siSegments;
    float     fRatio;
    bool      bPostGUI;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(vecPosition);
    argStream.ReadNumber(fRadius);
    argStream.ReadNumber(fStartAngle, 0);
    argStream.ReadNumber(fStopAngle, 360);
    argStream.ReadColor(color, 0xFFFFFFFF);
    argStream.ReadColor(colorCenter, color);
    argStream.ReadNumber(siSegments, 32);
    argStream.ReadNumber(fRatio, 1);
    argStream.ReadBool(bPostGUI, false);

    if (!argStream.HasErrors())
    {
        const short siMinimumSegments = 3;
        const short siMaximumSegments = 1024;
        if (siSegments >= siMinimumSegments && siSegments <= siMaximumSegments)
        {
            const float fMinimumRatio = 0;
            const float fMaximumRatio = 100;
            if (fRatio > fMinimumRatio && fRatio <= fMaximumRatio)
            {
                if (fRadius > 0 && fStartAngle != fStopAngle)
                {
                    if (fStopAngle < fStartAngle)
                        std::swap(fStopAngle, fStartAngle);

                    // Clamp the angle, so we never draw more than 360 degrees
                    if (fStartAngle + 360.0f < fStopAngle)
                        fStopAngle = fStartAngle + 360.0f;

                    g_pCore->GetGraphics()->DrawCircleQueued(vecPosition.fX, vecPosition.fY, fRadius, fStartAngle, fStopAngle, color, colorCenter, siSegments,
                                                             fRatio, bPostGUI);
                    lua_pushboolean(luaVM, true);
                    return 1;
                }
            }
            else
            {
                lua_pushboolean(luaVM, false);
                return 1;
            }
        }
        else
        {
            lua_pushboolean(luaVM, false);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawImage(lua_State* luaVM)
{
    //  bool dxDrawImage ( float posX, float posY, float width, float height, string filepath [, float rotation = 0, float rotationCenterOffsetX = 0,
    //      float rotationCenterOffsetY = 0, int color = white, bool postGUI = false ] )

    CVector2D        vecPosition;
    CVector2D        vecSize;
    CClientMaterial* pMaterialElement;
    float            fRotation;
    CVector2D        vecRotationCenter;
    SColor           color;
    bool             bPostGUI;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(vecPosition);
    argStream.ReadVector2D(vecSize);
    MixedReadMaterialString(argStream, pMaterialElement);
    argStream.ReadNumber(fRotation, 0);
    argStream.ReadVector2D(vecRotationCenter, CVector2D());
    argStream.ReadColor(color, 0xffffffff);
    argStream.ReadBool(bPostGUI, false);

    if (!argStream.HasErrors())
    {
        if (pMaterialElement)
        {
            g_pCore->GetGraphics()->DrawTextureQueued(vecPosition.fX, vecPosition.fY, vecSize.fX, vecSize.fY, 0, 0, 1, 1, true,
                                                      pMaterialElement->GetMaterialItem(), fRotation, vecRotationCenter.fX, vecRotationCenter.fY, color,
                                                      bPostGUI);
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    if (argStream.HasErrors())
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawImageSection(lua_State* luaVM)
{
    //  bool dxDrawImageSection ( float posX, float posY, float width, float height, float u, float v, float usize, float vsize, string filepath,
    //      [ float rotation = 0, float rotationCenterOffsetX = 0, float rotationCenterOffsetY = 0, int color = white, bool postGUI = false ] )
    CVector2D        vecPosition;
    CVector2D        vecSize;
    CVector2D        vecSectionPosition;
    CVector2D        vecSectionSize;
    CClientMaterial* pMaterialElement;
    float            fRotation;
    CVector2D        vecRotationCenter;
    SColor           color;
    bool             bPostGUI;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(vecPosition);
    argStream.ReadVector2D(vecSize);
    argStream.ReadVector2D(vecSectionPosition);
    argStream.ReadVector2D(vecSectionSize);
    MixedReadMaterialString(argStream, pMaterialElement);
    argStream.ReadNumber(fRotation, 0);
    argStream.ReadVector2D(vecRotationCenter, CVector2D());
    argStream.ReadColor(color, 0xffffffff);
    argStream.ReadBool(bPostGUI, false);

    if (!argStream.HasErrors())
    {
        if (pMaterialElement)
        {
            g_pCore->GetGraphics()->DrawTextureQueued(vecPosition.fX, vecPosition.fY, vecSize.fX, vecSize.fY, vecSectionPosition.fX, vecSectionPosition.fY,
                                                      vecSectionSize.fX, vecSectionSize.fY, false, pMaterialElement->GetMaterialItem(), fRotation,
                                                      vecRotationCenter.fX, vecRotationCenter.fY, color, bPostGUI);
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    if (argStream.HasErrors())
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawPrimitive3D(lua_State* luaVM)
{
    // bool DxDrawPrimitive3D (string primitiveType, bool postGUI, table vertice1, ...)
    D3DPRIMITIVETYPE ePrimitiveType;
    auto             pVecVertices = new std::vector<PrimitiveVertice>();
    eRenderStage     renderStage{eRenderStage::POST_FX};
    CScriptArgReader argStream(luaVM);
    argStream.ReadEnumString(ePrimitiveType);
    if (argStream.NextIsBool())
        renderStage = argStream.ReadBool() ? eRenderStage::POST_GUI : eRenderStage::POST_FX;
    else
        argStream.ReadEnumString(renderStage, eRenderStage::POST_FX);

    std::vector<double> vecTableContent;

    while (argStream.NextIsTable())
    {
        vecTableContent.clear();

        argStream.ReadNumberTable(vecTableContent);
        switch (vecTableContent.size())
        {
            case Primitive3DVerticeSizes::VERT_XYZ:
                pVecVertices->push_back(PrimitiveVertice{static_cast<float>(vecTableContent[0]), static_cast<float>(vecTableContent[1]),
                                                         static_cast<float>(vecTableContent[2]), (DWORD)0xFFFFFFFF});
                break;
            case Primitive3DVerticeSizes::VERT_XYZ_COLOR:
                pVecVertices->push_back(PrimitiveVertice{static_cast<float>(vecTableContent[0]), static_cast<float>(vecTableContent[1]),
                                                         static_cast<float>(vecTableContent[2]), static_cast<DWORD>(static_cast<int64_t>(vecTableContent[3]))});
                break;
            default:
                argStream.SetCustomError(SString("Expected table with 3 or 4 numbers, got %i numbers", vecTableContent.size()).c_str());
                break;
        }
    }

    if (argStream.HasErrors())
        return luaL_error(luaVM, argStream.GetFullErrorMessage());

    if (g_pCore->GetGraphics()->IsValidPrimitiveSize(pVecVertices->size(), ePrimitiveType))
    {
        g_pCore->GetGraphics()->DrawPrimitive3DQueued(pVecVertices, ePrimitiveType, renderStage);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    // Failed
    delete pVecVertices;
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawMaterialPrimitive3D(lua_State* luaVM)
{
    // bool DxDrawMaterialPrimitive3D (string primitiveType, dxMaterial material, bool postGUI, table vertice1, ...)
    D3DPRIMITIVETYPE ePrimitiveType;
    auto             pVecVertices = new std::vector<PrimitiveMaterialVertice>();
    CClientMaterial* pMaterialElement;
    eRenderStage     renderStage{eRenderStage::POST_FX};

    CScriptArgReader argStream(luaVM);
    argStream.ReadEnumString(ePrimitiveType);
    MixedReadMaterialString(argStream, pMaterialElement);
    if (argStream.NextIsBool())
        renderStage = argStream.ReadBool() ? eRenderStage::POST_GUI : eRenderStage::POST_FX;
    else
        argStream.ReadEnumString(renderStage, eRenderStage::POST_FX);

    std::vector<double> vecTableContent;

    while (argStream.NextIsTable())
    {
        vecTableContent.clear();

        argStream.ReadNumberTable(vecTableContent);
        switch (vecTableContent.size())
        {
            case Primitive3DVerticeSizes::VERT_XYZ_UV:
                pVecVertices->push_back(PrimitiveMaterialVertice{static_cast<float>(vecTableContent[0]), static_cast<float>(vecTableContent[1]),
                                                                 static_cast<float>(vecTableContent[2]), (DWORD)0xFFFFFFFF,
                                                                 static_cast<float>(vecTableContent[3]), static_cast<float>(vecTableContent[4])});
                break;
            case Primitive3DVerticeSizes::VERT_XYZ_COLOR_UV:
                pVecVertices->push_back(PrimitiveMaterialVertice{static_cast<float>(vecTableContent[0]), static_cast<float>(vecTableContent[1]),
                                                                 static_cast<float>(vecTableContent[2]),
                                                                 static_cast<DWORD>(static_cast<int64_t>(vecTableContent[3])),
                                                                 static_cast<float>(vecTableContent[4]), static_cast<float>(vecTableContent[5])});
                break;
            default:
                argStream.SetCustomError(SString("Expected table with 5 or 6 numbers, got %i numbers", vecTableContent.size()).c_str());
                break;
        }
    }

    if (argStream.HasErrors())
        return luaL_error(luaVM, argStream.GetFullErrorMessage());

    if (g_pCore->GetGraphics()->IsValidPrimitiveSize(pVecVertices->size(), ePrimitiveType))
    {
        g_pCore->GetGraphics()->DrawMaterialPrimitive3DQueued(pVecVertices, ePrimitiveType, pMaterialElement->GetMaterialItem(), renderStage);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    // Failed
    delete pVecVertices;
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawPrimitive(lua_State* luaVM)
{
    // bool dxDrawPrimitive (string primitiveType, bool postGUI, table vertice1, ...)
    D3DPRIMITIVETYPE ePrimitiveType;
    auto             pVecVertices = new std::vector<PrimitiveVertice>();
    bool             bPostGUI;

    CScriptArgReader argStream(luaVM);
    argStream.ReadEnumString(ePrimitiveType);
    argStream.ReadBool(bPostGUI);

    std::vector<double> vecTableContent;

    while (argStream.NextIsTable())
    {
        vecTableContent.clear();

        argStream.ReadNumberTable(vecTableContent);
        switch (vecTableContent.size())
        {
            case PrimitiveVerticeSizes::VERT_XY:
                pVecVertices->push_back(PrimitiveVertice{static_cast<float>(vecTableContent[0]), static_cast<float>(vecTableContent[1]), 0, (DWORD)0xFFFFFFFF});
                break;
            case PrimitiveVerticeSizes::VERT_XY_COLOR:
                pVecVertices->push_back(PrimitiveVertice{static_cast<float>(vecTableContent[0]), static_cast<float>(vecTableContent[1]), 0,
                                                         static_cast<DWORD>(static_cast<int64_t>(vecTableContent[2]))});
                break;
            default:
                argStream.SetCustomError(SString("Expected table with 2 or 3 numbers, got %i numbers", vecTableContent.size()).c_str());
                break;
        }
    }

    if (argStream.HasErrors())
        return luaL_error(luaVM, argStream.GetFullErrorMessage());

    if (g_pCore->GetGraphics()->IsValidPrimitiveSize(pVecVertices->size(), ePrimitiveType))
    {
        g_pCore->GetGraphics()->DrawPrimitiveQueued(pVecVertices, ePrimitiveType, bPostGUI);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    // Failed
    delete pVecVertices;
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxDrawMaterialPrimitive(lua_State* luaVM)
{
    // bool dxDrawPrimitive (string primitiveType, dxMaterial material, bool postGUI, table vertice1, ...)
    D3DPRIMITIVETYPE ePrimitiveType;
    auto             pVecVertices = new std::vector<PrimitiveMaterialVertice>();
    CClientMaterial* pMaterialElement;
    bool             bPostGUI;

    CScriptArgReader argStream(luaVM);
    argStream.ReadEnumString(ePrimitiveType);
    MixedReadMaterialString(argStream, pMaterialElement);
    argStream.ReadBool(bPostGUI);

    std::vector<double> vecTableContent;

    while (argStream.NextIsTable())
    {
        vecTableContent.clear();

        argStream.ReadNumberTable(vecTableContent);
        switch (vecTableContent.size())
        {
            case PrimitiveVerticeSizes::VERT_XY_UV:
                pVecVertices->push_back(PrimitiveMaterialVertice{static_cast<float>(vecTableContent[0]), static_cast<float>(vecTableContent[1]), 0,
                                                                 (DWORD)0xFFFFFFFF, static_cast<float>(vecTableContent[2]),
                                                                 static_cast<float>(vecTableContent[3])});
                break;
            case PrimitiveVerticeSizes::VERT_XY_COLOR_UV:
                pVecVertices->push_back(PrimitiveMaterialVertice{static_cast<float>(vecTableContent[0]), static_cast<float>(vecTableContent[1]), 0,
                                                                 static_cast<DWORD>(static_cast<int64_t>(vecTableContent[2])),
                                                                 static_cast<float>(vecTableContent[3]), static_cast<float>(vecTableContent[4])});
                break;
            default:
                argStream.SetCustomError(SString("Expected table with 4 or 5 numbers, got %i numbers", vecTableContent.size()).c_str());
                break;
        }
    }

    if (argStream.HasErrors())
        return luaL_error(luaVM, argStream.GetFullErrorMessage());

    if (g_pCore->GetGraphics()->IsValidPrimitiveSize(pVecVertices->size(), ePrimitiveType))
    {
        g_pCore->GetGraphics()->DrawMaterialPrimitiveQueued(pVecVertices, ePrimitiveType, pMaterialElement->GetMaterialItem(), bPostGUI);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    // Failed
    delete pVecVertices;
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetTextWidth(lua_State* luaVM)
{
    //  float dxGetTextWidth ( string text, [float scale=1, mixed font="default", bool colorCoded=false] )
    SString        strText;
    float          fScale;
    eFontType      fontType;
    CClientDxFont* pDxFontElement;
    bool           bColorCoded;

    CScriptArgReader argStream(luaVM);
    argStream.ReadString(strText);
    argStream.ReadNumber(fScale, 1);
    MixedReadDxFontString(argStream, fontType, FONT_DEFAULT, pDxFontElement);
    argStream.ReadBool(bColorCoded, false);

    if (!argStream.HasErrors())
    {
        ID3DXFont* pD3DXFont = CStaticFunctionDefinitions::ResolveD3DXFont(fontType, pDxFontElement);

        // Retrieve the longest line's extent
        std::stringstream ssText(strText);
        std::string       sLineText;
        float             fWidth = 0.0f, fLineExtent = 0.0f;

        while (std::getline(ssText, sLineText))
        {
            fLineExtent = g_pCore->GetGraphics()->GetDXTextExtent(sLineText.c_str(), fScale, pD3DXFont, bColorCoded);
            if (fLineExtent > fWidth)
                fWidth = fLineExtent;
        }

        // Success
        lua_pushnumber(luaVM, fWidth);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::OOP_DxGetTextWidth(lua_State* luaVM)
{
    //  float dxGetTextWidth ( string text, [float scale=1, mixed font="default", bool colorCoded=false] )
    SString        strText;
    float          fScale;
    CClientDxFont* pDxFontElement;
    bool           bColorCoded;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pDxFontElement);
    argStream.ReadString(strText);
    argStream.ReadNumber(fScale, 1);
    argStream.ReadBool(bColorCoded, false);

    if (!argStream.HasErrors())
    {
        ID3DXFont* pD3DXFont = CStaticFunctionDefinitions::ResolveD3DXFont(FONT_DEFAULT, pDxFontElement);

        // Retrieve the longest line's extent
        std::stringstream ssText(strText);
        std::string       sLineText;
        float             fWidth = 0.0f, fLineExtent = 0.0f;

        while (std::getline(ssText, sLineText))
        {
            fLineExtent = g_pCore->GetGraphics()->GetDXTextExtent(sLineText.c_str(), fScale, pD3DXFont, bColorCoded);
            if (fLineExtent > fWidth)
                fWidth = fLineExtent;
        }

        // Success
        lua_pushnumber(luaVM, fWidth);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

CVector2D CLuaDrawingDefs::OOP_DxGetTextSize(std::variant<CClientDxFont*, eFontType> variantFont, const std::string text, const std::optional<float> optWidth,
                                             const std::optional<std::variant<CVector2D, float>> optScaleXY, const std::optional<bool> optWordBreak,
                                             const std::optional<bool> optColorCoded)
{
    // float, float dxGetTextSize ( string text, [float width=0, float scaleXY=1.0, float=scaleY=1.0, mixed font="default",
    // bool wordBreak=false, bool colorCoded=false] )
    CGraphicsInterface* const graphics = g_pCore->GetGraphics();

    // resolve scale (use X as Y value, if optScaleY is empty)
    CVector2D scale(1.0f, 1.0f);
    if (optScaleXY.has_value())
    {
        std::variant<CVector2D, float> scaleXY = optScaleXY.value();
        if (std::holds_alternative<float>(scaleXY))
        {
            scale.fX = std::get<float>(scaleXY);
            scale.fY = scale.fX;
        }
        else
        {
            scale = std::get<CVector2D>(scaleXY);
        }
    }

    CVector2D vecSize;
    graphics->GetDXTextSize(vecSize, text.c_str(), optWidth.value_or(0.0f), scale.fX, scale.fY, CStaticFunctionDefinitions::ResolveD3DXFont(variantFont),
                            optWordBreak.value_or(false), optColorCoded.value_or(false));

    return vecSize;
}

int CLuaDrawingDefs::DxGetFontHeight(lua_State* luaVM)
{
    //  int dxGetFontHeight ( [float scale=1, mixed font="default"] )
    float          fScale;
    eFontType      fontType;
    CClientDxFont* pDxFontElement;

    CScriptArgReader argStream(luaVM);
    argStream.ReadNumber(fScale, 1);
    MixedReadDxFontString(argStream, fontType, FONT_DEFAULT, pDxFontElement);

    if (!argStream.HasErrors())
    {
        ID3DXFont* pD3DXFont = CStaticFunctionDefinitions::ResolveD3DXFont(fontType, pDxFontElement);

        float fHeight = g_pCore->GetGraphics()->GetDXFontHeight(fScale, pD3DXFont);
        // Success
        lua_pushnumber(luaVM, fHeight);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::OOP_DxGetFontHeight(lua_State* luaVM)
{
    //  int dxGetFontHeight ( [float scale=1, mixed font="default"] )
    float          fScale;
    CClientDxFont* pDxFontElement;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pDxFontElement);
    argStream.ReadNumber(fScale, 1);

    if (!argStream.HasErrors())
    {
        ID3DXFont* pD3DXFont = CStaticFunctionDefinitions::ResolveD3DXFont(FONT_DEFAULT, pDxFontElement);

        float fHeight = g_pCore->GetGraphics()->GetDXFontHeight(fScale, pD3DXFont);
        // Success
        lua_pushnumber(luaVM, fHeight);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxCreateTexture(lua_State* luaVM)
{
    //  element dxCreateTexture( string filepath [, string textureFormat = "argb", bool mipmaps = true, string textureEdge = "wrap" ] )
    //  element dxCreateTexture( string pixels [, string textureFormat = "argb", bool mipmaps = true, string textureEdge = "wrap" ] )
    //  element dxCreateTexture( int width, int height [, string textureFormat = "argb", string textureEdge = "wrap", string textureType = "2d", int depth ] )
    SString         strFilePath;
    CPixels         pixels;
    int             width = 0;
    int             height = 0;
    ERenderFormat   renderFormat;
    bool            bMipMaps = true;
    ETextureAddress textureAddress;
    ETextureType    textureType = TTYPE_TEXTURE;
    int             depth = 1;

    CScriptArgReader argStream(luaVM);
    if (!argStream.NextIsNumber())
    {
        argStream.ReadCharStringRef(pixels.externalData);
        if (!g_pCore->GetGraphics()->GetPixelsManager()->IsPixels(pixels))
        {
            // element dxCreateTexture( string filepath [, string textureFormat = "argb", bool mipmaps = true, string textureEdge = "wrap" ] )
            pixels = CPixels();
            argStream = CScriptArgReader(luaVM);
            argStream.ReadString(strFilePath);
            argStream.ReadEnumString(renderFormat, RFORMAT_UNKNOWN);
            argStream.ReadBool(bMipMaps, true);
            argStream.ReadEnumString(textureAddress, TADDRESS_WRAP);
        }
        else
        {
            // element dxCreateTexture( string pixels [, string textureFormat = "argb", bool mipmaps = true, string textureEdge = "wrap" ] )
            argStream.ReadEnumString(renderFormat, RFORMAT_UNKNOWN);
            argStream.ReadBool(bMipMaps, true);
            argStream.ReadEnumString(textureAddress, TADDRESS_WRAP);
        }
    }
    else
    {
        // element dxCreateTexture( int width, int height [, string textureFormat = "argb", string textureEdge = "wrap", string textureType = "2d", int depth ]
        // )
        argStream.ReadNumber(width);
        argStream.ReadNumber(height);
        argStream.ReadEnumString(renderFormat, RFORMAT_UNKNOWN);
        argStream.ReadEnumString(textureAddress, TADDRESS_WRAP);
        argStream.ReadEnumString(textureType, TTYPE_TEXTURE);
        if (textureType == TTYPE_VOLUMETEXTURE)
            argStream.ReadNumber(depth);
    }

    if (!argStream.HasErrors())
    {
        CLuaMain* pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        if (pLuaMain)
        {
            CResource* pParentResource = pLuaMain->GetResource();

            if (!strFilePath.empty())
            {
                // From file
                CResource* pFileResource = pParentResource;
                SString    strPath, strMetaPath;
                if (CResourceManager::ParseResourcePathInput(strFilePath, pFileResource, &strPath, &strMetaPath))
                {
                    if (FileExists(strPath))
                    {
                        CClientTexture* pTexture = g_pClientGame->GetManager()->GetRenderElementManager()->CreateTexture(
                            strPath, NULL, bMipMaps, RDEFAULT, RDEFAULT, renderFormat, textureAddress);
                        if (pTexture)
                        {
                            // Make it a child of the resource's file root ** CHECK  Should parent be pFileResource, and element added to pParentResource's
                            // ElementGroup? **
                            pTexture->SetParent(pParentResource->GetResourceDynamicEntity());
                            lua_pushelement(luaVM, pTexture);
                            return 1;
                        }
                        else
                        {
                            m_pScriptDebugging->LogCustom(
                                luaVM,
                                SString("[DxCreateTexture] Failed to create texture from file '%s' (may be corrupt, unsupported format, or out of memory)",
                                        strFilePath.c_str()));
                            lua_pushnil(luaVM);
                            return 1;
                        }
                    }
                    else
                        argStream.SetCustomError(strFilePath, "[DxCreateTexture] File not found");
                }
                else
                    argStream.SetCustomError(strFilePath, "[DxCreateTexture] Bad file path");
            }
            else if (pixels.GetSize())
            {
                // From pixels
                CClientTexture* pTexture = g_pClientGame->GetManager()->GetRenderElementManager()->CreateTexture("", &pixels, bMipMaps, RDEFAULT, RDEFAULT,
                                                                                                                 renderFormat, textureAddress);
                if (pTexture)
                {
                    pTexture->SetParent(pParentResource->GetResourceDynamicEntity());
                    lua_pushelement(luaVM, pTexture);
                    return 1;
                }
                else
                {
                    m_pScriptDebugging->LogCustom(luaVM, "[DxCreateTexture:Pixels] Failed to create texture from pixel data (invalid format or out of memory)");
                    lua_pushnil(luaVM);
                    return 1;
                }
            }
            else
            {
                // Blank sized
                CClientTexture* pTexture = g_pClientGame->GetManager()->GetRenderElementManager()->CreateTexture("", NULL, false, width, height, renderFormat,
                                                                                                                 textureAddress, textureType, depth);
                if (pTexture)
                {
                    pTexture->SetParent(pParentResource->GetResourceDynamicEntity());
                    lua_pushelement(luaVM, pTexture);
                    return 1;
                }
                else
                {
                    m_pScriptDebugging->LogCustom(
                        luaVM, SString("[DxCreateTexture:Blank] Failed to create blank texture %dx%d (invalid dimensions or out of memory)", width, height));
                    lua_pushnil(luaVM);
                    return 1;
                }
            }
        }
    }
    if (argStream.HasErrors())
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

namespace
{
    void PushShaderDiagnostics(lua_State* luaVM, const SShaderDiagnostics& diagnostics)
    {
        lua_createtable(luaVM, 0, 12);

        lua_pushstring(luaVM, "compiled");
        lua_pushboolean(luaVM, diagnostics.bCompiled);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "sourceIdentifier");
        lua_pushstring(luaVM, diagnostics.strSourceIdentifier);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "compileLog");
        lua_pushstring(luaVM, diagnostics.strCompileLog);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "selectedTechnique");
        lua_pushstring(luaVM, diagnostics.strSelectedTechnique);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "createHResult");
        lua_pushnumber(luaVM, diagnostics.lCreateHResult);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "vertexShaderProfile");
        lua_pushstring(luaVM, diagnostics.strVertexShaderProfile);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "pixelShaderProfile");
        lua_pushstring(luaVM, diagnostics.strPixelShaderProfile);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "usesVertexShader");
        lua_pushboolean(luaVM, diagnostics.bUsesVertexShader);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "usesDepthBuffer");
        lua_pushboolean(luaVM, diagnostics.bUsesDepthBuffer);
        lua_settable(luaVM, -3);
        lua_pushstring(luaVM, "usesMultipleRenderTargets");
        lua_pushboolean(luaVM, diagnostics.bUsesMultipleRenderTargets);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "techniques");
        lua_createtable(luaVM, static_cast<int>(diagnostics.techniques.size()), 0);
        for (size_t i = 0; i < diagnostics.techniques.size(); ++i)
        {
            const auto& technique = diagnostics.techniques[i];
            lua_createtable(luaVM, 0, 3);
            lua_pushstring(luaVM, "name");
            lua_pushstring(luaVM, technique.strName);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "passCount");
            lua_pushnumber(luaVM, technique.uiPassCount);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "valid");
            lua_pushboolean(luaVM, technique.bValid);
            lua_settable(luaVM, -3);
            lua_rawseti(luaVM, -2, static_cast<int>(i + 1));
        }
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "parameters");
        lua_createtable(luaVM, static_cast<int>(diagnostics.parameters.size()), 0);
        for (size_t i = 0; i < diagnostics.parameters.size(); ++i)
        {
            const auto& parameter = diagnostics.parameters[i];
            lua_createtable(luaVM, 0, 9);
            lua_pushstring(luaVM, "name");
            lua_pushstring(luaVM, parameter.strName);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "semantic");
            lua_pushstring(luaVM, parameter.strSemantic);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "automaticSemantic");
            lua_pushstring(luaVM, parameter.strAutomaticSemantic);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "class");
            lua_pushstring(luaVM, parameter.strClass);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "type");
            lua_pushstring(luaVM, parameter.strType);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "rows");
            lua_pushnumber(luaVM, parameter.uiRows);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "columns");
            lua_pushnumber(luaVM, parameter.uiColumns);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "elements");
            lua_pushnumber(luaVM, parameter.uiElements);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "annotations");
            lua_pushnumber(luaVM, parameter.uiAnnotations);
            lua_settable(luaVM, -3);
            lua_rawseti(luaVM, -2, static_cast<int>(i + 1));
        }
        lua_settable(luaVM, -3);
    }
}

int CLuaDrawingDefs::DxCreateShader(lua_State* luaVM)
{
    //  element dxCreateShader( string filepath / string raw_data [, float priority = 0, float maxdistance = 0, bool layered = false, string elementTypes =
    //  "world,vehicle,object,other" ] )
    SString                      strFile;
    EffectMacroList              macros;
    float                        fPriority;
    float                        fMaxDistance;
    bool                         bLayered;
    std::vector<EEntityTypeMask> elementTypeList;

    CScriptArgReader argStream(luaVM);
    argStream.ReadString(strFile);
    if (argStream.NextIsTable())
        argStream.ReadPairTable(macros);
    argStream.ReadNumber(fPriority, 0.0f);
    argStream.ReadNumber(fMaxDistance, 0.0f);
    argStream.ReadBool(bLayered, false);
    argStream.ReadEnumStringList(elementTypeList, "world,vehicle,object,other");

    if (argStream.HasErrors())
    {
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
        lua_pushnil(luaVM);
        return 1;
    }

    if (strFile.empty())
    {
        m_pScriptDebugging->LogCustom(luaVM, "expected non-empty string at argument 1");
        lua_pushnil(luaVM);
        return 1;
    }

    CLuaMain* const pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);

    if (!pLuaMain)
    {
        lua_pushnil(luaVM);
        return 1;
    }

    int iEntityTypeMaskResult = 0;

    for (EEntityTypeMask elementType : elementTypeList)
        iEntityTypeMaskResult |= elementType;

    CResource* pParentResource = pLuaMain->GetResource();
    CResource* pFileResource = pParentResource;
    SString    strPath, strMetaPath;

    bool bIsRawData = false;

    const bool bValidFilePath = CResourceManager::ParseResourcePathInput(strFile, pFileResource, &strPath, &strMetaPath);

    if (!bValidFilePath || (strFile[0] != '@' && strFile[0] != ':'))
    {
        bIsRawData = strFile.find("\n") != std::string::npos;

        if (!bIsRawData)
        {
            bIsRawData = (strFile.find("technique ") != std::string::npos) && (strFile.find("pass ") != std::string::npos) &&
                         (strFile.find('{') != std::string::npos) && (strFile.find('}') != std::string::npos);
        }
    }

    SString strRootPath;

    if (bIsRawData)
    {
        strPath = strFile;
        pFileResource = pParentResource;
        strRootPath = pFileResource->GetResourceDirectoryPath(ACCESS_PUBLIC, strMetaPath);
    }
    else
    {
        if (!pFileResource || !FileExists(strPath))
        {
            argStream.SetCustomError(strFile, "file doesn't exist");
            m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
            lua_pushboolean(luaVM, false);
            return 1;
        }

        strRootPath = strPath.Left(strPath.length() - strMetaPath.length());
    }

    SString        strStatus;
    CClientShader* pShader = g_pClientGame->GetManager()->GetRenderElementManager()->CreateShader(strPath, strRootPath, bIsRawData, strStatus, fPriority,
                                                                                                  fMaxDistance, bLayered, false, iEntityTypeMaskResult, macros);

    if (pShader)
    {
        pShader->SetParent(pParentResource->GetResourceDynamicEntity());
        lua_pushelement(luaVM, pShader);
        lua_pushstring(luaVM, strStatus);
        SShaderDiagnostics diagnostics;
        g_pCore->GetGraphics()->GetRenderItemManager()->GetShaderDiagnostics(pShader->GetShaderItem(), diagnostics);
        PushShaderDiagnostics(luaVM, diagnostics);
        return 3;
    }

    // Replace any path in the error message with our own one
    SString strRootPathWithoutResource = strRootPath.Left(strRootPath.TrimEnd("\\").length() - SStringX(pFileResource->GetName()).length());
    strStatus = strStatus.ReplaceI(strRootPathWithoutResource, "");
    argStream.SetCustomError(bIsRawData ? SStringX("raw data") : strFile, strStatus);
    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    lua_pushstring(luaVM, strStatus);
    SShaderDiagnostics diagnostics;
    if (bIsRawData)
        diagnostics.strSourceIdentifier = "<raw-data>";
    else
        diagnostics.strSourceIdentifier = strFile;
    diagnostics.strCompileLog = strStatus;
    PushShaderDiagnostics(luaVM, diagnostics);
    return 3;
}

int CLuaDrawingDefs::DxGetShaderDiagnostics(lua_State* luaVM)
{
    CClientShader* pShader = nullptr;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pShader);

    if (!argStream.HasErrors())
    {
        SShaderDiagnostics diagnostics;
        g_pCore->GetGraphics()->GetRenderItemManager()->GetShaderDiagnostics(pShader->GetShaderItem(), diagnostics);
        PushShaderDiagnostics(luaVM, diagnostics);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetRenderStatistics(lua_State* luaVM)
{
    SRenderStatistics statistics;
    g_pCore->GetGraphics()->GetRenderItemManager()->GetRenderStatistics(statistics);

    lua_createtable(luaVM, 0, 15);
#define PUSH_RENDER_STAT(Name, Value) \
    lua_pushstring(luaVM, Name); \
    lua_pushnumber(luaVM, Value); \
    lua_settable(luaVM, -3)
    PUSH_RENDER_STAT("openRenderPasses", statistics.uiOpenRenderPasses);
    PUSH_RENDER_STAT("renderPassesStarted", statistics.uiRenderPassesStarted);
    PUSH_RENDER_STAT("renderPassFailures", statistics.uiRenderPassFailures);
    PUSH_RENDER_STAT("forcedRenderPassClosures", statistics.uiForcedRenderPassClosures);
    PUSH_RENDER_STAT("renderItems", statistics.uiRenderItems);
    PUSH_RENDER_STAT("shaders", statistics.uiShaders);
    PUSH_RENDER_STAT("renderTargets", statistics.uiRenderTargets);
    PUSH_RENDER_STAT("depthTargets", statistics.uiDepthTargets);
    PUSH_RENDER_STAT("mrtSets", statistics.uiMrtSets);
    PUSH_RENDER_STAT("screenSources", statistics.uiScreenSources);
    PUSH_RENDER_STAT("sceneViews", g_pClientGame->GetManager()->GetRenderElementManager()->GetSceneViewCount());
    PUSH_RENDER_STAT("textureMemoryKB", statistics.iTextureMemoryKB);
    PUSH_RENDER_STAT("renderTargetMemoryKB", statistics.iRenderTargetMemoryKB);
    PUSH_RENDER_STAT("fontMemoryKB", statistics.iFontMemoryKB);
    PUSH_RENDER_STAT("freeMemoryKB", statistics.iFreeMemoryKB);
#undef PUSH_RENDER_STAT
    return 1;
}

int CLuaDrawingDefs::DxCreateRenderTarget(lua_State* luaVM)
{
    //  element dxCreateRenderTarget( int sizeX, int sizeY [, int withAlphaChannel = false ] )
    //  element dxCreateRenderTarget( int sizeX, int sizeY, SurfaceFormat surfaceFormat )
    CVector2D  vecSize;
    bool       bWithAlphaChannel = false;
    bool       bHasSurfaceFormat = false;
    _D3DFORMAT surfaceFormat;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(vecSize);
    if (argStream.NextIsString())
    {
        argStream.ReadEnumString(surfaceFormat);
        bHasSurfaceFormat = true;
    }
    else
        argStream.ReadBool(bWithAlphaChannel, false);

    if (!argStream.HasErrors())
    {
        CLuaMain*  pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource* pParentResource = pLuaMain ? pLuaMain->GetResource() : NULL;
        if (pParentResource)
        {
            CClientRenderTarget* pRenderTarget = g_pClientGame->GetManager()->GetRenderElementManager()->CreateRenderTarget(
                (uint)vecSize.fX, (uint)vecSize.fY, bHasSurfaceFormat, bWithAlphaChannel, surfaceFormat);
            if (pRenderTarget)
            {
                // Make it a child of the resource's file root ** CHECK  Should parent be pFileResource, and element added to pParentResource's ElementGroup? **
                pRenderTarget->SetParent(pParentResource->GetResourceDynamicEntity());

                lua_pushelement(luaVM, pRenderTarget);
                return 1;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

namespace
{
    // Deliberately separate from the shared _D3DFORMAT enum (CLuaFunctionParseHelpers.cpp) since
    // that one lists color formats for dxCreateRenderTarget/dxCreateTexture - these are hardware Z
    // formats, a different concept, and mixing the two lists would just confuse both call sites.
    bool StringToDepthStencilFormat(const SString& strFormat, D3DFORMAT& outFormat)
    {
        struct SFormatEntry
        {
            const char* szName;
            D3DFORMAT   format;
        };
        static const SFormatEntry formatList[] = {
            {"d24s8", D3DFMT_D24S8}, {"d24x8", D3DFMT_D24X8}, {"d24x4s4", D3DFMT_D24X4S4}, {"d32", D3DFMT_D32}, {"d16", D3DFMT_D16}, {"d15s1", D3DFMT_D15S1},
        };
        for (const auto& entry : formatList)
        {
            if (strFormat.CompareI(entry.szName))
            {
                outFormat = entry.format;
                return true;
            }
        }
        return false;
    }
}  // namespace

int CLuaDrawingDefs::DxCreateDepthStencilTarget(lua_State* luaVM)
{
    //  element dxCreateDepthStencilTarget( int sizeX, int sizeY [, string format = "d24s8" [, bool sampleable = false ] ] )
    CVector2D vecSize;
    SString   strFormat = "d24s8";
    bool      bSampleable = false;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(vecSize);
    argStream.ReadString(strFormat, "d24s8");
    argStream.ReadBool(bSampleable, false);

    D3DFORMAT depthFormat = D3DFMT_D24S8;
    if (!argStream.HasErrors() && !StringToDepthStencilFormat(strFormat, depthFormat))
        argStream.SetCustomError(SString("Expected valid depth-stencil format, got '%s'", strFormat.c_str()), "Bad argument");

    if (!argStream.HasErrors())
    {
        CLuaMain*  pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource* pParentResource = pLuaMain ? pLuaMain->GetResource() : NULL;
        if (pParentResource)
        {
            CClientDepthStencilTarget* pDepthStencilTarget = g_pClientGame->GetManager()->GetRenderElementManager()->CreateDepthStencilTarget(
                (uint)vecSize.fX, (uint)vecSize.fY, (_D3DFORMAT)depthFormat, bSampleable);
            if (pDepthStencilTarget)
            {
                pDepthStencilTarget->SetParent(pParentResource->GetResourceDynamicEntity());

                lua_pushelement(luaVM, pDepthStencilTarget);
                return 1;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments, or GPU/format rejected it (see debug log)
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxCreateMrtSet(lua_State* luaVM)
{
    //  element dxCreateMrtSet( table renderTargets [, element depthStencilTarget = false ] )
    std::vector<CClientRenderTarget*> renderTargetList;
    CClientDepthStencilTarget*        pDepthStencilTarget = NULL;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserDataTable(renderTargetList);
    argStream.ReadUserData<CClientDepthStencilTarget>(pDepthStencilTarget, (CClientDepthStencilTarget*)NULL);

    if (!argStream.HasErrors())
    {
        if (renderTargetList.empty())
            argStream.SetCustomError("renderTargets table must contain at least one render target", "Bad argument");
        else if (renderTargetList.size() > MAX_MRT_RENDER_TARGETS)
            argStream.SetCustomError(SString("renderTargets table must contain at most %d render targets", MAX_MRT_RENDER_TARGETS), "Bad argument");
    }

    if (!argStream.HasErrors())
    {
        CLuaMain*  pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource* pParentResource = pLuaMain ? pLuaMain->GetResource() : NULL;
        if (pParentResource)
        {
            CClientRenderTarget* colorTargets[MAX_MRT_RENDER_TARGETS] = {NULL, NULL, NULL, NULL};
            for (size_t i = 0; i < renderTargetList.size(); i++)
                colorTargets[i] = renderTargetList[i];

            CClientMrtSet* pMrtSet =
                g_pClientGame->GetManager()->GetRenderElementManager()->CreateMrtSet(colorTargets, (uint)renderTargetList.size(), pDepthStencilTarget);
            if (pMrtSet)
            {
                pMrtSet->SetParent(pParentResource->GetResourceDynamicEntity());

                lua_pushelement(luaVM, pMrtSet);
                return 1;
            }
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments, dimension mismatch, or slot count exceeds this GPU's limit (see debug log)
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxCreateSceneView(lua_State* luaVM)
{
    //  sceneView dxCreateSceneView ( int sizeX, int sizeY [, string colorFormat = "a8r8g8b8", string depthFormat = "d24s8", bool sampleableDepth = false ] )
    CVector2D  size;
    _D3DFORMAT colorFormat = (_D3DFORMAT)D3DFMT_A8R8G8B8;
    SString    depthFormatName = "d24s8";
    bool       bSampleableDepth = false;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(size);
    argStream.ReadEnumString(colorFormat, (_D3DFORMAT)D3DFMT_A8R8G8B8);
    argStream.ReadString(depthFormatName, "d24s8");
    argStream.ReadBool(bSampleableDepth, false);

    D3DFORMAT depthFormat = D3DFMT_D24S8;
    if (!argStream.HasErrors() && !StringToDepthStencilFormat(depthFormatName, depthFormat))
        argStream.SetCustomError(SString("Expected valid depth-stencil format, got '%s'", depthFormatName.c_str()), "Bad argument");

    // A SceneView's off-screen world render goes through RenderWare's own private camera Z-buffer raster
    // (RwCameraBeginUpdate rebinds the device's depth-stencil surface to that raster's own plain D3DFMT_D24S8
    // surface unconditionally - confirmed in-game, not just in theory), and D3D9's StretchRect requires
    // matching formats for depth-stencil surfaces (unlike color, which allows format conversion), so there is
    // no way to bridge that surface into a sampleable INTZ/DF24/DF16/RAWZ target the way the color output is
    // bridged. This is an architectural fact about how off-screen SceneViews render, not a hardware
    // capability gap, so it is rejected explicitly here rather than silently creating an item whose depth
    // content would never be populated. Standalone dxCreateDepthStencilTarget's sampleable path is unaffected
    // - it is used with dxBeginRenderPass/dxCreateMrtSet, which bind the D3D9 device directly and never go
    // through RenderWare's world-render camera at all. Use engineApplyShaderToSceneViewWorldTexture with a
    // depth-encoding shader (see shaders/depth_encode.fx in sceneview_world_shader_test) instead.
    if (!argStream.HasErrors() && bSampleableDepth)
        argStream.SetCustomError("sampleableDepth is not supported for SceneViews - see dxCreateSceneView documentation", "Not supported");

    if (!argStream.HasErrors())
    {
        CLuaMain*         pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource*        pResource = pLuaMain ? pLuaMain->GetResource() : nullptr;
        CClientSceneView* pSceneView = pResource
                                           ? g_pClientGame->GetManager()->GetRenderElementManager()->CreateSceneView(
                                                 static_cast<uint>(size.fX), static_cast<uint>(size.fY), colorFormat, (_D3DFORMAT)depthFormat, bSampleableDepth)
                                           : nullptr;
        if (pSceneView)
        {
            pSceneView->SetParent(pResource->GetResourceDynamicEntity());
            lua_pushelement(luaVM, pSceneView);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetSceneViewCamera(lua_State* luaVM)
{
    CClientSceneView* pSceneView = nullptr;
    CVector           position;
    CVector           target;
    CVector           up(0.0f, 0.0f, 1.0f);
    float             fFOV = 70.0f;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    argStream.ReadVector3D(position);
    argStream.ReadVector3D(target);
    argStream.ReadVector3D(up, CVector(0.0f, 0.0f, 1.0f));
    argStream.ReadNumber(fFOV, 70.0f);

    CVector front = target - position;
    if (!argStream.HasErrors() && (front.Normalize() == 0.0f || up.Normalize() == 0.0f || fabs(front.DotProduct(&up)) > 0.999f))
        argStream.SetCustomError("camera target and up vector must define a valid orientation", "Bad argument");
    if (!argStream.HasErrors() && (fFOV < 1.0f || fFOV > 179.0f))
        argStream.SetCustomError("field of view must be between 1 and 179 degrees", "Bad argument");

    if (!argStream.HasErrors())
    {
        CMatrix matrix;
        matrix.vPos = position;
        matrix.vFront = front;
        matrix.vUp = up;
        matrix.OrthoNormalize(CMatrix::AXIS_FRONT, CMatrix::AXIS_UP);
        pSceneView->SetCamera(matrix, fFOV);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetSceneViewMatrix(lua_State* luaVM)
{
    CClientSceneView* pSceneView = nullptr;
    CMatrix           matrix;
    float             fFOV = 70.0f;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    if (argStream.NextIsTable())
    {
        if (!ReadMatrix(luaVM, argStream.m_iIndex, matrix))
            argStream.SetCustomError("matrix must be a 4 x 4 matrix table", "Bad argument");
        else
            ++argStream.m_iIndex;
    }
    else
        argStream.ReadMatrix(matrix);
    argStream.ReadNumber(fFOV, 70.0f);

    const auto isFiniteVector = [](const CVector& vector) { return std::isfinite(vector.fX) && std::isfinite(vector.fY) && std::isfinite(vector.fZ); };

    if (!argStream.HasErrors() &&
        (!isFiniteVector(matrix.vPos) || !isFiniteVector(matrix.vRight) || !isFiniteVector(matrix.vFront) || !isFiniteVector(matrix.vUp)))
        argStream.SetCustomError("matrix contains a non-finite component", "Bad argument");

    CVector right = matrix.vRight;
    CVector front = matrix.vFront;
    CVector up = matrix.vUp;
    if (!argStream.HasErrors() && (right.Normalize() == 0.0f || front.Normalize() == 0.0f || up.Normalize() == 0.0f ||
                                   fabs(right.DotProduct(&front)) > 0.999f || fabs(right.DotProduct(&up)) > 0.999f || fabs(front.DotProduct(&up)) > 0.999f))
        argStream.SetCustomError("matrix axes must define a valid camera orientation", "Bad argument");
    if (!argStream.HasErrors() && (!std::isfinite(fFOV) || fFOV < 1.0f || fFOV > 179.0f))
        argStream.SetCustomError("field of view must be between 1 and 179 degrees", "Bad argument");

    if (!argStream.HasErrors())
    {
        // Keep the supplied forward direction as the primary camera axis and orthogonalize the remaining
        // basis. This accepts small floating-point drift from tracked element matrices without allowing a
        // skewed or singular camera transform into GTA and RenderWare's shared camera state.
        matrix.OrthoNormalize(CMatrix::AXIS_FRONT, CMatrix::AXIS_UP);
        pSceneView->SetCamera(matrix, fFOV);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetSceneViewOrthographicProjection(lua_State* luaVM)
{
    //  bool dxSetSceneViewOrthographicProjection ( sceneView theSceneView, float width, float height, float nearClip, float farClip )
    CClientSceneView* pSceneView = nullptr;
    float             fWidth = 0.0f;
    float             fHeight = 0.0f;
    float             fNearClip = 0.0f;
    float             fFarClip = 0.0f;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    argStream.ReadNumber(fWidth);
    argStream.ReadNumber(fHeight);
    argStream.ReadNumber(fNearClip);
    argStream.ReadNumber(fFarClip);

    if (!argStream.HasErrors() && (!std::isfinite(fWidth) || !std::isfinite(fHeight) || fWidth <= 0.0f || fHeight <= 0.0f))
        argStream.SetCustomError("width and height must be finite and greater than zero", "Bad argument");
    if (!argStream.HasErrors() && (!std::isfinite(fNearClip) || !std::isfinite(fFarClip) || fNearClip <= 0.0f || fFarClip <= fNearClip))
        argStream.SetCustomError("nearClip must be greater than zero and less than farClip", "Bad argument");

    if (!argStream.HasErrors())
    {
        pSceneView->SetOrthographicProjection(fWidth, fHeight, fNearClip, fFarClip);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetSceneViewPerspectiveProjection(lua_State* luaVM)
{
    //  bool dxSetSceneViewPerspectiveProjection ( sceneView theSceneView )
    CClientSceneView* pSceneView = nullptr;
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pSceneView);

    if (!argStream.HasErrors())
    {
        pSceneView->SetPerspectiveProjection();
        lua_pushboolean(luaVM, true);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxRequestSceneViewRender(lua_State* luaVM)
{
    CClientSceneView* pSceneView = nullptr;
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    if (!argStream.HasErrors())
    {
        if (!pSceneView->IsCameraConfigured())
        {
            m_pScriptDebugging->LogCustom(luaVM, "dxRequestSceneViewRender: scene view camera has not been configured");
            lua_pushboolean(luaVM, false);
            return 1;
        }
        pSceneView->RequestRender();
        lua_pushboolean(luaVM, true);
        return 1;
    }
    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetSceneViewUpdateMode(lua_State* luaVM)
{
    CClientSceneView* pSceneView = nullptr;
    SString           strMode;
    uint              uiValue = 0;
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    argStream.ReadString(strMode);
    argStream.ReadNumber(uiValue, 0);

    ESceneViewUpdateMode mode = ESceneViewUpdateMode::MANUAL;
    if (!argStream.HasErrors())
    {
        if (strMode == "manual")
            mode = ESceneViewUpdateMode::MANUAL;
        else if (strMode == "once")
            mode = ESceneViewUpdateMode::ONCE;
        else if (strMode == "always")
            mode = ESceneViewUpdateMode::ALWAYS;
        else if (strMode == "every_n_frames")
            mode = ESceneViewUpdateMode::EVERY_N_FRAMES;
        else if (strMode == "interval")
            mode = ESceneViewUpdateMode::INTERVAL;
        else
            argStream.SetCustomError("update mode must be manual, once, always, every_n_frames or interval", "Bad argument");
    }

    if (!argStream.HasErrors() && mode == ESceneViewUpdateMode::EVERY_N_FRAMES && (uiValue < 1 || uiValue > 10000))
        argStream.SetCustomError("every_n_frames value must be between 1 and 10000", "Bad argument");
    if (!argStream.HasErrors() && mode == ESceneViewUpdateMode::INTERVAL && (uiValue < 16 || uiValue > 60000))
        argStream.SetCustomError("interval value must be between 16 and 60000 milliseconds", "Bad argument");

    if (!argStream.HasErrors())
    {
        pSceneView->SetUpdateMode(mode, uiValue);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::EngineApplyShaderToSceneViewWorldTexture(lua_State* luaVM)
{
    //  bool engineApplyShaderToSceneViewWorldTexture ( shader theShader, sceneView theSceneView, string textureName, [ element targetElement, bool appendLayers
    //  ] )
    CClientShader*    pShader = nullptr;
    CClientSceneView* pSceneView = nullptr;
    SString           strTextureNameMatch;
    CClientEntity*    pTargetEntity = nullptr;
    bool              bAppendLayers = true;
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pShader);
    argStream.ReadUserData(pSceneView);
    argStream.ReadString(strTextureNameMatch);
    argStream.ReadUserData(pTargetEntity, nullptr);
    argStream.ReadBool(bAppendLayers, true);

    if (!argStream.HasErrors() && strTextureNameMatch.empty())
        argStream.SetCustomError("texture name match cannot be empty", "Bad argument");

    if (!argStream.HasErrors())
    {
        CLuaMain*      pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource*     pResource = pLuaMain ? pLuaMain->GetResource() : nullptr;
        CClientEntity* pResourceRoot = pResource ? pResource->GetResourceDynamicEntity() : nullptr;
        if (!pResourceRoot || !pResourceRoot->IsMyChild(pShader, true) || !pResourceRoot->IsMyChild(pSceneView, true))
        {
            m_pScriptDebugging->LogCustom(luaVM, "engineApplyShaderToSceneViewWorldTexture: shader and scene view must belong to this resource");
            lua_pushboolean(luaVM, false);
            return 1;
        }

        lua_pushboolean(luaVM, pSceneView->AddShaderAssignment(pShader->GetShaderItem(), strTextureNameMatch, pTargetEntity, bAppendLayers));
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::EngineRemoveShaderFromSceneViewWorldTexture(lua_State* luaVM)
{
    //  bool engineRemoveShaderFromSceneViewWorldTexture ( shader theShader, sceneView theSceneView, string textureName, [ element targetElement ] )
    CClientShader*    pShader = nullptr;
    CClientSceneView* pSceneView = nullptr;
    SString           strTextureNameMatch;
    CClientEntity*    pTargetEntity = nullptr;
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pShader);
    argStream.ReadUserData(pSceneView);
    argStream.ReadString(strTextureNameMatch);
    argStream.ReadUserData(pTargetEntity, nullptr);

    if (!argStream.HasErrors())
    {
        CLuaMain*      pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource*     pResource = pLuaMain ? pLuaMain->GetResource() : nullptr;
        CClientEntity* pResourceRoot = pResource ? pResource->GetResourceDynamicEntity() : nullptr;
        if (!pResourceRoot || !pResourceRoot->IsMyChild(pShader, true) || !pResourceRoot->IsMyChild(pSceneView, true))
        {
            m_pScriptDebugging->LogCustom(luaVM, "engineRemoveShaderFromSceneViewWorldTexture: shader and scene view must belong to this resource");
            lua_pushboolean(luaVM, false);
            return 1;
        }

        lua_pushboolean(luaVM, pSceneView->RemoveShaderAssignment(pShader->GetShaderItem(), strTextureNameMatch, pTargetEntity));
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetSceneViewOutputShader(lua_State* luaVM)
{
    CClientSceneView* pSceneView = nullptr;
    CClientShader*    pShader = nullptr;
    SString           strInputName = "SceneViewTexture";
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    argStream.ReadUserData(pShader);
    argStream.ReadString(strInputName, strInputName);

    if (!argStream.HasErrors() && strInputName.empty())
        argStream.SetCustomError("input parameter name cannot be empty", "Bad argument");

    if (!argStream.HasErrors())
    {
        CLuaMain*      pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource*     pResource = pLuaMain ? pLuaMain->GetResource() : nullptr;
        CClientEntity* pResourceRoot = pResource ? pResource->GetResourceDynamicEntity() : nullptr;
        if (!pResourceRoot || !pResourceRoot->IsMyChild(pShader, true) || !pResourceRoot->IsMyChild(pSceneView, true))
        {
            m_pScriptDebugging->LogCustom(luaVM, "dxSetSceneViewOutputShader: shader and scene view must belong to this resource");
            lua_pushboolean(luaVM, false);
            return 1;
        }

        CClientRenderElementManager* pManager = g_pClientGame->GetManager()->GetRenderElementManager();
        const bool                   bSet = pManager->SetSceneViewOutputShader(pSceneView, pShader->GetShaderItem(), strInputName);
        if (!bSet)
            m_pScriptDebugging->LogCustom(
                luaVM, SString("dxSetSceneViewOutputShader: texture parameter '%s' is missing or the intermediate target could not be created", *strInputName));
        lua_pushboolean(luaVM, bSet);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxRemoveSceneViewOutputShader(lua_State* luaVM)
{
    CClientSceneView* pSceneView = nullptr;
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    if (!argStream.HasErrors())
    {
        CLuaMain*      pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource*     pResource = pLuaMain ? pLuaMain->GetResource() : nullptr;
        CClientEntity* pResourceRoot = pResource ? pResource->GetResourceDynamicEntity() : nullptr;
        if (!pResourceRoot || !pResourceRoot->IsMyChild(pSceneView, true))
        {
            m_pScriptDebugging->LogCustom(luaVM, "dxRemoveSceneViewOutputShader: scene view must belong to this resource");
            lua_pushboolean(luaVM, false);
            return 1;
        }

        pSceneView->ClearOutputShader();
        lua_pushboolean(luaVM, true);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetSceneViewTexture(lua_State* luaVM)
{
    CClientSceneView* pSceneView = nullptr;
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    if (!argStream.HasErrors())
    {
        // The scene-view element derives from DxRenderTarget, so returning the same owned element avoids
        // introducing a second Lua wrapper with ambiguous lifetime for one D3D texture.
        lua_pushelement(luaVM, pSceneView);
        return 1;
    }
    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetSceneViewInfo(lua_State* luaVM)
{
    CClientSceneView* pSceneView = nullptr;
    CScriptArgReader  argStream(luaVM);
    argStream.ReadUserData(pSceneView);
    if (!argStream.HasErrors())
    {
        CRenderTargetItem* pTarget = pSceneView->GetRenderTargetItem();
        lua_createtable(luaVM, 0, 13);
#define PUSH_SCENE_VIEW_FIELD(Name, PushCall) \
    lua_pushstring(luaVM, Name); \
    PushCall; \
    lua_settable(luaVM, -3)
        PUSH_SCENE_VIEW_FIELD("width", lua_pushnumber(luaVM, pTarget->m_uiSizeX));
        PUSH_SCENE_VIEW_FIELD("height", lua_pushnumber(luaVM, pTarget->m_uiSizeY));
        PUSH_SCENE_VIEW_FIELD("fov", lua_pushnumber(luaVM, pSceneView->GetFOV()));
        PUSH_SCENE_VIEW_FIELD("orthographic", lua_pushboolean(luaVM, pSceneView->IsOrthographic()));
        PUSH_SCENE_VIEW_FIELD("renderRequested", lua_pushboolean(luaVM, pSceneView->IsRenderRequested()));
        PUSH_SCENE_VIEW_FIELD("lastRenderSucceeded", lua_pushboolean(luaVM, pSceneView->DidLastRenderSucceed()));
        PUSH_SCENE_VIEW_FIELD("outputShaderActive", lua_pushboolean(luaVM, pSceneView->GetOutputShaderItem() != nullptr));
        PUSH_SCENE_VIEW_FIELD("lastRenderError", lua_pushstring(luaVM, pSceneView->GetLastRenderError()));
        const char* szUpdateMode = "manual";
        switch (pSceneView->GetUpdateMode())
        {
            case ESceneViewUpdateMode::ONCE:
                szUpdateMode = "once";
                break;
            case ESceneViewUpdateMode::ALWAYS:
                szUpdateMode = "always";
                break;
            case ESceneViewUpdateMode::EVERY_N_FRAMES:
                szUpdateMode = "every_n_frames";
                break;
            case ESceneViewUpdateMode::INTERVAL:
                szUpdateMode = "interval";
                break;
            default:
                break;
        }
        PUSH_SCENE_VIEW_FIELD("updateMode", lua_pushstring(luaVM, szUpdateMode));
        PUSH_SCENE_VIEW_FIELD("updateValue", lua_pushnumber(luaVM, pSceneView->GetUpdateValue()));
        PUSH_SCENE_VIEW_FIELD("renderCount", lua_pushnumber(luaVM, pSceneView->GetRenderCount()));
        PUSH_SCENE_VIEW_FIELD("lastRenderFrame", lua_pushnumber(luaVM, pSceneView->GetLastRenderFrame()));
        PUSH_SCENE_VIEW_FIELD("lastRenderTick", lua_pushnumber(luaVM, pSceneView->GetLastRenderTick()));
#undef PUSH_SCENE_VIEW_FIELD
        return 1;
    }
    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

namespace
{
    // Matches CClientRenderElementManager::RenderRequestedCubemaps' faceDirections table order exactly:
    // D3DCUBEMAP_FACES 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
    const char* const g_CubemapFaceNames[6] = {"posx", "negx", "posy", "negy", "posz", "negz"};

    bool CubemapFaceNameToIndex(const SString& strName, uint& outIndex)
    {
        for (uint i = 0; i < 6; i++)
        {
            if (strName.CompareI(g_CubemapFaceNames[i]))
            {
                outIndex = i;
                return true;
            }
        }
        return false;
    }
}  // namespace

int CLuaDrawingDefs::DxCreateCubemapRenderTarget(lua_State* luaVM)
{
    //  cubemap dxCreateCubemapRenderTarget ( int edgeSize [, string colorFormat = "a8r8g8b8" ] )
    uint       uiEdgeSize = 0;
    _D3DFORMAT colorFormat = (_D3DFORMAT)D3DFMT_A8R8G8B8;

    CScriptArgReader argStream(luaVM);
    argStream.ReadNumber(uiEdgeSize);
    argStream.ReadEnumString(colorFormat, (_D3DFORMAT)D3DFMT_A8R8G8B8);

    if (!argStream.HasErrors() && (uiEdgeSize == 0 || uiEdgeSize > 4096))
        argStream.SetCustomError("edge size must be between 1 and 4096", "Bad argument");

    if (!argStream.HasErrors())
    {
        CLuaMain*                   pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource*                  pResource = pLuaMain ? pLuaMain->GetResource() : nullptr;
        CClientCubemapRenderTarget* pCubemap =
            pResource ? g_pClientGame->GetManager()->GetRenderElementManager()->CreateCubemapRenderTarget(uiEdgeSize, colorFormat) : nullptr;
        if (pCubemap)
        {
            pCubemap->SetParent(pResource->GetResourceDynamicEntity());
            lua_pushelement(luaVM, pCubemap);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetCubemapRenderTargetCamera(lua_State* luaVM)
{
    //  bool dxSetCubemapRenderTargetCamera ( cubemap theCubemap, float x, float y, float z [, float nearClip = 0.3, float farClip = 500 ] )
    CClientCubemapRenderTarget* pCubemap = nullptr;
    CVector                     position;
    float                       fNearClip = 0.3f;
    float                       fFarClip = 500.0f;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pCubemap);
    argStream.ReadVector3D(position);
    argStream.ReadNumber(fNearClip, 0.3f);
    argStream.ReadNumber(fFarClip, 500.0f);

    if (!argStream.HasErrors() &&
        (!std::isfinite(position.fX) || !std::isfinite(position.fY) || !std::isfinite(position.fZ) || !std::isfinite(fNearClip) || !std::isfinite(fFarClip)))
        argStream.SetCustomError("position, nearClip and farClip must be finite", "Bad argument");
    if (!argStream.HasErrors() && (fNearClip <= 0.0f || fFarClip <= fNearClip))
        argStream.SetCustomError("nearClip must be greater than zero and less than farClip", "Bad argument");

    if (!argStream.HasErrors())
    {
        pCubemap->SetCamera(position, fNearClip, fFarClip);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxRequestCubemapRenderTargetRender(lua_State* luaVM)
{
    //  bool dxRequestCubemapRenderTargetRender ( cubemap theCubemap [, table faces = { "posx", "negx", "posy", "negy", "posz", "negz" } ] )
    CClientCubemapRenderTarget* pCubemap = nullptr;
    CScriptArgReader            argStream(luaVM);
    argStream.ReadUserData(pCubemap);

    // All 6 faces render the same frame they're requested - there is no per-frame throttling. A script that
    // wants to spread the cost over multiple frames does so itself, by requesting a subset of faces each call.
    uint8 uiFaceMask = 0x3F;
    if (argStream.NextIsTable())
    {
        uiFaceMask = 0;
        lua_pushvalue(luaVM, argStream.m_iIndex);
        lua_pushnil(luaVM);
        while (lua_next(luaVM, -2) != 0)
        {
            if (lua_type(luaVM, -1) == LUA_TSTRING)
            {
                uint uiFaceIndex;
                if (CubemapFaceNameToIndex(lua_tostring(luaVM, -1), uiFaceIndex))
                    uiFaceMask |= (1 << uiFaceIndex);
            }
            lua_pop(luaVM, 1);
        }
        lua_pop(luaVM, 1);
        ++argStream.m_iIndex;

        if (!argStream.HasErrors() && uiFaceMask == 0)
            argStream.SetCustomError("faces table must name at least one valid face (posx, negx, posy, negy, posz, negz)", "Bad argument");
    }

    if (!argStream.HasErrors())
    {
        pCubemap->RequestFaces(uiFaceMask);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetCubemapRenderTargetTexture(lua_State* luaVM)
{
    //  cubemap dxGetCubemapRenderTargetTexture ( cubemap theCubemap )
    CClientCubemapRenderTarget* pCubemap = nullptr;
    CScriptArgReader            argStream(luaVM);
    argStream.ReadUserData(pCubemap);
    if (!argStream.HasErrors())
    {
        // Mirrors dxGetSceneViewTexture: the cubemap element derives from CClientTexture, so returning the
        // same owned element avoids introducing a second Lua wrapper with ambiguous lifetime for one D3D texture.
        lua_pushelement(luaVM, pCubemap);
        return 1;
    }
    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetCubemapRenderTargetInfo(lua_State* luaVM)
{
    //  table dxGetCubemapRenderTargetInfo ( cubemap theCubemap )
    CClientCubemapRenderTarget* pCubemap = nullptr;
    CScriptArgReader            argStream(luaVM);
    argStream.ReadUserData(pCubemap);
    if (!argStream.HasErrors())
    {
        CCubemapRenderTargetItem* pItem = pCubemap->GetCubemapRenderTargetItem();
        lua_createtable(luaVM, 0, 5);
#define PUSH_CUBEMAP_FIELD(Name, PushCall) \
    lua_pushstring(luaVM, Name); \
    PushCall; \
    lua_settable(luaVM, -3)
        PUSH_CUBEMAP_FIELD("edgeSize", lua_pushnumber(luaVM, pItem->m_uiEdgeSize));
        PUSH_CUBEMAP_FIELD("cameraConfigured", lua_pushboolean(luaVM, pCubemap->IsPositionConfigured()));
        PUSH_CUBEMAP_FIELD("lastRenderSucceeded", lua_pushboolean(luaVM, pCubemap->DidLastRenderSucceed()));
        PUSH_CUBEMAP_FIELD("renderCount", lua_pushnumber(luaVM, pCubemap->GetRenderCount()));
        PUSH_CUBEMAP_FIELD("lastRenderError", lua_pushstring(luaVM, pCubemap->GetLastRenderError()));
#undef PUSH_CUBEMAP_FIELD
        return 1;
    }
    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxCreateScreenSource(lua_State* luaVM)
{
    //  element dxCreateScreenSource( int sizeX, int sizeY )
    CVector2D vecSize;

    CScriptArgReader argStream(luaVM);
    argStream.ReadVector2D(vecSize);

    if (!argStream.HasErrors())
    {
        CLuaMain*  pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        CResource* pParentResource = pLuaMain ? pLuaMain->GetResource() : NULL;
        if (pParentResource)
        {
            CClientScreenSource* pScreenSource = g_pClientGame->GetManager()->GetRenderElementManager()->CreateScreenSource((uint)vecSize.fX, (uint)vecSize.fY);
            if (pScreenSource)
            {
                // Make it a child of the resource's file root ** CHECK  Should parent be pFileResource, and element added to pParentResource's ElementGroup? **
                pScreenSource->SetParent(pParentResource->GetResourceDynamicEntity());
            }
            lua_pushelement(luaVM, pScreenSource);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetMaterialSize(lua_State* luaVM)
{
    //  int, int [, int] dxGetMaterialSize( element material )
    CClientMaterial* pMaterial;
    SString          strName;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pMaterial);

    if (!argStream.HasErrors())
    {
        lua_pushnumber(luaVM, pMaterial->GetMaterialItem()->m_uiSizeX);
        lua_pushnumber(luaVM, pMaterial->GetMaterialItem()->m_uiSizeY);
        if (CFileTextureItem* pTextureItem = DynamicCast<CFileTextureItem>(pMaterial->GetMaterialItem()))
        {
            if (pTextureItem->m_TextureType == TTYPE_VOLUMETEXTURE)
            {
                lua_pushnumber(luaVM, pTextureItem->m_uiVolumeDepth);
                return 3;
            }
        }
        return 2;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetShaderValue(lua_State* luaVM)
{
    //  bool dxSetShaderValue( element shader, string name, mixed value )
    CClientShader* pShader;
    SString        strName;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pShader);
    argStream.ReadString(strName);

    if (!argStream.HasErrors())
    {
        // Try each mixed type in turn
        if (argStream.NextIsUserDataOfType<CLuaVector2D>())
        {
            CVector2D vecValue;
            argStream.ReadVector2D(vecValue);

            bool bResult = pShader->GetShaderItem()->SetValue(strName, &vecValue.fX, 2);
            lua_pushboolean(luaVM, bResult);
            return 1;
        }
        else if (argStream.NextIsUserDataOfType<CLuaVector3D>())
        {
            CVector vecValue;
            argStream.ReadVector3D(vecValue);

            bool bResult = pShader->GetShaderItem()->SetValue(strName, &vecValue.fX, 3);
            lua_pushboolean(luaVM, bResult);
            return 1;
        }
        else if (argStream.NextIsUserDataOfType<CLuaVector4D>())
        {
            CVector4D vecValue;
            argStream.ReadVector4D(vecValue);

            bool bResult = pShader->GetShaderItem()->SetValue(strName, &vecValue.fX, 4);
            lua_pushboolean(luaVM, bResult);
            return 1;
        }
        else if (argStream.NextIsUserDataOfType<CLuaMatrix>())
        {
            CMatrix matValue;
            argStream.ReadMatrix(matValue);
            float fBuffer[16];
            matValue.GetBuffer(fBuffer);

            bool bResult = pShader->GetShaderItem()->SetValue(strName, fBuffer, 16);
            lua_pushboolean(luaVM, bResult);
            return 1;
        }
        else if (argStream.NextIsUserData())
        {
            // Texture
            CClientTexture* pTexture;
            argStream.ReadUserData(pTexture);
            if (pTexture)
            {
                bool bResult = pShader->GetShaderItem()->SetValue(strName, pTexture->GetTextureItem());
                lua_pushboolean(luaVM, bResult);
                return 1;
            }
        }
        else if (argStream.NextIsBool())
        {
            // bool
            bool bValue;
            argStream.ReadBool(bValue);
            bool bResult = pShader->GetShaderItem()->SetValue(strName, bValue);
            lua_pushboolean(luaVM, bResult);
            return 1;
        }
        else if (argStream.NextCouldBeNumber())
        {
            // float(s)
            float fBuffer[16]{};
            uint  i;
            for (i = 0; i < NUMELMS(fBuffer);)
            {
                argStream.ReadNumber(fBuffer[i++]);
                if (!argStream.NextCouldBeNumber())
                    break;
            }
            bool bResult = pShader->GetShaderItem()->SetValue(strName, fBuffer, i);
            lua_pushboolean(luaVM, bResult);
            return 1;
        }
        else if (argStream.NextIsTable())
        {
            // table (of floats)
            float fBuffer[16]{};
            uint  i = 0;

            lua_pushnil(luaVM);  // Loop through our table, beginning at the first key
            while (lua_next(luaVM, argStream.m_iIndex) != 0 && i < NUMELMS(fBuffer))
            {
                fBuffer[i++] = static_cast<float>(lua_tonumber(luaVM, -1));  // Ignore the index at -2, and just read the value
                lua_pop(luaVM, 1);                                           // Remove the item and keep the key for the next iteration
            }
            bool bResult = pShader->GetShaderItem()->SetValue(strName, fBuffer, i);
            lua_pushboolean(luaVM, bResult);
            return 1;
        }
        argStream.SetCustomError("Expected number, bool, table or texture at argument 3");
    }
    if (argStream.HasErrors())
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetShaderDepthTextureValue(lua_State* luaVM)
{
    //  bool dxSetShaderDepthTextureValue( element shader, string name, sceneView theSceneView )
    // A scene view's depth-stencil target is owned directly by the CClientSceneView (never wrapped in
    // its own CClientDepthStencilTarget element), so it cannot go through dxSetShaderValue's generic
    // texture branch - passing a sceneView there already has an established, documented meaning (bind
    // its COLOR output, same element dxGetSceneViewTexture returns). This is a separate function so that
    // existing behavior is untouched.
    CClientShader*    pShader;
    SString           strName;
    CClientSceneView* pSceneView;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pShader);
    argStream.ReadString(strName);
    argStream.ReadUserData(pSceneView);

    if (!argStream.HasErrors())
    {
        CDepthStencilTargetItem* pDepthItem = pSceneView->GetDepthStencilTargetItem();
        if (pDepthItem && pDepthItem->m_pD3DTexture)
        {
            bool bResult = pShader->GetShaderItem()->SetValue(strName, pDepthItem);
            lua_pushboolean(luaVM, bResult);
            return 1;
        }
        m_pScriptDebugging->LogCustom(luaVM, "dxSetShaderDepthTextureValue: scene view has no sampleable depth target (create it with sampleableDepth = true)");
        lua_pushboolean(luaVM, false);
        return 1;
    }
    if (argStream.HasErrors())
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetShaderTessellation(lua_State* luaVM)
{
    //  bool dxSetShaderTessellation( element shader, int tessellationX, int tessellationY )
    //  bool shader:setShaderTessellation( element shader, Vector2 tessellation )
    CClientShader* pShader;
    CVector2D      vecTessellation;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pShader);
    argStream.ReadVector2D(vecTessellation);

    if (!argStream.HasErrors())
    {
        pShader->GetShaderItem()->SetTessellation((uint)vecTessellation.fX, (uint)vecTessellation.fY);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetShaderTransform(lua_State* luaVM)
{
    //  bool dxSetShaderTransform( element shader, lots )
    CClientShader*   pShader;
    SShaderTransform transform;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pShader);

    argStream.ReadVector3D(transform.vecRot, CVector());
    argStream.ReadVector3D(transform.vecRotCenOffset, CVector());
    argStream.ReadBool(transform.bRotCenOffsetOriginIsScreen, false);
    argStream.ReadVector2D(transform.vecPersCenOffset, CVector2D());
    argStream.ReadBool(transform.bPersCenOffsetOriginIsScreen, false);

    if (!argStream.HasErrors())
    {
        pShader->GetShaderItem()->SetTransform(transform);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetRenderTarget(lua_State* luaVM)
{
    //  bool setRenderTaget( element renderTarget [, bool clear = false ] )
    CClientRenderTarget* pRenderTarget;
    bool                 bClear;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pRenderTarget, NULL);
    argStream.ReadBool(bClear, false);

    // Check version ok for this function to be called now
    if (!g_pCore->GetGraphics()->GetRenderItemManager()->IsSetRenderTargetEnabledOldVer())
        MinClientReqCheck(argStream, MIN_CLIENT_REQ_DXSETRENDERTARGET_CALL_RESTRICTIONS, "function is being called outside certain events");

    if (!argStream.HasErrors())
    {
        bool bResult;
        if (pRenderTarget)
            bResult = g_pCore->GetGraphics()->GetRenderItemManager()->SetRenderTarget(pRenderTarget->GetRenderTargetItem(), bClear);
        else
            bResult = g_pCore->GetGraphics()->GetRenderItemManager()->RestoreDefaultRenderTarget();

        lua_pushboolean(luaVM, bResult);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxBeginRenderPass(lua_State* luaVM)
{
    //  bool dxBeginRenderPass( element target1 [, element target2, target3, target4] [, element depthStencilTarget] [, bool clear = true] )
    std::vector<CClientRenderTarget*> renderTargetList;
    CClientDepthStencilTarget*        pDepthStencilTarget = NULL;
    bool                              bClear = true;

    CScriptArgReader argStream(luaVM);
    while (renderTargetList.size() < MAX_MRT_RENDER_TARGETS && argStream.NextIsUserDataOfType<CClientRenderTarget>())
    {
        CClientRenderTarget* pRenderTarget = NULL;
        argStream.ReadUserData(pRenderTarget);
        renderTargetList.push_back(pRenderTarget);
    }
    if (argStream.NextIsUserDataOfType<CClientDepthStencilTarget>())
        argStream.ReadUserData(pDepthStencilTarget);
    argStream.ReadBool(bClear, true);

    if (!argStream.HasErrors() && renderTargetList.empty())
        argStream.SetCustomError("at least one render target is required", "Bad argument");

    if (!argStream.HasErrors())
    {
        CRenderTargetItem* colorTargets[MAX_MRT_RENDER_TARGETS] = {NULL, NULL, NULL, NULL};
        for (size_t i = 0; i < renderTargetList.size(); i++)
            colorTargets[i] = renderTargetList[i]->GetRenderTargetItem();

        CDepthStencilTargetItem* pDepthStencilTargetItem = pDepthStencilTarget ? pDepthStencilTarget->GetDepthStencilTargetItem() : NULL;

        bool bResult =
            g_pCore->GetGraphics()->GetRenderItemManager()->BeginRenderPass(colorTargets, (uint)renderTargetList.size(), pDepthStencilTargetItem, bClear);
        lua_pushboolean(luaVM, bResult);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxEndRenderPass(lua_State* luaVM)
{
    //  bool dxEndRenderPass()
    CScriptArgReader argStream(luaVM);

    if (!argStream.HasErrors())
    {
        bool bResult = g_pCore->GetGraphics()->GetRenderItemManager()->EndRenderPass();
        lua_pushboolean(luaVM, bResult);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxUpdateScreenSource(lua_State* luaVM)
{
    //  bool dxUpdateScreenSource( element screenSource [, bool resampleNow] )
    CClientScreenSource* pScreenSource;
    bool                 bResampleNow;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pScreenSource);
    argStream.ReadBool(bResampleNow, false);

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->GetRenderItemManager()->UpdateScreenSource(pScreenSource->GetScreenSourceItem(), bResampleNow);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxCreateFont(lua_State* luaVM)
{
    //  element dxCreateFont( string filepath [, int size=9, bool bold=false ] )
    SString      strFilePath;
    int          iSize;
    bool         bBold;
    eFontQuality ulFontQuality;

    CScriptArgReader argStream(luaVM);
    argStream.ReadString(strFilePath);
    argStream.ReadNumber(iSize, 9);
    argStream.ReadBool(bBold, false);
    argStream.ReadEnumString(ulFontQuality, FONT_QUALITY_PROOF);

    if (!argStream.HasErrors())
    {
        CLuaMain* pLuaMain = m_pLuaManager->GetVirtualMachine(luaVM);
        if (pLuaMain)
        {
            CResource* pParentResource = pLuaMain->GetResource();
            CResource* pFileResource = pParentResource;
            SString    strPath;
            if (CResourceManager::ParseResourcePathInput(strFilePath, pFileResource, &strPath))
            {
                if (FileExists(strPath))
                {
                    CClientDxFont* pDxFont = g_pClientGame->GetManager()->GetRenderElementManager()->CreateDxFont(strPath, iSize, bBold, ulFontQuality);

                    if (pDxFont)
                    {
                        // Make it a child of the resource's file root ** CHECK  Should parent be pFileResource, and element added to pParentResource's
                        // ElementGroup? **
                        pDxFont->SetParent(pParentResource->GetResourceDynamicEntity());
                        lua_pushelement(luaVM, pDxFont);
                        return 1;
                    }

                    argStream.SetCustomError(strFilePath, "Error creating font");
                }
                else
                    argStream.SetCustomError(strFilePath, "File not found");
            }
            else
                argStream.SetCustomError(strFilePath, "Bad file path");
        }
    }
    if (argStream.HasErrors())
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetTestMode(lua_State* luaVM)
{
    //  bool dxSetTestMode( string testMode )
    eDxTestMode testMode;

    CScriptArgReader argStream(luaVM);
    argStream.ReadEnumString(testMode, DX_TEST_MODE_NONE);

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->GetRenderItemManager()->SetTestMode(testMode);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetStatus(lua_State* luaVM)
{
    //  table dxGetStatus()

    CScriptArgReader argStream(luaVM);

    if (!argStream.HasErrors())
    {
        SDxStatus dxStatus;
        g_pCore->GetGraphics()->GetRenderItemManager()->GetDxStatus(dxStatus);

        lua_createtable(luaVM, 0, 24);

        lua_pushstring(luaVM, "TestMode");
        lua_pushstring(luaVM, EnumToString(dxStatus.testMode));
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoCardName");
        lua_pushstring(luaVM, dxStatus.videoCard.strName);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoCardRAM");
        lua_pushnumber(luaVM, dxStatus.videoCard.iInstalledMemoryKB / 1024);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoCardPSVersion");
        lua_pushstring(luaVM, dxStatus.videoCard.strPSVersion);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoCardMaxAnisotropy");
        lua_pushnumber(luaVM, dxStatus.videoCard.iMaxAnisotropy);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoCardNumRenderTargets");
        lua_pushnumber(luaVM, dxStatus.videoCard.iNumSimultaneousRTs);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoMemoryFreeForMTA");
        lua_pushnumber(luaVM, dxStatus.videoMemoryKB.iFreeForMTA / 1024);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoMemoryUsedByFonts");
        lua_pushnumber(luaVM, dxStatus.videoMemoryKB.iUsedByFonts / 1024);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoMemoryUsedByTextures");
        lua_pushnumber(luaVM, dxStatus.videoMemoryKB.iUsedByTextures / 1024);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VideoMemoryUsedByRenderTargets");
        lua_pushnumber(luaVM, dxStatus.videoMemoryKB.iUsedByRenderTargets / 1024);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingWindowed");
        lua_pushboolean(luaVM, dxStatus.settings.bWindowed);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingFullScreenStyle");
        lua_pushnumber(luaVM, dxStatus.settings.iFullScreenStyle);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingFXQuality");
        lua_pushnumber(luaVM, dxStatus.settings.iFXQuality);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingDrawDistance");
        lua_pushnumber(luaVM, dxStatus.settings.iDrawDistance);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingVolumetricShadows");
        lua_pushboolean(luaVM, dxStatus.settings.bVolumetricShadows);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingStreamingVideoMemoryForGTA");
        lua_pushnumber(luaVM, dxStatus.settings.iStreamingMemory);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "AllowScreenUpload");
        lua_pushboolean(luaVM, dxStatus.settings.bAllowScreenUpload);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "DepthBufferFormat");
        lua_pushstring(luaVM, EnumToString(dxStatus.videoCard.depthBufferFormat));
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "UsingDepthBuffer");
        lua_pushboolean(luaVM, dxStatus.state.iNumShadersUsingReadableDepthBuffer > 0);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "Setting32BitColor");
        lua_pushboolean(luaVM, dxStatus.settings.b32BitColor);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingGrassEffect");
        lua_pushboolean(luaVM, dxStatus.settings.bGrassEffect);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingHeatHaze");
        lua_pushboolean(luaVM, dxStatus.settings.bHeatHaze);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingAnisotropicFiltering");
        lua_pushnumber(luaVM, dxStatus.settings.iAnisotropicFiltering);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingAntiAliasing");
        lua_pushnumber(luaVM, dxStatus.settings.iAntiAliasing);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingAspectRatio");
        lua_pushstring(luaVM, EnumToString(dxStatus.settings.aspectRatio));
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingHUDMatchAspectRatio");
        lua_pushboolean(luaVM, dxStatus.settings.bHUDMatchAspectRatio);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingFOV");
        lua_pushnumber(luaVM, dxStatus.settings.fFieldOfView);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingHighDetailVehicles");
        lua_pushboolean(luaVM, dxStatus.settings.bHighDetailVehicles);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingHighDetailPeds");
        lua_pushboolean(luaVM, dxStatus.settings.bHighDetailPeds);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingBlur");
        lua_pushboolean(luaVM, dxStatus.settings.bBlur);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingCoronaReflections");
        lua_pushboolean(luaVM, dxStatus.settings.bCoronaReflections);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "SettingDynamicPedShadows");
        lua_pushboolean(luaVM, dxStatus.settings.bDynamicPedShadows);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "TotalPhysicalMemory");
        lua_pushnumber(luaVM, static_cast<lua_Number>(SharedUtil::GetWMITotalPhysicalMemory()) / 1024.0 / 1024.0);
        lua_settable(luaVM, -3);

        lua::Push(luaVM, "SettingDebugMode");
        lua::Push(luaVM,
                  []
                  {
                      switch (g_pCore->GetDiagnosticDebug())
                      {
                          case EDiagnosticDebug::GRAPHICS_6734:
                              return "#6734 Graphics";
                          case EDiagnosticDebug::D3D_6732:
                              return "#6732 D3D";
                          case EDiagnosticDebug::LOG_TIMING_0000:
                              return "#0000 Log timing";
                          case EDiagnosticDebug::JOYSTICK_0000:
                              return "#0000 Joystick";
                          case EDiagnosticDebug::LUA_TRACE_0000:
                              return "#0000 Lua trace";
                          case EDiagnosticDebug::RESIZE_ALWAYS_0000:
                              return "#0000 Resize always";
                          case EDiagnosticDebug::RESIZE_NEVER_0000:
                              return "#0000 Resize never";
                          default:
                              return "Default";
                      }
                  }());
        lua_settable(luaVM, -3);

        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetRenderCapabilities(lua_State* luaVM)
{
    //  table dxGetRenderCapabilities()
    //
    // A separate function from dxGetStatus (rather than added fields on it) so
    // existing scripts that assume dxGetStatus's table shape are never affected.

    CScriptArgReader argStream(luaVM);

    if (!argStream.HasErrors())
    {
        SDxCapabilities dxCaps;
        g_pCore->GetGraphics()->GetRenderItemManager()->GetDxCapabilities(dxCaps);

        lua_createtable(luaVM, 0, 13);

        lua_pushstring(luaVM, "PixelShader3Supported");
        lua_pushboolean(luaVM, dxCaps.bPixelShader3Supported);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "VertexShader3Supported");
        lua_pushboolean(luaVM, dxCaps.bVertexShader3Supported);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "MaxSimultaneousRenderTargets");
        lua_pushnumber(luaVM, dxCaps.iMaxSimultaneousRenderTargets);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "MaxBoundRenderTargets");
        lua_pushnumber(luaVM, dxCaps.iMaxBoundRenderTargets);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "IndependentMRTBlend");
        lua_pushboolean(luaVM, dxCaps.bIndependentMRTBlend);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "IndependentMRTWriteMasks");
        lua_pushboolean(luaVM, dxCaps.bIndependentMRTWriteMasks);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "DepthTextureSampleFormat");
        lua_pushstring(luaVM, EnumToString(dxCaps.depthTextureSampleFormat));
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "DepthTextureSamplingSupported");
        lua_pushboolean(luaVM, dxCaps.bDepthTextureSamplingSupported);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "CubemapRenderTargetSupported");
        lua_pushboolean(luaVM, dxCaps.bCubemapRenderTargetSupported);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "MaxCubemapEdgeLength");
        lua_pushnumber(luaVM, dxCaps.iMaxCubemapEdgeLength);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "MaxSceneViewsPerFrame");
        lua_pushnumber(luaVM, dxCaps.iMaxSceneViewsPerFrame);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "MaxRenderPassNestingDepth");
        lua_pushnumber(luaVM, dxCaps.iMaxRenderPassNestingDepth);
        lua_settable(luaVM, -3);

        lua_pushstring(luaVM, "RenderTargetFormats");
        lua_createtable(luaVM, 0, static_cast<int>(dxCaps.renderTargetFormats.size()));
        for (const auto& formatCap : dxCaps.renderTargetFormats)
        {
            lua_pushstring(luaVM, formatCap.strFormatName);

            lua_createtable(luaVM, 0, 3);
            lua_pushstring(luaVM, "renderable");
            lua_pushboolean(luaVM, formatCap.bRenderable);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "textureable");
            lua_pushboolean(luaVM, formatCap.bTextureable);
            lua_settable(luaVM, -3);
            lua_pushstring(luaVM, "filterable");
            lua_pushboolean(luaVM, formatCap.bFilterable);
            lua_settable(luaVM, -3);

            lua_settable(luaVM, -3);
        }
        lua_settable(luaVM, -3);

        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetTexturePixels(lua_State* luaVM)
{
    //  string dxGetTexturePixels( [ int surfaceIndex, ] element texture [, string pixelsFormat = "plain" [, string textureFormat = "argb"] [, bool mipmaps =
    //  true]]
    //                             [, int x, int y, int width, int height ] )
    CClientTexture*   pTexture;
    CVector2D         vecPosition;
    CVector2D         vecSize;
    int               surfaceIndex = 0;
    EPixelsFormatType pixelsFormat = EPixelsFormat::PLAIN;
    ERenderFormat     textureFormat = RFORMAT_UNKNOWN;
    bool              bMipMaps = true;

    CScriptArgReader argStream(luaVM);
    if (argStream.NextIsNumber())
        argStream.ReadNumber(surfaceIndex);
    argStream.ReadUserData(pTexture);

    if (argStream.NextIsEnumString(pixelsFormat))
    {
        argStream.ReadEnumString(pixelsFormat, EPixelsFormat::PLAIN);
        argStream.ReadIfNextIsEnumString(textureFormat, RFORMAT_UNKNOWN);
        argStream.ReadIfNextIsBool(bMipMaps, true);
    }

    argStream.ReadVector2D(vecPosition, CVector2D());
    argStream.ReadVector2D(vecSize, CVector2D());

    if (!argStream.HasErrors())
    {
        CVector2D vecEndPosition = vecPosition + vecSize;
        RECT      rc = {(int)vecPosition.fX, (int)vecPosition.fY, (int)vecEndPosition.fX, (int)vecEndPosition.fY};
        CPixels   pixels;

        // TODO: "height ? &rc : NULL" - height will always be set to 0 or another number! Why does this exist?
        if (g_pCore->GetGraphics()->GetPixelsManager()->GetTexturePixels(pTexture->GetTextureItem()->m_pD3DTexture, pixels, pixelsFormat, textureFormat,
                                                                         bMipMaps, vecSize.fY == 0 ? NULL : &rc, surfaceIndex))
        {
            lua_pushlstring(luaVM, pixels.GetData(), pixels.GetSize());
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetTexturePixels(lua_State* luaVM)
{
    //  bool dxSetTexturePixels( [ int sufaceIndex, ] element texture, string pixels [, int x, int y, int width, int height ] )
    CClientTexture* pTexture;
    CPixels         pixels;
    CVector2D       vecPosition;
    CVector2D       vecSize;
    int             surfaceIndex = 0;

    CScriptArgReader argStream(luaVM);
    if (argStream.NextIsNumber())
        argStream.ReadNumber(surfaceIndex);
    argStream.ReadUserData(pTexture);
    argStream.ReadCharStringRef(pixels.externalData);
    argStream.ReadVector2D(vecPosition, CVector2D());
    argStream.ReadVector2D(vecSize, CVector2D());

    if (!argStream.HasErrors())
    {
        CVector2D vecEndPosition = vecPosition + vecSize;
        RECT      rc = {(int)vecPosition.fX, (int)vecPosition.fY, (int)vecEndPosition.fX, (int)vecEndPosition.fY};
        if (g_pCore->GetGraphics()->GetPixelsManager()->SetTexturePixels(pTexture->GetTextureItem()->m_pD3DTexture, pixels, vecSize.fY == 0 ? NULL : &rc,
                                                                         surfaceIndex))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetPixelsSize(lua_State* luaVM)
{
    //  int x,y dxGetPixelsSize( string pixels )
    CPixels pixels;

    CScriptArgReader argStream(luaVM);
    argStream.ReadCharStringRef(pixels.externalData);

    if (!argStream.HasErrors())
    {
        uint uiSizeX;
        uint uiSizeY;
        if (g_pCore->GetGraphics()->GetPixelsManager()->GetPixelsSize(pixels, uiSizeX, uiSizeY))
        {
            lua_pushinteger(luaVM, uiSizeX);
            lua_pushinteger(luaVM, uiSizeY);
            return 2;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetPixelsFormat(lua_State* luaVM)
{
    //  string dxGetPixelsFormat( string pixels )
    CPixels pixels;

    CScriptArgReader argStream(luaVM);
    argStream.ReadCharStringRef(pixels.externalData);

    if (!argStream.HasErrors())
    {
        EPixelsFormatType format = g_pCore->GetGraphics()->GetPixelsManager()->GetPixelsFormat(pixels);
        if (format != EPixelsFormat::UNKNOWN)
        {
            lua_pushstring(luaVM, EnumToString(format));
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxConvertPixels(lua_State* luaVM)
{
    //  string dxConvertPixels( string pixels, string pixelFormat [, int quality] )
    CPixels           pixels;
    EPixelsFormatType format;
    int               quality;

    CScriptArgReader argStream(luaVM);
    argStream.ReadCharStringRef(pixels.externalData);
    argStream.ReadEnumString(format);
    argStream.ReadNumber(quality, 80);

    if (!argStream.HasErrors())
    {
        CPixels newPixels;
        if (g_pCore->GetGraphics()->GetPixelsManager()->ChangePixelsFormat(pixels, newPixels, format, quality))
        {
            lua_pushlstring(luaVM, newPixels.GetData(), newPixels.GetSize());
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetPixelColor(lua_State* luaVM)
{
    //  int r,g,b,a dxGetPixelColor( string pixels, int x, int y )
    CPixels   pixels;
    CVector2D vecPosition;

    CScriptArgReader argStream(luaVM);
    argStream.ReadCharStringRef(pixels.externalData);
    argStream.ReadVector2D(vecPosition);

    if (!argStream.HasErrors())
    {
        SColor color;
        if (g_pCore->GetGraphics()->GetPixelsManager()->GetPixelColor(pixels, (int)vecPosition.fX, (int)vecPosition.fY, color))
        {
            lua_pushnumber(luaVM, color.R);
            lua_pushnumber(luaVM, color.G);
            lua_pushnumber(luaVM, color.B);
            lua_pushnumber(luaVM, color.A);
            return 4;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetPixelColor(lua_State* luaVM)
{
    //  bool dxSetPixelColor( string pixels, int x, int y, int r, int g, int b [, int a] )
    CPixels   pixels;
    CVector2D vecPosition;
    SColor    color;

    CScriptArgReader argStream(luaVM);
    argStream.ReadCharStringRef(pixels.externalData);
    argStream.ReadVector2D(vecPosition);
    argStream.ReadNumber(color.R);
    argStream.ReadNumber(color.G);
    argStream.ReadNumber(color.B);
    argStream.ReadNumber(color.A, 255);

    if (!argStream.HasErrors())
    {
        if (g_pCore->GetGraphics()->GetPixelsManager()->SetPixelColor(pixels, (int)vecPosition.fX, (int)vecPosition.fY, color))
        {
            lua_pushboolean(luaVM, true);
            return 1;
        }
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // error: bad arguments
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxSetBlendMode(lua_State* luaVM)
{
    //  bool dxSetBlendMode ( string blendMode )
    EBlendModeType blendMode;

    CScriptArgReader argStream(luaVM);
    argStream.ReadEnumString(blendMode, EBlendMode::BLEND);

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->SetBlendMode(blendMode);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxGetBlendMode(lua_State* luaVM)
{
    //  string dxGetBlendMode ()
    EBlendModeType blendMode = g_pCore->GetGraphics()->GetBlendMode();
    lua_pushstring(luaVM, EnumToString(blendMode));
    return 1;
}

int CLuaDrawingDefs::DxSetAspectRatioAdjustmentEnabled(lua_State* luaVM)
{
    //  bool dxSetAspectRatioAdjustmentEnabled( bool enabled, float sourceRatio = 4/3 )
    bool  bEnabled;
    float fSourceRatio;

    CScriptArgReader argStream(luaVM);
    argStream.ReadBool(bEnabled);
    argStream.ReadNumber(fSourceRatio, 4 / 3.f);

    if (!g_bAllowAspectRatioAdjustment)
        argStream.SetCustomError("Function can only be used inside certain events");

    if (!argStream.HasErrors())
    {
        g_pCore->GetGraphics()->SetAspectRatioAdjustmentEnabled(bEnabled, fSourceRatio);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    // Failed
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaDrawingDefs::DxIsAspectRatioAdjustmentEnabled(lua_State* luaVM)
{
    //  bool, float dxIsAspectRatioAdjustmentEnabled()
    bool  bEnabled = g_pCore->GetGraphics()->IsAspectRatioAdjustmentEnabled();
    float fSourceRatio = g_pCore->GetGraphics()->GetAspectRatioAdjustmentSourceRatio();
    lua_pushboolean(luaVM, bEnabled);
    lua_pushnumber(luaVM, fSourceRatio);
    return 2;
}

int CLuaDrawingDefs::DxSetTextureEdge(lua_State* luaVM)
{
    //  bool dxSetTextureEdge ( texture theTexture, string textureEdge [, int border-color )
    CClientTexture* pTexture;
    ETextureAddress textureAddress;
    SColor          borderColor;

    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pTexture);
    argStream.ReadEnumString(textureAddress);
    argStream.ReadColor(borderColor, 0);

    if (!argStream.HasErrors())
    {
        pTexture->GetMaterialItem()->m_TextureAddress = textureAddress;
        pTexture->GetMaterialItem()->m_uiBorderColor = borderColor;
        lua_pushboolean(luaVM, true);
        return 1;
    }
    else
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());

    lua_pushboolean(luaVM, false);
    return 1;
}

bool CLuaDrawingDefs::DxDrawWiredSphere(lua_State* const luaVM, const CVector position, const float radius, std::optional<SColor> color,
                                        const std::optional<float> lineWidth, const std::optional<unsigned int> iterations)
{
    // Greater than 4, crash the game
    if (iterations.has_value() && (*iterations == 0 || *iterations > 4))
        throw std::invalid_argument("Iterations must be between 1 and 4");

    if (!color)
        color = SColorARGB(64, 255, 0, 0);

    g_pCore->GetGraphics()->DrawWiredSphere(position, radius, color.value(), lineWidth.value_or(1), iterations.value_or(1));
    return true;
}

bool CLuaDrawingDefs::DxDrawModel3D(std::uint32_t modelID, CVector position, CVector rotation, const std::optional<CVector> scale,
                                    const std::optional<float> lighting)
{
    std::uint16_t runtimeModelID = 0;
    if (modelID > SERVER_MODEL_ID_MAX ||
        !g_pClientGame->GetManager()->GetModelManager()->ResolveModelID(modelID, runtimeModelID, nullptr, false))
        throw std::invalid_argument("Invalid model ID");

    CModelInfo* pModelInfo = g_pGame->GetModelInfo(runtimeModelID);
    if (!pModelInfo)
        throw std::invalid_argument("Invalid model ID");

    if (auto modelType = pModelInfo->GetModelType();
        modelType == eModelInfoType::UNKNOWN || modelType == eModelInfoType::VEHICLE || modelType == eModelInfoType::PED)
    {
        throw std::invalid_argument("Invalid model type");
    }

    ConvertDegreesToRadians(rotation);

    return g_pClientGame->GetModelRenderer()->EnqueueModel(pModelInfo, CMatrix{position, rotation, scale.value_or(CVector{1.0f, 1.0f, 1.0f})},
                                                           lighting.value_or(0.0f));
}
