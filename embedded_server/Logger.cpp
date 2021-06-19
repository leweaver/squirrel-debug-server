#include "Logger.h"

#include <iostream>
#include <sstream>
#include <iomanip>

using namespace sdb;

#if defined(WIN32) || defined(_WIN32)
#include <WinSock2.h>

extern struct tm* localtime_r(time_t* _clock, struct tm* _result);
#endif

DebugStrLogger::DebugStrLogger(const Config& config)
    : m_config(config) {}

void DebugStrLogger::log(v_uint32 priority, const std::string& tag, const std::string& message) {

  bool indent = false;
  auto time = std::chrono::system_clock::now().time_since_epoch();

  std::lock_guard<std::mutex> lock(m_lock);

  std::stringstream ss;

  switch (priority) {
#if defined(WIN32) || defined(_WIN32)
    case PRIORITY_V:
      ss << " V |";
      break;

    case PRIORITY_D:
      ss << " D |";
      break;

    case PRIORITY_I:
      ss << " I |";
      break;

    case PRIORITY_W:
      ss << " W |";
      break;

    case PRIORITY_E:
      ss << " E |";
      break;
#else 
    case PRIORITY_V:
      ss << "\033[0;0m V \033[0m|";
      break;

    case PRIORITY_D:
      ss << "\033[34;0m D \033[0m|";
      break;

    case PRIORITY_I:
      ss << "\033[32;0m I \033[0m|";
      break;

    case PRIORITY_W:
      ss << "\033[45;0m W \033[0m|";
      break;

    case PRIORITY_E:
      ss << "\033[41;0m E \033[0m|";
      break;
#endif

    default:
      ss << " " << priority << " |";
  }

  if (m_config.timeFormat) {
    time_t seconds = std::chrono::duration_cast<std::chrono::seconds>(time).count();
    struct tm now;
    localtime_r(&seconds, &now);
#ifdef OATPP_DISABLE_STD_PUT_TIME
    char timeBuffer[50];
    strftime(timeBuffer, sizeof(timeBuffer), m_config.timeFormat, &now);
    ss << timeBuffer;
#else
    ss << std::put_time(&now, m_config.timeFormat);
#endif
    indent = true;
  }

  if (m_config.printTicks) {
    auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
    if (indent) {
      ss << " ";
    }
    ss << ticks;
    indent = true;
  }

  if (indent) {
    ss << "|";
  }

  if (message.empty()) {
    ss << " " << tag << std::endl;
  } else {
    ss << " " << tag << ":" << message << std::endl;
  }

  auto str = ss.str();
#if defined(WIN32) || defined(_WIN32)
  if (IsDebuggerPresent()) {
    OutputDebugStringA(ss.str().c_str());
  } else {
    std::cout << str.c_str();
  }
#else
  std::cout << str.c_str();
#endif
}

void DebugStrLogger::enablePriority(v_uint32 priority) {
  if (priority > PRIORITY_E) {
    return;
  }
  m_config.logMask |= (1 << priority);
}

void DebugStrLogger::disablePriority(v_uint32 priority) {
  if (priority > PRIORITY_E) {
    return;
  }
  m_config.logMask &= ~(1 << priority);
}

bool DebugStrLogger::isLogPriorityEnabled(v_uint32 priority) {
  if (priority > PRIORITY_E) {
    return true;
  }
  return m_config.logMask & (1 << priority);
}