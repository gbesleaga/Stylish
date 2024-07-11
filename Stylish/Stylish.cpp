// Stylish.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "Stylish.h"

#include "CaptureWindow.h"
#include "UiControls.h"
#include "Inference.h"
#include "StyleImageCache.h"
#include "PerformanceMetrics.h"

#include <windows.h>
#include <DbgHelp.h>
#include <wincodec.h>
#include <magnification.h>
#include <shellscalingapi.h>


// uncomment to view perfomance metrics
//#define MEASURE_PERF


namespace {
  HINSTANCE hInst;

  // ONNX Neral Style Inference
  std::unique_ptr<Inference> g_Inf;

  // Style Images
  std::unique_ptr<StyleImageCache> g_StyleImageCache;

  // Capture window
  std::unique_ptr<CaptureWindow> g_CaptureWindow;

  // ImGui UI (D3D11 backend)
  std::unique_ptr<UiControls> g_UI;

  // Show/Hide frame
  bool g_isUI = true;

  // keyboard input
  HHOOK keyboardHook = NULL;

  LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
      if (wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* keyInfo = (KBDLLHOOKSTRUCT*)lParam;

        if (g_UI->isBindingInvisibleModeKey()) {
          g_UI->setInvisibleModeKey(keyInfo->vkCode);
          return -1;
        }
        else {
          if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (keyInfo->vkCode == g_UI->getInvisibleModeKey())) {
            g_isUI = !g_isUI;

            if (!g_isUI) {
              g_CaptureWindow->enableInvisibleMode();
              ShowWindow(g_UI->getWindowHandle(), SW_HIDE);
            }
            else {
              g_CaptureWindow->disableInvisibleMode();
              ShowWindow(g_UI->getWindowHandle(), SW_SHOW);
            }
          }
        }
      }
    }

    return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
  }


  void CreateMiniDump(EXCEPTION_POINTERS* pep) {
    // Open the file for writing the dump
    HANDLE hFile = CreateFile(_T("crashdump.dmp"), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
      return;
    }

    // Initialize the MINIDUMP_EXCEPTION_INFORMATION structure
    MINIDUMP_EXCEPTION_INFORMATION mdei;
    mdei.ThreadId = GetCurrentThreadId();
    mdei.ExceptionPointers = pep;
    mdei.ClientPointers = FALSE;

    // Write the dump
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpWithDataSegs, (pep ? &mdei : nullptr), nullptr, nullptr);

    // Close the file
    CloseHandle(hFile);
  }


  LONG WINAPI AppUnhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo) {
    CreateMiniDump(ExceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
  }
}



int main() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  SetUnhandledExceptionFilter(AppUnhandledExceptionFilter);

  std::unique_ptr<PerfMetrics> perfMetrics;
#ifdef MEASURE_PERF
  perfMetrics = std::make_unique<PerfMetrics>();
#endif // MEASURE_PERF

  g_Inf = std::make_unique<Inference>(perfMetrics.get());
  g_StyleImageCache = std::make_unique<StyleImageCache>();

  // Create windows
  hInst = GetModuleHandle(NULL);
  auto nCmdShow = SW_NORMAL;

  g_CaptureWindow = std::make_unique<CaptureWindow>(hInst, nCmdShow, g_Inf.get(), g_StyleImageCache.get());
  g_UI = std::make_unique<UiControls>(hInst, g_CaptureWindow->getWindowHandle(), nCmdShow, g_Inf.get(), g_StyleImageCache.get(), perfMetrics.get());

  // Set up the keyboard hook
  keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);

  // Main message loop.
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);

    g_UI->render(g_CaptureWindow->getFPS());
  }

  // Clean up
  UnhookWindowsHookEx(keyboardHook);

  g_UI.reset();
  g_CaptureWindow.reset();
  g_StyleImageCache.reset();
  g_Inf.reset();

  return 0;
}