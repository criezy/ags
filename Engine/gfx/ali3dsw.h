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
// Software graphics factory, draws raw bitmaps onto a virtual screen,
// converts to SDL_Texture and finally presents with SDL_Renderer.
//
// TODO: replace nearest-neighbour software filter with SDL's own accelerated
// scaling, maybe add more filter types if SDL renderer supports them.
// Only keep Hqx filter as a software option (might need to change how the 
// filter code works).
//
//=============================================================================
#ifndef __AGS_EE_GFX__ALI3DSW_H
#define __AGS_EE_GFX__ALI3DSW_H
#include <memory>
#include <SDL.h>
#include "core/platform.h"
#include "gfx/bitmap.h"
#include "gfx/ddb.h"
#include "gfx/gfxdriverfactorybase.h"
#include "gfx/gfxdriverbase.h"

namespace AGS
{
namespace Engine
{
namespace ALSW
{

class SDLRendererGfxFilter;
using AGS::Common::Bitmap;

class ALSoftwareBitmap : public BaseDDB
{
public:
    uint32_t GetRefID() const override { return UINT32_MAX /* not supported */; }

    int  GetAlpha() const override { return _alpha; }
    void SetAlpha(int alpha) override { _alpha = alpha; }
    void SetFlippedLeftRight(bool isFlipped) override { _flipped = isFlipped; }
    void SetStretch(int width, int height, bool /*useResampler*/) override 
    {
        _stretchToWidth = width;
        _stretchToHeight = height;
    }
    void SetLightLevel(int /*lightLevel*/) override  { }
    void SetTint(int /*red*/, int /*green*/, int /*blue*/, int /*tintSaturation*/) override { }

    Bitmap *_bmp = nullptr;
    bool _flipped = false;
    int _stretchToWidth = 0, _stretchToHeight = 0;
    int _alpha = 255;

    ALSoftwareBitmap(int width, int height, int color_depth, bool opaque)
    {
        _width = width;
        _height = height;
        _colDepth = color_depth;
        _opaque = opaque;
        _stretchToWidth = _width;
        _stretchToHeight = _height;
    }

    ALSoftwareBitmap(Bitmap *bmp, bool opaque, bool hasAlpha)
    {
        _bmp = bmp;
        _width = bmp->GetWidth();
        _height = bmp->GetHeight();
        _colDepth = bmp->GetColorDepth();
        _opaque = opaque;
        _hasAlpha = hasAlpha;
        _stretchToWidth = _width;
        _stretchToHeight = _height;
    }

    int GetWidthToRender() { return _stretchToWidth; }
    int GetHeightToRender() { return _stretchToHeight; }

    ~ALSoftwareBitmap() override = default;
};


class SDLRendererGfxModeList : public IGfxModeList
{
public:
    SDLRendererGfxModeList(const std::vector<DisplayMode> &modes)
        : _modes(modes)
    {
    }

    int GetModeCount() const override
    {
        return _modes.size();
    }

    bool GetMode(int index, DisplayMode &mode) const override
    {
        if (index >= 0 && (size_t)index < _modes.size())
        {
            mode = _modes[index];
            return true;
        }
        return false;
    }

private:
    std::vector<DisplayMode> _modes;
};


typedef SpriteDrawListEntry<ALSoftwareBitmap> ALDrawListEntry;
// Software renderer's sprite batch
struct ALSpriteBatch
{
    uint32_t ID = 0;
    // Intermediate surface which will be drawn upon and transformed if necessary
    std::shared_ptr<Bitmap> Surface;
    // Whether surface is a virtual screen's region
    bool IsVirtualScreen = false;
    // Tells whether the surface is treated as opaque or transparent
    bool Opaque = false;
};
typedef std::vector<ALSpriteBatch> ALSpriteBatches;


class SDLRendererGraphicsDriver : public GraphicsDriverBase
{
public:
    SDLRendererGraphicsDriver();

    const char*GetDriverName() override { return "SDL 2D Software renderer"; }
    const char*GetDriverID() override { return "Software"; }
    void SetTintMethod(TintMethod method) override;
    bool SetDisplayMode(const DisplayMode &mode) override;
    void UpdateDeviceScreen(const Size &screen_sz) override;
    bool SetNativeResolution(const GraphicResolution &native_res) override;
    bool SetRenderFrame(const Rect &dst_rect) override;
    bool IsModeSupported(const DisplayMode &mode) override;
    int  GetDisplayDepthForNativeDepth(int native_color_depth) const override;
    IGfxModeList *GetSupportedModeList(int color_depth) override;
    PGfxFilter GetGraphicsFilter() const override;
    void UnInit();
    // Clears the screen rectangle. The coordinates are expected in the **native game resolution**.
    void ClearRectangle(int x1, int y1, int x2, int y2, RGB *colorToUse) override;
    int  GetCompatibleBitmapFormat(int color_depth) override;
    IDriverDependantBitmap* CreateDDB(int width, int height, int color_depth, bool opaque) override;
    IDriverDependantBitmap* CreateDDBFromBitmap(Bitmap *bitmap, bool hasAlpha, bool opaque) override;
    void UpdateDDBFromBitmap(IDriverDependantBitmap* ddb, Bitmap *bitmap, bool hasAlpha) override;
    void DestroyDDB(IDriverDependantBitmap* ddb) override;

    IDriverDependantBitmap *GetSharedDDB(uint32_t /*sprite_id*/,
        Common::Bitmap *bitmap, bool hasAlpha, bool opaque) override
    { // Software renderer does not require a texture cache, because it uses bitmaps directly
        return CreateDDBFromBitmap(bitmap, hasAlpha, opaque);
    }
    void UpdateSharedDDB(uint32_t /*sprite_id*/, Common::Bitmap * /*bitmap*/, bool /*hasAlpha*/, bool /*opaque*/)
        override { /* do nothing */ }
    void ClearSharedDDB(uint32_t /*sprite_id*/) override { /* do nothing */ }

    void DrawSprite(int x, int y, IDriverDependantBitmap* ddb) override;
    void SetScreenFade(int red, int green, int blue) override;
    void SetScreenTint(int red, int green, int blue) override;

    void RenderToBackBuffer() override;
    void Render() override;
    void Render(int xoff, int yoff, GlobalFlipType flip) override;
    bool GetCopyOfScreenIntoBitmap(Bitmap *destination, bool at_native_res, GraphicResolution *want_fmt) override;
    void FadeOut(int speed, int targetColourRed, int targetColourGreen, int targetColourBlue) override;
    void FadeIn(int speed, PALETTE pal, int targetColourRed, int targetColourGreen, int targetColourBlue) override;
    void BoxOutEffect(bool blackingOut, int speed, int delay) override;
    bool SupportsGammaControl() override ;
    void SetGamma(int newGamma) override;
    void UseSmoothScaling(bool /*enabled*/) override { }
    bool DoesSupportVsyncToggle() override { return false; }
    bool SetVsync(bool /*enabled*/) override
    {
        /* TODO: support toggling; need SDL_RenderSetVSync() from SDL 2.0.18 */
        return _mode.Vsync;
    }
    void RenderSpritesAtScreenResolution(bool /*enabled*/, int /*supersampling*/) override { }
    bool RequiresFullRedrawEachFrame() override { return false; }
    bool HasAcceleratedTransform() override { return false; }
    bool UsesMemoryBackBuffer() override { return true; }
    Bitmap *GetMemoryBackBuffer() override;
    void SetMemoryBackBuffer(Bitmap *backBuffer) override;
    Bitmap *GetStageBackBuffer(bool mark_dirty) override;
    bool GetStageMatrixes(RenderMatrixes &/*rm*/) override { return false; /* not supported */ }
    ~SDLRendererGraphicsDriver() override;

    typedef std::shared_ptr<SDLRendererGfxFilter> PSDLRenderFilter;

    void SetGraphicsFilter(PSDLRenderFilter filter);

private:
    PSDLRenderFilter _filter;

    bool _hasGamma = false;
    Uint16 _defaultGammaRed[256]{};
    Uint16 _defaultGammaGreen[256]{};
    Uint16 _defaultGammaBlue[256]{};

    SDL_RendererFlip _renderFlip = SDL_FLIP_NONE;
    SDL_Renderer *_renderer = nullptr;
    SDL_Texture *_screenTex = nullptr;
    // BITMAP struct for wrapping screen texture locked pixels, so that we may use blit()
    BITMAP *_fakeTexBitmap = nullptr;
    unsigned char *_lastTexPixels = nullptr;
    int _lastTexPitch = -1;

    // Original virtual screen created and managed by the renderer.
    std::unique_ptr<Bitmap> _origVirtualScreen;
    // Current virtual screen bitmap; may be either pointing to _origVirtualScreen,
    // or provided by external user (for example - plugin).
    // Its pixels are copied to the video texture to be presented by SDL_Renderer.
    Bitmap *virtualScreen;
    // Stage screen meant for particular rendering stages, may be referencing
    // actual virtual screen or separate bitmap of different size that is
    // blitted to virtual screen at the stage finalization.
    Bitmap *_stageVirtualScreen;
    int _tint_red, _tint_green, _tint_blue;

    // Sprite batches (parent scene nodes)
    ALSpriteBatches _spriteBatches;
    // List of sprites to render
    std::vector<ALDrawListEntry> _spriteList;

    void InitSpriteBatch(size_t index, const SpriteBatchDesc &desc) override;
    void ResetAllBatches() override;

    // Use gfx filter to create a new virtual screen
    void CreateVirtualScreen();
    void DestroyVirtualScreen();
    // Unset parameters and release resources related to the display mode
    void ReleaseDisplayMode();
    // Renders single sprite batch on the precreated surface
    size_t RenderSpriteBatch(const ALSpriteBatch &batch, size_t from, Common::Bitmap *surface, int surf_offx, int surf_offy);

    void highcolor_fade_in(Bitmap *vs, void(*draw_callback)(), int offx, int offy, int speed, int targetColourRed, int targetColourGreen, int targetColourBlue);
    void highcolor_fade_out(Bitmap *vs, void(*draw_callback)(), int offx, int offy, int speed, int targetColourRed, int targetColourGreen, int targetColourBlue);
    void __fade_from_range(PALETTE source, PALETTE dest, int speed, int from, int to) ;
    void __fade_out_range(int speed, int from, int to, int targetColourRed, int targetColourGreen, int targetColourBlue) ;
    // Copy raw screen bitmap pixels to the SDL texture
    void BlitToTexture();
    // Render SDL texture on screen
    void Present();
};


class SDLRendererGraphicsFactory : public GfxDriverFactoryBase<SDLRendererGraphicsDriver, SDLRendererGfxFilter>
{
public:
    ~SDLRendererGraphicsFactory() override;

    size_t               GetFilterCount() const override;
    const GfxFilterInfo *GetFilterInfo(size_t index) const override;
    String               GetDefaultFilterID() const override;

    static SDLRendererGraphicsFactory *GetFactory();

private:
    SDLRendererGraphicsDriver *EnsureDriverCreated() override;
    SDLRendererGfxFilter      *CreateFilter(const String &id) override;

    static SDLRendererGraphicsFactory *_factory;
};

} // namespace ALSW
} // namespace Engine
} // namespace AGS

#endif // __AGS_EE_GFX__ALI3DSW_H
