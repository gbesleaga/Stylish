#include "Inference.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>

#include <opencv2/core/hal/interface.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn/dnn.hpp>


namespace {
  cv::Mat HBITMAPToMat(HBITMAP hBitmap) {
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);

    int nChannels = bmp.bmBitsPixel / 8;
    int depth = (nChannels == 3) ? CV_8UC3 : CV_8UC4;

    cv::Mat mat(bmp.bmHeight, bmp.bmWidth, depth);

    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = -bmp.bmHeight; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = bmp.bmBitsPixel;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    HDC hdc = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hdc);
    HGDIOBJ oldObj = SelectObject(hMemDC, hBitmap);

    GetDIBits(hMemDC, hBitmap, 0, bmp.bmHeight, mat.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    if (depth == CV_8UC4) {
      cv::Mat matRGB;
      cv::cvtColor(mat, matRGB, cv::COLOR_RGBA2RGB);
      mat = matRGB;
    }

    SelectObject(hMemDC, oldObj);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hdc);

    //cv::imwrite("test.jpg", mat);

    return mat;
  }


  void MatToHBITMAP(const cv::Mat& mat, HBITMAP& hBitmap) {
    if (mat.empty()) {
      std::cout << "Input image is empty" << std::endl;
      return;
    }

    int nChannels = mat.channels();
    int depth = (nChannels == 3) ? CV_8UC3 : CV_8UC4;
    if (depth != CV_8UC3 && depth != CV_8UC4) {
      std::cout << "Unsupported image format. Only 3-channel (RGB) and 4-channel (RGBA) images are supported." << std::endl;
      return;
    }

    cv::Mat matRGBA;
    if (depth == CV_8UC3) {
      cv::cvtColor(mat, matRGBA, cv::COLOR_RGB2RGBA);
    }
    else {
      matRGBA = mat;
    }

    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = matRGBA.size().width;
    bi.biHeight = -matRGBA.size().height; // Negative to indicate a top-down DIB
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    HDC hdc = GetDC(NULL);

    if (hBitmap) {
      DeleteObject(hBitmap);
    }

    hBitmap = CreateCompatibleBitmap(hdc, matRGBA.size().width, matRGBA.size().height);
    HDC hMemDC = CreateCompatibleDC(hdc);
    HGDIOBJ oldObj = SelectObject(hMemDC, hBitmap);

    SetDIBits(hMemDC, hBitmap, 0, matRGBA.size().height, matRGBA.data, (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    SelectObject(hMemDC, oldObj);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hdc);
  }
}



Inference::Inference(PerfMetrics* metrics) : m_Metrics{ metrics } {
  try {
    auto startTime = std::chrono::high_resolution_clock::now();

    auto providers = Ort::GetAvailableProviders();
    std::cout << "--- Available ONNX Providers:" << std::endl;

    bool cudaReady = false;
    bool tensorrtReady = false;

    for (auto provider : providers) {
      std::cout << provider << std::endl;
      std::transform(provider.begin(), provider.end(), provider.begin(), [](unsigned char c) { return std::tolower(c); });

      if (provider.find("cuda") != std::string::npos) {
        cudaReady = true;
      }
      else if (provider.find("tensorrt") != std::string::npos) {
        tensorrtReady = true;
      }
    }

    std::cout << "------------------------------" << std::endl;


    m_Env = std::make_unique<Ort::Env>(OrtLoggingLevel::ORT_LOGGING_LEVEL_ERROR, "Default");

    const wchar_t* modelPath = L"models\\arbitrary-image-stylization.onnx";

    // TODO eager or lazy?

    // CPU

    m_SessionOptionsCPU.SetInterOpNumThreads(4);
    m_SessionOptionsCPU.SetIntraOpNumThreads(4);
    // Optimization will take time and memory during startup
    //m_SessionOptionsCPU.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
    m_SessionOptionsCPU.EnableCpuMemArena();
    m_SessionOptionsCPU.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    m_SessionCPU = std::make_unique<Ort::Session>(*m_Env, modelPath, m_SessionOptionsCPU);

    tensorrtReady = false; // todo figure out options before enabling

    // GPU
    try {
      if (tensorrtReady || cudaReady) {
        m_SessionOptionsGPU.SetInterOpNumThreads(4);
        m_SessionOptionsGPU.SetIntraOpNumThreads(4);
        m_SessionOptionsGPU.EnableCpuMemArena();
        m_SessionOptionsGPU.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (tensorrtReady) {
          OrtTensorRTProviderOptions opt{ 0 }; // crashes if not zeroing out
          opt.device_id = 0;
          m_SessionOptionsGPU.AppendExecutionProvider_TensorRT(opt);
        }
        else if (cudaReady)
        {
          OrtCUDAProviderOptions m_CudaOptions;
          m_CudaOptions.device_id = 0;
          m_CudaOptions.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
          m_CudaOptions.arena_extend_strategy = 0;
          m_CudaOptions.do_copy_in_default_stream = 0;
          m_SessionOptionsGPU.AppendExecutionProvider_CUDA(m_CudaOptions);
        }

        m_SessionGPU = std::make_unique<Ort::Session>(*m_Env, modelPath, m_SessionOptionsGPU);
      }
    } catch (Ort::Exception& oe) {
      std::cout << "ONNX exception caught: " << oe.what() << ". Code: " << oe.GetOrtErrorCode() << ".\n\n";
      std::cout << "Failed to configure GPU Providers: GPU options will be disabled.\n";
    }


    m_MemoryInfo = std::move(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));


    // determine input shape
    std::cout << "--- Input shape: " << std::endl;

    Ort::AllocatorWithDefaultOptions allocator;

    auto numInputNodes = m_SessionCPU->GetInputCount();

    for (int i = 0; i < numInputNodes; i++) {
      auto name = m_SessionCPU->GetInputNameAllocated(i, allocator);
      auto* namePtr = name.get();
      auto sz = strlen(namePtr) + 1;

      char* tempstr = new char[sz];
      strcpy_s(tempstr, sz, namePtr);

      m_InputNodeNames.push_back(tempstr);

      printf("Input %d : name=%s\n", i, m_InputNodeNames.back());

      auto typeInfo = m_SessionCPU->GetInputTypeInfo(i);
      auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
      m_InputNodeDims.push_back(tensorInfo.GetShape());

      printf("Input %d : num_dims=%zu\n", i, m_InputNodeDims.back().size());

      for (int j = 0; j < m_InputNodeDims.back().size(); j++) {
        printf("Input %d : dim %d=%jd\n", i, j, m_InputNodeDims.back()[j]);
      }

      //type = tensorInfo.GetElementType();
      //printf("Input %d : type=%d\n", i, type);
    }

    std::cout << "------------------------------" << std::endl;

    // determine output shape

    std::cout << "--- Output shape: " << std::endl;

    auto numOutputNodes = m_SessionCPU->GetOutputCount();

    for (int i = 0; i < numOutputNodes; i++) {
      auto name = m_SessionCPU->GetOutputNameAllocated(i, allocator);
      auto* namePtr = name.get();
      auto sz = strlen(namePtr) + 1;

      char* tempstr = new char[sz];
      strcpy_s(tempstr, sz, namePtr);

      m_OutputNodeNames.push_back(tempstr);

      printf("Output %d : name=%s\n", i, m_OutputNodeNames.back());
    }

    std::cout << "------------------------------" << std::endl;


    auto endTime = std::chrono::high_resolution_clock::now();
    if (m_Metrics) {
      std::chrono::duration<float, std::milli> elapsed = endTime - startTime;
      m_Metrics->collectInfStart(elapsed.count());
    }
  } catch (Ort::Exception& oe) {
    std::cout << "ONNX exception caught: " << oe.what() << ". Code: " << oe.GetOrtErrorCode() << ".\n";
    throw;
  }
}


void Inference::run(HBITMAP& inputImage, std::vector<float>& styleImgBlob, const std::pair<int, int>& styleImgSize) {
  if (!m_Enabled) {
    return;
  }



  // input0: content image
  // input1: style image
  // (-1, -1, -1, 3)
  
  // prepare input

  auto startTime = std::chrono::high_resolution_clock::now();
  
  auto input = HBITMAPToMat(inputImage);
  
  cv::Mat nnInput;
  double downscalingFactor = 1.0 / pow(2, m_QualityPerfRange.second - m_QualityPerfFactor);

  cv::Size scaledSz = input.size();
  scaledSz.width = static_cast<int>(round(scaledSz.width * downscalingFactor));
  scaledSz.height = static_cast<int>(round(scaledSz.height * downscalingFactor));
 
  // round to multiple of 4
  scaledSz.width &= ~3;
  scaledSz.height &= ~3;

  cv::resize(input, nnInput, scaledSz, cv::INTER_AREA);

  int height = nnInput.size().height;
  int width = nnInput.size().width;
  int channels = 3;

  m_InputNodeDims[0][0] = 1;
  m_InputNodeDims[0][1] = height;
  m_InputNodeDims[0][2] = width;

  m_InputNodeDims[1][0] = 1;
  m_InputNodeDims[1][1] = styleImgSize.second;
  m_InputNodeDims[1][2] = styleImgSize.first;

  nnInput.convertTo(nnInput, CV_32FC3, 1.0 / 255.0);

  std::vector<Ort::Value> inputTensor;

  try {
    inputTensor.emplace_back(Ort::Value::CreateTensor<float>(m_MemoryInfo, (float*)nnInput.data, width * height * channels, m_InputNodeDims[0].data(), m_InputNodeDims[0].size()));
    inputTensor.emplace_back(Ort::Value::CreateTensor<float>(m_MemoryInfo, styleImgBlob.data(), styleImgBlob.size(), m_InputNodeDims[1].data(), m_InputNodeDims[1].size()));
  }
  catch (Ort::Exception oe) {
    std::cout << "ONNX exception caught: " << oe.what() << ". Code: " << oe.GetOrtErrorCode() << ".\n";
    throw;
  }

  // run inference

  auto runModelTime = std::chrono::high_resolution_clock::now();

  std::vector<Ort::Value> outputTensor;

  try {
    Ort::Session* ses = m_SessionCPU.get();
    if (m_Provider == Provider::GPU && m_SessionGPU) {
      ses = m_SessionGPU.get();
    }

    outputTensor = ses->Run(Ort::RunOptions{ nullptr }, m_InputNodeNames.data(), inputTensor.data(), inputTensor.size(), m_OutputNodeNames.data(), 1);
    // TODO ses->RunAsync
  }
  catch (Ort::Exception oe) {
    std::cout << "ONNX exception caught: " << oe.what() << ". Code: " << oe.GetOrtErrorCode() << ".\n";
    throw;
  }
 

  // process output

  auto postModelTime = std::chrono::high_resolution_clock::now();

  float* output_data = outputTensor.front().GetTensorMutableData<float>();
  cv::Mat outputNN(cv::Size(width, height), nnInput.type(), output_data);
  outputNN.convertTo(outputNN, CV_8UC3, 255.0);
  
  cv::Mat output;
  cv::resize(outputNN, output, input.size(), cv::INTER_CUBIC); // TODO INTER_LINEAR as user-configurable option
  
  MatToHBITMAP(output, inputImage);

  auto endTime = std::chrono::high_resolution_clock::now();

  if (m_Metrics) {
    std::chrono::duration<float, std::milli> preMs = runModelTime - startTime;
    std::chrono::duration<float, std::milli> modelMs = postModelTime - runModelTime;
    std::chrono::duration<float, std::milli> postMs = endTime - postModelTime;
    std::chrono::duration<float, std::milli> totalMs = endTime - startTime;
    m_Metrics->collectInfRun({ totalMs.count(), preMs.count(), modelMs.count(), postMs.count() });
  }
}



void Inference::setProvider(Provider prv) {
  m_Provider = prv;
  if (!isGPUReady()) {
    m_Provider = Provider::CPU;
  }
}



void Inference::setQualityPerfFactor(int val) {
  m_QualityPerfFactor = std::clamp(val, m_QualityPerfRange.first, m_QualityPerfRange.second);
}