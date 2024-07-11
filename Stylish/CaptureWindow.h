#pragma once

#include <vector>


// Ensure that the following definition is in effect before winuser.h is included.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501    
#endif

#include <windows.h>

#include <magnification.h>

class Inference;
class StyleImageCache;


class CaptureWindow {
public:
  CaptureWindow(HMODULE hInstance, int nCmdShow, Inference* inf, StyleImageCache* styleImgCache);

  virtual ~CaptureWindow();

  CaptureWindow(const CaptureWindow&) = delete;
  CaptureWindow(CaptureWindow&&) = delete;

  CaptureWindow& operator=(const CaptureWindow&) = delete;
  CaptureWindow& operator=(CaptureWindow&&) = delete;

  HWND getWindowHandle() const { return m_HwndHost; }
  HWND getMagnifierWindowHandle() const { return m_HwndMag; }

  // moving or resizing
  void onWindowBoundsChanging();

  void fitMagWindow();

  void render();

  void setStartupMonitor(HMONITOR mon) { m_Monitor = mon; }
  bool isBeyondMonitor() const;

  void enableInference() { m_bCanRunInference = true; }
  void disableInference() { m_bCanRunInference = false; }

  float getFPS() const { return m_FPS; }

  void updateFullScreenState(bool bFullScreen);

  void enableInvisibleMode(); // TODO
  void disableInvisibleMode();

  void onCapture(
    HWND hwnd, 
    void* srcdata, MAGIMAGEHEADER srcheader, 
    void* destdata, MAGIMAGEHEADER destheader, 
    RECT unclipped, RECT clipped, 
    HRGN dirty);

private:

  // Monitor info
  // Capturing via Magnification API only works on the primary monitor

  HMONITOR m_Monitor = nullptr;
  bool m_bBeyondMonitor = false;

  // Don't run inference while window is changing
  // because size changes are computationally expensive

  bool m_bCanRunInference = true; 

  // Window info
  // Host -> main window, Mag -> magnification control child window that spans the entire host

  HWND m_HwndMag;
  HWND m_HwndHost;
  RECT m_MagRect;
  RECT m_HostRect;

  // Fullscreen status

  bool m_bFullScreen = false;
  bool m_bFullScreenDirty = false; 

  // Timer for update function

  UINT_PTR m_TimerId;
  const UINT m_TimerInterval = 10;

  // FPS computation

  LARGE_INTEGER m_Frequency;
  LARGE_INTEGER m_LastTime;
  float m_DeltaTime = 0.0f;
  int m_Frames = 0;
  float m_FPS = 0.0f;

  // Captured image
  
  HBITMAP m_Capture = NULL;
  std::vector<unsigned char> m_CaptureData;

  // Invisible mode
  bool m_bInvisibleMode = false;
  LONG g_styleExUI;
  LONG g_styleUI;

  // External dependencies

  Inference* m_Inf;
  StyleImageCache* m_StyleImageCache;


  BOOL SetupMagnifier(HINSTANCE hInst);
};