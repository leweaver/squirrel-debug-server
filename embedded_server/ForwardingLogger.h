//
// Created by Lewis weaver on 5/30/2021.
//
#pragma once

#ifndef FORWARDING_LOGGER_H
#define FORWARDING_LOGGER_H

#include <oatpp/core/base/Environment.hpp>

namespace sdb {
/**
 * A class that forwards OATPP log messages to the LogInterface.
 */
class ForwardingLogger final : public oatpp::base::Logger {
 public:
  /**
   * Default Logger Config.
   */
  struct Config {

    /**
     * Constructor.
     * @param initialTimeFormat - time format.
     * @param printMicroTicks - show ticks in microseconds.
     * @param initialLogMask - bitmask of PRIORITY_X constants with enabled log levels
     */
    Config(const char* initialTimeFormat, const bool printMicroTicks, const v_uint32 initialLogMask)
        : timeFormat(initialTimeFormat)
        , printTicks(printMicroTicks)
        , logMask(initialLogMask)
    {}

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

  /**
   * Constructor.
   * @param config - Logger config.
   */
  explicit ForwardingLogger(
          const Config& config =
                  Config("%Y-%m-%d %H:%M:%S", true,
                         (1U << PRIORITY_V) | (1U << PRIORITY_D) | (1U << PRIORITY_I) | (1U << PRIORITY_W) |
                                 (1U << PRIORITY_E)));

  /**
   * Log message with priority, tag, message.
   * @param priority - log-priority channel of the message.
   * @param tag - tag of the log message.
   * @param message - message.
   */
  void log(v_uint32 priority, const std::string& tag, const std::string& message) override;

  /**
   * Returns whether or not a priority should be logged/printed
   * @param priority - a PRIORITY_X define
   * @return true if given priority should be logged
   */
  bool isLogPriorityEnabled(v_uint32 priority) override;

 private:
  Config config_;
};
}// namespace sdb

#endif// FORWARDING_LOGGER_H