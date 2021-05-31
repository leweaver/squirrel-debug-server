//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef SAMPLE_APP_LOGGER_H
#define SAMPLE_APP_LOGGER_H

#include <oatpp/core/base/Environment.hpp>

namespace qdb {
class DebugStrLogger : public oatpp::base::Logger {
 public:
  /**
   * Default Logger Config.
   */
  struct Config {

    /**
     * Constructor.
     * @param tfmt - time format.
     * @param printMicroTicks - show ticks in microseconds.
     */
    Config(const char* tfmt, bool printMicroTicks, v_uint32 initialLogMask)
        : timeFormat(tfmt), printTicks(printMicroTicks), logMask(initialLogMask) {}

    /**
     * Time format of the log message.
     * If nullptr then do not print time.
     */
    const char* timeFormat;

    /**
     * Print micro-ticks in the log message.
     */
    bool printTicks;

    /**
     * Log mask to enable/disable certain priorities
     */
    v_uint32 logMask;
  };

 private:
  Config m_config;
  std::mutex m_lock;

 public:
  /**
   * Constructor.
   * @param config - Logger config.
   */
  explicit DebugStrLogger(const Config& config = Config(
                                  "%Y-%m-%d %H:%M:%S",
                                  true,
                                  (1 << PRIORITY_V) | (1 << PRIORITY_D) | (1 << PRIORITY_I) | (1 << PRIORITY_W) | (1 << PRIORITY_E)));

  /**
   * Log message with priority, tag, message.
   * @param priority - log-priority channel of the message.
   * @param tag - tag of the log message.
   * @param message - message.
   */
  void log(v_uint32 priority, const std::string& tag, const std::string& message) override;

  /**
   * Enables logging of a priorities for this instance
   * @param priority - the priority level to enable
   */
  void enablePriority(v_uint32 priority);

  /**
   * Disables logging of a priority for this instance
   * @param priority - the priority level to disable
   */
  void disablePriority(v_uint32 priority);

  /**
   * Returns wether or not a priority should be logged/printed
   * @param priority
   * @return - true if given priority should be logged
   */
  bool isLogPriorityEnabled(v_uint32 priority) override;
};
}// namespace qdb

#endif// SAMPLE_APP_LOGGER_H