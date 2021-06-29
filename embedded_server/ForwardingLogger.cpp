#include "ForwardingLogger.h"

#include <sdb/LogInterface.h>

using namespace sdb;
using log::LogString;

ForwardingLogger::ForwardingLogger(const Config& config)
    : config_(config)
{}

void ForwardingLogger::log(v_uint32 priority, const std::string& tag, const std::string& message) {
  LogString(tag.c_str(), 0, static_cast<log::Level>(priority), message.c_str());
}

bool ForwardingLogger::isLogPriorityEnabled(const v_uint32 priority) {
  if (priority > PRIORITY_E) {
    return true;
  }
  return (config_.logMask & (1U << priority)) != 0;
}