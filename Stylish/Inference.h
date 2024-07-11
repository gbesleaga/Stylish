#pragma once

#include "PerformanceMetrics.h"

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include <windows.h>


class Inference {
public:

  enum Provider {
    CPU = 0,
    GPU
  };

  Inference(PerfMetrics* metrics);

  void run(HBITMAP& inputImg, std::vector<float>& styleImgBlob, const std::pair<int, int>& styleImgSize);

  // Providers (limited config atm.) // TODO allow finer control (TensorRT, Cuda, etc.)

  bool isGPUReady() const { return m_SessionGPU != nullptr; }

  Provider getProvider() const { return m_Provider; }
  void setProvider(Provider prv);

  // Enable/disable inference run
  
  void enable() { m_Enabled = true; }
  void disable() { m_Enabled = false; }
  bool isEnabled() const { return m_Enabled; }


  // Quality/Performance
  
  std::pair<int, int> getQualityPerfRange() const { return m_QualityPerfRange; }
  int getQualityPerfFactor() const { return m_QualityPerfFactor; }
  void setQualityPerfFactor(int val);
  

private:
  bool m_Enabled = true;
  
  Provider m_Provider = Provider::GPU; // 0 - CPU, 1 - GPU

  const std::pair<int, int> m_QualityPerfRange = { 0, 3 }; // 0 - max performance, 3 - max quality
  int m_QualityPerfFactor = 2;
  
  std::unique_ptr<Ort::Env> m_Env;

  Ort::SessionOptions m_SessionOptionsCPU;
  Ort::SessionOptions m_SessionOptionsGPU;

  std::unique_ptr<Ort::Session> m_SessionCPU;
  std::unique_ptr<Ort::Session> m_SessionGPU;

  Ort::MemoryInfo m_MemoryInfo{ nullptr };

  std::vector<const char*> m_InputNodeNames;
  std::vector<const char*> m_OutputNodeNames;

  std::vector<std::vector<int64_t>> m_InputNodeDims;    // Input node dimension.

  PerfMetrics* m_Metrics;
};