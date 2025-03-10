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
#include "gfx/ali3dexception.h"
#include "gfx/bitmap.h"
#include "gfx/gfxfilter.h"
#include "gfx/gfxdriverbase.h"
#include "gfx/gfx_util.h"

using namespace AGS::Common;

namespace AGS
{
namespace Engine
{

GraphicsDriverBase::GraphicsDriverBase()
    : _pollingCallback(nullptr)
    , _drawScreenCallback(nullptr)
    , _nullSpriteCallback(nullptr)
    , _initGfxCallback(nullptr)
{
    // Initialize default sprite batch, it will be used when no other batch was activated
    _actSpriteBatch = 0;
    _spriteBatchDesc.push_back(SpriteBatchDesc());
}

bool GraphicsDriverBase::IsModeSet() const
{
    return _mode.Width != 0 && _mode.Height != 0 && _mode.ColorDepth != 0;
}

bool GraphicsDriverBase::IsNativeSizeValid() const
{
    return !_srcRect.IsEmpty();
}

bool GraphicsDriverBase::IsRenderFrameValid() const
{
    return !_srcRect.IsEmpty() && !_dstRect.IsEmpty();
}

DisplayMode GraphicsDriverBase::GetDisplayMode() const
{
    return _mode;
}

Size GraphicsDriverBase::GetNativeSize() const
{
    return _srcRect.GetSize();
}

Rect GraphicsDriverBase::GetRenderDestination() const
{
    return _dstRect;
}

void GraphicsDriverBase::BeginSpriteBatch(const Rect &viewport, const SpriteTransform &transform,
    const Point offset, GlobalFlipType flip, PBitmap surface)
{
    _spriteBatchDesc.push_back(SpriteBatchDesc(_actSpriteBatch, viewport, transform, offset, flip, surface));
    _actSpriteBatch = _spriteBatchDesc.size() - 1;
    InitSpriteBatch(_actSpriteBatch, _spriteBatchDesc[_actSpriteBatch]);
}

void GraphicsDriverBase::EndSpriteBatch()
{
    _actSpriteBatch = _spriteBatchDesc[_actSpriteBatch].Parent;
}

void GraphicsDriverBase::ClearDrawLists()
{
    ResetAllBatches();
    _actSpriteBatch = 0;
    _spriteBatchDesc.resize(1);
}

void GraphicsDriverBase::OnInit()
{
}

void GraphicsDriverBase::OnUnInit()
{
}

void GraphicsDriverBase::OnModeSet(const DisplayMode &mode)
{
    _mode = mode;
}

void GraphicsDriverBase::OnModeReleased()
{
    _mode = DisplayMode();
    _dstRect = Rect();
}

void GraphicsDriverBase::OnScalingChanged()
{
    PGfxFilter filter = GetGraphicsFilter();
    if (filter)
        _filterRect = filter->SetTranslation(_srcRect.GetSize(), _dstRect);
    else
        _filterRect = Rect();
    _scaling.Init(_srcRect.GetSize(), _dstRect);
}

void GraphicsDriverBase::OnSetNativeRes(const GraphicResolution &native_res)
{
    _srcRect = RectWH(0, 0, native_res.Width, native_res.Height);
    _srcColorDepth = native_res.ColorDepth;
    OnScalingChanged();

    // Adjust default sprite batch making it comply to native size
    _spriteBatchDesc[0].Viewport = RectWH(native_res);
    InitSpriteBatch(_actSpriteBatch, _spriteBatchDesc[_actSpriteBatch]);
}

void GraphicsDriverBase::OnSetRenderFrame(const Rect &dst_rect)
{
    _dstRect = dst_rect;
    OnScalingChanged();
}

void GraphicsDriverBase::OnSetFilter()
{
    _filterRect = GetGraphicsFilter()->SetTranslation(Size(_srcRect.GetSize()), _dstRect);
}


VideoMemoryGraphicsDriver::VideoMemoryGraphicsDriver()
    : _stageScreenDirty(false)
    , _fxIndex(0)
{
    // Only to have something meaningful as default
    _vmem_a_shift_32 = 24;
    _vmem_r_shift_32 = 16;
    _vmem_g_shift_32 = 8;
    _vmem_b_shift_32 = 0;
}

VideoMemoryGraphicsDriver::~VideoMemoryGraphicsDriver()
{
    DestroyAllStageScreens();
}

bool VideoMemoryGraphicsDriver::UsesMemoryBackBuffer()
{
    // Although we do use ours, we do not let engine draw upon it;
    // only plugin handling are allowed to request our mem buffer.
    // TODO: find better workaround?
    return false;
}

Bitmap *VideoMemoryGraphicsDriver::GetMemoryBackBuffer()
{
    return nullptr;
}

void VideoMemoryGraphicsDriver::SetMemoryBackBuffer(Bitmap* /*backBuffer*/)
{ // do nothing, video-memory drivers don't use main back buffer, only stage bitmaps they pass to plugins
}

Bitmap *VideoMemoryGraphicsDriver::GetStageBackBuffer(bool mark_dirty)
{
    _stageScreenDirty |= mark_dirty;
    return _stageScreen.Bitmap.get();
}

bool VideoMemoryGraphicsDriver::GetStageMatrixes(RenderMatrixes &rm)
{
    rm = _stageMatrixes;
    return true;
}

IDriverDependantBitmap *VideoMemoryGraphicsDriver::CreateDDBFromBitmap(Bitmap *bitmap, bool hasAlpha, bool opaque)
{
    IDriverDependantBitmap *ddb = CreateDDB(bitmap->GetWidth(), bitmap->GetHeight(), bitmap->GetColorDepth(), opaque);
    if (ddb)
        UpdateDDBFromBitmap(ddb, bitmap, hasAlpha);
    return ddb;
}

IDriverDependantBitmap *VideoMemoryGraphicsDriver::GetSharedDDB(uint32_t sprite_id, Bitmap *bitmap, bool hasAlpha, bool opaque)
{
    const auto found = _txRefs.find(sprite_id);
    if (found != _txRefs.end())
    {
        const auto &item = found->second;
        if (!item.Data.expired())
            return CreateDDB(item.Data.lock(), item.Res.Width, item.Res.Height, item.Res.ColorDepth, opaque);
    }

    // Create and add a new element
    std::shared_ptr<TextureData> txdata(CreateTextureData(bitmap->GetWidth(), bitmap->GetHeight(), opaque));
    txdata->ID = sprite_id;
    UpdateTextureData(txdata.get(), bitmap, opaque, hasAlpha);
    // only add into the map when has valid sprite ID
    if (sprite_id != UINT32_MAX)
    {
        _txRefs[sprite_id] = TextureCacheItem(txdata,
            GraphicResolution(bitmap->GetWidth(), bitmap->GetHeight(), bitmap->GetColorDepth()));
    }
    return CreateDDB(txdata, bitmap->GetWidth(), bitmap->GetHeight(), bitmap->GetColorDepth(), opaque);
}

void VideoMemoryGraphicsDriver::UpdateSharedDDB(uint32_t sprite_id, Common::Bitmap *bitmap, bool hasAlpha, bool opaque)
{
    const auto found = _txRefs.find(sprite_id);
    if (found != _txRefs.end())
    {
        auto txdata = found->second.Data.lock();
        if (txdata)
            UpdateTextureData(txdata.get(), bitmap, opaque, hasAlpha);
    }
}

void VideoMemoryGraphicsDriver::ClearSharedDDB(uint32_t sprite_id)
{
    const auto found = _txRefs.find(sprite_id);
    if (found != _txRefs.end())
        _txRefs.erase(found);
}

void VideoMemoryGraphicsDriver::DestroyDDB(IDriverDependantBitmap* ddb)
{
    uint32_t sprite_id = ddb->GetRefID();
    DestroyDDBImpl(ddb);
    // Remove shared object from ref list if no more active refs left
    const auto found = _txRefs.find(sprite_id);
    if (found != _txRefs.end() && found->second.Data.expired())
        _txRefs.erase(found);
}

VideoMemoryGraphicsDriver::StageScreen
    VideoMemoryGraphicsDriver::CreateStageScreen(size_t index, const Size &sz)
{
    if (_stageScreens.size() <= index)
        _stageScreens.resize(index + 1);

    if (sz.IsNull())
    {
        _stageScreens[index].Bitmap.reset();
        if (_stageScreens[index].DDB)
            DestroyDDB(_stageScreens[index].DDB);
        _stageScreens[index].DDB = nullptr;
    }
    else if (_stageScreens[index].Bitmap == nullptr || _stageScreens[index].Bitmap->GetSize() != sz)
    {
        if (_stageScreens[index].DDB)
            DestroyDDB(_stageScreens[index].DDB);
        _stageScreens[index].Bitmap.reset(new Bitmap(sz.Width, sz.Height, _mode.ColorDepth));
        _stageScreens[index].DDB = CreateDDB(sz.Width, sz.Height, _mode.ColorDepth, false);
    }
    return _stageScreens[index];
}

VideoMemoryGraphicsDriver::StageScreen
    VideoMemoryGraphicsDriver::GetStageScreen(size_t index)
{
    if (index < _stageScreens.size())
        return _stageScreens[index];
    return StageScreen();
}

void VideoMemoryGraphicsDriver::DestroyAllStageScreens()
{
    for (size_t i = 0; i < _stageScreens.size(); ++i)
    {
        _stageScreens[i].Bitmap.reset();
        if (_stageScreens[i].DDB)
            DestroyDDB(_stageScreens[i].DDB);
        _stageScreens[i].DDB = nullptr;
    }
    _stageScreen.Bitmap.reset();
    _stageScreen.DDB = nullptr;
}

bool VideoMemoryGraphicsDriver::DoNullSpriteCallback(int x, int y)
{
    if (!_nullSpriteCallback)
        throw Ali3DException("Unhandled attempt to draw null sprite");
    _stageScreenDirty = false;
    _stageScreen.Bitmap->ClearTransparent();
    // NOTE: this is not clear whether return value of callback may be
    // relied on. Existing plugins do not seem to return anything but 0,
    // even if they handle this event.
    _stageScreenDirty |= _nullSpriteCallback(x, y) != 0;
    if (_stageScreenDirty)
    {
        UpdateDDBFromBitmap(_stageScreen.DDB, _stageScreen.Bitmap.get(), true);
        return true;
    }
    return false;
}

IDriverDependantBitmap *VideoMemoryGraphicsDriver::MakeFx(int r, int g, int b)
{
    if (_fxIndex == _fxPool.size()) _fxPool.push_back(ScreenFx());
    ScreenFx &fx = _fxPool[_fxIndex];
    if (fx.DDB == nullptr)
    {
        fx.Raw = BitmapHelper::CreateBitmap(16, 16, _mode.ColorDepth);
        fx.DDB = CreateDDBFromBitmap(fx.Raw, false, true);
    }
    if (r != fx.Red || g != fx.Green || b != fx.Blue)
    {
        fx.Raw->Clear(makecol_depth(fx.Raw->GetColorDepth(), r, g, b));
        this->UpdateDDBFromBitmap(fx.DDB, fx.Raw, false);
        fx.Red = r;
        fx.Green = g;
        fx.Blue = b;
    }
    _fxIndex++;
    return fx.DDB;
}

void VideoMemoryGraphicsDriver::ResetFxPool()
{
    _fxIndex = 0;
}

void VideoMemoryGraphicsDriver::DestroyFxPool()
{
    for (auto &fx : _fxPool)
    {
        if (fx.DDB)
            DestroyDDB(fx.DDB);
        delete fx.Raw;
    }
    _fxPool.clear();
    _fxIndex = 0;
}


#define algetr32(c) getr32(c)
#define algetg32(c) getg32(c)
#define algetb32(c) getb32(c)
#define algeta32(c) geta32(c)

#define algetr16(c) getr16(c)
#define algetg16(c) getg16(c)
#define algetb16(c) getb16(c)

#define algetr8(c)  getr8(c)
#define algetg8(c)  getg8(c)
#define algetb8(c)  getb8(c)


__inline void get_pixel_if_not_transparent8(unsigned char *pixel, unsigned char *red, unsigned char *green, unsigned char *blue, unsigned char *divisor)
{
  if (pixel[0] != MASK_COLOR_8)
  {
    *red += algetr8(pixel[0]);
    *green += algetg8(pixel[0]);
    *blue += algetb8(pixel[0]);
    divisor[0]++;
  }
}

__inline void get_pixel_if_not_transparent16(unsigned short *pixel, unsigned short *red, unsigned short *green, unsigned short *blue, unsigned short *divisor)
{
  if (pixel[0] != MASK_COLOR_16)
  {
    *red += algetr16(pixel[0]);
    *green += algetg16(pixel[0]);
    *blue += algetb16(pixel[0]);
    divisor[0]++;
  }
}

__inline void get_pixel_if_not_transparent32(unsigned int *pixel, unsigned int *red, unsigned int *green, unsigned int *blue, unsigned int *divisor)
{
  if (pixel[0] != MASK_COLOR_32)
  {
    *red += algetr32(pixel[0]);
    *green += algetg32(pixel[0]);
    *blue += algetb32(pixel[0]);
    divisor[0]++;
  }
}


#define VMEMCOLOR_RGBA(r,g,b,a) \
    ( (((a) & 0xFF) << _vmem_a_shift_32) | (((r) & 0xFF) << _vmem_r_shift_32) | (((g) & 0xFF) << _vmem_g_shift_32) | (((b) & 0xFF) << _vmem_b_shift_32) )


void VideoMemoryGraphicsDriver::BitmapToVideoMem(const Bitmap *bitmap, const bool has_alpha, const TextureTile *tile,
                                                 char *dst_ptr, const int dst_pitch, const bool usingLinearFiltering)
{
  const int src_depth = bitmap->GetColorDepth();
  bool lastPixelWasTransparent = false;
  for (int y = 0; y < tile->height; y++)
  {
    lastPixelWasTransparent = false;
    const uint8_t *scanline_before = bitmap->GetScanLine(y + tile->y - 1);
    const uint8_t *scanline_at     = bitmap->GetScanLine(y + tile->y);
    const uint8_t *scanline_after  = bitmap->GetScanLine(y + tile->y + 1);
    unsigned int* memPtrLong = (unsigned int*)dst_ptr;

    for (int x = 0; x < tile->width; x++)
    {
      if (src_depth == 8)
      {
        unsigned char* srcData = (unsigned char*)&scanline_at[(x + tile->x) * sizeof(char)];
        if (*srcData == MASK_COLOR_8)
        {
          if (!usingLinearFiltering)
            memPtrLong[x] = 0;
          // set to transparent, but use the colour from the neighbouring 
          // pixel to stop the linear filter doing black outlines
          else
          {
            unsigned char red = 0, green = 0, blue = 0, divisor = 0;
            if (x > 0)
              get_pixel_if_not_transparent8(&srcData[-1], &red, &green, &blue, &divisor);
            if (x < tile->width - 1)
              get_pixel_if_not_transparent8(&srcData[1], &red, &green, &blue, &divisor);
            if (y > 0)
              get_pixel_if_not_transparent8((unsigned char*)&scanline_before[(x + tile->x) * sizeof(char)], &red, &green, &blue, &divisor);
            if (y < tile->height - 1)
              get_pixel_if_not_transparent8((unsigned char*)&scanline_after[(x + tile->x) * sizeof(char)], &red, &green, &blue, &divisor);
            if (divisor > 0)
              memPtrLong[x] = VMEMCOLOR_RGBA(red / divisor, green / divisor, blue / divisor, 0);
            else
              memPtrLong[x] = 0;
          }
          lastPixelWasTransparent = true;
        }
        else
        {
          memPtrLong[x] = VMEMCOLOR_RGBA(algetr8(*srcData), algetg8(*srcData), algetb8(*srcData), 0xFF);
          if (lastPixelWasTransparent)
          {
            // update the colour of the previous tranparent pixel, to
            // stop black outlines when linear filtering
            memPtrLong[x - 1] = memPtrLong[x] & 0x00FFFFFF;
            lastPixelWasTransparent = false;
          }
        }
      }
      else if (src_depth == 16)
      {
        unsigned short* srcData = (unsigned short*)&scanline_at[(x + tile->x) * sizeof(short)];
        if (*srcData == MASK_COLOR_16) 
        {
          if (!usingLinearFiltering)
            memPtrLong[x] = 0;
          // set to transparent, but use the colour from the neighbouring 
          // pixel to stop the linear filter doing black outlines
          else
          {
            unsigned short red = 0, green = 0, blue = 0, divisor = 0;
            if (x > 0)
              get_pixel_if_not_transparent16(&srcData[-1], &red, &green, &blue, &divisor);
            if (x < tile->width - 1)
              get_pixel_if_not_transparent16(&srcData[1], &red, &green, &blue, &divisor);
            if (y > 0)
              get_pixel_if_not_transparent16((unsigned short*)&scanline_before[(x + tile->x) * sizeof(short)], &red, &green, &blue, &divisor);
            if (y < tile->height - 1)
              get_pixel_if_not_transparent16((unsigned short*)&scanline_after[(x + tile->x) * sizeof(short)], &red, &green, &blue, &divisor);
            if (divisor > 0)
              memPtrLong[x] = VMEMCOLOR_RGBA(red / divisor, green / divisor, blue / divisor, 0);
            else
              memPtrLong[x] = 0;
          }
          lastPixelWasTransparent = true;
        }
        else
        {
          memPtrLong[x] = VMEMCOLOR_RGBA(algetr16(*srcData), algetg16(*srcData), algetb16(*srcData), 0xFF);
          if (lastPixelWasTransparent)
          {
            // update the colour of the previous tranparent pixel, to
            // stop black outlines when linear filtering
            memPtrLong[x - 1] = memPtrLong[x] & 0x00FFFFFF;
            lastPixelWasTransparent = false;
          }
        }
      }
      else if (src_depth == 32)
      {
        unsigned int* srcData = (unsigned int*)&scanline_at[(x + tile->x) * sizeof(int)];
        if (*srcData == MASK_COLOR_32)
        {
          if (!usingLinearFiltering)
            memPtrLong[x] = 0;
          // set to transparent, but use the colour from the neighbouring 
          // pixel to stop the linear filter doing black outlines
          else
          {
            unsigned int red = 0, green = 0, blue = 0, divisor = 0;
            if (x > 0)
              get_pixel_if_not_transparent32(&srcData[-1], &red, &green, &blue, &divisor);
            if (x < tile->width - 1)
              get_pixel_if_not_transparent32(&srcData[1], &red, &green, &blue, &divisor);
            if (y > 0)
              get_pixel_if_not_transparent32((unsigned int*)&scanline_before[(x + tile->x) * sizeof(int)], &red, &green, &blue, &divisor);
            if (y < tile->height - 1)
              get_pixel_if_not_transparent32((unsigned int*)&scanline_after[(x + tile->x) * sizeof(int)], &red, &green, &blue, &divisor);
            if (divisor > 0)
              memPtrLong[x] = VMEMCOLOR_RGBA(red / divisor, green / divisor, blue / divisor, 0);
            else
              memPtrLong[x] = 0;
          }
          lastPixelWasTransparent = true;
        }
        else if (has_alpha)
        {
          memPtrLong[x] = VMEMCOLOR_RGBA(algetr32(*srcData), algetg32(*srcData), algetb32(*srcData), algeta32(*srcData));
        }
        else
        {
          memPtrLong[x] = VMEMCOLOR_RGBA(algetr32(*srcData), algetg32(*srcData), algetb32(*srcData), 0xFF);
          if (lastPixelWasTransparent)
          {
            // update the colour of the previous tranparent pixel, to
            // stop black outlines when linear filtering
            memPtrLong[x - 1] = memPtrLong[x] & 0x00FFFFFF;
            lastPixelWasTransparent = false;
          }
        }
      }
    }

    dst_ptr += dst_pitch;
  }
}

void VideoMemoryGraphicsDriver::BitmapToVideoMemOpaque(const Bitmap *bitmap, const bool has_alpha, const TextureTile *tile,
    char *dst_ptr, const int dst_pitch)
{
  const int src_depth = bitmap->GetColorDepth();
  for (int y = 0; y < tile->height; y++)
  {
    const uint8_t *scanline_at = bitmap->GetScanLine(y + tile->y);
    unsigned int* memPtrLong = (unsigned int*)dst_ptr;

    for (int x = 0; x < tile->width; x++)
    {
      if (src_depth == 8)
      {
        unsigned char* srcData = (unsigned char*)&scanline_at[(x + tile->x) * sizeof(char)];
        memPtrLong[x] = VMEMCOLOR_RGBA(algetr8(*srcData), algetg8(*srcData), algetb8(*srcData), 0xFF);
      }
      else if (src_depth == 16)
      {
        unsigned short* srcData = (unsigned short*)&scanline_at[(x + tile->x) * sizeof(short)];
        memPtrLong[x] = VMEMCOLOR_RGBA(algetr16(*srcData), algetg16(*srcData), algetb16(*srcData), 0xFF);
      }
      else if (src_depth == 32)
      {
        unsigned int* srcData = (unsigned int*)&scanline_at[(x + tile->x) * sizeof(int)];
        if (has_alpha)
          memPtrLong[x] = VMEMCOLOR_RGBA(algetr32(*srcData), algetg32(*srcData), algetb32(*srcData), algeta32(*srcData));
        else
          memPtrLong[x] = VMEMCOLOR_RGBA(algetr32(*srcData), algetg32(*srcData), algetb32(*srcData), 0xFF);
      }
    }

    dst_ptr += dst_pitch;
  }
}

} // namespace Engine
} // namespace AGS
