/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        game_sa/CDefinitiveRadarSA.cpp
 *  PURPOSE:     GTA Definitive Edition-style radar renderer
 *
 *  Camera constants, map-space conversion and presentation behaviour are
 *  derived from Radar Trilogy SA by LLIEPLLIEHb (MIT). The integration,
 *  resource lifetime and GTA/MTA dispatch code are specific to MTA.
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CDefinitiveRadarSA.h"

#include "CGameSA.h"
#include "CMarkerSA.h"
#include "CRadarSA.h"
#include "CRenderWareSA.h"
#include "gamesa_renderware.h"

#include <core/CCoreInterface.h>
#include <core/CGraphicsInterface.h>
#include <game/CCam.h>
#include <game/CCamera.h>
#include <game/CHud.h>
#include <game/CModelInfo.h>
#include <game/CPed.h>
#include <game/CPools.h>
#include <game/CVehicle.h>
#include <game/RenderWareD3D.h>
#include <unrar/dll.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

extern CGameSA*        pGame;
extern CCoreInterface* g_pCore;

namespace
{
    constexpr DWORD FUNC_DRAW_RADAR = 0x58A330;
    constexpr DWORD FUNC_DRAW_HUD = 0x58FAE0;
    constexpr DWORD VAR_FRONTEND_MAP_ACTIVE = 0xBA67A1;
    constexpr DWORD VAR_RADAR_RANGE = 0xBA8314;
    constexpr DWORD VAR_SCREEN_WIDTH = 0xC17044;
    constexpr DWORD VAR_SCREEN_HEIGHT = 0xC17048;

    constexpr int   MAP_TILE_COUNT = 144;
    constexpr int   MAP_TILES_PER_ROW = 12;
    constexpr float MAP_SIZE = 6000.0f;
    constexpr float MAP_OFFSET_X = 3000.0f;
    constexpr float MAP_OFFSET_Y = -3000.0f;

    constexpr float RADAR_PI = 3.14159265358979323846f;
    constexpr float DEFAULT_CAMERA_HEIGHT = 445.0f;
    constexpr float DEFAULT_CAMERA_OFFSET_Y = -105.0f;
    constexpr float DEFAULT_CAMERA_PITCH = -26.0f * RADAR_PI / 180.0f;
    constexpr float DEFAULT_CAMERA_FOV = 70.0f * RADAR_PI / 180.0f;
    constexpr float NEAR_PLANE = 0.3f;
    constexpr float FAR_PLANE = 10000.0f;

    constexpr std::uint64_t MAP_TXD_SIZE = 151013416;
    constexpr std::uint64_t BLIP_TXD_SIZE = 1434412;

    using DrawRadarFunction = void(__cdecl*)();
    DrawRadarFunction g_OriginalDrawRadar = nullptr;
    void*             g_OriginalDrawRadarGateway = nullptr;
    bool              g_RadarHookInstalled = false;
    bool              g_RadarVisible = true;

    struct ArchiveHandleDeleter
    {
        void operator()(HANDLE archive) const noexcept
        {
            if (archive)
                RARCloseArchive(archive);
        }
    };

    using ArchiveHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, ArchiveHandleDeleter>;

    struct Vec3
    {
        float x{};
        float y{};
        float z{};
    };

    Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
    {
        return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }
    Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
    {
        return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }
    Vec3 operator*(const Vec3& value, float scalar)
    {
        return {value.x * scalar, value.y * scalar, value.z * scalar};
    }

    float Dot(const Vec3& lhs, const Vec3& rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
    {
        return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x};
    }

    Vec3 Normalize(const Vec3& value)
    {
        const float length = std::sqrt(Dot(value, value));
        return length > 0.00001f ? value * (1.0f / length) : Vec3{};
    }

    struct CameraState
    {
        Vec3  position{};
        Vec3  playerMapPosition{};
        Vec3  xAxis{};
        Vec3  yAxis{};
        Vec3  zAxis{};
        float yaw{};
        float fov{DEFAULT_CAMERA_FOV};
    };

    struct ScreenVertex
    {
        float x;
        float y;
        float z;
        float rhw;
        DWORD color;
        float u;
        float v;
    };

    struct ViewVertex
    {
        float x;
        float y;
        float z;
        float u;
        float v;
    };

    struct WorldVertex
    {
        float x;
        float y;
        float z;
        DWORD color;
        float u;
        float v;
    };

    constexpr DWORD SCREEN_VERTEX_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    constexpr DWORD WORLD_VERTEX_FVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

    IDirect3DTexture9* GetD3DTexture(RwTexDictionary* dictionary, const char* name)
    {
        if (!dictionary || !name)
            return nullptr;

        RwTexture* texture = RwTexDictionaryFindNamedTexture(dictionary, name);
        if (!texture || !texture->raster)
            return nullptr;

        auto* raster = reinterpret_cast<RwD3D9Raster*>(&texture->raster->renderResource);
        return raster->texture;
    }

    bool GetTextureCopyLayout(D3DFORMAT format, UINT width, UINT height, std::size_t& rowBytes, UINT& rowCount)
    {
        switch (format)
        {
            case D3DFMT_A8R8G8B8:
            case D3DFMT_X8R8G8B8:
            case D3DFMT_A8B8G8R8:
            case D3DFMT_X8B8G8R8:
                rowBytes = static_cast<std::size_t>(width) * 4;
                rowCount = height;
                return true;
            case D3DFMT_R5G6B5:
            case D3DFMT_X1R5G5B5:
            case D3DFMT_A1R5G5B5:
            case D3DFMT_A4R4G4B4:
                rowBytes = static_cast<std::size_t>(width) * 2;
                rowCount = height;
                return true;
            case D3DFMT_A8:
            case D3DFMT_L8:
            case D3DFMT_P8:
                rowBytes = width;
                rowCount = height;
                return true;
            case D3DFMT_DXT1:
                rowBytes = static_cast<std::size_t>(std::max(1u, (width + 3) / 4)) * 8;
                rowCount = std::max(1u, (height + 3) / 4);
                return true;
            case D3DFMT_DXT2:
            case D3DFMT_DXT3:
            case D3DFMT_DXT4:
            case D3DFMT_DXT5:
                rowBytes = static_cast<std::size_t>(std::max(1u, (width + 3) / 4)) * 16;
                rowCount = std::max(1u, (height + 3) / 4);
                return true;
            default:
                return false;
        }
    }

    IDirect3DTexture9* CloneTextureForRawDevice(IDirect3DDevice9* device, IDirect3DTexture9* source)
    {
        if (!device || !source)
            return nullptr;

        D3DSURFACE_DESC baseDescription{};
        const UINT      levelCount = source->GetLevelCount();
        if (!levelCount || FAILED(source->GetLevelDesc(0, &baseDescription)))
            return nullptr;

        IDirect3DTexture9* copy = nullptr;
        if (FAILED(
                device->CreateTexture(baseDescription.Width, baseDescription.Height, levelCount, 0, baseDescription.Format, D3DPOOL_MANAGED, &copy, nullptr)) ||
            !copy)
            return nullptr;

        for (UINT level = 0; level < levelCount; ++level)
        {
            D3DSURFACE_DESC description{};
            std::size_t     rowBytes = 0;
            UINT            rowCount = 0;
            if (FAILED(source->GetLevelDesc(level, &description)) ||
                !GetTextureCopyLayout(description.Format, description.Width, description.Height, rowBytes, rowCount))
            {
                copy->Release();
                return nullptr;
            }

            D3DLOCKED_RECT sourceLock{};
            D3DLOCKED_RECT destinationLock{};
            if (FAILED(source->LockRect(level, &sourceLock, nullptr, D3DLOCK_READONLY)))
            {
                copy->Release();
                return nullptr;
            }
            if (FAILED(copy->LockRect(level, &destinationLock, nullptr, 0)))
            {
                source->UnlockRect(level);
                copy->Release();
                return nullptr;
            }

            const std::size_t copyBytes =
                std::min({rowBytes, static_cast<std::size_t>(std::abs(sourceLock.Pitch)), static_cast<std::size_t>(std::abs(destinationLock.Pitch))});
            for (UINT row = 0; row < rowCount; ++row)
            {
                const auto* sourceBytes = static_cast<const BYTE*>(sourceLock.pBits) + static_cast<std::ptrdiff_t>(row) * sourceLock.Pitch;
                auto*       destinationBytes = static_cast<BYTE*>(destinationLock.pBits) + static_cast<std::ptrdiff_t>(row) * destinationLock.Pitch;
                std::memcpy(destinationBytes, sourceBytes, copyBytes);
            }

            copy->UnlockRect(level);
            source->UnlockRect(level);
        }

        return copy;
    }

    SString FindAssetArchive()
    {
        // gta_sa.exe runs from MTA's isolated GTA mirror, so GetLaunchPath()
        // does not identify the custom client installation. Resolve the base
        // directory from core.dll instead, which remains stable for installed
        // and development builds.
        const SString&               mtaBaseDirectory = SharedUtil::GetMTAProcessBaseDir();
        const std::array<SString, 3> candidates = {
            PathJoin(mtaBaseDirectory, "MTA", "radar-definitive", "texture.rar"),
            PathJoin(mtaBaseDirectory, "radar-definitive", "texture.rar"),
            PathJoin(SharedUtil::GetMTADataPath(), "radar-definitive", "texture.rar"),
        };

        for (const SString& candidate : candidates)
        {
            if (FileExists(candidate))
                return candidate;
        }
        return {};
    }

    bool ExtractRadarAssets(SString& mapPath, SString& blipPath)
    {
        const SString cacheDirectory = PathJoin(SharedUtil::GetMTADataPath(), "radar-definitive", "096aa7a7");
        mapPath = PathJoin(cacheDirectory, "map.txd");
        blipPath = PathJoin(cacheDirectory, "blip.txd");

        if (FileSize(mapPath) == MAP_TXD_SIZE && FileSize(blipPath) == BLIP_TXD_SIZE)
            return true;

        const SString archivePath = FindAssetArchive();
        if (archivePath.empty())
        {
            OutputReleaseLine("[Radar] Definitive assets are missing (MTA/radar-definitive/texture.rar)");
            return false;
        }

        if (!SharedUtil::MkDir(cacheDirectory))
        {
            OutputReleaseLine("[Radar] Could not create the Definitive radar cache directory");
            return false;
        }

        RAROpenArchiveDataEx archiveData{};
        archiveData.ArcName = const_cast<char*>(archivePath.c_str());
        archiveData.OpenMode = RAR_OM_EXTRACT;
        ArchiveHandle archive{RAROpenArchiveEx(&archiveData)};
        if (!archive || archiveData.OpenResult != ERAR_SUCCESS)
        {
            OutputReleaseLine("[Radar] Could not open the Definitive radar texture archive");
            return false;
        }

        bool processedMap = false;
        bool processedBlips = false;
        while (true)
        {
            RARHeaderDataEx header{};
            const int       readResult = RARReadHeaderEx(archive.get(), &header);
            if (readResult == ERAR_END_ARCHIVE)
                break;
            if (readResult != ERAR_SUCCESS)
                return false;

            const bool isMap = _stricmp(header.FileName, "map.txd") == 0;
            const bool isBlips = _stricmp(header.FileName, "blip.txd") == 0;
            const int  operation = (isMap || isBlips) ? RAR_EXTRACT : RAR_SKIP;
            if (RARProcessFile(archive.get(), operation, const_cast<char*>(cacheDirectory.c_str()), nullptr) != ERAR_SUCCESS)
                return false;

            processedMap |= isMap;
            processedBlips |= isBlips;
        }

        const bool valid = processedMap && processedBlips && FileSize(mapPath) == MAP_TXD_SIZE && FileSize(blipPath) == BLIP_TXD_SIZE;
        if (!valid)
            OutputReleaseLine("[Radar] Definitive radar texture extraction failed validation");
        return valid;
    }

    class CDefinitiveRadarRenderer
    {
    public:
        ~CDefinitiveRadarRenderer() { Shutdown(); }

        bool Render(bool& safeToFallback)
        {
            safeToFallback = true;
            if (!m_Device)
            {
                CGraphicsInterface* graphics = g_pCore ? g_pCore->GetGraphics() : nullptr;
                m_Device = graphics ? graphics->GetDevice() : nullptr;
            }

            if (!m_Device || m_Device->TestCooperativeLevel() != D3D_OK)
                return false;

            if (!m_Initialized)
            {
                // TXD loading also touches RenderWare/D3D state. Isolate the
                // one-time asset conversion just like every radar frame so a
                // resource effect active around CHud::DrawRadar survives it.
                IDirect3DStateBlock9* initializationState = nullptr;
                if (FAILED(m_Device->CreateStateBlock(D3DSBT_ALL, &initializationState)) || !initializationState || FAILED(initializationState->Capture()))
                {
                    SafeRelease(initializationState);
                    return false;
                }

                const bool initialized = EnsureInitialized();
                const bool stateRestored = SUCCEEDED(initializationState->Apply());
                SafeRelease(initializationState);
                if (!initialized || !stateRestored)
                {
                    if (!stateRestored)
                    {
                        Shutdown();
                        m_InitializationAttempted = true;
                        safeToFallback = false;
                    }
                    return false;
                }
            }

            CPed* player = pGame->GetPools()->GetPedFromRef(1);
            if (!player || player->GetAreaCode() != 0)
                return true;

            CVector2D layoutPosition;
            CVector2D layoutSize;
            if (!pGame->GetRadar()->GetLayout(layoutPosition, layoutSize))
                return false;

            const float radarSize = std::max(1.0f, std::min(layoutSize.fX, layoutSize.fY));
            const float radarX = layoutPosition.fX + (layoutSize.fX - radarSize) * 0.5f;
            const float radarY = layoutPosition.fY + (layoutSize.fY - radarSize) * 0.5f;
            const int   targetSize = std::max(1, static_cast<int>(radarSize + 0.5f));

            CameraState camera;
            if (!UpdateCamera(player, camera))
                return false;

            IDirect3DSurface9* oldTarget = nullptr;
            IDirect3DSurface9* oldDepth = nullptr;
            D3DVIEWPORT9       oldViewport{};
            const HRESULT      getDepthResult = m_Device->GetDepthStencilSurface(&oldDepth);
            if (FAILED(m_Device->GetRenderTarget(0, &oldTarget)) || !oldTarget || (FAILED(getDepthResult) && getDepthResult != D3DERR_NOTFOUND) ||
                FAILED(m_Device->GetViewport(&oldViewport)))
            {
                SafeRelease(oldTarget);
                SafeRelease(oldDepth);
                return false;
            }

            IDirect3DTexture9* targetTexture = nullptr;
            IDirect3DSurface9* targetSurface = nullptr;
            const HRESULT      createResult =
                m_Device->CreateTexture(targetSize, targetSize, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &targetTexture, nullptr);
            if (SUCCEEDED(createResult) && targetTexture)
                targetTexture->GetSurfaceLevel(0, &targetSurface);

            if (!targetSurface)
            {
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                SafeRelease(oldTarget);
                SafeRelease(oldDepth);
                return false;
            }

            const D3DVIEWPORT9 radarViewport{0, 0, static_cast<DWORD>(targetSize), static_cast<DWORD>(targetSize), 0.0f, 1.0f};
            // Vanilla CHud::DrawRadar is also the RenderWare world-to-HUD
            // transition: its texture bindings close world shaders and its
            // final fixed-function states are consumed by the HUD and chat.
            // Run it only on the throwaway target, then capture its real exit
            // state before replacing the pixels with the Definitive radar.
            safeToFallback = false;
            if (FAILED(m_Device->SetDepthStencilSurface(nullptr)) || FAILED(m_Device->SetRenderTarget(0, targetSurface)) ||
                FAILED(m_Device->SetViewport(&radarViewport)))
            {
                safeToFallback = RestoreTargets(oldTarget, oldDepth, oldViewport);
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                SafeRelease(oldTarget);
                SafeRelease(oldDepth);
                return false;
            }

            if (!g_OriginalDrawRadar)
            {
                safeToFallback = RestoreTargets(oldTarget, oldDepth, oldViewport);
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                SafeRelease(oldTarget);
                SafeRelease(oldDepth);
                return false;
            }
            g_OriginalDrawRadar();

            if (!RestoreTargets(oldTarget, oldDepth, oldViewport))
            {
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                SafeRelease(oldTarget);
                SafeRelease(oldDepth);
                return false;
            }

            IDirect3DStateBlock9* stateBlock = nullptr;
            if (FAILED(m_Device->CreateStateBlock(D3DSBT_ALL, &stateBlock)) || !stateBlock || FAILED(stateBlock->Capture()))
            {
                SafeRelease(stateBlock);
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                SafeRelease(oldTarget);
                SafeRelease(oldDepth);
                safeToFallback = true;
                return false;
            }

            if (FAILED(m_Device->SetDepthStencilSurface(nullptr)) || FAILED(m_Device->SetRenderTarget(0, targetSurface)) ||
                FAILED(m_Device->SetViewport(&radarViewport)))
            {
                safeToFallback = RestoreDevice(oldTarget, oldDepth, oldViewport, stateBlock);
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                return false;
            }

            const bool  drawMap = pGame->GetHud()->IsComponentVisible(HUD_RADAR_MAP);
            const DWORD clearColor = drawMap ? D3DCOLOR_ARGB(255, 123, 196, 249) : 0;
            if (FAILED(m_Device->Clear(0, nullptr, D3DCLEAR_TARGET, clearColor, 1.0f, 0)))
            {
                safeToFallback = RestoreDevice(oldTarget, oldDepth, oldViewport, stateBlock);
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                return false;
            }

            ConfigureWorldDrawing(camera);
            if (drawMap)
                DrawMap();

            if (FAILED(m_Device->SetRenderTarget(0, oldTarget)) || FAILED(m_Device->SetDepthStencilSurface(oldDepth)) ||
                FAILED(m_Device->SetViewport(&oldViewport)))
            {
                safeToFallback = RestoreDevice(oldTarget, oldDepth, oldViewport, stateBlock);
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                return false;
            }

            ConfigureTexturedDrawing();
            DrawCircle(targetTexture, radarX, radarY, radarSize);
            DrawBorder(radarX, radarY, radarSize, std::max(2.0f, radarSize * (8.0f / 265.0f)));

            if (pGame->GetHud()->IsComponentVisible(HUD_RADAR_BLIPS))
            {
                DrawBlips(camera, radarX, radarY, radarSize, static_cast<float>(targetSize));
                DrawNorth(camera, radarX, radarY, radarSize);
            }

            // Preserve the projected DE player marker while compositing it last,
            // matching GTA's ordering when another blip shares its position. Reuse
            // the map target for a transparent player-only pass after its pixels
            // have already been presented, avoiding a second per-frame allocation.
            m_Device->SetTexture(0, nullptr);
            if (FAILED(m_Device->SetDepthStencilSurface(nullptr)) || FAILED(m_Device->SetRenderTarget(0, targetSurface)) ||
                FAILED(m_Device->SetViewport(&radarViewport)) || FAILED(m_Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))
            {
                safeToFallback = RestoreDevice(oldTarget, oldDepth, oldViewport, stateBlock);
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                return false;
            }

            ConfigureWorldDrawing(camera);
            // Store the texture's straight alpha in the cleared overlay target;
            // blending here and again during composition would attenuate its edges twice.
            m_Device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            DrawPlayer(player);

            if (FAILED(m_Device->SetRenderTarget(0, oldTarget)) || FAILED(m_Device->SetDepthStencilSurface(oldDepth)) ||
                FAILED(m_Device->SetViewport(&oldViewport)))
            {
                safeToFallback = RestoreDevice(oldTarget, oldDepth, oldViewport, stateBlock);
                SafeRelease(targetSurface);
                SafeRelease(targetTexture);
                return false;
            }

            ConfigureTexturedDrawing();
            DrawCircle(targetTexture, radarX, radarY, radarSize);

            const bool restored = RestoreDevice(oldTarget, oldDepth, oldViewport, stateBlock);
            SafeRelease(targetSurface);
            SafeRelease(targetTexture);
            safeToFallback = restored;
            return restored;
        }

        void Shutdown()
        {
            for (IDirect3DTexture9*& texture : m_MapTextures)
                SafeRelease(texture);
            for (IDirect3DTexture9*& texture : m_BlipTextures)
                SafeRelease(texture);

            DestroyDictionary(m_MapDictionary);
            DestroyDictionary(m_BlipDictionary);
            m_Device = nullptr;
            m_InitializationAttempted = false;
            m_Initialized = false;
        }

    private:
        template <class T>
        static void SafeRelease(T*& object)
        {
            if (object)
            {
                object->Release();
                object = nullptr;
            }
        }

        bool EnsureInitialized()
        {
            if (m_Initialized)
                return true;
            if (m_InitializationAttempted)
                return false;
            m_InitializationAttempted = true;

            CGraphicsInterface* graphics = g_pCore ? g_pCore->GetGraphics() : nullptr;
            m_Device = graphics ? graphics->GetDevice() : nullptr;
            if (!m_Device)
                return false;

            SString mapPath;
            SString blipPath;
            if (!ExtractRadarAssets(mapPath, blipPath))
                return false;

            auto*            renderWare = static_cast<CRenderWareSA*>(pGame->GetRenderWare());
            RwTexDictionary* previousDictionary = RwTexDictionaryGetCurrent();
            m_MapDictionary = renderWare->ReadTXD(mapPath, {});
            m_BlipDictionary = renderWare->ReadTXD(blipPath, {});
            RwTexDictionarySetCurrent(previousDictionary);
            if (!m_MapDictionary || !m_BlipDictionary)
            {
                Shutdown();
                m_InitializationAttempted = true;
                OutputReleaseLine("[Radar] Could not load the Definitive radar TXD dictionaries");
                return false;
            }

            for (int index = 0; index < MAP_TILE_COUNT; ++index)
            {
                char name[16];
                snprintf(name, sizeof(name), "radar%02d", index);
                m_MapTextures[index] = CloneTextureForRawDevice(m_Device, GetD3DTexture(m_MapDictionary, name));
            }
            for (int index = 2; index < static_cast<int>(m_BlipTextures.size()); ++index)
            {
                char name[16];
                snprintf(name, sizeof(name), "%d", index);
                m_BlipTextures[index] = CloneTextureForRawDevice(m_Device, GetD3DTexture(m_BlipDictionary, name));
            }

            const bool allMapTilesLoaded =
                std::all_of(m_MapTextures.begin(), m_MapTextures.end(), [](IDirect3DTexture9* texture) { return texture != nullptr; });
            if (!allMapTilesLoaded || !m_BlipTextures[2])
            {
                Shutdown();
                m_InitializationAttempted = true;
                OutputReleaseLine("[Radar] Definitive radar TXD is incomplete");
                return false;
            }

            // Every texture used by the renderer is now an independent
            // managed-device copy. Keeping the source TXDs alive would retain
            // another ~145 MiB and eventually starve transient render targets
            // and unrelated UI textures on lower-memory devices.
            DestroyDictionary(m_MapDictionary);
            DestroyDictionary(m_BlipDictionary);

            m_Initialized = true;
            OutputReleaseLine("[Radar] Definitive Edition renderer loaded 144 map tiles and its blip atlas");
            return true;
        }

        bool UpdateCamera(CPed* player, CameraState& camera)
        {
            CVector* playerPosition = player->GetPosition();
            if (!playerPosition)
                return false;

            CCam*       activeCamera = pGame->GetCamera()->GetCam(pGame->GetCamera()->GetActiveCam());
            CVector*    front = activeCamera ? activeCamera->GetFront() : nullptr;
            const float yaw = front ? std::atan2(front->fX, front->fY) : 0.0f;

            float targetHeight = DEFAULT_CAMERA_HEIGHT;
            float targetOffset = DEFAULT_CAMERA_OFFSET_Y;
            float targetFov = DEFAULT_CAMERA_FOV;
            float targetPitch = DEFAULT_CAMERA_PITCH;

            if (CVehicle* vehicle = player->GetVehicle())
            {
                CModelInfo* modelInfo = pGame->GetModelInfo(vehicle->GetModelIndex());
                if (modelInfo && modelInfo->IsPlane())
                {
                    targetHeight = 500.0f;
                    targetOffset = -60.0f;
                }
                else if (modelInfo && modelInfo->IsHeli())
                {
                    targetHeight = 480.0f;
                    targetOffset = -75.0f;
                }
                else
                {
                    CVector velocity;
                    vehicle->GetMoveSpeed(&velocity);
                    const float speedKmh = std::sqrt(velocity.fX * velocity.fX + velocity.fY * velocity.fY) * 180.0f;
                    const float factor = std::clamp((speedKmh - 60.0f) / 40.0f, 0.0f, 1.0f);
                    targetHeight = DEFAULT_CAMERA_HEIGHT + (471.25f - DEFAULT_CAMERA_HEIGHT) * factor;
                    targetOffset = DEFAULT_CAMERA_OFFSET_Y + (-81.25f - DEFAULT_CAMERA_OFFSET_Y) * factor;
                    targetFov = DEFAULT_CAMERA_FOV + (RADAR_PI / 180.0f) * factor;
                }
            }

            constexpr float INTERPOLATION = 0.15f;
            m_CameraHeight += (targetHeight - m_CameraHeight) * INTERPOLATION;
            m_CameraOffset += (targetOffset - m_CameraOffset) * INTERPOLATION;
            m_CameraFov += (targetFov - m_CameraFov) * INTERPOLATION;
            m_CameraPitch += (targetPitch - m_CameraPitch) * INTERPOLATION;

            const float invertedYaw = -yaw;
            const float cameraMapX = playerPosition->fX + MAP_OFFSET_X + m_CameraOffset * std::sin(invertedYaw);
            const float cameraMapY = playerPosition->fY + MAP_OFFSET_Y - m_CameraOffset * std::cos(invertedYaw);

            const float cp = std::cos(m_CameraPitch);
            const float sp = std::sin(m_CameraPitch);
            const float cy = std::cos(invertedYaw);
            const float sy = std::sin(invertedYaw);

            const Vec3  worldRight{cy, sy, 0.0f};
            const Vec3  worldForward{-cp * sy, cy * cp, sp};
            const Vec3  worldUp{sy * sp, -cy * sp, cp};
            const float dotDown = std::clamp(Dot({0.0f, 0.0f, -1.0f}, worldForward), -1.0f, 1.0f);
            const float rotationOffset = 600.0f * std::acos(dotDown) / (0.5f * RADAR_PI);
            const Vec3  shaderOffset = worldRight + worldForward - worldUp * rotationOffset;
            const Vec3  viewPosition{cameraMapX + shaderOffset.x, cameraMapY + shaderOffset.y, m_CameraHeight + shaderOffset.z};

            camera.zAxis = Normalize(worldForward);
            camera.xAxis = Normalize(Cross(worldUp * -1.0f, camera.zAxis));
            camera.yAxis = Cross(camera.xAxis, camera.zAxis);
            camera.position = viewPosition;
            camera.playerMapPosition = {playerPosition->fX + MAP_OFFSET_X, playerPosition->fY + MAP_OFFSET_Y, playerPosition->fZ};
            camera.yaw = yaw;
            camera.fov = m_CameraFov;
            return true;
        }

        static ViewVertex ToView(const CameraState& camera, const Vec3& point, float u = 0.0f, float v = 0.0f)
        {
            const Vec3 delta = point - camera.position;
            return {Dot(delta, camera.xAxis), Dot(delta, camera.yAxis), Dot(delta, camera.zAxis), u, v};
        }

        static bool ProjectView(const CameraState& camera, const ViewVertex& point, float targetSize, ScreenVertex& output)
        {
            if (point.z < NEAR_PLANE || point.z > FAR_PLANE)
                return false;

            const float screenAspect =
                static_cast<float>(*reinterpret_cast<const int*>(0xC17048)) / std::max(1.0f, static_cast<float>(*reinterpret_cast<const int*>(0xC17044)));
            const float widthScale = 1.0f / std::tan(camera.fov * 0.5f);
            const float heightScale = widthScale / screenAspect;
            const float ndcX = point.x * widthScale / point.z;
            const float ndcY = point.y * heightScale / point.z;
            output = {(ndcX + 1.0f) * 0.5f * targetSize - 0.5f, (1.0f - ndcY) * 0.5f * targetSize - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, point.u, point.v};
            return true;
        }

        static bool Project(const CameraState& camera, const Vec3& point, float targetSize, float& screenX, float& screenY)
        {
            ScreenVertex projected{};
            if (!ProjectView(camera, ToView(camera, point), targetSize, projected))
                return false;
            screenX = projected.x + 0.5f;
            screenY = projected.y + 0.5f;
            return true;
        }

        void ConfigureTexturedDrawing()
        {
            m_Device->SetVertexShader(nullptr);
            m_Device->SetPixelShader(nullptr);
            m_Device->SetFVF(SCREEN_VERTEX_FVF);
            m_Device->SetRenderState(D3DRS_ZENABLE, FALSE);
            m_Device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            m_Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            m_Device->SetRenderState(D3DRS_LIGHTING, FALSE);
            m_Device->SetRenderState(D3DRS_COLORVERTEX, TRUE);
            m_Device->SetRenderState(D3DRS_FOGENABLE, FALSE);
            m_Device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            m_Device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            m_Device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            m_Device->SetRenderState(D3DRS_COLORWRITEENABLE,
                                     D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
            m_Device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
            m_Device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
            m_Device->SetRenderState(D3DRS_CLIPPING, FALSE);
            m_Device->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
            m_Device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
            m_Device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            m_Device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            m_Device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            m_Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
            m_Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            m_Device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            m_Device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            m_Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            m_Device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            m_Device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
            m_Device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            m_Device->SetTextureStageState(0, D3DTSS_RESULTARG, D3DTA_CURRENT);
            m_Device->SetTexture(1, nullptr);
            m_Device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            m_Device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            m_Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            m_Device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            m_Device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            m_Device->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 4);
            m_Device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            m_Device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        }

        void ConfigureWorldDrawing(const CameraState& camera)
        {
            ConfigureTexturedDrawing();
            m_Device->SetFVF(WORLD_VERTEX_FVF);
            m_Device->SetRenderState(D3DRS_CLIPPING, TRUE);

            D3DMATRIX world{};
            world._11 = 1.0f;
            world._22 = 1.0f;
            world._33 = 1.0f;
            world._44 = 1.0f;

            D3DMATRIX view{};
            view._11 = camera.xAxis.x;
            view._12 = camera.yAxis.x;
            view._13 = camera.zAxis.x;
            view._21 = camera.xAxis.y;
            view._22 = camera.yAxis.y;
            view._23 = camera.zAxis.y;
            view._31 = camera.xAxis.z;
            view._32 = camera.yAxis.z;
            view._33 = camera.zAxis.z;
            view._41 = -Dot(camera.xAxis, camera.position);
            view._42 = -Dot(camera.yAxis, camera.position);
            view._43 = -Dot(camera.zAxis, camera.position);
            view._44 = 1.0f;

            const float screenAspect =
                static_cast<float>(*reinterpret_cast<const int*>(0xC17048)) / std::max(1.0f, static_cast<float>(*reinterpret_cast<const int*>(0xC17044)));
            const float widthScale = 1.0f / std::tan(camera.fov * 0.5f);
            const float heightScale = widthScale / screenAspect;
            const float depthScale = FAR_PLANE / (FAR_PLANE - NEAR_PLANE);
            D3DMATRIX   projection{};
            projection._11 = widthScale;
            projection._22 = heightScale;
            projection._33 = depthScale;
            projection._34 = 1.0f;
            projection._43 = -depthScale * NEAR_PLANE;

            m_Device->SetTransform(D3DTS_WORLD, &world);
            m_Device->SetTransform(D3DTS_VIEW, &view);
            m_Device->SetTransform(D3DTS_PROJECTION, &projection);
        }

        void DrawMap()
        {
            constexpr float TILE_SIZE = MAP_SIZE / MAP_TILES_PER_ROW;
            constexpr float HALF_TILE = TILE_SIZE * 0.5f;

            for (int index = 0; index < MAP_TILE_COUNT; ++index)
            {
                const int   row = index / MAP_TILES_PER_ROW;
                const int   column = index % MAP_TILES_PER_ROW;
                const float centerX = -MAP_SIZE * 0.5f + (static_cast<float>(column) + 0.5f) * TILE_SIZE + MAP_OFFSET_X;
                const float centerY = MAP_SIZE * 0.5f - (static_cast<float>(row) + 0.5f) * TILE_SIZE + MAP_OFFSET_Y;

                // This is the exact local plane and UV flip used by the
                // upstream Image3D shader. World-space vertices are essential:
                // D3D then performs homogeneous clipping and perspective-
                // correct interpolation instead of treating a projected map
                // tile as a flat XYZRHW sprite.
                const std::array<WorldVertex, 4> vertices = {
                    WorldVertex{centerX - HALF_TILE, centerY - HALF_TILE, 0.0f, 0xFFFFFFFF, 0.0f, 1.0f},
                    WorldVertex{centerX - HALF_TILE, centerY + HALF_TILE, 0.0f, 0xFFFFFFFF, 0.0f, 0.0f},
                    WorldVertex{centerX + HALF_TILE, centerY - HALF_TILE, 0.0f, 0xFFFFFFFF, 1.0f, 1.0f},
                    WorldVertex{centerX + HALF_TILE, centerY + HALF_TILE, 0.0f, 0xFFFFFFFF, 1.0f, 0.0f},
                };

                m_Device->SetTexture(0, m_MapTextures[index]);
                m_Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices.data(), sizeof(WorldVertex));
            }
        }

        void DrawPlayer(CPed* player)
        {
            IDirect3DTexture9* texture = m_BlipTextures[2];
            CVector*           position = player->GetPosition();
            if (!texture || !position)
                return;

            const float                                  halfSize = 8.5f * m_CameraHeight / DEFAULT_CAMERA_HEIGHT;
            const float                                  heading = player->GetCurrentRotation();
            const float                                  cosine = std::cos(heading);
            const float                                  sine = std::sin(heading);
            const Vec3                                   center{position->fX + MAP_OFFSET_X, position->fY + MAP_OFFSET_Y, 0.1f};
            const std::array<std::pair<float, float>, 4> local = {{{-halfSize, -halfSize}, {-halfSize, halfSize}, {halfSize, -halfSize}, {halfSize, halfSize}}};
            std::array<WorldVertex, 4>                   vertices{};
            for (std::size_t i = 0; i < local.size(); ++i)
            {
                const float worldX = center.x + local[i].first * cosine - local[i].second * sine;
                const float worldY = center.y + local[i].first * sine + local[i].second * cosine;
                vertices[i] = {worldX, worldY, center.z, 0xFFFFFFFF, (i >= 2) ? 1.0f : 0.0f, (i == 0 || i == 2) ? 1.0f : 0.0f};
            }
            m_Device->SetTexture(0, texture);
            m_Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices.data(), sizeof(WorldVertex));
        }

        void DrawCircle(IDirect3DTexture9* texture, float x, float y, float size)
        {
            constexpr int                          SEGMENTS = 128;
            std::array<ScreenVertex, SEGMENTS + 2> vertices{};
            vertices[0] = {x + size * 0.5f - 0.5f, y + size * 0.5f - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, 0.5f, 0.5f};
            for (int index = 0; index <= SEGMENTS; ++index)
            {
                const float angle = -2.0f * RADAR_PI * static_cast<float>(index) / static_cast<float>(SEGMENTS);
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                vertices[index + 1] = {x + size * (0.5f + 0.4925f * cosine) - 0.5f,
                                       y + size * (0.5f + 0.4925f * sine) - 0.5f,
                                       0.0f,
                                       1.0f,
                                       0xFFFFFFFF,
                                       0.5f + 0.4925f * cosine,
                                       0.5f + 0.4925f * sine};
            }
            m_Device->SetTexture(0, texture);
            m_Device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, SEGMENTS, vertices.data(), sizeof(ScreenVertex));
        }

        void DrawBorder(float x, float y, float size, float thickness)
        {
            constexpr int                                SEGMENTS = 128;
            std::array<ScreenVertex, (SEGMENTS + 1) * 2> vertices{};
            const float                                  outerRadius = size * 0.5f;
            const float                                  innerRadius = std::max(0.0f, outerRadius - thickness);
            const float                                  centerX = x + outerRadius;
            const float                                  centerY = y + outerRadius;
            for (int index = 0; index <= SEGMENTS; ++index)
            {
                const float angle = 2.0f * RADAR_PI * static_cast<float>(index) / static_cast<float>(SEGMENTS);
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                vertices[index * 2] = {centerX + cosine * outerRadius - 0.5f, centerY + sine * outerRadius - 0.5f, 0.0f, 1.0f, 0xFF000000, 0.0f, 0.0f};
                vertices[index * 2 + 1] = {centerX + cosine * innerRadius - 0.5f, centerY + sine * innerRadius - 0.5f, 0.0f, 1.0f, 0xFF000000, 0.0f, 0.0f};
            }
            m_Device->SetTexture(0, nullptr);
            m_Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            m_Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            m_Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, SEGMENTS * 2, vertices.data(), sizeof(ScreenVertex));
            ConfigureTexturedDrawing();
        }

        void DrawSprite(IDirect3DTexture9* texture, float centerX, float centerY, float size, float rotation = 0.0f)
        {
            if (!texture)
                return;

            const float                                  half = size * 0.5f;
            const float                                  cosine = std::cos(rotation);
            const float                                  sine = std::sin(rotation);
            const std::array<std::pair<float, float>, 4> local = {{{-half, -half}, {half, -half}, {-half, half}, {half, half}}};
            std::array<ScreenVertex, 4>                  vertices{};
            for (std::size_t i = 0; i < local.size(); ++i)
            {
                const float x = centerX + local[i].first * cosine - local[i].second * sine;
                const float y = centerY + local[i].first * sine + local[i].second * cosine;
                vertices[i] = {x - 0.5f, y - 0.5f, 0.0f, 1.0f, 0xFFFFFFFF, (i == 1 || i == 3) ? 1.0f : 0.0f, (i >= 2) ? 1.0f : 0.0f};
            }
            m_Device->SetTexture(0, texture);
            m_Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices.data(), sizeof(ScreenVertex));
        }

        void DrawDefaultCoordinateBlip(float centerX, float centerY, float halfWidth, float halfHeight, DWORD color)
        {
            const float                       screenWidth = static_cast<float>(*reinterpret_cast<const int*>(VAR_SCREEN_WIDTH));
            const float                       screenHeight = static_cast<float>(*reinterpret_cast<const int*>(VAR_SCREEN_HEIGHT));
            const float                       borderX = screenWidth / 640.0f;
            const float                       borderY = screenHeight / 448.0f;
            const DWORD                       alpha = color & 0xFF000000;
            const std::array<ScreenVertex, 4> borderVertices = {
                ScreenVertex{centerX - halfWidth - borderX - 0.5f, centerY - halfHeight - borderY - 0.5f, 0.0f, 1.0f, alpha, 0.0f, 0.0f},
                ScreenVertex{centerX + halfWidth + borderX - 0.5f, centerY - halfHeight - borderY - 0.5f, 0.0f, 1.0f, alpha, 1.0f, 0.0f},
                ScreenVertex{centerX - halfWidth - borderX - 0.5f, centerY + halfHeight + borderY - 0.5f, 0.0f, 1.0f, alpha, 0.0f, 1.0f},
                ScreenVertex{centerX + halfWidth + borderX - 0.5f, centerY + halfHeight + borderY - 0.5f, 0.0f, 1.0f, alpha, 1.0f, 1.0f},
            };
            const std::array<ScreenVertex, 4> fillVertices = {
                ScreenVertex{centerX - halfWidth - 0.5f, centerY - halfHeight - 0.5f, 0.0f, 1.0f, color, 0.0f, 0.0f},
                ScreenVertex{centerX + halfWidth - 0.5f, centerY - halfHeight - 0.5f, 0.0f, 1.0f, color, 1.0f, 0.0f},
                ScreenVertex{centerX - halfWidth - 0.5f, centerY + halfHeight - 0.5f, 0.0f, 1.0f, color, 0.0f, 1.0f},
                ScreenVertex{centerX + halfWidth - 0.5f, centerY + halfHeight - 0.5f, 0.0f, 1.0f, color, 1.0f, 1.0f},
            };

            // GTA's ShowRadarTraceWithHeight draws the normal-height branch as
            // a colored rectangle surrounded by a one-reference-pixel black
            // border. Reproduce that primitive instead of using the DE atlas:
            // sprite 0 deliberately has no atlas icon in the native renderer.
            m_Device->SetTexture(0, nullptr);
            m_Device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
            m_Device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            m_Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, borderVertices.data(), sizeof(ScreenVertex));
            m_Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, fillVertices.data(), sizeof(ScreenVertex));
            ConfigureTexturedDrawing();
        }

        void DrawBlips(const CameraState& camera, float radarX, float radarY, float radarSize, float targetSize)
        {
            const float centerX = radarX + radarSize * 0.5f;
            const float centerY = radarY + radarSize * 0.5f;
            const float screenWidth = static_cast<float>(*reinterpret_cast<const int*>(VAR_SCREEN_WIDTH));
            const float screenHeight = static_cast<float>(*reinterpret_cast<const int*>(VAR_SCREEN_HEIGHT));
            const float iconSize = std::max(12.0f, 24.0f * screenWidth / 1920.0f);
            const float usableRadius = radarSize * 0.5f - iconSize * 0.55f;
            const float radarRange = *reinterpret_cast<const float*>(VAR_RADAR_RANGE);

            for (int index = 0; index < MAX_MARKERS; ++index)
            {
                const CMarkerSAInterface& marker = *reinterpret_cast<const CMarkerSAInterface*>(ARRAY_CMarker + index * sizeof(CMarkerSAInterface));
                if (!marker.bTrackingBlip || marker.nBlipDisplayFlag == 0 || marker.nBlipSprite == 2 || marker.nBlipSprite == 4)
                    continue;

                int        sprite = marker.nBlipSprite;
                const bool defaultCoordinateBlip = sprite == 0 && (marker.BlipType == 4 || marker.BlipType == 5 || marker.BlipType == 6);
                if (!defaultCoordinateBlip && (sprite < 0 || sprite >= static_cast<int>(m_BlipTextures.size()) || !m_BlipTextures[sprite]))
                    continue;

                const Vec3  worldPosition{marker.position.fX + MAP_OFFSET_X, marker.position.fY + MAP_OFFSET_Y, 0.1f};
                const float dxWorld = worldPosition.x - camera.playerMapPosition.x;
                const float dyWorld = worldPosition.y - camera.playerMapPosition.y;
                if (marker.bShortRange && dxWorld * dxWorld + dyWorld * dyWorld > radarRange * radarRange)
                    continue;

                float      targetX = 0.0f;
                float      targetY = 0.0f;
                const bool projected = Project(camera, worldPosition, targetSize, targetX, targetY);
                float      screenX = centerX;
                float      screenY = centerY;
                bool       inside = false;
                if (projected)
                {
                    screenX = radarX + targetX / targetSize * radarSize;
                    screenY = radarY + targetY / targetSize * radarSize;
                    const float dx = screenX - centerX;
                    const float dy = screenY - centerY;
                    inside = dx * dx + dy * dy <= usableRadius * usableRadius;
                }

                if (!inside)
                {
                    if (defaultCoordinateBlip || sprite != 41)
                        continue;
                    float directionX = screenX - centerX;
                    float directionY = screenY - centerY;
                    if (!projected || std::abs(directionX) + std::abs(directionY) < 0.001f)
                    {
                        const float worldAngle = std::atan2(worldPosition.y - camera.playerMapPosition.y, worldPosition.x - camera.playerMapPosition.x);
                        const float orbitAngle = -worldAngle - camera.yaw;
                        directionX = std::cos(orbitAngle);
                        directionY = std::sin(orbitAngle);
                    }
                    const float length = std::max(0.001f, std::sqrt(directionX * directionX + directionY * directionY));
                    screenX = centerX + directionX / length * usableRadius;
                    screenY = centerY + directionY / length * usableRadius;
                }

                if (defaultCoordinateBlip)
                {
                    const DWORD color =
                        D3DCOLOR_ARGB((marker.nColour) & 0xFF, (marker.nColour >> 24) & 0xFF, (marker.nColour >> 16) & 0xFF, (marker.nColour >> 8) & 0xFF);
                    const float halfWidth = static_cast<float>(marker.nBlipScale) * screenWidth / 640.0f;
                    const float halfHeight = static_cast<float>(marker.nBlipScale) * screenHeight / 448.0f;
                    DrawDefaultCoordinateBlip(screenX, screenY, halfWidth, halfHeight, color);
                }
                else
                    DrawSprite(m_BlipTextures[sprite], screenX, screenY, iconSize);
            }
        }

        void DrawNorth(const CameraState& camera, float radarX, float radarY, float radarSize)
        {
            if (!m_BlipTextures[4])
                return;
            const float iconSize = std::max(14.0f, 28.0f * static_cast<float>(*reinterpret_cast<const int*>(0xC17044)) / 1920.0f);
            const float radius = radarSize * 0.5f - iconSize * 0.5f;
            const float angle = -camera.yaw - RADAR_PI * 0.5f;
            DrawSprite(m_BlipTextures[4], radarX + radarSize * 0.5f + std::cos(angle) * radius, radarY + radarSize * 0.5f + std::sin(angle) * radius, iconSize);
        }

        void DestroyDictionary(RwTexDictionary*& dictionary)
        {
            if (!dictionary || !pGame || !pGame->GetRenderWare())
                return;

            auto*                   renderWare = static_cast<CRenderWareSA*>(pGame->GetRenderWare());
            std::vector<RwTexture*> textures;
            CRenderWareSA::GetTxdTextures(textures, dictionary);
            for (RwTexture* texture : textures)
                renderWare->ScriptRemovedTexture(texture);
            renderWare->DestroyTXD(dictionary);
            dictionary = nullptr;
        }

        bool RestoreTargets(IDirect3DSurface9* oldTarget, IDirect3DSurface9* oldDepth, const D3DVIEWPORT9& oldViewport)
        {
            bool restored = oldTarget && SUCCEEDED(m_Device->SetRenderTarget(0, oldTarget));
            restored = SUCCEEDED(m_Device->SetDepthStencilSurface(oldDepth)) && restored;
            restored = SUCCEEDED(m_Device->SetViewport(&oldViewport)) && restored;
            return restored;
        }

        bool RestoreDevice(IDirect3DSurface9*& oldTarget, IDirect3DSurface9*& oldDepth, const D3DVIEWPORT9& oldViewport, IDirect3DStateBlock9*& stateBlock)
        {
            bool restored = RestoreTargets(oldTarget, oldDepth, oldViewport);
            restored = stateBlock && SUCCEEDED(stateBlock->Apply()) && restored;
            SafeRelease(stateBlock);
            SafeRelease(oldTarget);
            SafeRelease(oldDepth);
            return restored;
        }

        IDirect3DDevice9*                              m_Device{};
        RwTexDictionary*                               m_MapDictionary{};
        RwTexDictionary*                               m_BlipDictionary{};
        std::array<IDirect3DTexture9*, MAP_TILE_COUNT> m_MapTextures{};
        std::array<IDirect3DTexture9*, 64>             m_BlipTextures{};
        float                                          m_CameraHeight{DEFAULT_CAMERA_HEIGHT};
        float                                          m_CameraOffset{DEFAULT_CAMERA_OFFSET_Y};
        float                                          m_CameraFov{DEFAULT_CAMERA_FOV};
        float                                          m_CameraPitch{DEFAULT_CAMERA_PITCH};
        bool                                           m_InitializationAttempted{};
        bool                                           m_Initialized{};
    };

    CDefinitiveRadarRenderer g_Renderer;

    void __cdecl DrawRadarDispatcher()
    {
        int style = 0;
        if (g_pCore)
            g_pCore->GetCVars()->Get("radar_style", style);

        const bool frontendMapActive = *reinterpret_cast<const bool*>(VAR_FRONTEND_MAP_ACTIVE);
        const bool hudDisabled = *reinterpret_cast<const BYTE*>(FUNC_DRAW_HUD) == 0xC3;
        if (style != 1 || frontendMapActive)
        {
            if (g_OriginalDrawRadar && g_RadarVisible && !hudDisabled)
                g_OriginalDrawRadar();
            return;
        }

        if (!g_RadarVisible || hudDisabled)
            return;

        // Asset or device failures must never make the radar disappear. GTA's
        // original renderer is the safe fallback until the next client start.
        bool safeToFallback = true;
        if (!g_Renderer.Render(safeToFallback) && safeToFallback && g_OriginalDrawRadar)
            g_OriginalDrawRadar();
    }
}

bool InstallDefinitiveRadarRenderer()
{
    if (g_RadarHookInstalled)
        return true;

    constexpr std::array<BYTE, 5> expectedBytes = {0xA1, 0xCC, 0xA7, 0x96, 0x00};
    if (pGame->GetGameVersion() != VERSION_US_10 ||
        std::memcmp(reinterpret_cast<const void*>(FUNC_DRAW_RADAR), expectedBytes.data(), expectedBytes.size()) != 0)
    {
        OutputReleaseLine("[Radar] Definitive renderer disabled: CHud::DrawRadar validation failed");
        return false;
    }

    auto* gateway = static_cast<BYTE*>(VirtualAlloc(nullptr, expectedBytes.size() + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!gateway)
        return false;

    std::memcpy(gateway, expectedBytes.data(), expectedBytes.size());
    gateway[expectedBytes.size()] = 0xE9;
    *reinterpret_cast<std::int32_t*>(gateway + expectedBytes.size() + 1) =
        static_cast<std::int32_t>((FUNC_DRAW_RADAR + expectedBytes.size()) - reinterpret_cast<DWORD>(gateway + expectedBytes.size() + 5));

    if (!HookInstall(FUNC_DRAW_RADAR, &DrawRadarDispatcher, expectedBytes.size()))
    {
        VirtualFree(gateway, 0, MEM_RELEASE);
        return false;
    }

    g_OriginalDrawRadarGateway = gateway;
    g_OriginalDrawRadar = reinterpret_cast<DrawRadarFunction>(gateway);
    g_RadarHookInstalled = true;
    OutputReleaseLine("[Radar] Definitive Edition full renderer dispatcher installed");
    return true;
}

void ShutdownDefinitiveRadarRenderer()
{
    g_Renderer.Shutdown();
    if (!g_RadarHookInstalled)
        return;

    constexpr std::array<BYTE, 5> originalBytes = {0xA1, 0xCC, 0xA7, 0x96, 0x00};
    if (*reinterpret_cast<const BYTE*>(FUNC_DRAW_RADAR) == 0xE9)
        MemCpy(reinterpret_cast<void*>(FUNC_DRAW_RADAR), originalBytes.data(), originalBytes.size());

    if (g_OriginalDrawRadarGateway)
        VirtualFree(g_OriginalDrawRadarGateway, 0, MEM_RELEASE);
    g_OriginalDrawRadarGateway = nullptr;
    g_OriginalDrawRadar = nullptr;
    g_RadarHookInstalled = false;
}

void SetDefinitiveRadarVisible(bool visible)
{
    g_RadarVisible = visible;
}

bool IsDefinitiveRadarVisible()
{
    return g_RadarVisible;
}
