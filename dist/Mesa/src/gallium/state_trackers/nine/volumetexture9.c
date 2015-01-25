/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include "device9.h"
#include "volumetexture9.h"
#include "nine_helpers.h"
#include "nine_pipe.h"

#define DBG_CHANNEL DBG_VOLUMETEXTURE

static HRESULT
NineVolumeTexture9_ctor( struct NineVolumeTexture9 *This,
                         struct NineUnknownParams *pParams,
                         UINT Width, UINT Height, UINT Depth, UINT Levels,
                         DWORD Usage,
                         D3DFORMAT Format,
                         D3DPOOL Pool,
                         HANDLE *pSharedHandle )
{
    struct pipe_resource *info = &This->base.base.info;
    struct pipe_screen *screen = pParams->device->screen;
    enum pipe_format pf;
    unsigned l;
    D3DVOLUME_DESC voldesc;
    HRESULT hr;

    DBG("This=%p pParams=%p Width=%u Height=%u Depth=%u Levels=%u "
        "Usage=%d Format=%d Pool=%d pSharedHandle=%p\n",
        This, pParams, Width, Height, Depth, Levels,
        Usage, Format, Pool, pSharedHandle);

    /* An IDirect3DVolume9 cannot be bound as a render target can it ? */
    user_assert(!(Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)),
                D3DERR_INVALIDCALL);
    user_assert(!(Usage & D3DUSAGE_AUTOGENMIPMAP) ||
                (Pool != D3DPOOL_SYSTEMMEM && Levels <= 1), D3DERR_INVALIDCALL);

    user_assert(!pSharedHandle, D3DERR_INVALIDCALL); /* TODO */

    if (Usage & D3DUSAGE_AUTOGENMIPMAP)
        Levels = 0;

    pf = d3d9_to_pipe_format(Format);
    if (pf == PIPE_FORMAT_NONE ||
        !screen->is_format_supported(screen, pf, PIPE_TEXTURE_3D, 0, PIPE_BIND_SAMPLER_VIEW)) {
        return D3DERR_INVALIDCALL;
    }

    /* We support ATI1 and ATI2 hacks only for 2D textures */
    if (Format == D3DFMT_ATI1 || Format == D3DFMT_ATI2)
        return D3DERR_INVALIDCALL;

    info->screen = pParams->device->screen;
    info->target = PIPE_TEXTURE_3D;
    info->format = pf;
    info->width0 = Width;
    info->height0 = Height;
    info->depth0 = Depth;
    if (Levels)
        info->last_level = Levels - 1;
    else
        info->last_level = util_logbase2(MAX2(MAX2(Width, Height), Depth));
    info->array_size = 1;
    info->nr_samples = 0;
    info->bind = PIPE_BIND_SAMPLER_VIEW;
    info->usage = PIPE_USAGE_DEFAULT;
    info->flags = 0;

    if (Usage & D3DUSAGE_DYNAMIC) {
        info->usage = PIPE_USAGE_DYNAMIC;
        info->bind |=
            PIPE_BIND_TRANSFER_READ |
            PIPE_BIND_TRANSFER_WRITE;
    }

    This->volumes = CALLOC(info->last_level + 1, sizeof(*This->volumes));
    if (!This->volumes)
        return E_OUTOFMEMORY;
    This->base.pstype = 3;

    hr = NineBaseTexture9_ctor(&This->base, pParams, NULL,
                               D3DRTYPE_VOLUMETEXTURE, Format, Pool, Usage);
    if (FAILED(hr))
        return hr;

    voldesc.Format = Format;
    voldesc.Type = D3DRTYPE_VOLUME;
    voldesc.Usage = Usage;
    voldesc.Pool = Pool;
    for (l = 0; l <= info->last_level; ++l) {
        voldesc.Width = u_minify(Width, l);
        voldesc.Height = u_minify(Height, l);
        voldesc.Depth = u_minify(Depth, l);

        hr = NineVolume9_new(This->base.base.base.device, NineUnknown(This),
                             This->base.base.resource, l,
                             &voldesc, &This->volumes[l]);
        if (FAILED(hr))
            return hr;
    }

    return D3D_OK;
}

static void
NineVolumeTexture9_dtor( struct NineVolumeTexture9 *This )
{
    unsigned l;

    DBG("This=%p\n", This);

    if (This->volumes) {
        for (l = 0; l < This->base.base.info.last_level; ++l)
            NineUnknown_Destroy(&This->volumes[l]->base);
        FREE(This->volumes);
    }

    NineBaseTexture9_dtor(&This->base);
}

HRESULT WINAPI
NineVolumeTexture9_GetLevelDesc( struct NineVolumeTexture9 *This,
                                 UINT Level,
                                 D3DVOLUME_DESC *pDesc )
{
    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);

    *pDesc = This->volumes[Level]->desc;

    return D3D_OK;
}

HRESULT WINAPI
NineVolumeTexture9_GetVolumeLevel( struct NineVolumeTexture9 *This,
                                   UINT Level,
                                   IDirect3DVolume9 **ppVolumeLevel )
{
    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);

    NineUnknown_AddRef(NineUnknown(This->volumes[Level]));
    *ppVolumeLevel = (IDirect3DVolume9 *)This->volumes[Level];

    return D3D_OK;
}

HRESULT WINAPI
NineVolumeTexture9_LockBox( struct NineVolumeTexture9 *This,
                            UINT Level,
                            D3DLOCKED_BOX *pLockedVolume,
                            const D3DBOX *pBox,
                            DWORD Flags )
{
    DBG("This=%p Level=%u pLockedVolume=%p pBox=%p Flags=%d\n",
        This, Level, pLockedVolume, pBox, Flags);

    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);

    return NineVolume9_LockBox(This->volumes[Level], pLockedVolume, pBox,
                               Flags);
}

HRESULT WINAPI
NineVolumeTexture9_UnlockBox( struct NineVolumeTexture9 *This,
                              UINT Level )
{
    DBG("This=%p Level=%u\n", This, Level);

    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);

    return NineVolume9_UnlockBox(This->volumes[Level]);
}

HRESULT WINAPI
NineVolumeTexture9_AddDirtyBox( struct NineVolumeTexture9 *This,
                                const D3DBOX *pDirtyBox )
{
    DBG("This=%p pDirtybox=%p\n", This, pDirtyBox);

    if (This->base.base.pool != D3DPOOL_MANAGED) {
        if (This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP)
            This->base.dirty_mip = TRUE;
        return D3D_OK;
    }
    This->base.dirty = TRUE;

    BASETEX_REGISTER_UPDATE(&This->base);

    if (!pDirtyBox) {
        This->dirty_box.x = 0;
        This->dirty_box.y = 0;
        This->dirty_box.z = 0;
        This->dirty_box.width = This->base.base.info.width0;
        This->dirty_box.height = This->base.base.info.height0;
        This->dirty_box.depth = This->base.base.info.depth0;
    } else {
        struct pipe_box box;
        d3dbox_to_pipe_box(&box, pDirtyBox);
        u_box_union_3d(&This->dirty_box, &This->dirty_box, &box);
    }
    return D3D_OK;
}

IDirect3DVolumeTexture9Vtbl NineVolumeTexture9_vtable = {
    (void *)NineUnknown_QueryInterface,
    (void *)NineUnknown_AddRef,
    (void *)NineUnknown_Release,
    (void *)NineUnknown_GetDevice, /* actually part of Resource9 iface */
    (void *)NineResource9_SetPrivateData,
    (void *)NineResource9_GetPrivateData,
    (void *)NineResource9_FreePrivateData,
    (void *)NineResource9_SetPriority,
    (void *)NineResource9_GetPriority,
    (void *)NineBaseTexture9_PreLoad,
    (void *)NineResource9_GetType,
    (void *)NineBaseTexture9_SetLOD,
    (void *)NineBaseTexture9_GetLOD,
    (void *)NineBaseTexture9_GetLevelCount,
    (void *)NineBaseTexture9_SetAutoGenFilterType,
    (void *)NineBaseTexture9_GetAutoGenFilterType,
    (void *)NineBaseTexture9_GenerateMipSubLevels,
    (void *)NineVolumeTexture9_GetLevelDesc,
    (void *)NineVolumeTexture9_GetVolumeLevel,
    (void *)NineVolumeTexture9_LockBox,
    (void *)NineVolumeTexture9_UnlockBox,
    (void *)NineVolumeTexture9_AddDirtyBox
};

static const GUID *NineVolumeTexture9_IIDs[] = {
    &IID_IDirect3DVolumeTexture9,
    &IID_IDirect3DBaseTexture9,
    &IID_IDirect3DResource9,
    &IID_IUnknown,
    NULL
};

HRESULT
NineVolumeTexture9_new( struct NineDevice9 *pDevice,
                        UINT Width, UINT Height, UINT Depth, UINT Levels,
                        DWORD Usage,
                        D3DFORMAT Format,
                        D3DPOOL Pool,
                        struct NineVolumeTexture9 **ppOut,
                        HANDLE *pSharedHandle )
{
    NINE_DEVICE_CHILD_NEW(VolumeTexture9, ppOut, pDevice,
                          Width, Height, Depth, Levels,
                          Usage, Format, Pool, pSharedHandle);
}

