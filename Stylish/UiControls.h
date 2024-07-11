#pragma once

#include "PerformanceMetrics.h"

#include <windows.h>

#include <d3d11.h>

#include "imgui/imgui.h"


class Inference;
class StyleImageCache;

class UiControls {
public:
  UiControls(HMODULE hInstance, HWND hwndParent, int nCmdShow, Inference* inf, StyleImageCache* styleImgCache, PerfMetrics* metrics);

  virtual ~UiControls();

  UiControls(const UiControls&) = delete;
  UiControls(UiControls&&) = delete;
  
  UiControls& operator=(const UiControls&) = delete;
  UiControls& operator=(UiControls&&) = delete;

  HWND getWindowHandle() const { return m_HwndUI; }

  void setDpiScaleFactor(float factor) { m_DpiScaleFactor = factor; }
  float getDpiScaleFactor() const { return m_DpiScaleFactor; }

  void setInvisibleModeKey(DWORD key) { m_InvisibleModeKey = key; m_IsBinding = false; }
  DWORD getInvisibleModeKey() const { return m_InvisibleModeKey; }

  bool isBindingInvisibleModeKey() const { return m_IsBinding; }

  void onWindowSizeChanged(UINT width, UINT height);

  void render(float fps);

  void saveState(ImGuiTextBuffer* buf);
  void restoreState(const char* line);

private:
  HWND m_HwndUI = nullptr;

  float m_DpiScaleFactor = 1.0f;

  ID3D11Device* m_D3DDevice = nullptr;
  ID3D11DeviceContext* m_D3DDeviceContext = nullptr;
  IDXGISwapChain* m_SwapChain = nullptr;
  ID3D11RenderTargetView* m_MainRenderTargetView = nullptr;

  Inference* m_Inf;
  StyleImageCache* m_StyleImageCache;
  PerfMetrics* m_Metrics;

  bool m_IsBinding = false;
  DWORD m_InvisibleModeKey = 'U';


  void CreateRenderTarget();
  void CleanupRenderTarget();
  HRESULT CreateDeviceD3D(HWND hWnd);
  void CleanupDeviceD3D();
};