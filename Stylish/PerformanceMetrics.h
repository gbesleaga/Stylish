#pragma once

#include <vector>


class PerfMetrics {
public:

  PerfMetrics();

  float infStart() const { return m_InfStart; }
  
  // note: values in the beginning will be inacurate while buffer fills up 
  float infRunTotal() const;
  float infRunPre() const;
  float infRunModel() const;
  float infRunPost() const;

  // metrics - total, pre, model, post
  void collectInfStart(float startTime) { m_InfStart = startTime; }
  void collectInfRun(const std::vector<float>& metrics);

private:

  float m_InfStart = 0.0f;

  std::vector<float> m_InfRun;
  std::vector<std::vector<float>> m_Samples;
  int m_Index = 0;
};