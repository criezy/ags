//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================
//
// Direct3D graphics factory
//
//=============================================================================
#ifndef __AGS_EE_GFX__ALI3DD3D_H
#define __AGS_EE_GFX__ALI3DD3D_H

#include "core/platform.h"

#if ! AGS_PLATFORM_OS_WINDOWS
#error This file should only be included on the Windows build
#endif

#include <memory>
#define NOMINMAX
#define BITMAP WINDOWS_BITMAP
#include <d3d9.h>
#undef BITMAP
#include "gfx/bitmap.h"
#include "gfx/ddb.h"
#include "gfx/gfxdriverfactorybase.h"
#include "gfx/gfxdriverbase.h"
#include "util/library.h"
#include "util/string.h"

namespace AGS
{
namespace Engine
{
namespace D3D
{

using AGS::Common::Bitmap;
using AGS::Common::String;
class D3DGfxFilter;

struct D3DTextureTile : public TextureTile
{
    IDirect3DTexture9 *texture = nullptr;
};

// Full Direct3D texture data
struct D3DTextureData : TextureData
{
    IDirect3DVertexBuffer9 *_vertex = nullptr;
    D3DTextureTile *_tiles = nullptr;
    size_t _numTiles = 0;

    ~D3DTextureData();
};

class D3DBitmap : public BaseDDB
{
public:
    uint32_t GetRefID() const override { return _data->ID; }

    int  GetAlpha() const override { return _alpha; }
    void SetAlpha(int alpha) override { _alpha = alpha; }
    void SetFlippedLeftRight(bool isFlipped) override { _flipped = isFlipped; }
    void SetStretch(int width, int height, bool useResampler = true) override
    {
        _stretchToWidth = width;
        _stretchToHeight = height;
        _useResampler = useResampler;
    }
    void SetLightLevel(int lightLevel) override { _lightLevel = lightLevel; }
    void SetTint(int red, int green, int blue, int tintSaturation) override
    {
        _red = red;
        _green = green;
        _blue = blue;
        _tintSaturation = tintSaturation;
    }

    // Direct3D texture data
    std::shared_ptr<D3DTextureData> _data;

    // Drawing parameters
    bool _flipped;
    int _stretchToWidth, _stretchToHeight;
    bool _useResampler;
    int _red, _green, _blue;
    int _tintSaturation;
    int _lightLevel;
    int _alpha;

    D3DBitmap(int width, int height, int colDepth, bool opaque)
    {
        _width = width;
        _height = height;
        _colDepth = colDepth;
        _flipped = false;
        _hasAlpha = false;
        _stretchToWidth = _width;
        _stretchToHeight = _height;
        _useResampler = false;
        _red = _green = _blue = 0;
        _tintSaturation = 0;
        _lightLevel = 0;
        _alpha = 255;
        _opaque = opaque;
    }

    int GetWidthToRender() { return _stretchToWidth; }
    int GetHeightToRender() { return _stretchToHeight; }

    ~D3DBitmap() override = default;
};

class D3DGfxModeList : public IGfxModeList
{
public:
    D3DGfxModeList(IDirect3D9 *direct3d, D3DFORMAT d3dformat)
        : _direct3d(direct3d)
        , _pixelFormat(d3dformat)
    {
        _modeCount = _direct3d ? _direct3d->GetAdapterModeCount(D3DADAPTER_DEFAULT, _pixelFormat) : 0;
    }

    ~D3DGfxModeList() override
    {
        if (_direct3d)
            _direct3d->Release();
    }

    int GetModeCount() const override
    {
        return _modeCount;
    }

    bool GetMode(int index, DisplayMode &mode) const override;

private:
    IDirect3D9 *_direct3d;
    D3DFORMAT   _pixelFormat;
    int         _modeCount;
};

struct CUSTOMVERTEX
{
    D3DVECTOR   position; // The position.
    D3DVECTOR   normal;
    FLOAT       tu, tv;   // The texture coordinates.
};

typedef SpriteDrawListEntry<D3DBitmap> D3DDrawListEntry;
// D3D renderer's sprite batch
struct D3DSpriteBatch
{
    uint32_t ID = 0;
    // Clipping viewport
    Rect Viewport;
    // Transformation matrix, built from the batch description
    // TODO: investigate possibility of using glm here (might need conversion to D3D matrix format)
    D3DMATRIX Matrix;
    // Batch color transformation
    SpriteColorTransform Color;

    D3DSpriteBatch() = default;
    D3DSpriteBatch(uint32_t id, const Rect view, const D3DMATRIX &matrix, const SpriteColorTransform &color)
        : ID(id), Viewport(view), Matrix(matrix), Color(color) {}
};
typedef std::vector<D3DSpriteBatch>    D3DSpriteBatches;


class D3DGraphicsDriver : public VideoMemoryGraphicsDriver
{
public:
    const char*GetDriverName() override { return "Direct3D 9"; }
    const char*GetDriverID() override { return "D3D9"; }
    void SetTintMethod(TintMethod method) override;
    bool SetDisplayMode(const DisplayMode &mode) override;
    void UpdateDeviceScreen(const Size &screen_sz) override;
    bool SetNativeResolution(const GraphicResolution &native_res) override;
    bool SetRenderFrame(const Rect &dst_rect) override;
    int  GetDisplayDepthForNativeDepth(int native_color_depth) const override;
    IGfxModeList *GetSupportedModeList(int color_depth) override;
    bool IsModeSupported(const DisplayMode &mode) override;
    PGfxFilter GetGraphicsFilter() const override;
    // Clears the screen rectangle. The coordinates are expected in the **native game resolution**.
    void ClearRectangle(int x1, int y1, int x2, int y2, RGB *colorToUse) override;
    int  GetCompatibleBitmapFormat(int color_depth) override;
    IDriverDependantBitmap* CreateDDB(int width, int height, int color_depth, bool opaque) override;
    // Create texture data with the given parameters
    TextureData *CreateTextureData(int width, int height, bool opaque) override;
    // Update texture data from the given bitmap
    void UpdateTextureData(TextureData *txdata, Bitmap *bitmap, bool opaque, bool hasAlpha) override;
    // Create DDB using preexisting texture data
    IDriverDependantBitmap *CreateDDB(std::shared_ptr<TextureData> txdata,
        int width, int height, int color_depth, bool opaque) override;
    // Retrieve shared texture data object from the given DDB
    std::shared_ptr<TextureData> GetTextureData(IDriverDependantBitmap *ddb) override;
    void UpdateDDBFromBitmap(IDriverDependantBitmap* ddb, Bitmap *bitmap, bool hasAlpha) override;
    void DestroyDDBImpl(IDriverDependantBitmap* ddb) override;
    void DrawSprite(int x, int y, IDriverDependantBitmap* ddb) override;
    void SetScreenFade(int red, int green, int blue) override;
    void SetScreenTint(int red, int green, int blue) override;
    void RenderToBackBuffer() override;
    void Render() override;
    void Render(int xoff, int yoff, GlobalFlipType flip) override;
    bool GetCopyOfScreenIntoBitmap(Bitmap *destination, bool at_native_res, GraphicResolution *want_fmt) override;
    bool DoesSupportVsyncToggle() override { return false; }
    bool SetVsync(bool /*enabled*/) override { return _mode.Vsync; /* TODO: support toggling */ }
    void RenderSpritesAtScreenResolution(bool enabled, int /*supersampling*/) override { _renderSprAtScreenRes = enabled; };
    void FadeOut(int speed, int targetColourRed, int targetColourGreen, int targetColourBlue) override;
    void FadeIn(int speed, PALETTE p, int targetColourRed, int targetColourGreen, int targetColourBlue) override;
    void BoxOutEffect(bool blackingOut, int speed, int delay) override;
    bool SupportsGammaControl() override;
    void SetGamma(int newGamma) override;
    void UseSmoothScaling(bool enabled) override { _smoothScaling = enabled; }
    bool RequiresFullRedrawEachFrame() override { return true; }
    bool HasAcceleratedTransform() override { return true; }

    typedef std::shared_ptr<D3DGfxFilter> PD3DFilter;

    // Clears screen rect, coordinates are expected in display resolution
    void ClearScreenRect(const Rect &r, RGB *colorToUse);
    void UnInit();
    void SetGraphicsFilter(PD3DFilter filter);

    // Internal; TODO: find a way to hide these
    int _initDLLCallback(const DisplayMode &mode);
    int _resetDeviceIfNecessary();

    D3DGraphicsDriver(IDirect3D9 *d3d);
    ~D3DGraphicsDriver() override;

private:
    PD3DFilter _filter;

    IDirect3D9 *direct3d;
    D3DPRESENT_PARAMETERS d3dpp;
    IDirect3DDevice9* direct3ddevice;
    D3DGAMMARAMP defaultgammaramp;
    D3DGAMMARAMP currentgammaramp;
    D3DCAPS9 direct3ddevicecaps;
    IDirect3DVertexBuffer9* vertexbuffer;
    IDirect3DSurface9 *pNativeSurface;
    IDirect3DTexture9 *pNativeTexture;
    RECT viewport_rect;
    UINT availableVideoMemory;
    CUSTOMVERTEX defaultVertices[4];
    String previousError;
    IDirect3DPixelShader9* pixelShader;
    bool _smoothScaling;
    bool _legacyPixelShader;
    float _pixelRenderXOffset;
    float _pixelRenderYOffset;
    bool _renderSprAtScreenRes;

    // Sprite batches (parent scene nodes)
    D3DSpriteBatches _spriteBatches;
    // List of sprites to render
    std::vector<D3DDrawListEntry> _spriteList;
    // TODO: these draw list backups are needed only for the fade-in/out effects
    // find out if it's possible to reimplement these effects in main drawing routine.
    SpriteBatchDescs _backupBatchDescs;
    D3DSpriteBatches _backupBatches;
    std::vector<D3DDrawListEntry> _backupSpriteList;

    D3DVIEWPORT9 _d3dViewport;

    // Called after new mode was successfully initialized
    void OnModeSet(const DisplayMode &mode) override;
    void InitSpriteBatch(size_t index, const SpriteBatchDesc &desc) override;
    void ResetAllBatches() override;
    // Called when the direct3d device is created for the first time
    int  FirstTimeInit();
    void initD3DDLL(const DisplayMode &mode);
    void InitializeD3DState();
    void SetupViewport();
    HRESULT ResetD3DDevice();
    // Unset parameters and release resources related to the display mode
    void ReleaseDisplayMode();
    void set_up_default_vertices();
    void AdjustSizeToNearestSupportedByCard(int *width, int *height);
    void UpdateTextureRegion(D3DTextureTile *tile, Bitmap *bitmap, bool opaque, bool hasAlpha);
    void CreateVirtualScreen();
    void do_fade(bool fadingOut, int speed, int targetColourRed, int targetColourGreen, int targetColourBlue);
    bool IsTextureFormatOk( D3DFORMAT TextureFormat, D3DFORMAT AdapterFormat );
    // Backup all draw lists in the temp storage
    void BackupDrawLists();
    // Restore draw lists from the temp storage
    void RestoreDrawLists();
    // Deletes draw list backups
    void ClearDrawBackups();
    void _renderAndPresent(bool clearDrawListAfterwards);
    void _render(bool clearDrawListAfterwards);
    void _reDrawLastFrame();
    void SetScissor(const Rect &clip);
    void RenderSpriteBatches();
    size_t RenderSpriteBatch(const D3DSpriteBatch &batch, size_t from);
    void _renderSprite(const D3DDrawListEntry *entry, const D3DMATRIX &matGlobal, const SpriteColorTransform &color);
    void _renderFromTexture();
};


class D3DGraphicsFactory : public GfxDriverFactoryBase<D3DGraphicsDriver, D3DGfxFilter>
{
public:
    ~D3DGraphicsFactory() override;

    size_t               GetFilterCount() const override;
    const GfxFilterInfo *GetFilterInfo(size_t index) const override;
    String               GetDefaultFilterID() const override;

    static D3DGraphicsFactory   *GetFactory();
    static D3DGraphicsDriver    *GetD3DDriver();

private:
    D3DGraphicsFactory();

    D3DGraphicsDriver   *EnsureDriverCreated() override;
    D3DGfxFilter        *CreateFilter(const String &id) override;

    bool Init();

    static D3DGraphicsFactory *_factory;
    //
    // IMPORTANT NOTE: since the Direct3d9 device is created with
    // D3DCREATE_MULTITHREADED behavior flag, once it is created the d3d9.dll may
    // only be unloaded after window is destroyed, as noted in the MSDN's article
    // on "D3DCREATE"
    // (http://msdn.microsoft.com/en-us/library/windows/desktop/bb172527.aspx).
    // Otherwise window becomes either destroyed prematurely or broken (details
    // are unclear), which causes errors during Allegro deinitialization.
    //
    // Curiously, this problem was only confirmed under WinXP so far.
    //
    // For the purpose of avoiding this problem, we have a static library wrapper
    // that unloads library only at the very program exit (except cases of device
    // creation failure).
    //
    // TODO: find out if there is better solution.
    // 
    static Library      _library;
    IDirect3D9         *_direct3d;
};

} // namespace D3D
} // namespace Engine
} // namespace AGS

#endif // __AGS_EE_GFX__ALI3DD3D_H
