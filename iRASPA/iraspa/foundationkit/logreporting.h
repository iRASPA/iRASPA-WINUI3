/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <vector>

class LogReporting
{
public:
  enum class ErrorLevel
  {
    error = 0, warning = 1, info = 2, verbose = 3
  };

  virtual void logMessage(ErrorLevel level, RKString message) = 0;
  virtual void insert(const std::vector<RKString> &messages) = 0;
  virtual ~LogReporting() = 0;
};

struct LogReportingConsumer
{
  virtual void setLogReportingWidget(LogReporting *logReporting) = 0;
  virtual LogReporting* logReporter() const = 0;
  virtual ~LogReportingConsumer() = 0;
};
