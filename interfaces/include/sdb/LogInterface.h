//
// Created by Lewis weaver on 6/28/2021.
//
#pragma once

#ifndef SDB_LOG_INTERFACE_H
#define SDB_LOG_INTERFACE_H

namespace sdb::log {
enum class Level { Verbose, Debug, Info, Warning, Error };

// Your implementation should define this function body.
void LogFormatted(const char* tag, size_t line, Level level, const char* message, ...);
void LogString(const char* tag, size_t line, Level level, const char* str);

#define SDB_LOGD(tag, ...) sdb::log::LogFormatted(tag, __LINE__, sdb::log::Level::Debug, __VA_ARGS__)
#define SDB_LOGI(tag, ...) sdb::log::LogFormatted(tag, __LINE__, sdb::log::Level::Info, __VA_ARGS__)
#define SDB_LOGW(tag, ...) sdb::log::LogFormatted(tag, __LINE__, sdb::log::Level::Warning, __VA_ARGS__)
#define SDB_LOGE(tag, ...) sdb::log::LogFormatted(tag, __LINE__, sdb::log::Level::Error, __VA_ARGS__)

}// namespace sdb::log

#endif// SDB_LOG_INTERFACE_H
