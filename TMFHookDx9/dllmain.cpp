// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include <d3d9.h>
#include "imgui-dx9/imgui.h"
#include "imgui-dx9/imgui_impl_dx9.h"
#include "imgui-dx9/imgui_impl_win32.h"

using ResetFn = HRESULT(APIENTRY*)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
using PresentFn = long(__stdcall*)(LPDIRECT3DDEVICE9 pDevice, LPVOID, LPVOID, HWND, LPVOID);

ResetFn oReset = nullptr;
PresentFn oPresent = nullptr;
WNDPROC oWndProc = nullptr;
HWND Window = NULL;

bool Initialized = false;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

template <typename T>
T Read(uintptr_t Addr)
{
    return *reinterpret_cast<T*>(Addr);
}

template <typename T>
void Write(T Value, uintptr_t Addr)
{
    *reinterpret_cast<T*>(Addr) = Value;
}

uintptr_t Virtual(unsigned int Idx, uintptr_t This)
{
    return Read<uintptr_t>(Read<uintptr_t>(This) + Idx * 4);
}

uintptr_t VirtualWrite(unsigned int Idx, uintptr_t This, uintptr_t ToWrite)
{
    uintptr_t Original = Read<uintptr_t>(Read<uintptr_t>(This) + Idx * 4);
    Write<uintptr_t>(ToWrite, Read<uintptr_t>(This) + Idx * 4);
    return Original;
}

uintptr_t GetExeBaseAddr()
{
    return (uintptr_t)GetModuleHandle(NULL);
}

uintptr_t GetTrackmania()
{
    // Modloader: GetExeBaseAddr() + 0x972EB8
    // United: 0xD6A2A4
    // Nations: 0xD68C44
    return Read<uintptr_t>(GetExeBaseAddr() + 0x972EB8);
}

uintptr_t GetVisionViewport()
{
    return Read<uintptr_t>(GetTrackmania() + 0x64);
}

IDirect3DDevice9* GetD3DDevice()
{
    return Read<IDirect3DDevice9*>(GetVisionViewport() + 0x9F8);
}

static LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    auto& ImIo = ImGui::GetIO();

    auto ImWndProcResult = ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
    if (ImWndProcResult)
    {
        return ImWndProcResult;
    }

    if ((uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST && ImIo.WantCaptureKeyboard) || (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST && ImIo.WantCaptureMouse))
    {
        return 1;
    }

    return CallWindowProcA(oWndProc, hWnd, uMsg, wParam, lParam);
}

static long __stdcall hkReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pParams)
{
    if (!Initialized) return oReset(pDevice, pParams);
    ImGui_ImplDX9_InvalidateDeviceObjects();
    const HRESULT result = oReset(pDevice, pParams);
    ImGui_ImplDX9_CreateDeviceObjects();
    return result;
}

static void InitImGui(LPDIRECT3DDEVICE9 pDevice)
{
    ImGui::CreateContext();

    ImGui_ImplWin32_Init(Window);
    ImGui_ImplDX9_Init(pDevice);
}

static long __stdcall hkPresent(LPDIRECT3DDEVICE9 pDevice, LPVOID A, LPVOID B, HWND C, LPVOID D)
{
    if (!Initialized)
    {
        InitImGui(pDevice);
        Initialized = true;
    }

    IDirect3DStateBlock9* pStateBlock = NULL;
    if (pDevice->CreateStateBlock(D3DSBT_ALL, &pStateBlock) == D3D_OK)
    {
        pStateBlock->Capture();
        pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        pDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        pDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::ShowDemoWindow();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

        pStateBlock->Apply();
        pStateBlock->Release();
    }

    return oPresent(pDevice, A, B, C, D);
}

static DWORD WINAPI MainThread(LPVOID lpReserved)
{
    while (!GetTrackmania()) Sleep(1);
    while (!GetVisionViewport()) Sleep(1);
    while (!GetD3DDevice()) Sleep(1);

    auto D3DDevice = GetD3DDevice();
    D3DDEVICE_CREATION_PARAMETERS D3DCreationParams = {};
    while ((D3DDevice->GetCreationParameters(&D3DCreationParams) != D3D_OK) or !D3DCreationParams.hFocusWindow) Sleep(1);

    Window = D3DCreationParams.hFocusWindow;
    oWndProc = (WNDPROC)SetWindowLongPtr(D3DCreationParams.hFocusWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);
    oPresent = reinterpret_cast<PresentFn>(VirtualWrite(17, (uintptr_t)D3DDevice, (uintptr_t)hkPresent));
    oReset = reinterpret_cast<ResetFn>(VirtualWrite(16, (uintptr_t)D3DDevice, (uintptr_t)hkReset));

    return TRUE;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

