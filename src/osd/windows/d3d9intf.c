// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//============================================================
//
//  d3d9intf.c - Direct3D 9 abstraction layer
//
//============================================================

// standard windows headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#undef interface

// MAME headers
#include "emu.h"

// MAMEOS headers
#include "d3dintf.h"
#include "winmain.h"
#include "strconv.h"
#include "d3dcomm.h"


//============================================================
//  TYPE DEFINITIONS
//============================================================

typedef HRESULT (WINAPI *direct3dcreate9ex_ptr)(UINT SDKVersion, IDirect3D9Ex **ppD3D);
typedef HRESULT (WINAPI *direct3dx9_loadeffect_ptr)(LPDIRECT3DDEVICE9 pDevice, LPCTSTR pSrcFile, const D3DXMACRO *pDefines, LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXEFFECTPOOL pPool, LPD3DXEFFECT *ppEffect, LPD3DXBUFFER *ppCompilationErrors);

namespace d3d
{

static direct3dx9_loadeffect_ptr g_load_effect = NULL;
static ID3DXEffect *g_effect = NULL;

//============================================================
//  PROTOTYPES
//============================================================

static void set_interfaces(base *d3dptr);

//============================================================
//  INLINES
//============================================================

INLINE void convert_present_params(const present_parameters *params, D3DPRESENT_PARAMETERS *d3d9params)
{
	memset(d3d9params, 0, sizeof(*d3d9params));
	d3d9params->BackBufferWidth = params->BackBufferWidth;
	d3d9params->BackBufferHeight = params->BackBufferHeight;
	d3d9params->BackBufferFormat = params->BackBufferFormat;
	d3d9params->BackBufferCount = params->BackBufferCount;
	d3d9params->MultiSampleType = params->MultiSampleType;
	d3d9params->MultiSampleQuality = params->MultiSampleQuality;
	d3d9params->SwapEffect = params->SwapEffect;
	d3d9params->hDeviceWindow = params->hDeviceWindow;
	d3d9params->Windowed = params->Windowed;
	d3d9params->EnableAutoDepthStencil = params->EnableAutoDepthStencil;
	d3d9params->AutoDepthStencilFormat = params->AutoDepthStencilFormat;
	d3d9params->Flags = params->Flags;
	d3d9params->FullScreen_RefreshRateInHz = params->FullScreen_RefreshRateInHz;
	d3d9params->PresentationInterval = params->PresentationInterval;
}


INLINE void convert_display_mode_d3d9params(const D3DPRESENT_PARAMETERS *d3d9params, D3DDISPLAYMODEEX *displaymode)
{
	memset(displaymode, 0, sizeof(D3DDISPLAYMODEEX));
	displaymode->Size = sizeof(D3DDISPLAYMODEEX);
	displaymode->Width = d3d9params->BackBufferWidth;
	displaymode->Height = d3d9params->BackBufferHeight;
	displaymode->RefreshRate = d3d9params->FullScreen_RefreshRateInHz;
	displaymode->Format = d3d9params->BackBufferFormat;
	displaymode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
}

//============================================================
//  drawd3d9_init
//============================================================

base *drawd3d9_init(void)
{
	bool post_available = true;

	// dynamically grab the create function from d3d9.dll
	HINSTANCE dllhandle = LoadLibrary(TEXT("d3d9.dll"));
	if (dllhandle == NULL)
	{
		osd_printf_verbose("Direct3D: Unable to access d3d9.dll\n");
		return NULL;
	}

	// import the create function
	direct3dcreate9ex_ptr direct3dcreate9ex = (direct3dcreate9ex_ptr)GetProcAddress(dllhandle, "Direct3DCreate9Ex");
	if (direct3dcreate9ex == NULL)
	{
		osd_printf_verbose("Direct3D: Unable to find Direct3DCreate9Ex\n");
		FreeLibrary(dllhandle);
		dllhandle = NULL;
		return NULL;
	}

	// create our core direct 3d object
	IDirect3D9Ex *d3d9 = NULL;
	(*direct3dcreate9ex)(D3D_SDK_VERSION, &d3d9);
	if (d3d9 == NULL)
	{
		osd_printf_verbose("Direct3D: Error attempting to initialize Direct3D9Ex\n");
		FreeLibrary(dllhandle);
		dllhandle = NULL;
		return NULL;
	}

	// dynamically grab the shader load function from d3dx9.dll
	HINSTANCE fxhandle = NULL;
	for (int idx = 99; idx >= 0; idx--) // a shameful moogle
	{
		wchar_t dllbuf[13];
		wsprintf(dllbuf, TEXT("d3dx9_%d.dll"), idx);
		fxhandle = LoadLibrary(dllbuf);
		if (fxhandle != NULL)
		{
			break;
		}
	}
	if (fxhandle == NULL)
	{
		osd_printf_verbose("Direct3D: Warning - Unable find any D3D9 DLLs; disabling post-effect rendering\n");
		post_available = false;
	}
	else
	{
		g_load_effect = (direct3dx9_loadeffect_ptr)GetProcAddress(fxhandle, "D3DXCreateEffectFromFileW");
		if (g_load_effect == NULL)
		{
			osd_printf_verbose("Direct3D: Unable to find D3DXCreateEffectFromFileW\n");
		}
	}

	// allocate an object to hold our data
	base *d3dptr = global_alloc(base);
	d3dptr->version = 9;
	d3dptr->d3dobj = d3d9;
	d3dptr->dllhandle = dllhandle;
	d3dptr->post_fx_available = post_available;
	d3dptr->libhandle = fxhandle;
	set_interfaces(d3dptr);

	osd_printf_verbose("Direct3D: Using Direct3D 9\n");
	return d3dptr;
}



//============================================================
//  Direct3D interfaces
//============================================================

static HRESULT check_device_format(base *d3dptr, UINT adapter, D3DDEVTYPE devtype, D3DFORMAT adapterformat, DWORD usage, D3DRESOURCETYPE restype, D3DFORMAT format)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	return IDirect3D9_CheckDeviceFormat(d3d9, adapter, devtype, adapterformat, usage, restype, format);
}


static HRESULT check_device_type(base *d3dptr, UINT adapter, D3DDEVTYPE devtype, D3DFORMAT format, D3DFORMAT backformat, BOOL windowed)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	return IDirect3D9_CheckDeviceType(d3d9, adapter, devtype, format, backformat, windowed);
}

static HRESULT create_device(base *d3dptr, UINT adapter, D3DDEVTYPE devtype, HWND focus, DWORD behavior, present_parameters *params, device **dev)
{
	HRESULT result;
	IDirect3D9Ex *d3d9ex = (IDirect3D9Ex *) d3dptr->d3dobj;
	D3DPRESENT_PARAMETERS d3d9params;
	convert_present_params(params, &d3d9params);
	
	if (params->Windowed)
	{
		result = d3d9ex->CreateDeviceEx(adapter, devtype, focus, behavior, &d3d9params, NULL, (IDirect3DDevice9Ex **) dev);
	}
	else
	{
		D3DDISPLAYMODEEX displaymode;
		convert_display_mode_d3d9params(&d3d9params, &displaymode);
		result = d3d9ex->CreateDeviceEx(adapter, devtype, focus, behavior, &d3d9params, &displaymode, (IDirect3DDevice9Ex **) dev);
	}
	
	if (result == D3D_OK)
	{
		HRESULT latency_ok = IDirect3DDevice9Ex_SetMaximumFrameLatency((IDirect3DDevice9Ex *) *dev, 1);
		if (latency_ok != D3D_OK)
		{
			osd_printf_error("Unable to set Direct3D device maximum frame latency\n");
		}
	}
	return result;
}

static HRESULT enum_adapter_modes(base *d3dptr, UINT adapter, D3DFORMAT format, UINT index, D3DDISPLAYMODE *mode)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	return IDirect3D9_EnumAdapterModes(d3d9, adapter, format, index, mode);
}


static UINT get_adapter_count(base *d3dptr)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	return IDirect3D9_GetAdapterCount(d3d9);
}


static HRESULT get_adapter_display_mode(base *d3dptr, UINT adapter, D3DDISPLAYMODE *mode)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	return IDirect3D9_GetAdapterDisplayMode(d3d9, adapter, mode);
}


static HRESULT get_adapter_identifier(base *d3dptr, UINT adapter, DWORD flags, adapter_identifier *identifier)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	D3DADAPTER_IDENTIFIER9 id;
	HRESULT result = IDirect3D9_GetAdapterIdentifier(d3d9, adapter, flags, &id);
	memcpy(identifier->Driver, id.Driver, sizeof(identifier->Driver));
	memcpy(identifier->Description, id.Description, sizeof(identifier->Description));
	identifier->DriverVersion = id.DriverVersion;
	identifier->VendorId = id.VendorId;
	identifier->DeviceId = id.DeviceId;
	identifier->SubSysId = id.SubSysId;
	identifier->Revision = id.Revision;
	identifier->DeviceIdentifier = id.DeviceIdentifier;
	identifier->WHQLLevel = id.WHQLLevel;
	return result;
}


static UINT get_adapter_mode_count(base *d3dptr, UINT adapter, D3DFORMAT format)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	return IDirect3D9_GetAdapterModeCount(d3d9, adapter, format);
}


static HMONITOR get_adapter_monitor(base *d3dptr, UINT adapter)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	return IDirect3D9_GetAdapterMonitor(d3d9, adapter);
}


static HRESULT get_caps_dword(base *d3dptr, UINT adapter, D3DDEVTYPE devtype, caps_index which, DWORD *value)
{
	IDirect3D9 *d3d9 = (IDirect3D9 *)d3dptr->d3dobj;
	D3DCAPS9 caps;
	HRESULT result = IDirect3D9_GetDeviceCaps(d3d9, adapter, devtype, &caps);
	switch (which)
	{
		case CAPS_PRESENTATION_INTERVALS:   *value = caps.PresentationIntervals;    break;
		case CAPS_CAPS2:                    *value = caps.Caps2;                    break;
		case CAPS_DEV_CAPS:                 *value = caps.DevCaps;                  break;
		case CAPS_SRCBLEND_CAPS:            *value = caps.SrcBlendCaps;             break;
		case CAPS_DSTBLEND_CAPS:            *value = caps.DestBlendCaps;            break;
		case CAPS_TEXTURE_CAPS:             *value = caps.TextureCaps;              break;
		case CAPS_TEXTURE_FILTER_CAPS:      *value = caps.TextureFilterCaps;        break;
		case CAPS_TEXTURE_ADDRESS_CAPS:     *value = caps.TextureAddressCaps;       break;
		case CAPS_TEXTURE_OP_CAPS:          *value = caps.TextureOpCaps;            break;
		case CAPS_MAX_TEXTURE_ASPECT:       *value = caps.MaxTextureAspectRatio;    break;
		case CAPS_MAX_TEXTURE_WIDTH:        *value = caps.MaxTextureWidth;          break;
		case CAPS_MAX_TEXTURE_HEIGHT:       *value = caps.MaxTextureHeight;         break;
		case CAPS_STRETCH_RECT_FILTER:      *value = caps.StretchRectFilterCaps;    break;
		case CAPS_MAX_PS30_INSN_SLOTS:      *value = caps.MaxPixelShader30InstructionSlots; break;
	}
	return result;
}


static ULONG release(base *d3dptr)
{
	IDirect3D9Ex *d3d9 = (IDirect3D9Ex *)d3dptr->d3dobj;
	ULONG result = IDirect3D9Ex_Release(d3d9);
	FreeLibrary(d3dptr->dllhandle);
	if (d3dptr->libhandle != NULL) FreeLibrary(d3dptr->libhandle);
	global_free(d3dptr);
	return result;
}


static const interface d3d9_interface =
{
	check_device_format,
	check_device_type,
	create_device,
	enum_adapter_modes,
	get_adapter_count,
	get_adapter_display_mode,
	get_adapter_identifier,
	get_adapter_mode_count,
	get_adapter_monitor,
	get_caps_dword,
	release
};



//============================================================
//  Direct3DDevice interfaces
//============================================================

static HRESULT device_begin_scene(device *dev)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_BeginScene(device);
}

static HRESULT device_clear(device *dev, DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_Clear(device, count, rects, flags, color, z, stencil);
}


static HRESULT device_create_offscreen_plain_surface(device *dev, UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, surface **surface)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_CreateOffscreenPlainSurface(device, width, height, format, pool, (IDirect3DSurface9 **)surface, NULL);
}

static HRESULT device_create_texture(device *dev, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, texture **texture)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_CreateTexture(device, width, height, levels, usage, format, pool, (IDirect3DTexture9 **)texture, NULL);
}


static HRESULT device_create_vertex_buffer(device *dev, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, vertex_buffer **buf)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_CreateVertexBuffer(device, length, usage, fvf, pool, (IDirect3DVertexBuffer9 **)buf, NULL);
}


static HRESULT device_draw_primitive(device *dev, D3DPRIMITIVETYPE type, UINT start, UINT count)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_DrawPrimitive(device, type, start, count);
}


static HRESULT device_end_scene(device *dev)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_EndScene(device);
}


static HRESULT device_get_raster_status(device *dev, D3DRASTER_STATUS *status)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_GetRasterStatus(device, 0, status);
}


static HRESULT device_get_render_target(device *dev, DWORD index, surface **surface)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_GetRenderTarget(device, index, (IDirect3DSurface9 **)surface);
}


static HRESULT device_get_render_target_data(device *dev, surface *rendertarget, surface *destsurface)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_GetRenderTargetData(device, (IDirect3DSurface9 *)rendertarget, (IDirect3DSurface9 *)destsurface);
}


static HRESULT device_present(device *dev, const RECT *source, const RECT *dest, HWND override, RGNDATA *dirty, DWORD flags)
{
	IDirect3DDevice9Ex *device = (IDirect3DDevice9Ex *)dev;
	
	if (flags != 0)
	{
		IDirect3DSwapChain9 *chain;
		HRESULT result = IDirect3DDevice9Ex_GetSwapChain(device, 0, &chain);
		if (result == D3D_OK)
		{
			result = IDirect3DSwapChain9Ex_Present(chain, source, dest, override, dirty, flags);
			IDirect3DSwapChain9Ex_Release(chain);
			return result;
		}
	}
	return IDirect3DDevice9Ex_Present(device, source, dest, override, dirty);
}


static ULONG device_release(device *dev)
{
	IDirect3DDevice9Ex *device = (IDirect3DDevice9Ex *)dev;
	if (g_effect != NULL)
	{
		g_effect->Release();
		g_effect = NULL;
	}
	return IDirect3DDevice9Ex_Release(device);
}


static HRESULT device_reset(device *dev, present_parameters *params)
{
	HRESULT result;
	IDirect3DDevice9Ex *device = (IDirect3DDevice9Ex *)dev;
	D3DPRESENT_PARAMETERS d3d9params;
	convert_present_params(params, &d3d9params);
	
	if (params->Windowed)
	{
		result = IDirect3DDevice9Ex_ResetEx(device, &d3d9params, NULL);
	}
	else
	{
		D3DDISPLAYMODEEX displaymode;
		convert_display_mode_d3d9params(&d3d9params, &displaymode);
		result = IDirect3DDevice9Ex_ResetEx(device, &d3d9params, &displaymode);
	}
	
	return result;
}


static void device_set_gamma_ramp(device *dev, DWORD flags, const D3DGAMMARAMP *ramp)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	IDirect3DDevice9_SetGammaRamp(device, 0, flags, ramp);
}


static HRESULT device_set_render_state(device *dev, D3DRENDERSTATETYPE state, DWORD value)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_SetRenderState(device, state, value);
}


static HRESULT device_set_render_target(device *dev, DWORD index, surface *surf)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	IDirect3DSurface9 *surface = (IDirect3DSurface9 *)surf;
	return IDirect3DDevice9_SetRenderTarget(device, index, surface);
}


static HRESULT device_create_render_target(device *dev, UINT width, UINT height, D3DFORMAT format, surface **surface)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_CreateRenderTarget(device, width, height, format, D3DMULTISAMPLE_NONE, 0, false, (IDirect3DSurface9 **)surface, NULL);
}


static HRESULT device_set_stream_source(device *dev, UINT number, vertex_buffer *vbuf, UINT stride)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	IDirect3DVertexBuffer9 *vertexbuf = (IDirect3DVertexBuffer9 *)vbuf;
	return IDirect3DDevice9_SetStreamSource(device, number, vertexbuf, 0, stride);
}


static HRESULT device_set_texture(device *dev, DWORD stage, texture *tex)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_SetTexture(device, stage, (IDirect3DBaseTexture9*)tex);
}


static void device_shader_set_texture(device *dev, texture_info *tex)
{
	if (tex != NULL && g_effect != NULL)
	{
		UINT32 flags = tex->get_flags();
		
		if (PRIMFLAG_GET_SCREENTEX(flags))
		{
			vec2f& dims = tex->get_rawdims();
			vec2f delta = tex->get_uvstop() - tex->get_uvstart();
			float w = dims.c.x * delta.c.x;
			float h = dims.c.y * delta.c.y;
			int o = PRIMFLAG_GET_TEXORIENT(flags);
			g_effect->SetTexture("Diffuse", (IDirect3DTexture9 *)tex->get_finaltex());
			g_effect->SetInt("GameWidth", w);
			g_effect->SetInt("GameHeight", h);
			g_effect->SetInt("GameOrientation", o);
		}
	}
}


static HRESULT device_set_texture_stage_state(device *dev, DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;

	// some state which was here got pushed into sampler state in D3D9
	switch ((DWORD)state)
	{
		case D3DTSS_ADDRESSU:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_ADDRESSU, value);
		case D3DTSS_ADDRESSV:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_ADDRESSV, value);
		case D3DTSS_BORDERCOLOR:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_BORDERCOLOR, value);
		case D3DTSS_MAGFILTER:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_MAGFILTER, value);
		case D3DTSS_MINFILTER:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_MINFILTER, value);
		case D3DTSS_MIPFILTER:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_MIPFILTER, value);
		case D3DTSS_MIPMAPLODBIAS:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_MIPMAPLODBIAS, value);
		case D3DTSS_MAXMIPLEVEL:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_MAXMIPLEVEL, value);
		case D3DTSS_MAXANISOTROPY:
			return IDirect3DDevice9_SetSamplerState(device, stage, D3DSAMP_MAXANISOTROPY, value);
		default:
			return IDirect3DDevice9_SetTextureStageState(device, stage, state, value);
	}
}


static HRESULT device_set_vertex_format(device *dev, D3DFORMAT format)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	return IDirect3DDevice9_SetFVF(device, format);
}


static HRESULT device_stretch_rect(device *dev, surface *source, const RECT *srcrect, surface *dest, const RECT *dstrect, D3DTEXTUREFILTERTYPE filter)
{
	IDirect3DDevice9 *device = (IDirect3DDevice9 *)dev;
	IDirect3DSurface9 *ssurface = (IDirect3DSurface9 *)source;
	IDirect3DSurface9 *dsurface = (IDirect3DSurface9 *)dest;
	return IDirect3DDevice9_StretchRect(device, ssurface, srcrect, dsurface, dstrect, filter);
}


static HRESULT device_test_cooperative_level(device *dev)
{
	IDirect3DDevice9Ex *device = (IDirect3DDevice9Ex *) dev;
	return IDirect3DDevice9Ex_CheckDeviceState(device, NULL);
}


static HRESULT device_set_shader(device *dev, const char *path)
{
	LPD3DXBUFFER buffer_errors = NULL;
	char name_cstr[1024];
	sprintf(name_cstr, "%s", path);
	TCHAR *effect_name = tstring_from_utf8(name_cstr);
	
	if (g_effect != NULL)
	{
		g_effect->Release();
		g_effect = NULL;
	}
	
	HRESULT load_result = (*g_load_effect)((IDirect3DDevice9Ex *) dev, effect_name, NULL, NULL, 0, NULL, &g_effect, &buffer_errors);
	if (FAILED(load_result))
	{
		if(buffer_errors != NULL)
		{
			LPVOID compile_errors = buffer_errors->GetBufferPointer();
			osd_printf_error("Direct3D: Unable to compile shader: %s\n", (const char*)compile_errors);
		}
		else
		{
			osd_printf_error("Direct3D: Shader %s is missing, corrupt or cannot be compiled.\n", path);
		}
	}
	else
	{
		D3DXHANDLE technique;
		if (g_effect->FindNextValidTechnique(NULL, &technique) == D3D_OK)
		{
			g_effect->SetTechnique(technique);
			osd_printf_verbose("Direct3D: Shader %s loaded.\n", path);
		}
		else
		{
			g_effect->Release();
			g_effect = NULL;
			load_result = E_FAIL;
			osd_printf_error("Direct3D: Shader error: no valid technique found.\n");
		}
	}

	osd_free(effect_name);
	return load_result;
}


static HRESULT device_shader_draw_primitive(device *dev, D3DPRIMITIVETYPE type, UINT start, UINT count)
{
	HRESULT result = E_NOTIMPL;
	if (g_effect != NULL)
	{
		UINT num_passes = 0;
		g_effect->Begin(&num_passes, 0);
		for (UINT pass = 0; pass < num_passes; pass++)
		{
			g_effect->BeginPass(pass);
			result = device_draw_primitive(dev, type, start, count);
			if (result != D3D_OK) osd_printf_verbose("Direct3D: Error %08X during device draw_primitive call\n", (int)result);
			g_effect->EndPass();
		}
		g_effect->End();
	}
	else
	{
	    result = device_draw_primitive(dev, type, start, count);
	}
	return result;
}


static const device_interface d3d9_device_interface =
{
	device_begin_scene,
	device_clear,
	device_create_offscreen_plain_surface,
	device_create_texture,
	device_create_vertex_buffer,
	device_create_render_target,
	device_draw_primitive,
	device_end_scene,
	device_get_raster_status,
	device_get_render_target,
	device_get_render_target_data,
	device_present,
	device_release,
	device_reset,
	device_set_gamma_ramp,
	device_set_render_state,
	device_set_render_target,
	device_set_stream_source,
	device_set_texture,
	device_shader_set_texture,
	device_set_texture_stage_state,
	device_set_vertex_format,
	device_stretch_rect,
	device_test_cooperative_level,
	device_set_shader,
	device_shader_draw_primitive
};



//============================================================
//  Direct3DSurface interfaces
//============================================================

static HRESULT surface_lock_rect(surface *surf, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
	IDirect3DSurface9 *surface = (IDirect3DSurface9 *)surf;
	return IDirect3DSurface9_LockRect(surface, locked, rect, flags);
}


static ULONG surface_release(surface *surf)
{
	IDirect3DSurface9 *surface = (IDirect3DSurface9 *)surf;
	return IDirect3DSurface9_Release(surface);
}


static HRESULT surface_unlock_rect(surface *surf)
{
	IDirect3DSurface9 *surface = (IDirect3DSurface9 *)surf;
	return IDirect3DSurface9_UnlockRect(surface);
}


static const surface_interface d3d9_surface_interface =
{
	surface_lock_rect,
	surface_release,
	surface_unlock_rect
};



//============================================================
//  Direct3DTexture interfaces
//============================================================

static HRESULT texture_get_surface_level(texture *tex, UINT level, surface **surface)
{
	IDirect3DTexture9 *texture = (IDirect3DTexture9 *)tex;
	return IDirect3DTexture9_GetSurfaceLevel(texture, level, (IDirect3DSurface9 **)surface);
}


static HRESULT texture_lock_rect(texture *tex, UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags)
{
	IDirect3DTexture9 *texture = (IDirect3DTexture9 *)tex;
	return IDirect3DTexture9_LockRect(texture, level, locked, rect, flags);
}


static ULONG texture_release(texture *tex)
{
	IDirect3DTexture9 *texture = (IDirect3DTexture9 *)tex;
	return IDirect3DTexture9_Release(texture);
}


static HRESULT texture_unlock_rect(texture *tex, UINT level)
{
	IDirect3DTexture9 *texture = (IDirect3DTexture9 *)tex;
	return IDirect3DTexture9_UnlockRect(texture, level);
}


static const texture_interface d3d9_texture_interface =
{
	texture_get_surface_level,
	texture_lock_rect,
	texture_release,
	texture_unlock_rect
};



//============================================================
//  Direct3DVertexBuffer interfaces
//============================================================

static HRESULT vertex_buffer_lock(vertex_buffer *vbuf, UINT offset, UINT size, VOID **data, DWORD flags)
{
	IDirect3DVertexBuffer9 *vertexbuf = (IDirect3DVertexBuffer9 *)vbuf;
	return IDirect3DVertexBuffer9_Lock(vertexbuf, offset, size, data, flags);
}


static ULONG vertex_buffer_release(vertex_buffer *vbuf)
{
	IDirect3DVertexBuffer9 *vertexbuf = (IDirect3DVertexBuffer9 *)vbuf;
	return IDirect3DVertexBuffer9_Release(vertexbuf);
}


static HRESULT vertex_buffer_unlock(vertex_buffer *vbuf)
{
	IDirect3DVertexBuffer9 *vertexbuf = (IDirect3DVertexBuffer9 *)vbuf;
	return IDirect3DVertexBuffer9_Unlock(vertexbuf);
}


static const vertex_buffer_interface d3d9_vertex_buffer_interface =
{
	vertex_buffer_lock,
	vertex_buffer_release,
	vertex_buffer_unlock
};


//============================================================
//  set_interfaces
//============================================================

static void set_interfaces(base *d3dptr)
{
	d3dptr->d3d = d3d9_interface;
	d3dptr->device = d3d9_device_interface;
	d3dptr->surface = d3d9_surface_interface;
	d3dptr->texture = d3d9_texture_interface;
	d3dptr->vertexbuf = d3d9_vertex_buffer_interface;
}

};
