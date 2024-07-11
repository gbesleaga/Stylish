#include "UiControls.h"

#include "Inference.h"
#include "StyleImageCache.h"

#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_internal.h"

#include <stdexcept>



extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


namespace {
  LRESULT CALLBACK UiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 1;

    UiControls* ui = (UiControls*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    switch (msg) {
      case WM_CREATE: {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        UiControls* pUserData = (UiControls*)pCreate->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pUserData);
      
        break;
      }

      case WM_DPICHANGED: {
        // Get new DPI scale factor
        ui->setDpiScaleFactor(LOWORD(wParam) / 96.0f);

        // Get the recommended new window rectangle and apply it
        RECT* const prcNewWindow = (RECT*)lParam;
        SetWindowPos(hWnd, NULL, prcNewWindow->left, prcNewWindow->top,
          prcNewWindow->right - prcNewWindow->left,
          prcNewWindow->bottom - prcNewWindow->top,
          SWP_NOZORDER | SWP_NOACTIVATE);

        break;
      }

      case WM_SIZE: {
        if (wParam != SIZE_MINIMIZED) {
          ui->onWindowSizeChanged((UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
        }
        return 0;
      }

      case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
      }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
  }



  void* MyUserData_ReadOpen(ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name) {
    // Read: Called when entering into a new ini entry e.g. "[Window][Name]"
    return handler->UserData;
  }



  void MyUserData_ReadLine(ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line) {
    // Read: Called for every line of text within an ini entry
    if (auto* controls = reinterpret_cast<UiControls*>(handler->UserData)) {
      controls->restoreState(line);
    }
  }



  void MyUserData_WriteAll(ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buf) {    
    if (auto* controls = reinterpret_cast<UiControls*>(handler->UserData)) {
      buf->appendf("[%s][Neural Style Transfer]\n", handler->TypeName);
      controls->saveState(buf);
    }
  }

}



UiControls::UiControls(HMODULE hInstance, HWND hwndParent, int nCmdShow, Inference* inf, StyleImageCache* styleImgCache, PerfMetrics* metrics)
  : m_Inf{ inf }
  , m_StyleImageCache{ styleImgCache }
  , m_Metrics{ metrics } {

  const TCHAR WindowClassName[] = TEXT("UI");

  WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, UiWndProc, 0L, 0L, hInstance, NULL, NULL, NULL, NULL, WindowClassName, NULL };
  RegisterClassEx(&wc);

  // Create a temporary window to get the DPI

  HWND hwndTemp = CreateWindowW(
    wc.lpszClassName,
    nullptr,
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT, CW_USEDEFAULT,
    0, 0,
    nullptr, nullptr,
    hInstance, nullptr
  );

  if (hwndTemp) {
    m_DpiScaleFactor = GetDpiForWindow(hwndTemp) / 96.0f;
  }


  RECT rect;
  rect.left = 100;
  rect.top = 100;
  rect.right = static_cast<int>(round(100 + 336 * m_DpiScaleFactor));
  rect.bottom = static_cast<int>(round(100 + 940 * m_DpiScaleFactor));

  if (metrics) {
    rect.right += static_cast<int>(round(336 * m_DpiScaleFactor));
  }
  
  const TCHAR WindowTitle[] = TEXT("Stylish - Controls");

  m_HwndUI = CreateWindow(
    wc.lpszClassName, WindowTitle, WS_OVERLAPPEDWINDOW,
    rect.left, rect.top,
    rect.right - rect.left, rect.bottom - rect.top,
    hwndParent, NULL, hInstance, this);

  ShowWindow(m_HwndUI, nCmdShow);
  UpdateWindow(m_HwndUI);

  // Initialize Direct3D

  if (FAILED(CreateDeviceD3D(m_HwndUI)))
  {
    CleanupDeviceD3D();
    throw std::runtime_error("Failed to create D3D device!");
  }


  // load style images
  // TODO weird that this happens in the "UI" .. move out
  std::string styleFolder = "styles";
  m_StyleImageCache->load(styleFolder, m_D3DDevice, m_D3DDeviceContext);

  // Setup Dear ImGui context

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  // Setup Platform/Renderer bindings

  ImGui_ImplWin32_Init(m_HwndUI);
  ImGui_ImplDX11_Init(m_D3DDevice, m_D3DDeviceContext);

  // Save/Restore state using .ini

  ImGuiSettingsHandler ini_handler;
  ini_handler.TypeName = "UserData";
  ini_handler.TypeHash = ImHashStr("UserData");
  ini_handler.ReadOpenFn = MyUserData_ReadOpen;
  ini_handler.ReadLineFn = MyUserData_ReadLine;
  ini_handler.WriteAllFn = MyUserData_WriteAll;
  ini_handler.UserData = this;
  ImGui::AddSettingsHandler(&ini_handler);
}



UiControls::~UiControls() {
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  m_StyleImageCache->clear();

  CleanupDeviceD3D();
}



void UiControls::onWindowSizeChanged(UINT width, UINT height) {
  if (m_D3DDevice != NULL) {
    CleanupRenderTarget();
    m_SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
  }
}



HRESULT UiControls::CreateDeviceD3D(HWND hWnd) {
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 1;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hWnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };

  HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2,
    D3D11_SDK_VERSION, &sd, &m_SwapChain, &m_D3DDevice, &featureLevel, &m_D3DDeviceContext);

  if (FAILED(hr)) return hr;

  CreateRenderTarget();
  return S_OK;
}



void UiControls::CreateRenderTarget() {
  ID3D11Texture2D* pBackBuffer;
  m_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
  m_D3DDevice->CreateRenderTargetView(pBackBuffer, NULL, &m_MainRenderTargetView);
  pBackBuffer->Release();
}



void UiControls::CleanupDeviceD3D()
{
  CleanupRenderTarget();
  
  if (m_SwapChain) { 
    m_SwapChain->Release(); 
    m_SwapChain = nullptr; 
  }
  
  if (m_D3DDeviceContext) { 
    m_D3DDeviceContext->Release(); 
    m_D3DDeviceContext = nullptr; 
  }

  if (m_D3DDevice) { 
    m_D3DDevice->Release(); 
    m_D3DDevice = nullptr; 
  }
}



void UiControls::CleanupRenderTarget()
{
  if (m_MainRenderTargetView) { 
    m_MainRenderTargetView->Release(); 
    m_MainRenderTargetView = nullptr;
  }
}




void UiControls::render(float fps)
{
  // Start ImGui frame
  
  ImGui::GetIO().FontGlobalScale = m_DpiScaleFactor;

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // Setup UI Controls

  RECT rect;
  GetClientRect(m_HwndUI, &rect);
  auto w = static_cast<float>(rect.right - rect.left);
  auto h = static_cast<float>(rect.bottom - rect.top);

  auto sectionSpacing = ImVec2{ 0, ImGui::GetFontSize() / 2 };

  ImGui::SetNextWindowSize({ w - 20, h - 20 });
  ImGui::SetNextWindowPos({ 10, 10 });

  ImGui::Begin("Neural Style Transfer", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

  float textHeight = ImGui::GetTextLineHeight();
  float frameHeight = ImGui::GetFrameHeight();
  float verticalOffset = (frameHeight - textHeight) * 0.5f;


  // General Section

  ImGui::SeparatorText("General");

  static bool enabled = m_Inf->isEnabled();
  if (ImGui::Checkbox("Enable", &enabled)) {
    if (enabled) {
      m_Inf->enable();
    }
    else {
      m_Inf->disable();
    }
  }

  ImGui::SameLine(0, 8 * ImGui::GetFontSize());

  static int provider = m_Inf->getProvider();

  if (ImGui::RadioButton("CPU", &provider, 0)) {
    m_Inf->setProvider(static_cast<Inference::Provider>(provider));
  }
   
  ImGui::SameLine();

  if (!m_Inf->isGPUReady()) {
    ImGui::BeginDisabled();
  }

  if (ImGui::RadioButton("GPU", &provider, 1)) {
    m_Inf->setProvider(static_cast<Inference::Provider>(provider));
  }

  if (!m_Inf->isGPUReady()) {
    ImGui::EndDisabled();
  }

  ImGui::Spacing();
  ImGui::Spacing();
  ImGui::Spacing();

  ImGui::Text("Toggle Invisible Mode: CTRL+SHIFT+");

  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - verticalOffset);

  std::string str;
  str.push_back(static_cast<char>(m_InvisibleModeKey));
  const char* binding = str.c_str();

  if (m_IsBinding) {
    binding = "##invisibleModeKeyBindingButton";
  }

  if (ImGui::Button(binding)) {
    m_IsBinding = !m_IsBinding;
  }

  ImGui::Spacing();
  ImGui::Spacing();

  ImGui::Text("FPS: %d", static_cast<int>(round(fps)));


  ImGui::Dummy(sectionSpacing);


  // Style Image section

  ImGui::PushItemWidth(8 * ImGui::GetFontSize());
  ImGui::SeparatorText("Style Image");
  ImGui::SameLine();

  auto colorReload = ImGui::GetStyle().Colors[ImGuiCol_Button];
  colorReload.w = 0.7f;

  ImGui::PushStyleColor(ImGuiCol_Button, colorReload);

  float windowWidth = ImGui::GetWindowSize().x;
  const char* buttonLabel = "Reload";
  float buttonWidth = ImGui::CalcTextSize(buttonLabel).x + ImGui::GetStyle().FramePadding.x * 2; // Consider padding
  float newCursorPosX = windowWidth - buttonWidth - ImGui::GetStyle().WindowPadding.x;
  newCursorPosX = std::max(0.0f, newCursorPosX);

  ImGui::SetCursorPosX(newCursorPosX);

  if (ImGui::Button(buttonLabel)) {
    m_StyleImageCache->load(m_StyleImageCache->getPathToStyleFolder(), m_D3DDevice, m_D3DDeviceContext);
  }

  ImGui::PopStyleColor();

  ImGui::Spacing();
  ImGui::Spacing();

  auto sz = m_StyleImageCache->getImageSize();
  ImVec2 imageSize(sz.first * m_DpiScaleFactor, sz.second * m_DpiScaleFactor);

  ImGui::BeginChild("ScrollingRegion", ImVec2(1.15f * imageSize.x, 2.5f * imageSize.y), ImGuiChildFlags_Border);

  std::string activeImg;
  if (auto* img = m_StyleImageCache->getActiveImage()) {
    activeImg = img->path;
  }

  for (const auto& [path, img] : m_StyleImageCache->getImages()) {
    if (path == activeImg) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.84f, 0.0f, 0.43f, 1.0f));
    }

    if (ImGui::ImageButton(img.m_Thumbnail, imageSize)) {
      m_StyleImageCache->setActiveImage(path);
    }

    ImGui::Spacing();

    if (path == activeImg) {
      ImGui::PopStyleColor();
    }
  }

  ImGui::EndChild();


  // Performance metrics section
  if (m_Metrics) {
    ImGui::SameLine(0.0f, 30.0f);

    ImGui::BeginChild("PerformanceMetrics", ImVec2(1.15f * imageSize.x, 2.5f * imageSize.y), ImGuiChildFlags_Border);

    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 117, 24, 255)); // Red color
    ImGui::Text("Performance");
    ImGui::PopStyleColor();
    ImGui::Text("Inference");
    ImGui::Text("++startup %f ms", m_Metrics->infStart());
    ImGui::Text("++run %f ms", m_Metrics->infRunTotal());
    ImGui::Text("++++pre-processing %f ms", m_Metrics->infRunPre());
    ImGui::Text("++++run the model %f ms", m_Metrics->infRunModel());
    ImGui::Text("++++post-processing %f ms", m_Metrics->infRunPost());

    ImGui::EndChild();
  }

  ImGui::Dummy(sectionSpacing);

  // Stylization TODO

  // Calculate vertical alignment offsets for sliders and text to each side
  //float textHeight = ImGui::GetTextLineHeight();
  //float frameHeight = ImGui::GetFrameHeight();
  //float verticalOffset = (frameHeight - textHeight) * 0.5f;

  /*
  ImGui::SeparatorText("Stylization");

  static int stylization = 0;

  ImGui::Text("Content Img");
  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - verticalOffset);
  ImGui::PushItemWidth(8 * ImGui::GetFontSize());
  ImGui::SliderInt("##sliderStylization", &stylization, 0, 7, "");

  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - verticalOffset);
  ImGui::Text("Style Img");
  */


  ImGui::SeparatorText("Quality");

  static int quality = m_Inf->getQualityPerfFactor();
  auto qualityRange = m_Inf->getQualityPerfRange();

  ImGui::Text("Performance");
  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - verticalOffset);
  ImGui::PushItemWidth(8 * ImGui::GetFontSize());
  if (ImGui::SliderInt("##sliderQuality", &quality, qualityRange.first, qualityRange.second, "")) {
    m_Inf->setQualityPerfFactor(quality);
  }

  ImGui::SameLine();
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() - verticalOffset);
  ImGui::Text("Quality");

  // End ImGui frame

  ImGui::End();

  // Render

  ImGui::Render();

  const float clear_color[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
  m_D3DDeviceContext->OMSetRenderTargets(1, &m_MainRenderTargetView, NULL);
  m_D3DDeviceContext->ClearRenderTargetView(m_MainRenderTargetView, clear_color);
  
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  
  m_SwapChain->Present(1, 0);
}



void UiControls::saveState(ImGuiTextBuffer* buf) {
  buf->appendf("Enabled=%d\n", m_Inf->isEnabled());
  buf->appendf("Provider=%d\n", m_Inf->getProvider());
  buf->appendf("InvisibleModeKey=%d\n", m_InvisibleModeKey);

  if (auto* img = m_StyleImageCache->getActiveImage()) {
    std::hash<std::string> hash_fn;
    buf->appendf("StyleImage=%zu\n", hash_fn(img->path));
  }

  buf->appendf("Quality=%d\n", m_Inf->getQualityPerfFactor());
}



void UiControls::restoreState(const char* line) {
  int val;
  size_t h;

  if (sscanf_s(line, "Enabled=%d", &val) == 1) {
    if (val) m_Inf->enable();
    else m_Inf->disable();
  }
  else if (sscanf_s(line, "Provider=%d", &val) == 1) {
    m_Inf->setProvider(static_cast<Inference::Provider>(val));
  }
  else if (sscanf_s(line, "InvisibleModeKey=%d", &val) == 1) {
    m_InvisibleModeKey = val;
  }
  else if (sscanf_s(line, "StyleImage=%zu", &h) == 1) {
    std::hash<std::string> hash_fn;

    for (const auto& [path, _] : m_StyleImageCache->getImages()) {
      if (h == hash_fn(path)) {
        m_StyleImageCache->setActiveImage(path);
        break;
      }
    }
  }
  else if (sscanf_s(line, "Quality=%d", &val) == 1) { m_Inf->setQualityPerfFactor(val); }
}





