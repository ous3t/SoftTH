/*
SoftTH, Software multihead solution for Direct3D
Copyright (C) 2005-2012 Keijo Ruotsalainen, www.kegetys.fi

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <d3d11.h>
#include "dxgiSwapChain.h"
#include "helper.h"
#include "version.h"

#include <dxgi.h>
#include "main.h"
#include <D3DX10Tex.h>

#include "InputHandler.h"

#include <INITGUID.H>
DEFINE_GUID(IID_IDXGISwapChainNew, 0x41ba0075, 0xbc7b, 0x4eee, 0x99, 0x8d, 0xb6, 0xdb, 0xb7, 0xba, 0xeb, 0x46);

volatile extern int SoftTHActive; // >0 if SoftTH is currently active and resolution is overridden
/*
IDXGISwapChainNew::IDXGISwapChainNew(IDXGISwapChain *dxgscNew, IDXGIFactory1 *parentNew, ID3D10Device *dev10new, HWND winNew)
{
  dbg("IDXGISwapChainNew 0x%08X 0x%08X", this, dev10new);
  dxgsc = dxgscNew;
  parent = parentNew;
  dev10 = dev10new;
  win = winNew;
  newbb = NULL;

  updateBB();

  DWORD pid;
  char name[256];
  GetWindowText(win, name, 256);
  GetWindowThreadProcessId(win, &pid);
  dbg("Device window 0x%08X <%s>, thread 0x%08X", win, name, pid);
  ihGlobal.hookRemoteThread(pid);
}*/

IDXGISwapChainNew::IDXGISwapChainNew(IDXGIFactory1 *parentNew, IDXGIFactory1 *dxgifNew, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *scd)
{
  // Creating new swapchain
  dbg("IDXGISwapChainNew 0x%08X 0x%08X", this, pDevice);

  win = scd->OutputWindow;
  newbb = NULL;
  dev10 = NULL;
  dxgsc = NULL;
  dxgif = dxgifNew;
  parent = parentNew;
  realbb = NULL;

  if(!pDevice)
    dbg("ERROR: NULL device!");
  else {
    // Check for D3D10 device
    if(pDevice->QueryInterface(__uuidof(ID3D10Device), (void**) &dev10) == S_OK)
      dbg("Got Direct3D 10 device");
    else if(pDevice->QueryInterface(__uuidof(ID3D10Device1), (void**) &dev10) == S_OK)
      dbg("Got Direct3D 10.1 device");
    else if(pDevice->QueryInterface(__uuidof(ID3D11Device), (void**) &dev10) == S_OK)
      dbg("Got Direct3D 11 device");
    else
      dbg("ERROR: Unknown swapchain device type!");

    if(dev10) {
      // Check for TH mode and create bb texture
      preUpdateBB(&scd->BufferDesc.Width, &scd->BufferDesc.Height);
    }
  }

  // Create the swapchain
  HRESULT ret = dxgif->CreateSwapChain(pDevice, scd, &dxgsc);
  if(ret != S_OK)
    dbg("CreateSwapChain failed!");
  else
    updateBB();
}

IDXGISwapChainNew::~IDXGISwapChainNew()
{
  dbg("~IDXGISwapChainNew 0x%08X", this);
}

HRESULT IDXGISwapChainNew::GetBuffer(UINT Buffer, REFIID riid, void **ppSurface)
{
  // App wants pointer to backbuffer
  dbg("dxgsc: GetBuffer %d %s", Buffer, matchRiid(riid));
  if(newbb) {
    // Return our fake buffer
    *ppSurface = newbb;
    newbb->AddRef();
    return S_OK;
  } else {
    // Return real backbuffer
    return dxgsc->GetBuffer(Buffer, riid, ppSurface);
  }
}

HRESULT IDXGISwapChainNew::GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc)
{
  dbg("dxgsc: GetDesc");
  HRESULT ret = dxgsc->GetDesc(pDesc);
  if(newbb) {
    // Pretend it is triplehead
    pDesc->BufferDesc.Width = config.main.renderResolution.x;
    pDesc->BufferDesc.Height = config.main.renderResolution.y;
  }
  return ret;
};

HRESULT IDXGISwapChainNew::Present(UINT SyncInterval,UINT Flags)
{
  dbg("dxgsc: Present %d %d", SyncInterval, newbb);
  if(!newbb) {
    // Not multihead mode, plain old present
    return dxgsc->Present(SyncInterval, Flags);
  }

  D3D10_TEXTURE2D_DESC dt, ds;
  dbg("realbbdesc... %d", realbb);
  realbb->GetDesc(&dt);
  newbb->GetDesc(&ds);

  dbg("Source: %dx%d ms%d %s", ds.Width, ds.Height, ds.SampleDesc.Count, getFormatDXGI(ds.Format));
  dbg("Target: %dx%d ms%d %s", dt.Width, dt.Height, dt.SampleDesc.Count, getFormatDXGI(dt.Format));

  HEAD *h = config.getPrimaryHead();
  D3D10_BOX sb = {h->sourceRect.left, h->sourceRect.top, 0, h->sourceRect.right, h->sourceRect.bottom, 1};
  dev10->CopySubresourceRegion(realbb, 0, 0, 0, 0, newbb, 0, &sb);

  if(GetKeyState('O') < 0)
    D3DX10SaveTextureToFile(realbb, D3DX10_IFF_JPG, "d:\\pelit\\_realbb.jpg");
  if(GetKeyState('P') < 0)
    D3DX10SaveTextureToFile(newbb, D3DX10_IFF_JPG, "d:\\pelit\\_newbb.jpg");

  // Copy & Present secondary heads
  for(int i=0;i<numDevs;i++)
  {
    OUTDEVICE *o = &outDevs[i];
    D3D10_BOX sb = {o->cfg->sourceRect.left, o->cfg->sourceRect.top, 0, o->cfg->sourceRect.right, o->cfg->sourceRect.bottom, 1};
    dev10->CopySubresourceRegion(o->localSurf, 0, 0, 0, 0, newbb, 0, &sb);
    dev10->Flush();

    o->output->present();
  }

  HRESULT ret = dxgsc->Present(SyncInterval, Flags);
  if(ret != S_OK)
    dbg("IDXGISwapChainNew::Present: Failed");
  return ret;
}

HRESULT IDXGISwapChainNew::ResizeTarget(const DXGI_MODE_DESC *tgtp)
{
  dbg("dxgsc: ResizeTarget %dx%d", tgtp->Width, tgtp->Height);

  DXGI_MODE_DESC m = *tgtp;
  m.Width = 1920;
  m.Height = 1200;

  HRESULT ret = dxgsc->ResizeTarget(&m);
  if(ret != S_OK)
    dbg("dxgsc: ResizeTarget failed %dx%d", tgtp->Width, tgtp->Height);
  return ret;
};

HRESULT IDXGISwapChainNew::ResizeBuffers(UINT BufferCount,UINT Width,UINT Height,DXGI_FORMAT NewFormat,UINT SwapChainFlags)
{
  dbg("dxgsc: ResizeBuffers %dx%d", Width, Height);
  preUpdateBB(&Width, &Height);
  HRESULT ret = dxgsc->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
  if(ret == S_OK) {
    updateBB(); // Backbuffer was recreated
  } else {
    dbg("ResizeBuffers failed!");
  }
  return ret;
};

void IDXGISwapChainNew::preUpdateBB(UINT *width, UINT *height)
{
  int rrx = config.main.renderResolution.x;
  int rry = config.main.renderResolution.y;
  if(*width == rrx && *height == rry) {
    dbg("Multihead swapchain mode detected");
    HEAD *h = config.getPrimaryHead();
    *width = h->screenMode.x;
    *height = h->screenMode.y;

    // Set mouse hook on application focus window
    ihGlobal.setHWND(win);
    SoftTHActive++;
    h->hwnd = win;

    // Create new backbuffer
    // TODO: format
    CD3D10_TEXTURE2D_DESC d(DXGI_FORMAT_R8G8B8A8_UNORM, rrx, rry, 1, 1, D3D10_BIND_RENDER_TARGET, D3D10_USAGE_DEFAULT, NULL);
    newbbDesc = d;
    if(dev10->CreateTexture2D(&newbbDesc, NULL, &newbb) != S_OK)
      dbg("CreateTexture2D failed :("), exit(0);

    // Initialize outputs
    numDevs = config.getNumAdditionalHeads();
    dbg("Initializing %d outputs", numDevs);
    int logoStopTime = GetTickCount() + 4000;

    bool fpuPreserve = true; // TODO: does this exist in d3d10?

    outDevs = new OUTDEVICE[numDevs];
    for(int i=0;i<numDevs;i++)
    {
      OUTDEVICE *o = &outDevs[i];

      // Create the output device
      HEAD *h = config.getHead(i);
      bool local = h->transportMethod==OUTMETHOD_LOCAL;
      dbg("Initializing head %d (DevID: %d, %s)...", i+1, h->devID, local?"local":"non-local");
      o->output = new outDirect3D10(h->devID, h->screenMode.x, h->screenMode.y, h->transportRes.x, h->transportRes.y, win);
      o->cfg = h;

      // Create shared surfaces
      HANDLE sha = o->output->GetShareHandle();
      if(sha) {
        o->localSurf = NULL;

        { // Open surfA share handle
          ID3D10Resource *tr;
          if(dev10->OpenSharedResource(sha, __uuidof(ID3D10Resource), (void**)(&tr)) != S_OK)
            dbg("OpenSharedResource A failed!"), exit(0);
          if(tr->QueryInterface(__uuidof(ID3D10Texture2D), (void**)(&o->localSurf)) != S_OK)
            dbg("Shared surface QueryInterface failed!"), exit(0);
          tr->Release();
        }
        dbg("Opened share handles");
      } else
        dbg("ERROR: Head %d: No share handle!", i+1), exit(0);
    }

  } else {
    dbg("Singlehead swapchain mode");
    SoftTHActive--;

    if(newbb)
      SAFE_RELEASE_LAST(newbb);
    newbb = NULL;
  }
}

void IDXGISwapChainNew::updateBB()
{
  dbg("IDXGISwapChainNew::updateBB!");

  // Get realbb
  if(dxgsc->GetBuffer(0, IID_ID3D10Texture2D, (void**)&realbb) != S_OK)
    dbg("dxgsc->GetBuffer failed!"), exit(0);

  realbb->GetDesc(&realbbDesc);
  dbg("Backbuffer: %dx%d ms%d %s", realbbDesc.Width, realbbDesc.Height, realbbDesc.SampleDesc.Count, getFormatDXGI(realbbDesc.Format));
  realbb->Release();  // Pretend we didn't get pointer to it so it can be released by size changes
}
