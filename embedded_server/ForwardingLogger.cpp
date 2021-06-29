#include "ForwardingLogger.h"

#include <sdb/LogInterface.h>

using namespace sdb;
using log::logString;

ForwardingLogger::ForwardingLogger(const Config& config)
    : m_config(config) {}

void ForwardingLogger::log(v_uint32 priority, const std::string& tag, const std::string& message) {
  logString(tag.c_str(), 0, static_cast<log::Level>(priority), message.c_str());
}

void ForwardingLogger::enablePriority(v_uint32 priority) {
  if (priority > PRIORITY_E) {
    return;
  }
  m_config.logMask |= (1 << priority);
}

void ForwardingLogger::disablePriority(v_uint32 priority) {
  if (priority > PRIORITY_E) {
    return;
  }
  m_config.logMask &= ~(1 << priority);
}

bool ForwardingLogger::isLogPriorityEnabled(v_uint32 priority) {
  if (priority > PRIORITY_E) {
    return true;
  }
  return m_config.logMask & (1 << priority);
}