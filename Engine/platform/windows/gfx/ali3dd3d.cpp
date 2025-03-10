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

#include "core/platform.h"

#if AGS_HAS_DIRECT3D

#include "platform/windows/gfx/ali3dd3d.h"
#include <algorithm>
#include <SDL.h>
#include <glm/ext.hpp>
#include "ac/sys_events.h"
#include "ac/timer.h"
#include "debug/out.h"
#include "gfx/ali3dexception.h"
#include "gfx/gfx_def.h"
#include "gfx/gfxfilter_d3d.h"
#include "gfx/gfxfilter_aad3d.h"
#include "platform/base/agsplatformdriver.h"
#include "platform/base/sys_main.h"
#include "util/library.h"

using namespace AGS::Common;

// Necessary to update textures from 8-bit bitmaps
extern RGB palette[256];

//
// Following functions implement various matrix operations. Normally they are found in the auxiliary d3d9x.dll,
// but we do not want AGS to be dependent on it.
// TODO: investigate possibility of using glm here (might need conversion to D3D matrix format)
//
// Setup identity matrix
void MatrixIdentity(D3DMATRIX &m)
{
    m = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
}
// Setup translation matrix
void MatrixTranslate(D3DMATRIX &m, float x, float y, float z)
{
    MatrixIdentity(m);
    m.m[3][0] = x;
    m.m[3][1] = y;
    m.m[3][2] = z;
}
// Setup scaling matrix
void MatrixScale(D3DMATRIX &m, float sx, float sy, float sz)
{
    MatrixIdentity(m);
    m.m[0][0] = sx;
    m.m[1][1] = sy;
    m.m[2][2] = sz;
}
// Setup rotation around Z axis; angle is in radians
void MatrixRotateZ(D3DMATRIX &m, float angle)
{
    MatrixIdentity(m);
    m.m[0][0] = cos(angle);
    m.m[1][1] = cos(angle);
    m.m[0][1] = sin(angle);
    m.m[1][0] = -sin(angle);
}
// Matrix multiplication
void MatrixMultiply(D3DMATRIX &mr, const D3DMATRIX &m1, const D3DMATRIX &m2)
{
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            mr.m[i][j] = m1.m[i][0] * m2.m[0][j] + m1.m[i][1] * m2.m[1][j] + m1.m[i][2] * m2.m[2][j] + m1.m[i][3] * m2.m[3][j];
}
// Setup full 2D transformation matrix
void MatrixTransform2D(D3DMATRIX &m, float x, float y, float sx, float sy, float anglez)
{
    D3DMATRIX translate;
    D3DMATRIX rotate;
    D3DMATRIX scale;
    MatrixTranslate(translate, x, y, 0.f);
    MatrixRotateZ(rotate, anglez);
    MatrixScale(scale, sx, sy, 1.f);

    D3DMATRIX tr1;
    MatrixMultiply(tr1, scale, rotate);
    MatrixMultiply(m, tr1, translate);
}
// Setup inverse 2D transformation matrix
void MatrixTransformInverse2D(D3DMATRIX &m, float x, float y, float sx, float sy, float anglez)
{
    D3DMATRIX translate;
    D3DMATRIX rotate;
    D3DMATRIX scale;
    MatrixTranslate(translate, x, y, 0.f);
    MatrixRotateZ(rotate, anglez);
    MatrixScale(scale, sx, sy, 1.f);

    D3DMATRIX tr1;
    MatrixMultiply(tr1, translate, rotate);
    MatrixMultiply(m, tr1, scale);
}


namespace AGS
{
namespace Engine
{
namespace D3D
{

using namespace Common;

D3DTextureData::~D3DTextureData()
{
    if (_tiles)
    {
        for (size_t i = 0; i < _numTiles; ++i)
            _tiles[i].texture->Release();
        delete[] _tiles;
    }
    if (_vertex)
    {
        _vertex->Release();
    }
}

static D3DFORMAT color_depth_to_d3d_format(int color_depth, bool wantAlpha);
static int d3d_format_to_color_depth(D3DFORMAT format, bool secondary);

bool D3DGfxModeList::GetMode(int index, DisplayMode &mode) const
{
    if (_direct3d && index >= 0 && index < _modeCount)
    {
        D3DDISPLAYMODE d3d_mode;
        if (SUCCEEDED(_direct3d->EnumAdapterModes(D3DADAPTER_DEFAULT, _pixelFormat, index, &d3d_mode)))
        {
            mode.Width = d3d_mode.Width;
            mode.Height = d3d_mode.Height;
            mode.ColorDepth = d3d_format_to_color_depth(d3d_mode.Format, false);
            mode.RefreshRate = d3d_mode.RefreshRate;
            return true;
        }
    }
    return false;
}

// The custom FVF, which describes the custom vertex structure.
#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1)

static int wnd_create_device();
// TODO: is there better way to not have this mode as a global static variable?
// wnd_create_device() is being called using wnd_call_proc, which does not take
// user parameters.
static DisplayMode d3d_mode_to_init;

D3DGraphicsDriver::D3DGraphicsDriver(IDirect3D9 *d3d) 
{
  direct3d = d3d;
  direct3ddevice = NULL;
  vertexbuffer = NULL;
  pixelShader = NULL;
  _legacyPixelShader = false;
  set_up_default_vertices();
  pNativeSurface = NULL;
  pNativeTexture = NULL;
  availableVideoMemory = 0;
  _smoothScaling = false;
  _pixelRenderXOffset = 0;
  _pixelRenderYOffset = 0;
  _renderSprAtScreenRes = false;

  // Shifts comply to D3DFMT_A8R8G8B8
  _vmem_a_shift_32 = 24;
  _vmem_r_shift_32 = 16;
  _vmem_g_shift_32 = 8;
  _vmem_b_shift_32 = 0;

  // Initialize default sprite batch, it will be used when no other batch was activated
  D3DGraphicsDriver::InitSpriteBatch(0, _spriteBatchDesc[0]);
}

void D3DGraphicsDriver::set_up_default_vertices()
{
  defaultVertices[0].position.x = 0.0f;
  defaultVertices[0].position.y = 0.0f;
  defaultVertices[0].position.z = 0.0f;
  defaultVertices[0].normal.x = 0.0f;
  defaultVertices[0].normal.y = 0.0f;
  defaultVertices[0].normal.z = -1.0f;
  //defaultVertices[0].color=0xffffffff;
  defaultVertices[0].tu=0.0;
  defaultVertices[0].tv=0.0;
  defaultVertices[1].position.x = 1.0f;
  defaultVertices[1].position.y = 0.0f;
  defaultVertices[1].position.z = 0.0f;
  defaultVertices[1].normal.x = 0.0f;
  defaultVertices[1].normal.y = 0.0f;
  defaultVertices[1].normal.z = -1.0f;
  //defaultVertices[1].color=0xffffffff;
  defaultVertices[1].tu=1.0;
  defaultVertices[1].tv=0.0;
  defaultVertices[2].position.x = 0.0f;
  defaultVertices[2].position.y = -1.0f;
  defaultVertices[2].position.z = 0.0f;
  defaultVertices[2].normal.x = 0.0f;
  defaultVertices[2].normal.y = 0.0f;
  defaultVertices[2].normal.z = -1.0f;
  //defaultVertices[2].color=0xffffffff;
  defaultVertices[2].tu=0.0;
  defaultVertices[2].tv=1.0;
  defaultVertices[3].position.x = 1.0f;
  defaultVertices[3].position.y = -1.0f;
  defaultVertices[3].position.z = 0.0f;
  defaultVertices[3].normal.x = 0.0f;
  defaultVertices[3].normal.y = 0.0f;
  defaultVertices[3].normal.z = -1.0f;
  //defaultVertices[3].color=0xffffffff;
  defaultVertices[3].tu=1.0;
  defaultVertices[3].tv=1.0;
}

void D3DGraphicsDriver::OnModeSet(const DisplayMode &mode)
{
  GraphicsDriverBase::OnModeSet(mode);

  // The display mode has been set up successfully, save the
  // final refresh rate that we are using
  D3DDISPLAYMODE final_display_mode;
  if (direct3ddevice->GetDisplayMode(0, &final_display_mode) == D3D_OK) {
    _mode.RefreshRate = final_display_mode.RefreshRate;
  }
  else {
    _mode.RefreshRate = 0;
  }
}

void D3DGraphicsDriver::ReleaseDisplayMode()
{
  if (!IsModeSet())
    return;

  OnModeReleased();
  ClearDrawLists();
  ClearDrawBackups();
  DestroyFxPool();
  DestroyAllStageScreens();

  sys_window_set_style(kWnd_Windowed);
}

int D3DGraphicsDriver::FirstTimeInit()
{
  HRESULT hr;

  direct3ddevice->GetDeviceCaps(&direct3ddevicecaps);

  // the PixelShader.fx uses ps_1_4
  // the PixelShaderLegacy.fx needs ps_2_0
  int requiredPSMajorVersion = 1;
  int requiredPSMinorVersion = 4;
  if (_legacyPixelShader) {
    requiredPSMajorVersion = 2;
    requiredPSMinorVersion = 0;
  }

  if (direct3ddevicecaps.PixelShaderVersion < D3DPS_VERSION(requiredPSMajorVersion, requiredPSMinorVersion))  
  {
    direct3ddevice->Release();
    direct3ddevice = NULL;
    SDL_SetError("Graphics card does not support Pixel Shader %d.%d", requiredPSMajorVersion, requiredPSMinorVersion);
    previousError = SDL_GetError();
    return -1;
  }

  // Load the pixel shader!!
  HMODULE exeHandle = GetModuleHandle(NULL);
  HRSRC hRes = FindResource(exeHandle, (_legacyPixelShader) ? "PIXEL_SHADER_LEGACY" : "PIXEL_SHADER", "DATA");
  if (hRes)
  {
    HGLOBAL hGlobal = LoadResource(exeHandle, hRes);
    if (hGlobal)
    {
      DWORD *dataPtr = (DWORD*)LockResource(hGlobal);
      hr = direct3ddevice->CreatePixelShader(dataPtr, &pixelShader);
      if (hr != D3D_OK)
      {
        direct3ddevice->Release();
        direct3ddevice = NULL;
        SDL_SetError("Failed to create pixel shader: 0x%08X", hr);
        previousError = SDL_GetError();
        return -1;
      }
      UnlockResource(hGlobal);
    }
  }
  
  if (pixelShader == NULL)
  {
    direct3ddevice->Release();
    direct3ddevice = NULL;
    SDL_SetError("Failed to load pixel shader resource");
    previousError = SDL_GetError();
    return -1;
  }

  if (direct3ddevice->CreateVertexBuffer(4*sizeof(CUSTOMVERTEX), D3DUSAGE_WRITEONLY,
          D3DFVF_CUSTOMVERTEX, D3DPOOL_MANAGED, &vertexbuffer, NULL) != D3D_OK) 
  {
    direct3ddevice->Release();
    direct3ddevice = NULL;
    SDL_SetError("Failed to create vertex buffer");
    previousError = SDL_GetError();
    return -1;
  }

  // This line crashes because my card doesn't support 8-bit textures
  //direct3ddevice->SetCurrentTexturePalette(0);

  CUSTOMVERTEX *vertices;
  vertexbuffer->Lock(0, 0, (void**)&vertices, D3DLOCK_DISCARD);

  for (int i = 0; i < 4; i++)
  {
    vertices[i] = defaultVertices[i];
  }

  vertexbuffer->Unlock();

  direct3ddevice->GetGammaRamp(0, &defaultgammaramp);

  if (defaultgammaramp.red[255] < 256)
  {
    // correct bug in some gfx drivers that returns gamma ramp
    // values from 0-255 instead of 0-65535
    for (int i = 0; i < 256; i++)
    {
      defaultgammaramp.red[i] *= 256;
      defaultgammaramp.green[i] *= 256;
      defaultgammaramp.blue[i] *= 256;
    }
  }
  currentgammaramp = defaultgammaramp;

  return 0;
}

void D3DGraphicsDriver::initD3DDLL(const DisplayMode &mode) 
{
   if (!IsModeSupported(mode))
   {
     throw Ali3DException(SDL_GetError());
   }

   d3d_mode_to_init = mode;
   if (wnd_create_device()) {
     throw Ali3DException(SDL_GetError());
   }

   availableVideoMemory = direct3ddevice->GetAvailableTextureMem();

   return;
}

/* color_depth_to_d3d_format:
 *  Convert a colour depth into the appropriate D3D tag
 */
static D3DFORMAT color_depth_to_d3d_format(int color_depth, bool wantAlpha)
{
  if (wantAlpha)
  {
    switch (color_depth)
    {
      case 8:
        return D3DFMT_P8;
      case 15:
      case 16:
        return D3DFMT_A1R5G5B5;
      case 24:
      case 32:
        return D3DFMT_A8R8G8B8;
    }
  }
  else
  {
    switch (color_depth)
    {
      case 8:
        return D3DFMT_P8;
      case 15:  // don't use X1R5G5B5 because some cards don't support it
        return D3DFMT_A1R5G5B5;
      case 16:
        return D3DFMT_R5G6B5;
      case 24:
        return D3DFMT_R8G8B8;
      case 32:
        return D3DFMT_X8R8G8B8;
    }
  }
  return D3DFMT_UNKNOWN;
}

/* d3d_format_to_color_depth:
 *  Convert a D3D tag to colour depth
 *
 * TODO: this is currently an inversion of color_depth_to_d3d_format;
 * check later if more formats should be handled
 */
static int d3d_format_to_color_depth(D3DFORMAT format, bool secondary)
{
  switch (format)
  {
  case D3DFMT_P8:
    return 8;
  case D3DFMT_A1R5G5B5:
    return secondary ? 15 : 16;
  case D3DFMT_X1R5G5B5:
    return secondary ? 15 : 16;
  case D3DFMT_R5G6B5:
    return 16;
  case D3DFMT_R8G8B8:
    return secondary ? 24 : 32;
  case D3DFMT_A8R8G8B8:
  case D3DFMT_X8R8G8B8:
    return 32;
  }
  return 0;
}

bool D3DGraphicsDriver::IsModeSupported(const DisplayMode &mode)
{
  if (mode.Width <= 0 || mode.Height <= 0 || mode.ColorDepth <= 0)
  {
    SDL_SetError("Invalid resolution parameters: %d x %d x %d", mode.Width, mode.Height, mode.ColorDepth);
    return false;
  }

  if (!mode.IsRealFullscreen())
  {
    return true;
  }

  D3DFORMAT pixelFormat = color_depth_to_d3d_format(mode.ColorDepth, false);
  D3DDISPLAYMODE d3d_mode;

  int mode_count = direct3d->GetAdapterModeCount(D3DADAPTER_DEFAULT, pixelFormat);
  for (int i = 0; i < mode_count; i++)
  {
    if (FAILED(direct3d->EnumAdapterModes(D3DADAPTER_DEFAULT, pixelFormat, i, &d3d_mode)))
    {
      SDL_SetError("IDirect3D9::EnumAdapterModes failed");
      return false;
    }

    if ((d3d_mode.Width == static_cast<uint32_t>(mode.Width)) &&
        (d3d_mode.Height == static_cast<uint32_t>(mode.Height)))
    {
      return true;
    }
  }

  SDL_SetError("The requested adapter mode is not supported");
  return false;
}

bool D3DGraphicsDriver::SupportsGammaControl() 
{
  if ((direct3ddevicecaps.Caps2 & D3DCAPS2_FULLSCREENGAMMA) == 0)
    return false;

  if (!_mode.IsRealFullscreen())
    return false;

  return true;
}

void D3DGraphicsDriver::SetGamma(int newGamma)
{
  for (int i = 0; i < 256; i++) 
  {
    int newValue = ((int)defaultgammaramp.red[i] * newGamma) / 100;
    if (newValue >= 65535)
      newValue = 65535;
    currentgammaramp.red[i] = newValue;
    currentgammaramp.green[i] = newValue;
    currentgammaramp.blue[i] = newValue;
  }

  direct3ddevice->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &currentgammaramp);
}

/* wnd_set_video_mode:
 *  Called by window thread to set a gfx mode; this is needed because DirectDraw can only
 *  change the mode in the thread that handles the window.
 */
static int wnd_create_device()
{
  return D3DGraphicsFactory::GetD3DDriver()->_initDLLCallback(d3d_mode_to_init);
}

static int wnd_reset_device()
{
  return D3DGraphicsFactory::GetD3DDriver()->_resetDeviceIfNecessary();
}

int D3DGraphicsDriver::_resetDeviceIfNecessary()
{
  HRESULT hr = direct3ddevice->TestCooperativeLevel();
  if (hr == D3DERR_DEVICELOST)
  {
    return hr;
  }

  if (hr == D3DERR_DEVICENOTRESET)
  {
    hr = ResetD3DDevice();
    if (hr != D3D_OK)
    {
      Debug::Printf("D3DGraphicsDriver: failed to reset D3D device");
      return hr;
    }

    InitializeD3DState();
    CreateVirtualScreen();
    direct3ddevice->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &currentgammaramp);
  }

  return 0;
}

int D3DGraphicsDriver::_initDLLCallback(const DisplayMode &mode)
{
  SDL_Window *window = sys_get_window();
  if (!window)
  {
    sys_window_create("", mode.Width, mode.Height, mode.Mode);
  }

  HWND hwnd = (HWND)sys_win_get_window();
  memset( &d3dpp, 0, sizeof(d3dpp) );
  d3dpp.BackBufferWidth = mode.Width;
  d3dpp.BackBufferHeight = mode.Height;
  d3dpp.BackBufferFormat = color_depth_to_d3d_format(mode.ColorDepth, false);
  d3dpp.BackBufferCount = 1;
  d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
  // THIS MUST BE SWAPEFFECT_COPY FOR PlayVideo TO WORK
  d3dpp.SwapEffect = D3DSWAPEFFECT_COPY; //D3DSWAPEFFECT_DISCARD; 
  d3dpp.hDeviceWindow = hwnd;
  d3dpp.Windowed = mode.IsRealFullscreen() ? FALSE : TRUE;
  d3dpp.EnableAutoDepthStencil = FALSE;
  d3dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER; // we need this flag to access the backbuffer with lockrect
  d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
  if(mode.Vsync)
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
  else
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

  /* If full screen, specify the refresh rate */
  // TODO find a way to avoid the wrong refreshrate to be set on mode.RefreshRate
  // for now it's best to let it set automatically, so we prevent alt tab delays due to mismatching refreshrates
  //if ((d3dpp.Windowed == FALSE) && (mode.RefreshRate > 0))
  //  d3dpp.FullScreen_RefreshRateInHz = mode.RefreshRate;

  if (_initGfxCallback != NULL)
    _initGfxCallback(&d3dpp);

  bool first_time_init = direct3ddevice == NULL;
  HRESULT hr = 0;
  if (direct3ddevice)
  {
    hr = ResetD3DDevice();
  }
  else
  {
    hr = direct3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                      D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,  // multithreaded required for AVI player
                      &d3dpp, &direct3ddevice);
  }
  if (hr != D3D_OK)
  {
    if (!previousError.IsEmpty())
      SDL_SetError(previousError.GetCStr());
    else
      SDL_SetError("Failed to create Direct3D Device: 0x%08X", hr);
    return -1;
  }

  if (first_time_init)
  {
    int ft_res = FirstTimeInit();
    if (ft_res != 0)
      return ft_res;
  }
  else
  {
    // Only adjust window styles in non-fullscreen modes:
    // for real fullscreen Direct3D will adjust the window and display.
    if (!mode.IsRealFullscreen())
    {
      sys_window_set_style(mode.Mode, Size(mode.Width, mode.Height));
    }
  }
  return 0;
}

void D3DGraphicsDriver::InitializeD3DState()
{
  direct3ddevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_RGBA(0, 0, 0, 255), 0.5f, 0);

  // set the render flags.
  direct3ddevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

  direct3ddevice->SetRenderState(D3DRS_LIGHTING, true);
  direct3ddevice->SetRenderState(D3DRS_ZENABLE, FALSE);

  direct3ddevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  direct3ddevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  direct3ddevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

  direct3ddevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
  direct3ddevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER); 
  direct3ddevice->SetRenderState(D3DRS_ALPHAREF, (DWORD)0);

  direct3ddevice->SetFVF(D3DFVF_CUSTOMVERTEX);

  D3DMATERIAL9 material;
  ZeroMemory(&material, sizeof(material));				//zero memory ( NEW )
  material.Diffuse.r = 1.0f;						//diffuse color ( NEW )
  material.Diffuse.g = 1.0f;
  material.Diffuse.b = 1.0f;
  direct3ddevice->SetMaterial(&material);

  D3DLIGHT9 d3dLight;
  ZeroMemory(&d3dLight, sizeof(D3DLIGHT9));
      
  // Set up a white point light.
  d3dLight.Type = D3DLIGHT_DIRECTIONAL;
  d3dLight.Diffuse.r  = 1.0f;
  d3dLight.Diffuse.g  = 1.0f;
  d3dLight.Diffuse.b  = 1.0f;
  d3dLight.Diffuse.a  = 1.0f;
  d3dLight.Ambient.r  = 1.0f;
  d3dLight.Ambient.g  = 1.0f;
  d3dLight.Ambient.b  = 1.0f;
  d3dLight.Specular.r = 1.0f;
  d3dLight.Specular.g = 1.0f;
  d3dLight.Specular.b = 1.0f;
      
  // Position it high in the scene and behind the user.
  // Remember, these coordinates are in world space, so
  // the user could be anywhere in world space, too. 
  // For the purposes of this example, assume the user
  // is at the origin of world space.
  d3dLight.Direction.x = 0.0f;
  d3dLight.Direction.y = 0.0f;
  d3dLight.Direction.z = 1.0f;
      
  // Don't attenuate.
  d3dLight.Attenuation0 = 1.0f; 
  d3dLight.Range        = 1000.0f;
      
  // Set the property information for the first light.
  direct3ddevice->SetLight(0, &d3dLight);
  direct3ddevice->LightEnable(0, TRUE);

  // If we already have a render frame configured, then setup viewport immediately
  SetupViewport();
}

void D3DGraphicsDriver::SetupViewport()
{
  if (!IsModeSet() || !IsRenderFrameValid() || !IsNativeSizeValid())
    return;

  const float src_width = _srcRect.GetWidth();
  const float src_height = _srcRect.GetHeight();
  const float disp_width = _mode.Width;
  const float disp_height = _mode.Height;

  // Setup orthographic projection matrix
  D3DMATRIX matOrtho = {
    (2.0f / src_width), 0.0, 0.0, 0.0,
    0.0, (2.0f / src_height), 0.0, 0.0,
    0.0, 0.0, 0.0, 0.0,
    0.0, 0.0, 0.0, 1.0
  };

  D3DMATRIX matIdentity = {
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
  };

  direct3ddevice->SetTransform(D3DTS_WORLD, &matIdentity);
  direct3ddevice->SetTransform(D3DTS_VIEW, &matIdentity);
  direct3ddevice->SetTransform(D3DTS_PROJECTION, &matOrtho);

  // See "Directly Mapping Texels to Pixels" MSDN article for why this is necessary
  // http://msdn.microsoft.com/en-us/library/windows/desktop/bb219690.aspx
  _pixelRenderXOffset = (src_width / disp_width) / 2.0f;
  _pixelRenderYOffset = (src_height / disp_height) / 2.0f;

  // Clear the screen before setting a viewport.
  ClearScreenRect(RectWH(0, 0, _mode.Width, _mode.Height), nullptr);

  // Set Viewport.
  ZeroMemory(&_d3dViewport, sizeof(D3DVIEWPORT9));
  _d3dViewport.X = _dstRect.Left;
  _d3dViewport.Y = _dstRect.Top;
  _d3dViewport.Width = _dstRect.GetWidth();
  _d3dViewport.Height = _dstRect.GetHeight();
  _d3dViewport.MinZ = 0.0f;
  _d3dViewport.MaxZ = 1.0f;
  direct3ddevice->SetViewport(&_d3dViewport);

  viewport_rect.left   = _dstRect.Left;
  viewport_rect.right  = _dstRect.Right + 1;
  viewport_rect.top    = _dstRect.Top;
  viewport_rect.bottom = _dstRect.Bottom + 1;

  // View and Projection matrixes are currently fixed in Direct3D renderer
  memcpy(glm::value_ptr(_stageMatrixes.View), &matIdentity, sizeof(float[16]));
  memcpy(glm::value_ptr(_stageMatrixes.Projection), &matOrtho, sizeof(float[16]));
}

void D3DGraphicsDriver::SetGraphicsFilter(PD3DFilter filter)
{
  _filter = filter;
  OnSetFilter();
}

void D3DGraphicsDriver::SetTintMethod(TintMethod method) 
{
  _legacyPixelShader = (method == TintReColourise);
}

bool D3DGraphicsDriver::SetDisplayMode(const DisplayMode &mode)
{
  ReleaseDisplayMode();

  if (mode.ColorDepth < 15)
  {
    SDL_SetError("Direct3D driver does not support 256-color display mode");
    return false;
  }

  try
  {
    initD3DDLL(mode);
  }
  catch (Ali3DException exception)
  {
    if (exception._message != SDL_GetError())
      SDL_SetError("%s", exception._message);
    return false;
  }
  OnInit();
  OnModeSet(mode);
  InitializeD3DState();
  CreateVirtualScreen();
  return true;
}

void D3DGraphicsDriver::UpdateDeviceScreen(const Size &screen_sz)
{
  _mode.Width = screen_sz.Width;
  _mode.Height = screen_sz.Height;
  // TODO: following resets D3D9 device, which may be sub-optimal;
  // there seem to be an option to not do this if new window size is smaller
  // and SWAPEFFECT_COPY flag is set, in which case (supposedly) we could
  // draw using same device parameters, but adjusting viewport accordingly.
  d3dpp.BackBufferWidth = _mode.Width;
  d3dpp.BackBufferHeight = _mode.Height;
  HRESULT hr = ResetD3DDevice();
  if (hr != D3D_OK)
  {
      Debug::Printf("D3DGraphicsDriver: Failed to reset D3D device");
      return;
  }
  InitializeD3DState();
  CreateVirtualScreen();
  direct3ddevice->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &currentgammaramp);
  SetupViewport();
}

void D3DGraphicsDriver::CreateVirtualScreen()
{
  if (!IsModeSet() || !IsNativeSizeValid())
    return;

  // set up native surface
  if (pNativeSurface != NULL)
  {
    pNativeSurface->Release();
    pNativeSurface = NULL;
  }
  if (pNativeTexture != NULL)
  {
      pNativeTexture->Release();
      pNativeTexture = NULL;
  }
  if (direct3ddevice->CreateTexture(
      _srcRect.GetWidth(),
      _srcRect.GetHeight(),
      1,
      D3DUSAGE_RENDERTARGET,
      color_depth_to_d3d_format(_mode.ColorDepth, false),
      D3DPOOL_DEFAULT,
      &pNativeTexture,
      NULL) != D3D_OK)
  {
      throw Ali3DException("CreateTexture failed");
  }
  if (pNativeTexture->GetSurfaceLevel(0, &pNativeSurface) != D3D_OK)
  {
      throw Ali3DException("GetSurfaceLevel failed");
  }

  direct3ddevice->ColorFill(pNativeSurface, NULL, 0);

  // create initial stage screen for plugin raw drawing
  CreateStageScreen(0, _srcRect.GetSize());
  _stageScreen = GetStageScreen(0);
}

HRESULT D3DGraphicsDriver::ResetD3DDevice()
{
  // Direct3D documentation:
  // Before calling the IDirect3DDevice9::Reset method for a device,
  // an application should release any explicit render targets, depth stencil
  // surfaces, additional swap chains, state blocks, and D3DPOOL_DEFAULT
  // resources associated with the device.
  if (pNativeSurface != NULL)
  {
    pNativeSurface->Release();
    pNativeSurface = NULL;
  }
  if (pNativeTexture != NULL)
  {
      pNativeTexture->Release();
      pNativeTexture = NULL;
  }
  return direct3ddevice->Reset(&d3dpp);
}

bool D3DGraphicsDriver::SetNativeResolution(const GraphicResolution &native_res)
{
  OnSetNativeRes(native_res);
  // Also make sure viewport is updated using new native & destination rectangles
  SetupViewport();
  CreateVirtualScreen();
  return !_srcRect.IsEmpty();
}

bool D3DGraphicsDriver::SetRenderFrame(const Rect &dst_rect)
{
  OnSetRenderFrame(dst_rect);
  // Also make sure viewport is updated using new native & destination rectangles
  SetupViewport();
  return !_dstRect.IsEmpty();
}

int D3DGraphicsDriver::GetDisplayDepthForNativeDepth(int /*native_color_depth*/) const
{
    // TODO: check for device caps to know which depth is supported?
    return 32;
}

IGfxModeList *D3DGraphicsDriver::GetSupportedModeList(int color_depth)
{
  direct3d->AddRef();
  return new D3DGfxModeList(direct3d, color_depth_to_d3d_format(color_depth, false));
}

PGfxFilter D3DGraphicsDriver::GetGraphicsFilter() const
{
    return _filter;
}

void D3DGraphicsDriver::UnInit() 
{
  OnUnInit();
  ReleaseDisplayMode();

  if (pNativeSurface)
  {
    pNativeSurface->Release();
    pNativeSurface = NULL;
  }
  if (pNativeTexture)
  {
      pNativeTexture->Release();
      pNativeTexture = NULL;
  }

  if (vertexbuffer)
  {
    vertexbuffer->Release();
    vertexbuffer = NULL;
  }

  if (pixelShader)
  {
    pixelShader->Release();
    pixelShader = NULL;
  }

  if (direct3ddevice)
  {
    direct3ddevice->Release();
    direct3ddevice = NULL;
  }

  sys_window_destroy();
}

D3DGraphicsDriver::~D3DGraphicsDriver()
{
  D3DGraphicsDriver::UnInit();

  if (direct3d)
    direct3d->Release();
}

void D3DGraphicsDriver::ClearRectangle(int x1, int y1, int x2, int y2, RGB *colorToUse)
{
  // NOTE: this function is practically useless at the moment, because D3D redraws whole game frame each time
  if (!direct3ddevice) return;
  Rect r(x1, y1, x2, y2);
  r = _scaling.ScaleRange(r);
  ClearScreenRect(r, colorToUse);
}

void D3DGraphicsDriver::ClearScreenRect(const Rect &r, RGB *colorToUse)
{
  D3DRECT rectToClear;
  rectToClear.x1 = r.Left;
  rectToClear.y1 = r.Top;
  rectToClear.x2 = r.Right + 1;
  rectToClear.y2 = r.Bottom + 1;
  DWORD colorDword = 0;
  if (colorToUse != NULL)
    colorDword = D3DCOLOR_XRGB(colorToUse->r, colorToUse->g, colorToUse->b);
  direct3ddevice->Clear(1, &rectToClear, D3DCLEAR_TARGET, colorDword, 0.5f, 0);
}

bool D3DGraphicsDriver::GetCopyOfScreenIntoBitmap(Bitmap *destination, bool at_native_res, GraphicResolution *want_fmt)
{
  // Currently don't support copying in screen resolution when we are rendering in native
  if (!_renderSprAtScreenRes)
      at_native_res = true;
  Size need_size = at_native_res ? _srcRect.GetSize() : _dstRect.GetSize();
  if (destination->GetColorDepth() != _mode.ColorDepth || destination->GetSize() != need_size)
  {
    if (want_fmt)
      *want_fmt = GraphicResolution(need_size.Width, need_size.Height, _mode.ColorDepth);
    return false;
  }
  // If we are rendering sprites at the screen resolution, and requested native res,
  // re-render last frame to the native surface
  if (at_native_res && _renderSprAtScreenRes)
  {
    _renderSprAtScreenRes = false;
    _reDrawLastFrame();
    _render(true);
    _renderSprAtScreenRes = true;
  }
  
  IDirect3DSurface9* surface = NULL;
  {
    if (_pollingCallback)
      _pollingCallback();

    if (at_native_res)
    {
      // with render to texture the texture mipmap surface can't be locked directly
      // we have to create a surface with D3DPOOL_SYSTEMMEM for GetRenderTargetData
      if (direct3ddevice->CreateOffscreenPlainSurface(
        _srcRect.GetWidth(),
        _srcRect.GetHeight(),
        color_depth_to_d3d_format(_mode.ColorDepth, false),
        D3DPOOL_SYSTEMMEM,
        &surface,
        NULL) != D3D_OK)
      {
        throw Ali3DException("CreateOffscreenPlainSurface failed");
      }
      if (direct3ddevice->GetRenderTargetData(pNativeSurface, surface) != D3D_OK)
      {
        throw Ali3DException("GetRenderTargetData failed");
      }
      
    }
    // Get the back buffer surface
    else if (direct3ddevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface) != D3D_OK)
    {
      throw Ali3DException("IDirect3DDevice9::GetBackBuffer failed");
    }

    if (_pollingCallback)
      _pollingCallback();

    D3DLOCKED_RECT lockedRect;
    if (surface->LockRect(&lockedRect, (at_native_res ? NULL : &viewport_rect), D3DLOCK_READONLY ) != D3D_OK)
    {
      throw Ali3DException("IDirect3DSurface9::LockRect failed");
    }

    BitmapHelper::ReadPixelsFromMemory(destination, (uint8_t*)lockedRect.pBits, lockedRect.Pitch);

    surface->UnlockRect();
    surface->Release();

    if (_pollingCallback)
      _pollingCallback();
  }
  return true;
}

void D3DGraphicsDriver::RenderToBackBuffer()
{
  throw Ali3DException("D3D driver does not have a back buffer");
}

void D3DGraphicsDriver::Render()
{
  Render(0, 0, kFlip_None);
}

void D3DGraphicsDriver::Render(int /*xoff*/, int /*yoff*/, GlobalFlipType /*flip*/)
{
  if (wnd_reset_device())
  {
    throw Ali3DFullscreenLostException();
  }

  _renderAndPresent(true);
}

void D3DGraphicsDriver::_reDrawLastFrame()
{
  RestoreDrawLists();
}

void D3DGraphicsDriver::_renderSprite(const D3DDrawListEntry *drawListEntry, const D3DMATRIX &matGlobal,
    const SpriteColorTransform &color)
{
  HRESULT hr;
  D3DBitmap *bmpToDraw = drawListEntry->ddb;
  D3DMATRIX matSelfTransform;
  D3DMATRIX matTransform;

  const int alpha = (color.Alpha * bmpToDraw->_alpha) / 255;

  if (bmpToDraw->_tintSaturation > 0)
  {
    // Use custom pixel shader
    float vector[8];
    if (_legacyPixelShader)
    {
      rgb_to_hsv(bmpToDraw->_red, bmpToDraw->_green, bmpToDraw->_blue, &vector[0], &vector[1], &vector[2]);
      vector[0] /= 360.0; // In HSV, Hue is 0-360
    }
    else
    {
      vector[0] = (float)bmpToDraw->_red / 256.0;
      vector[1] = (float)bmpToDraw->_green / 256.0;
      vector[2] = (float)bmpToDraw->_blue / 256.0;
    }

    vector[3] = (float)bmpToDraw->_tintSaturation / 256.0;
    vector[4] = (float)alpha / 256.0;

    if (bmpToDraw->_lightLevel > 0)
      vector[5] = (float)bmpToDraw->_lightLevel / 256.0;
    else
      vector[5] = 1.0f;

    direct3ddevice->SetPixelShaderConstantF(0, &vector[0], 2);
    direct3ddevice->SetPixelShader(pixelShader);
  }
  else
  {
    // Not using custom pixel shader; set up the default one
    direct3ddevice->SetPixelShader(NULL);
    int useTintRed = 255;
    int useTintGreen = 255;
    int useTintBlue = 255;
    int textureColorOp = D3DTOP_MODULATE;

    if ((bmpToDraw->_lightLevel > 0) && (bmpToDraw->_lightLevel < 256))
    {
      // darkening the sprite... this stupid calculation is for
      // consistency with the allegro software-mode code that does
      // a trans blend with a (8,8,8) sprite
      useTintRed = (bmpToDraw->_lightLevel * 192) / 256 + 64;
      useTintGreen = useTintRed;
      useTintBlue = useTintRed;
    }
    else if (bmpToDraw->_lightLevel > 256)
    {
      // ideally we would use a multi-stage operation here
      // because we need to do TEXTURE + (TEXTURE x LIGHT)
      // but is it worth having to set the device to 2-stage?
      textureColorOp = D3DTOP_ADD;
      useTintRed = (bmpToDraw->_lightLevel - 256) / 2;
      useTintGreen = useTintRed;
      useTintBlue = useTintRed;
    }

    direct3ddevice->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_RGBA(useTintRed, useTintGreen, useTintBlue, alpha));
    direct3ddevice->SetTextureStageState(0, D3DTSS_COLOROP, textureColorOp);
    direct3ddevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    direct3ddevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);

    if (alpha == 255)
    {
      // No transparency, use texture alpha component
      direct3ddevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
      direct3ddevice->SetTextureStageState(0, D3DTSS_ALPHAARG1,  D3DTA_TEXTURE);
    }
    else
    {
      // Fixed transparency, use (TextureAlpha x FixedTranslucency)
      direct3ddevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
      direct3ddevice->SetTextureStageState(0, D3DTSS_ALPHAARG1,  D3DTA_TEXTURE);
      direct3ddevice->SetTextureStageState(0, D3DTSS_ALPHAARG2,  D3DTA_TFACTOR);
    }
  }

  const auto *txdata = bmpToDraw->_data.get();
  if (txdata->_vertex == NULL)
  {
    hr = direct3ddevice->SetStreamSource(0, vertexbuffer, 0, sizeof(CUSTOMVERTEX));
  }
  else
  {
    hr = direct3ddevice->SetStreamSource(0, txdata->_vertex, 0, sizeof(CUSTOMVERTEX));
  }
  if (hr != D3D_OK) 
  {
    throw Ali3DException("IDirect3DDevice9::SetStreamSource failed");
  }

  float width = bmpToDraw->GetWidthToRender();
  float height = bmpToDraw->GetHeightToRender();
  float xProportion = width / (float)bmpToDraw->_width;
  float yProportion = height / (float)bmpToDraw->_height;
  float drawAtX = drawListEntry->x;
  float drawAtY = drawListEntry->y;

  for (size_t ti = 0; ti < txdata->_numTiles; ++ti)
  {
    width = txdata->_tiles[ti].width * xProportion;
    height = txdata->_tiles[ti].height * yProportion;
    float xOffs;
    float yOffs = txdata->_tiles[ti].y * yProportion;
    if (bmpToDraw->_flipped)
      xOffs = (bmpToDraw->_width - (txdata->_tiles[ti].x + txdata->_tiles[ti].width)) * xProportion;
    else
      xOffs = txdata->_tiles[ti].x * xProportion;
    float thisX = drawAtX + xOffs;
    float thisY = drawAtY + yOffs;
    thisX = (-(_srcRect.GetWidth() / 2)) + thisX;
    thisY = (_srcRect.GetHeight() / 2) - thisY;

    //Setup translation and scaling matrices
    float widthToScale = (float)width;
    float heightToScale = (float)height;
    if (bmpToDraw->_flipped)
    {
      // The usual transform changes 0..1 into 0..width
      // So first negate it (which changes 0..w into -w..0)
      widthToScale = -widthToScale;
      // and now shift it over to make it 0..w again
      thisX += width;
    }

    // Multiply object's own and global matrixes
    MatrixTransform2D(matSelfTransform, (float)thisX - _pixelRenderXOffset, (float)thisY + _pixelRenderYOffset, widthToScale, heightToScale, 0.f);
    MatrixMultiply(matTransform, matSelfTransform, matGlobal);

    if ((_smoothScaling) && bmpToDraw->_useResampler && (bmpToDraw->_stretchToHeight > 0) &&
        ((bmpToDraw->_stretchToHeight != bmpToDraw->_height) ||
         (bmpToDraw->_stretchToWidth != bmpToDraw->_width)))
    {
      direct3ddevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
      direct3ddevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    }
    else if (!_renderSprAtScreenRes)
    {
      direct3ddevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
      direct3ddevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    }
    else
    {
      _filter->SetSamplerStateForStandardSprite(direct3ddevice);
    }

    direct3ddevice->SetTransform(D3DTS_WORLD, &matTransform);
    direct3ddevice->SetTexture(0, txdata->_tiles[ti].texture);

    hr = direct3ddevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, ti * 4, 2);
    if (hr != D3D_OK) 
    {
      throw Ali3DException("IDirect3DDevice9::DrawPrimitive failed");
    }

  }
}

void D3DGraphicsDriver::_renderFromTexture()
{
    if (direct3ddevice->SetStreamSource(0, vertexbuffer, 0, sizeof(CUSTOMVERTEX)) != D3D_OK)
    {
        throw Ali3DException("IDirect3DDevice9::SetStreamSource failed");
    }

    float width = _srcRect.GetWidth();
    float height = _srcRect.GetHeight();
    float drawAtX = -(_srcRect.GetWidth() / 2);
    float drawAtY = _srcRect.GetHeight() / 2;

    D3DMATRIX matrix;
    MatrixTransform2D(matrix, (float)drawAtX - _pixelRenderXOffset, (float)drawAtY + _pixelRenderYOffset, width, height, 0.f);

    direct3ddevice->SetTransform(D3DTS_WORLD, &matrix);

    direct3ddevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    direct3ddevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    _filter->SetSamplerStateForStandardSprite(direct3ddevice);

    direct3ddevice->SetTexture(0, pNativeTexture);

    if (direct3ddevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2) != D3D_OK)
    {
        throw Ali3DException("IDirect3DDevice9::DrawPrimitive failed");
    }
}

void D3DGraphicsDriver::_renderAndPresent(bool clearDrawListAfterwards)
{
  _render(clearDrawListAfterwards);
  direct3ddevice->Present(NULL, NULL, NULL, NULL);
}

void D3DGraphicsDriver::_render(bool clearDrawListAfterwards)
{
  IDirect3DSurface9 *pBackBuffer = NULL;

  if (direct3ddevice->GetRenderTarget(0, &pBackBuffer) != D3D_OK) {
    throw Ali3DException("IDirect3DSurface9::GetRenderTarget failed");
  }
  direct3ddevice->ColorFill(pBackBuffer, nullptr, D3DCOLOR_RGBA(0, 0, 0, 255));

  if (!_renderSprAtScreenRes) {
    if (direct3ddevice->SetRenderTarget(0, pNativeSurface) != D3D_OK)
    {
      throw Ali3DException("IDirect3DSurface9::SetRenderTarget failed");
    }
  }

  direct3ddevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_RGBA(0, 0, 0, 255), 0.5f, 0);
  if (direct3ddevice->BeginScene() != D3D_OK)
    throw Ali3DException("IDirect3DDevice9::BeginScene failed");

  // if showing at 2x size, the sprite can get distorted otherwise
  direct3ddevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
  direct3ddevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  direct3ddevice->SetSamplerState(0, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);

  RenderSpriteBatches();

  if (!_renderSprAtScreenRes) {
    if (direct3ddevice->SetRenderTarget(0, pBackBuffer)!= D3D_OK)
    {
      throw Ali3DException("IDirect3DSurface9::SetRenderTarget failed");
    }
    direct3ddevice->SetViewport(&_d3dViewport);
    _renderFromTexture();
  }

  direct3ddevice->EndScene();

  pBackBuffer->Release();

  if (clearDrawListAfterwards)
  {
    BackupDrawLists();
    ClearDrawLists();
  }
  ResetFxPool();
}

void D3DGraphicsDriver::SetScissor(const Rect &clip)
{
    if (!clip.IsEmpty())
    {
        direct3ddevice->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
        RECT scissor;
        if (_renderSprAtScreenRes)
        {
            scissor.left = _scaling.X.ScalePt(clip.Left);
            scissor.top = _scaling.Y.ScalePt(clip.Top);
            scissor.right = _scaling.X.ScalePt(clip.Right + 1);
            scissor.bottom = _scaling.Y.ScalePt(clip.Bottom + 1);
        }
        else
        {
            scissor.left = clip.Left;
            scissor.top = clip.Top;
            scissor.right = clip.Right + 1;
            scissor.bottom = clip.Bottom + 1;
        }
        direct3ddevice->SetScissorRect(&scissor);
    }
    else
    {
        direct3ddevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    }
}

void D3DGraphicsDriver::RenderSpriteBatches()
{
    // Close unended batches, and issue a warning
    assert(_actSpriteBatch == 0);
    while (_actSpriteBatch > 0)
        EndSpriteBatch();

    // Render all the sprite batches with necessary transformations
    for (size_t cur_spr = 0; cur_spr < _spriteList.size();)
    {
        const D3DSpriteBatch &batch = _spriteBatches[_spriteList[cur_spr].node];
        SetScissor(batch.Viewport);
        _stageScreen = GetStageScreen(batch.ID);
        memcpy(glm::value_ptr(_stageMatrixes.World), batch.Matrix.m, sizeof(float[16]));
        cur_spr = RenderSpriteBatch(batch, cur_spr);
    }

    _stageScreen = GetStageScreen(0);
    memcpy(glm::value_ptr(_stageMatrixes.World), _spriteBatches[0].Matrix.m, sizeof(float[16]));
    direct3ddevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
}

size_t D3DGraphicsDriver::RenderSpriteBatch(const D3DSpriteBatch &batch, size_t from)
{
    for (; (from < _spriteList.size()) && (_spriteList[from].node == batch.ID); ++from)
    {
        const auto &e = _spriteList[from];
        if (e.skip)
            continue;

        switch (reinterpret_cast<intptr_t>(e.ddb))
        {
        case DRAWENTRY_STAGECALLBACK:
            // raw-draw plugin support
            if (DoNullSpriteCallback(e.x, (int)direct3ddevice))
            {
                auto stageEntry = D3DDrawListEntry((D3DBitmap*)_stageScreen.DDB, batch.ID, 0, 0);
                _renderSprite(&stageEntry, batch.Matrix, batch.Color);
            }
            break;
        default:
            _renderSprite(&e, batch.Matrix, batch.Color);
            break;
        }
    }
    return from;
}

void D3DGraphicsDriver::InitSpriteBatch(size_t index, const SpriteBatchDesc &desc)
{
    Rect viewport = desc.Viewport;
    // Combine both world transform and viewport transform into one matrix for faster perfomance
    D3DMATRIX matRoomToViewport, matViewport, matViewportFinal;
    // IMPORTANT: while the sprites are usually transformed in the order of Scale-Rotate-Translate,
    // the camera's transformation is essentially reverse world transformation. And the operations
    // are inverse: Translate-Rotate-Scale
    MatrixTransformInverse2D(matRoomToViewport,
        desc.Transform.X, -(desc.Transform.Y),
        desc.Transform.ScaleX, desc.Transform.ScaleY, desc.Transform.Rotate);
    // Next step is translate to viewport position; remove this if this is
    // changed to a separate operation at some point
    // TODO: find out if this is an optimal way to translate scaled room into Top-Left screen coordinates
    float scaled_offx = (_srcRect.GetWidth() - desc.Transform.ScaleX * (float)_srcRect.GetWidth()) / 2.f;
    float scaled_offy = (_srcRect.GetHeight() - desc.Transform.ScaleY * (float)_srcRect.GetHeight()) / 2.f;
    MatrixTranslate(matViewport, viewport.Left - scaled_offx, -(viewport.Top - scaled_offy), 0.f);
    MatrixMultiply(matViewportFinal, matRoomToViewport, matViewport);

    // Then apply global node transformation (flip and offset)
    int node_tx = desc.Offset.X, node_ty = desc.Offset.Y;
    float node_sx = 1.f, node_sy = 1.f;
    if ((desc.Flip == kFlip_Vertical) || (desc.Flip == kFlip_Both))
    {
        int left = _srcRect.GetWidth() - (viewport.Right + 1);
        viewport.MoveToX(left);
        node_sx = -1.f;
    }
    if ((desc.Flip == kFlip_Horizontal) || (desc.Flip == kFlip_Both))
    {
        int top = _srcRect.GetHeight() - (viewport.Bottom + 1);
        viewport.MoveToY(top);
        node_sy = -1.f;
    }
    viewport = Rect::MoveBy(viewport, node_tx, node_ty);
    D3DMATRIX matFlip, matFinal;
    MatrixTransform2D(matFlip, node_tx, -(node_ty), node_sx, node_sy, 0.f);
    MatrixMultiply(matFinal, matViewportFinal, matFlip);

    // Apply parent batch's settings, if preset
    if (desc.Parent > 0)
    {
        const auto &parent = _spriteBatches[desc.Parent];
        D3DMATRIX matCombined;
        MatrixMultiply(matCombined, parent.Matrix, matFinal);
        matFinal = matCombined;
        viewport = ClampToRect(parent.Viewport, viewport);
    }

    // Assign the new spritebatch
    if (_spriteBatches.size() <= index)
        _spriteBatches.resize(index + 1);
    _spriteBatches[index] = D3DSpriteBatch(index, viewport, matFinal, desc.Transform.Color);

    // create stage screen for plugin raw drawing
    int src_w = viewport.GetWidth() / desc.Transform.ScaleX;
    int src_h = viewport.GetHeight() / desc.Transform.ScaleY;
    CreateStageScreen(index, Size(src_w, src_h));
}

void D3DGraphicsDriver::ResetAllBatches()
{
    _spriteBatches.clear();
    _spriteList.clear();
}

void D3DGraphicsDriver::ClearDrawBackups()
{
    _backupBatchDescs.clear();
    _backupBatches.clear();
    _backupSpriteList.clear();
}

void D3DGraphicsDriver::BackupDrawLists()
{
    _backupBatchDescs = _spriteBatchDesc;
    _backupBatches = _spriteBatches;
    _backupSpriteList = _spriteList;
}

void D3DGraphicsDriver::RestoreDrawLists()
{
    _spriteBatchDesc = _backupBatchDescs;
    _spriteBatches = _backupBatches;
    _spriteList = _backupSpriteList;
    _actSpriteBatch = 0;
}

void D3DGraphicsDriver::DrawSprite(int x, int y, IDriverDependantBitmap* ddb)
{
    _spriteList.push_back(D3DDrawListEntry((D3DBitmap*)ddb, _actSpriteBatch, x, y));
}

void D3DGraphicsDriver::DestroyDDBImpl(IDriverDependantBitmap* ddb)
{
    // Remove deleted DDB from backups
    for (auto &backup_spr : _backupSpriteList)
    {
        if (backup_spr.ddb == ddb)
            backup_spr.skip = true;
    }
    delete (D3DBitmap*)ddb;
}

void D3DGraphicsDriver::UpdateTextureRegion(D3DTextureTile *tile, Bitmap *bitmap, bool opaque, bool hasAlpha)
{
  IDirect3DTexture9* newTexture = tile->texture;

  D3DLOCKED_RECT lockedRegion;
  HRESULT hr = newTexture->LockRect(0, &lockedRegion, NULL, D3DLOCK_NOSYSLOCK | D3DLOCK_DISCARD);
  if (hr != D3D_OK)
  {
    throw Ali3DException("Unable to lock texture");
  }

  bool usingLinearFiltering = _filter->NeedToColourEdgeLines();
  char *memPtr = (char*)lockedRegion.pBits;

  if (opaque)
    BitmapToVideoMemOpaque(bitmap, hasAlpha, tile, memPtr, lockedRegion.Pitch);
  else
    BitmapToVideoMem(bitmap, hasAlpha, tile, memPtr, lockedRegion.Pitch, usingLinearFiltering);

  newTexture->UnlockRect(0);
}

void D3DGraphicsDriver::UpdateDDBFromBitmap(IDriverDependantBitmap* bitmapToUpdate, Bitmap *bitmap, bool hasAlpha)
{
  D3DBitmap *target = (D3DBitmap*)bitmapToUpdate;
  if (target->_width != bitmap->GetWidth() || target->_height != bitmap->GetHeight())
    throw Ali3DException("UpdateDDBFromBitmap: mismatched bitmap size");
  const int color_depth = bitmap->GetColorDepth();
  if (color_depth != target->_colDepth)
    throw Ali3DException("UpdateDDBFromBitmap: mismatched colour depths");

  target->_hasAlpha = hasAlpha;
  UpdateTextureData(target->_data.get(), bitmap, target->_opaque, hasAlpha);
}

void D3DGraphicsDriver::UpdateTextureData(TextureData *txdata, Bitmap *bitmap, bool opaque, bool hasAlpha)
{
  const int color_depth = bitmap->GetColorDepth();
  if (color_depth == 8)
      select_palette(palette);

  auto *d3ddata = reinterpret_cast<D3DTextureData*>(txdata);
  for (size_t i = 0; i < d3ddata->_numTiles; ++i)
  {
    UpdateTextureRegion(&d3ddata->_tiles[i], bitmap, opaque, hasAlpha);
  }

  if (color_depth == 8)
      unselect_palette();
}

int D3DGraphicsDriver::GetCompatibleBitmapFormat(int color_depth)
{
  if (color_depth == 8)
    return 8;
  if (color_depth > 8 && color_depth <= 16)
    return 16;
  return 32;
}

void D3DGraphicsDriver::AdjustSizeToNearestSupportedByCard(int *width, int *height)
{
  int allocatedWidth = *width, allocatedHeight = *height;

  if (direct3ddevicecaps.TextureCaps & D3DPTEXTURECAPS_POW2)
  {
    bool foundWidth = false, foundHeight = false;
    int tryThis = 2;
    while ((!foundWidth) || (!foundHeight))
    {
      if ((tryThis >= allocatedWidth) && (!foundWidth))
      {
        allocatedWidth = tryThis;
        foundWidth = true;
      }

      if ((tryThis >= allocatedHeight) && (!foundHeight))
      {
        allocatedHeight = tryThis;
        foundHeight = true;
      }

      tryThis = tryThis << 1;
    }
  }

  if (direct3ddevicecaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
  {
    if (allocatedWidth > allocatedHeight) 
    {
     allocatedHeight = allocatedWidth;
    }
    else
    {
     allocatedWidth = allocatedHeight;
    }
  }

  *width = allocatedWidth;
  *height = allocatedHeight;
}

bool D3DGraphicsDriver::IsTextureFormatOk( D3DFORMAT TextureFormat, D3DFORMAT AdapterFormat ) 
{
    HRESULT hr = direct3d->CheckDeviceFormat( D3DADAPTER_DEFAULT,
                                          D3DDEVTYPE_HAL,
                                          AdapterFormat,
                                          0,
                                          D3DRTYPE_TEXTURE,
                                          TextureFormat);
    
    return SUCCEEDED( hr );
}

IDriverDependantBitmap* D3DGraphicsDriver::CreateDDB(int width, int height, int color_depth, bool opaque)
{
  if (color_depth != GetCompatibleBitmapFormat(color_depth))
    throw Ali3DException("CreateDDB: bitmap colour depth not supported");
  D3DBitmap *ddb = new D3DBitmap(width, height, color_depth, opaque);
  ddb->_data.reset(reinterpret_cast<D3DTextureData*>(CreateTextureData(width, height, opaque)));
  return ddb;
}

IDriverDependantBitmap *D3DGraphicsDriver::CreateDDB(std::shared_ptr<TextureData> txdata,
    int width, int height, int color_depth, bool opaque)
{
    auto *ddb = reinterpret_cast<D3DBitmap*>(CreateDDB(width, height, color_depth, opaque));
    if (ddb)
        ddb->_data = std::static_pointer_cast<D3DTextureData>(txdata);
    return ddb;
}

std::shared_ptr<TextureData> D3DGraphicsDriver::GetTextureData(IDriverDependantBitmap *ddb)
{
    return std::static_pointer_cast<TextureData>((reinterpret_cast<D3DBitmap*>(ddb))->_data);
}

TextureData *D3DGraphicsDriver::CreateTextureData(int width, int height, bool opaque)
{
  assert(width > 0);
  assert(height > 0);
  int allocatedWidth = width;
  int allocatedHeight = height;
  AdjustSizeToNearestSupportedByCard(&allocatedWidth, &allocatedHeight);
  int tilesAcross = 1, tilesDown = 1;

  auto *txdata = new D3DTextureData();
  // Calculate how many textures will be necessary to
  // store this image
  tilesAcross = (allocatedWidth + direct3ddevicecaps.MaxTextureWidth - 1) / direct3ddevicecaps.MaxTextureWidth;
  tilesDown = (allocatedHeight + direct3ddevicecaps.MaxTextureHeight - 1) / direct3ddevicecaps.MaxTextureHeight;
  assert(tilesAcross > 0);
  assert(tilesDown > 0);
  tilesAcross = std::max(1, tilesAcross);
  tilesDown = std::max(1, tilesDown);
  int tileWidth = width / tilesAcross;
  int lastTileExtraWidth = width % tilesAcross;
  int tileHeight = height / tilesDown;
  int lastTileExtraHeight = height % tilesDown;
  int tileAllocatedWidth = tileWidth;
  int tileAllocatedHeight = tileHeight;

  AdjustSizeToNearestSupportedByCard(&tileAllocatedWidth, &tileAllocatedHeight);

  int numTiles = tilesAcross * tilesDown;
  D3DTextureTile *tiles = new D3DTextureTile[numTiles];

  CUSTOMVERTEX *vertices = NULL;

  if ((numTiles == 1) &&
      (allocatedWidth == width) &&
      (allocatedHeight == height))
  {
    // use default whole-image vertices
  }
  else
  {
     // The texture is not the same as the bitmap, so create some custom vertices
     // so that only the relevant portion of the texture is rendered
     int vertexBufferSize = numTiles * 4 * sizeof(CUSTOMVERTEX);
     HRESULT hr = direct3ddevice->CreateVertexBuffer(vertexBufferSize, 0,
         D3DFVF_CUSTOMVERTEX, D3DPOOL_MANAGED, &txdata->_vertex, NULL);

     if (hr != D3D_OK) 
     {
        delete []tiles;
        char errorMessage[200];
        snprintf(errorMessage, sizeof(errorMessage), "Direct3DDevice9::CreateVertexBuffer(Length=%d) for texture failed: error code %08X", vertexBufferSize, hr);
        throw Ali3DException(errorMessage);
     }

     if (txdata->_vertex->Lock(0, 0, (void**)&vertices, D3DLOCK_DISCARD) != D3D_OK)
     {
       delete []tiles;
       throw Ali3DException("Failed to lock vertex buffer");
     }
  }

  for (int x = 0; x < tilesAcross; x++)
  {
    for (int y = 0; y < tilesDown; y++)
    {
      D3DTextureTile *thisTile = &tiles[y * tilesAcross + x];
      int thisAllocatedWidth = tileAllocatedWidth;
      int thisAllocatedHeight = tileAllocatedHeight;
      thisTile->x = x * tileWidth;
      thisTile->y = y * tileHeight;
      thisTile->width = tileWidth;
      thisTile->height = tileHeight;
      if (x == tilesAcross - 1) 
      {
        thisTile->width += lastTileExtraWidth;
        thisAllocatedWidth = thisTile->width;
        AdjustSizeToNearestSupportedByCard(&thisAllocatedWidth, &thisAllocatedHeight);
      }
      if (y == tilesDown - 1) 
      {
        thisTile->height += lastTileExtraHeight;
        thisAllocatedHeight = thisTile->height;
        AdjustSizeToNearestSupportedByCard(&thisAllocatedWidth, &thisAllocatedHeight);
      }

      if (vertices != NULL)
      {
        for (int vidx = 0; vidx < 4; vidx++)
        {
          int i = (y * tilesAcross + x) * 4 + vidx;
          vertices[i] = defaultVertices[vidx];
          if (vertices[i].tu > 0.0)
          {
            vertices[i].tu = (float)thisTile->width / (float)thisAllocatedWidth;
          }
          if (vertices[i].tv > 0.0)
          {
            vertices[i].tv = (float)thisTile->height / (float)thisAllocatedHeight;
          }
        }
      }

      // NOTE: pay attention that the texture format depends on the **display mode**'s color format,
      // rather than source bitmap's color depth!
      D3DFORMAT textureFormat = color_depth_to_d3d_format(_mode.ColorDepth, !opaque);
      HRESULT hr = direct3ddevice->CreateTexture(thisAllocatedWidth, thisAllocatedHeight, 1, 0, 
                                      textureFormat,
                                      D3DPOOL_MANAGED, &thisTile->texture, NULL);
      if (hr != D3D_OK)
      {
        char errorMessage[200];
        snprintf(errorMessage, sizeof(errorMessage), "Direct3DDevice9::CreateTexture(X=%d, Y=%d, FMT=%d) failed: error code %08X", thisAllocatedWidth, thisAllocatedHeight, textureFormat, hr);
        throw Ali3DException(errorMessage);
      }

    }
  }

  if (vertices != NULL)
  {
      txdata->_vertex->Unlock();
  }

  txdata->_numTiles = numTiles;
  txdata->_tiles = tiles;
  return txdata;
}

void D3DGraphicsDriver::do_fade(bool fadingOut, int speed, int targetColourRed, int targetColourGreen, int targetColourBlue)
{
  // Construct scene in order: game screen, fade fx, post game overlay
  // NOTE: please keep in mind: redrawing last saved frame here instead of constructing new one
  // is done because of backwards-compatibility issue: originally AGS faded out using frame
  // drawn before the script that triggers blocking fade (e.g. instigated by ChangeRoom).
  // Unfortunately some existing games were changing looks of the screen during same function,
  // but these were not supposed to get on screen until before fade-in.
  if (fadingOut)
    this->_reDrawLastFrame();
  else if (_drawScreenCallback != NULL)
    _drawScreenCallback();
  Bitmap *blackSquare = BitmapHelper::CreateBitmap(16, 16, 32);
  blackSquare->Clear(makecol32(targetColourRed, targetColourGreen, targetColourBlue));
  IDriverDependantBitmap *d3db = this->CreateDDBFromBitmap(blackSquare, false, true);
  delete blackSquare;
  BeginSpriteBatch(_srcRect, SpriteTransform());
  d3db->SetStretch(_srcRect.GetWidth(), _srcRect.GetHeight(), false);
  this->DrawSprite(0, 0, d3db);
  EndSpriteBatch();
  if (_drawPostScreenCallback != NULL)
      _drawPostScreenCallback();

  if (speed <= 0) speed = 16;
  speed *= 2;  // harmonise speeds with software driver which is faster
  for (int a = 1; a < 255; a += speed)
  {
    d3db->SetAlpha(fadingOut ? a : (255 - a));
    this->_renderAndPresent(false);

    sys_evt_process_pending();
    if (_pollingCallback)
      _pollingCallback();
    WaitForNextFrame();
  }

  if (fadingOut)
  {
    d3db->SetAlpha(255);
    this->_renderAndPresent(false);
  }

  this->DestroyDDB(d3db);
  this->ClearDrawLists();
  ResetFxPool();
}

void D3DGraphicsDriver::FadeOut(int speed, int targetColourRed, int targetColourGreen, int targetColourBlue) 
{
  do_fade(true, speed, targetColourRed, targetColourGreen, targetColourBlue);
}

void D3DGraphicsDriver::FadeIn(int speed, PALETTE /*p*/, int targetColourRed, int targetColourGreen, int targetColourBlue) 
{
  do_fade(false, speed, targetColourRed, targetColourGreen, targetColourBlue);
}

void D3DGraphicsDriver::BoxOutEffect(bool blackingOut, int speed, int delay)
{
  // Construct scene in order: game screen, fade fx, post game overlay
  if (blackingOut)
     this->_reDrawLastFrame();
  else if (_drawScreenCallback != NULL)
    _drawScreenCallback();
  Bitmap *blackSquare = BitmapHelper::CreateBitmap(16, 16, 32);
  blackSquare->Clear();
  IDriverDependantBitmap *d3db = this->CreateDDBFromBitmap(blackSquare, false, true);
  delete blackSquare;
  BeginSpriteBatch(_srcRect, SpriteTransform());
  const size_t fx_batch = _actSpriteBatch;
  d3db->SetStretch(_srcRect.GetWidth(), _srcRect.GetHeight(), false);
  this->DrawSprite(0, 0, d3db);
  if (!blackingOut)
  {
    // when fading in, draw four black boxes, one
    // across each side of the screen
    this->DrawSprite(0, 0, d3db);
    this->DrawSprite(0, 0, d3db);
    this->DrawSprite(0, 0, d3db);
  }
  EndSpriteBatch();
  std::vector<D3DDrawListEntry> &drawList = _spriteList;
  const size_t last = drawList.size() - 1;

  if (_drawPostScreenCallback != NULL)
    _drawPostScreenCallback();

  int yspeed = _srcRect.GetHeight() / (_srcRect.GetWidth() / speed);
  int boxWidth = speed;
  int boxHeight = yspeed;

  while (boxWidth < _srcRect.GetWidth())
  {
    boxWidth += speed;
    boxHeight += yspeed;
    
    if (blackingOut)
    {
      drawList[last].x = _srcRect.GetWidth() / 2- boxWidth / 2;
      drawList[last].y = _srcRect.GetHeight() / 2 - boxHeight / 2;
      d3db->SetStretch(boxWidth, boxHeight, false);
    }
    else
    {
      drawList[last - 3].x = _srcRect.GetWidth() / 2 - boxWidth / 2 - _srcRect.GetWidth();
      drawList[last - 2].y = _srcRect.GetHeight() / 2 - boxHeight / 2 - _srcRect.GetHeight();
      drawList[last - 1].x = _srcRect.GetWidth() / 2 + boxWidth / 2;
      drawList[last    ].y = _srcRect.GetHeight() / 2 + boxHeight / 2;
      d3db->SetStretch(_srcRect.GetWidth(), _srcRect.GetHeight(), false);
    }
    
    this->_renderAndPresent(false);

    sys_evt_process_pending();
    if (_pollingCallback)
      _pollingCallback();
    platform->Delay(delay);
  }

  this->DestroyDDB(d3db);
  this->ClearDrawLists();
  ResetFxPool();
}

void D3DGraphicsDriver::SetScreenFade(int red, int green, int blue)
{
    D3DBitmap *ddb = static_cast<D3DBitmap*>(MakeFx(red, green, blue));
    ddb->SetStretch(_spriteBatches[_actSpriteBatch].Viewport.GetWidth(),
        _spriteBatches[_actSpriteBatch].Viewport.GetHeight(), false);
    ddb->SetAlpha(255);
    _spriteList.push_back(D3DDrawListEntry(ddb, _actSpriteBatch, 0, 0));
}

void D3DGraphicsDriver::SetScreenTint(int red, int green, int blue)
{ 
    if (red == 0 && green == 0 && blue == 0) return;
    D3DBitmap *ddb = static_cast<D3DBitmap*>(MakeFx(red, green, blue));
    ddb->SetStretch(_spriteBatches[_actSpriteBatch].Viewport.GetWidth(),
        _spriteBatches[_actSpriteBatch].Viewport.GetHeight(), false);
    ddb->SetAlpha(128);
    _spriteList.push_back(D3DDrawListEntry(ddb, _actSpriteBatch, 0, 0));
}


D3DGraphicsFactory *D3DGraphicsFactory::_factory = NULL;
Library D3DGraphicsFactory::_library;

D3DGraphicsFactory::~D3DGraphicsFactory()
{
    DestroyDriver(); // driver must be destroyed before d3d library is disposed
    ULONG ref_cnt = _direct3d->Release();
    if (ref_cnt > 0)
        Debug::Printf(kDbgMsg_Warn, "WARNING: Not all of the Direct3D resources have been disposed; ID3D ref count: %d", ref_cnt);
    _factory = NULL;
}

size_t D3DGraphicsFactory::GetFilterCount() const
{
    return 2;
}

const GfxFilterInfo *D3DGraphicsFactory::GetFilterInfo(size_t index) const
{
    switch (index)
    {
    case 0:
        return &D3DGfxFilter::FilterInfo;
    case 1:
        return &AAD3DGfxFilter::FilterInfo;
    default:
        return NULL;
    }
}

String D3DGraphicsFactory::GetDefaultFilterID() const
{
    return D3DGfxFilter::FilterInfo.Id;
}

/* static */ D3DGraphicsFactory *D3DGraphicsFactory::GetFactory()
{
    if (!_factory)
    {
        _factory = new D3DGraphicsFactory();
        if (!_factory->Init())
        {
            delete _factory;
            _factory = NULL;
        }
    }
    return _factory;
}

/* static */ D3DGraphicsDriver *D3DGraphicsFactory::GetD3DDriver()
{
    if (!_factory)
        _factory = GetFactory();
    if (_factory)
        return _factory->EnsureDriverCreated();
    return NULL;
}


D3DGraphicsFactory::D3DGraphicsFactory()
    : _direct3d(NULL)
{
}

D3DGraphicsDriver *D3DGraphicsFactory::EnsureDriverCreated()
{
    if (!_driver)
    {
        _factory->_direct3d->AddRef();
        _driver = new D3DGraphicsDriver(_factory->_direct3d);
    }
    return _driver;
}

D3DGfxFilter *D3DGraphicsFactory::CreateFilter(const String &id)
{
    if (D3DGfxFilter::FilterInfo.Id.CompareNoCase(id) == 0)
        return new D3DGfxFilter();
    else if (AAD3DGfxFilter::FilterInfo.Id.CompareNoCase(id) == 0)
        return new AAD3DGfxFilter();
    return NULL;
}

bool D3DGraphicsFactory::Init()
{
    assert(_direct3d == NULL);
    if (_direct3d)
        return true;

    if (!_library.Load("d3d9"))
    {
        SDL_SetError("Direct3D 9 is not installed");
        return false;
    }

    typedef IDirect3D9 * WINAPI D3D9CreateFn(UINT);
    D3D9CreateFn *lpDirect3DCreate9 = (D3D9CreateFn*)_library.GetFunctionAddress("Direct3DCreate9");
    if (!lpDirect3DCreate9)
    {
        _library.Unload();
        SDL_SetError("Entry point not found in d3d9.dll");
        return false;
    }
    _direct3d = lpDirect3DCreate9(D3D_SDK_VERSION);
    if (!_direct3d)
    {
        _library.Unload();
        SDL_SetError("Direct3DCreate failed!");
        return false;
    }
    return true;
}

} // namespace D3D
} // namespace Engine
} // namespace AGS

#endif // AGS_HAS_DIRECT3D
