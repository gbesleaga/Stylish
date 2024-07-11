#include "CaptureWindow.h"

#include "Inference.h"
#include "StyleImageCache.h"

#include <wincodec.h>
#include <shellscalingapi.h>

#include <stdexcept>


// No magnification .. capture screen as-is
#define MAGFACTOR  1.0f
#define RESTOREDWINDOWSTYLES WS_SIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN | WS_CAPTION | WS_MAXIMIZEBOX


namespace {
  //
  // Function to check if a window is fully within a monitor or spans multiple monitors
  // Returns handle to monitor if window spans a single monitor, NULL otherwise
  //
  HMONITOR IsWindowFullyWithinSingleMonitor(HWND hwnd) {
    RECT windowRect;
    GetClientRect(hwnd, &windowRect);
    ClientToScreen(hwnd, reinterpret_cast<POINT*>(&windowRect.left));
    ClientToScreen(hwnd, reinterpret_cast<POINT*>(&windowRect.right));

    // TODO: just collect once and then on every WM_DISPLAYCHANGE 
    // Enumerate all monitors and get their rectangles
    std::vector<std::pair<HMONITOR, RECT>> monitors;
    EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor, LPARAM dwData) -> BOOL {
      std::vector<std::pair<HMONITOR, RECT>>* pMonitorRects = reinterpret_cast<std::vector<std::pair<HMONITOR, RECT>>*>(dwData);
      pMonitorRects->push_back({ hMonitor, *lprcMonitor });
      return TRUE;
      }, reinterpret_cast<LPARAM>(&monitors));

    // Check if the window is fully within any monitor
    for (const auto& mon : monitors) {
      const auto& monitorRect = mon.second;
      if (windowRect.left >= monitorRect.left && windowRect.right <= monitorRect.right &&
        windowRect.top >= monitorRect.top && windowRect.bottom <= monitorRect.bottom) {
        return mon.first;
      }
    }

    // The window spans multiple monitors if it's not fully within any single monitor
    return NULL;
  }



  LRESULT CALLBACK HostWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    CaptureWindow* captureWindow = (CaptureWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (message)
    {
    case WM_CREATE: {
      CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
      CaptureWindow* pUserData = (CaptureWindow*)pCreate->lpCreateParams;
      SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pUserData);

      // Get the monitor on which the window is created
      pUserData->setStartupMonitor(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST));

      break;
    }

    case WM_DESTROY:
      PostQuitMessage(0);
      break;

    case WM_MOVE:
    case WM_SIZING:
      captureWindow->onWindowBoundsChanging();
      break;

    case WM_ENTERSIZEMOVE:
      captureWindow->disableInference();
      break;

    case WM_EXITSIZEMOVE:
      captureWindow->enableInference();
      break;

    case WM_SIZE:
      captureWindow->updateFullScreenState(wParam == SIZE_MAXIMIZED);
      captureWindow->fitMagWindow();
      break;

    case WM_PAINT:
      captureWindow->render();
      break;

    default:
      return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
  }


  //
  // Sets the source rectangle and updates the window
  //
  void CALLBACK UpdateMagWindow(HWND hWnd, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) {
    CaptureWindow* captureWindow = (CaptureWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    if (captureWindow->isBeyondMonitor()) {
      return;
    }

    auto hwndHost = captureWindow->getWindowHandle();
    auto hwndMag = captureWindow->getMagnifierWindowHandle();

    // Reclaim topmost status, to prevent unmagnified menus from remaining in view. 
    SetWindowPos(hwndHost, HWND_TOPMOST, 0, 0, 0, 0,
      SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);


    RECT rcCapture;
    GetClientRect(hwndHost, &rcCapture);
    ClientToScreen(hwndHost, reinterpret_cast<POINT*>(&rcCapture.left));
    ClientToScreen(hwndHost, reinterpret_cast<POINT*>(&rcCapture.right));

    // Set the source rectangle for the magnifier control.
    MagSetWindowSource(hwndMag, rcCapture);

    // Force redraw.
    InvalidateRect(hwndMag, NULL, TRUE);
  }

  //
  // Callback from Magnification API when there is new data
  //
  BOOL WINAPI MyMagImageScalingCallback(HWND hwnd, void* srcdata, MAGIMAGEHEADER srcheader, void* destdata, MAGIMAGEHEADER destheader, RECT unclipped, RECT clipped, HRGN dirty) {
    HWND hwndHost = GetParent(GetParent(hwnd));
    CaptureWindow* captureWindow = (CaptureWindow*)GetWindowLongPtr(hwndHost, GWLP_USERDATA);

    if (captureWindow->isBeyondMonitor()) {
      return FALSE;
    }

    captureWindow->onCapture(hwnd, srcdata, srcheader, destdata, destheader, unclipped, clipped, dirty);

    return TRUE;
  }
}



CaptureWindow::CaptureWindow(HMODULE hInstance, int nCmdShow, Inference* inf, StyleImageCache* styleImgCache)
  : m_Inf{ inf }
  , m_StyleImageCache{ styleImgCache } {

  if (FALSE == MagInitialize()) {
    throw std::runtime_error("Failed to initialize Magnification API!");
  }

  // register host window class
  const TCHAR WindowClassName[] = TEXT("MagnifierWindow");
  const TCHAR WindowTitle[] = TEXT("Stylish");

  WNDCLASSEX wcex = {};

  wcex.cbSize = sizeof(WNDCLASSEX);
  wcex.style = CS_HREDRAW | CS_VREDRAW;
  wcex.lpfnWndProc = HostWndProc;
  wcex.hInstance = hInstance;
  wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcex.hbrBackground = (HBRUSH)(1 + COLOR_BTNFACE);
  wcex.lpszClassName = WindowClassName;

  RegisterClassEx(&wcex);

  // create host window
  m_HostRect.top = 100;
  m_HostRect.bottom = 600 + 34;
  m_HostRect.left = 100;
  m_HostRect.right = 600;


  m_HwndHost = CreateWindowEx(WS_EX_TOPMOST | WS_EX_LAYERED,
    WindowClassName, WindowTitle,
    RESTOREDWINDOWSTYLES,
    m_HostRect.left, m_HostRect.top,
    m_HostRect.right - m_HostRect.left, m_HostRect.bottom - m_HostRect.top,
    NULL, NULL, hInstance, this);

  if (!m_HwndHost) {
    throw std::runtime_error("Failed to create capture window!");
  }

  // Make host window opaque.
  SetLayeredWindowAttributes(m_HwndHost, 0, 255, LWA_ALPHA);


  if (FALSE == SetupMagnifier(hInstance)) {
    throw std::runtime_error("Failed to create magnifier!");
  }

  ShowWindow(m_HwndHost, nCmdShow);
  UpdateWindow(m_HwndHost);

  // Create a timer to update the Magnification control
  m_TimerId = SetTimer(m_HwndHost, 0, m_TimerInterval, UpdateMagWindow);

  // Needed for FPS computation
  QueryPerformanceFrequency(&m_Frequency);
  QueryPerformanceCounter(&m_LastTime);
}



BOOL CaptureWindow::SetupMagnifier(HINSTANCE hInst) {
  // Create a magnifier control that fills the client area.
  GetClientRect(m_HwndHost, &m_MagRect);
  
  m_HwndMag = CreateWindow(WC_MAGNIFIER, TEXT("MagnifierWindow"),
    WS_CHILD | MS_SHOWMAGNIFIEDCURSOR | WS_VISIBLE,
    m_MagRect.left, m_MagRect.top, m_MagRect.right, m_MagRect.bottom, m_HwndHost, NULL, hInst, NULL);
  
  if (!m_HwndMag) {
    return FALSE;
  }

  // Set the magnification factor.
  MAGTRANSFORM matrix;
  memset(&matrix, 0, sizeof(matrix));
  matrix.v[0][0] = MAGFACTOR;
  matrix.v[1][1] = MAGFACTOR;
  matrix.v[2][2] = 1.0f;

  BOOL ret = MagSetWindowTransform(m_HwndMag, &matrix);

  // Set the image scaling callback
  MagSetImageScalingCallback(m_HwndMag, MyMagImageScalingCallback);

  ShowWindow(m_HwndMag, SW_HIDE);

  return ret;
}



CaptureWindow::~CaptureWindow() {
  KillTimer(NULL, m_TimerId);
  MagUninitialize();
}



void CaptureWindow::onWindowBoundsChanging() {
  auto mon = IsWindowFullyWithinSingleMonitor(m_HwndHost);

  if (mon != m_Monitor) {
    m_bBeyondMonitor = true;
    InvalidateRect(m_HwndHost, NULL, FALSE);
  }
  else {
    m_bBeyondMonitor = false;
  }
}


// 
// Resize the control to fill the window
// 
void CaptureWindow::fitMagWindow() {
  if (!m_HwndMag) {
    return;
  }

  GetClientRect(m_HwndHost, &m_MagRect);
    
  SetWindowPos(m_HwndMag, NULL,
    m_MagRect.left, m_MagRect.top, 
    m_MagRect.right - m_MagRect.left, m_MagRect.bottom - m_MagRect.top, NULL);


  if (m_bFullScreenDirty) {
      MagSetImageScalingCallback(getMagnifierWindowHandle(), MyMagImageScalingCallback);
      m_bFullScreenDirty = false;
  }
}



void CaptureWindow::updateFullScreenState(bool bFullScreen) {
  if (bFullScreen != m_bFullScreen) {
    m_bFullScreen = bFullScreen;
    m_bFullScreenDirty = true;

    if (m_HwndMag) {
      // The Magnification callback needs to be temporarily removed on 
      // fullscreen changes, to prevent crashes

      MagSetImageScalingCallback(m_HwndMag, NULL);
    }
  }
}



void CaptureWindow::render() {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(m_HwndHost, &ps);

  if (m_bBeyondMonitor) {  // Display error message
    HDC memDC = CreateCompatibleDC(hdc);
    RECT rect;
    GetClientRect(m_HwndHost, &rect);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(memDC, hBitmap);

    // Draw a black background on the bitmap
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memDC, &rect, hBrush);
    DeleteObject(hBrush);

    // Draw white text in the middle of the bitmap

    const wchar_t* g_text = L"Please return the window \nwithin the bounds of the primary monitor.\n\n\nSorry!\n\n ¯\\_(ツ)_/¯";

    int fontSize = 30;
    HFONT hFont = CreateFont(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
      DEFAULT_PITCH | FF_SWISS, L"SegoeUI");
    HFONT hOldFont = (HFONT)SelectObject(memDC, hFont);

    SetTextColor(memDC, RGB(255, 255, 255));
    SetBkMode(memDC, TRANSPARENT);

    auto textRect = rect;
    textRect.top += 50;

    DrawText(memDC, g_text, -1, &textRect, DT_CENTER | DT_WORDBREAK);

    SelectObject(memDC, hOldFont);
    DeleteObject(hFont);

    BitBlt(hdc, 0, 0, rect.right, rect.bottom, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
  }
  else {
    if (m_Capture) {
      if (m_bCanRunInference) {
        if (auto* styleImg = const_cast<StyleImageCache::StyleImage*>(m_StyleImageCache->getActiveImage())) {
          m_Inf->run(m_Capture, styleImg->m_Blob, m_StyleImageCache->getImageSize());
        }

      }

      HDC hdcMemLocal = CreateCompatibleDC(hdc);
      HGDIOBJ hbmOld = SelectObject(hdcMemLocal, m_Capture);
      BITMAP bm;
      GetObject(m_Capture, sizeof(bm), &bm);
      BitBlt(hdc, 0, 0, bm.bmWidth, bm.bmHeight, hdcMemLocal, 0, 0, SRCCOPY);
      SelectObject(hdcMemLocal, hbmOld);
      DeleteDC(hdcMemLocal);


      // Calculate FPS
      m_Frames++;

      LARGE_INTEGER currentTime;
      QueryPerformanceCounter(&currentTime);
      m_DeltaTime += float(currentTime.QuadPart - m_LastTime.QuadPart) / m_Frequency.QuadPart;
      m_LastTime = currentTime;

      if (m_DeltaTime >= 1) {

        m_FPS = 0.5f * m_FPS + 0.5f * (m_Frames / m_DeltaTime);

        m_DeltaTime = 0;
        m_Frames = 0;
      }

    }
  }

  EndPaint(m_HwndHost, &ps);
}



void CaptureWindow::onCapture(
  HWND hwnd,
  void* srcdata, MAGIMAGEHEADER srcheader,
  void* destdata, MAGIMAGEHEADER destheader,
  RECT unclipped, RECT clipped,
  HRGN dirty) {

  auto sz = destheader.height * destheader.stride;
  m_CaptureData.resize(sz);
  memcpy(m_CaptureData.data(), destdata, sz);


  BITMAPINFO bmi = { 0 };
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = destheader.width;
  bmi.bmiHeader.biHeight = -(LONG)destheader.height;  // Negative to indicate top-down bitmap
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  HDC hdc = GetDC(NULL);
  HDC hdcMem = CreateCompatibleDC(hdc);

  if (m_Capture) {
    DeleteObject(m_Capture);
  }

  m_Capture = CreateCompatibleBitmap(hdc, destheader.width, destheader.height);
  SelectObject(hdcMem, m_Capture);

  SetDIBits(hdcMem, m_Capture, 0, destheader.height, m_CaptureData.data(), &bmi, DIB_RGB_COLORS);

  DeleteDC(hdcMem);
  ReleaseDC(NULL, hdc);

  InvalidateRect(m_HwndHost, NULL, FALSE);

}



bool CaptureWindow::isBeyondMonitor() const { 
  return m_bBeyondMonitor || (IsWindowFullyWithinSingleMonitor(m_HwndHost) != m_Monitor); // prevent crashes from Magnification API :(
}



void CaptureWindow::enableInvisibleMode() {
  if (m_bInvisibleMode) {
    return;
  }

  m_bInvisibleMode = true;

  g_styleUI = GetWindowLong(m_HwndHost, GWL_STYLE);
  g_styleExUI = GetWindowLong(m_HwndHost, GWL_EXSTYLE);

  if (!m_bFullScreen) {
    // Invisible Mode not fully supported for fullscreen mode .. buggy
    // We don't remove window frame for now .. TODO

    LONG lStyle = g_styleUI;
    lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowLong(m_HwndHost, GWL_STYLE, lStyle);
  }

  SetWindowLong(m_HwndHost, GWL_EXSTYLE, g_styleExUI | WS_EX_TRANSPARENT | WS_EX_LAYERED);
}



void CaptureWindow::disableInvisibleMode() {
  if (!m_bInvisibleMode) {
    return;
  }

  m_bInvisibleMode = false;

  SetWindowLong(m_HwndHost, GWL_STYLE, g_styleUI);
  SetWindowLong(m_HwndHost, GWL_EXSTYLE, g_styleExUI);
}