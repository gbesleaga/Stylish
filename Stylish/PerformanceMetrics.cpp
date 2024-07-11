#include "PerformanceMetrics.h"


namespace {
  int SampleCount = 100;
}



PerfMetrics::PerfMetrics() {
  m_InfRun = std::vector<float>(4, 0.0f);
  m_Samples = std::vector<std::vector<float>>(4, std::vector<float>(SampleCount, 0.0f));
}



void PerfMetrics::collectInfRun(const std::vector<float>& metrics) {
  for (int i = 0; i < 4; i++) {
    m_InfRun[i] += metrics[i] - m_Samples[i][m_Index];
    m_Samples[i][m_Index] = metrics[i];
  }

  m_Index = (m_Index + 1) % SampleCount;
}



float PerfMetrics::infRunTotal() const { return m_InfRun[0] / SampleCount; }
float PerfMetrics::infRunPre() const { return m_InfRun[1] / SampleCount; }
float PerfMetrics::infRunModel() const { return m_InfRun[2] / SampleCount; }
float PerfMetrics::infRunPost() const { return m_InfRun[3] / SampleCount; }