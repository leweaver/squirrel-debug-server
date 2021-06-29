//
// Created by Lewis weaver on 5/31/2021.
//
#pragma once

#ifndef SDB_BREAKPOINT_MAP_H
#define SDB_BREAKPOINT_MAP_H

#include <memory>
#include <string>
#include <unordered_map>

namespace sdb {
struct Breakpoint {
  uint32_t line = 0;
};

class BreakpointMap {
 public:
  void Clear(const std::string& fileName);

  // Adds all of the given breakpoints. If a breakpoint already exists on the given line, it will be replaced.
  void AddAll(const std::string& fileName, std::vector<Breakpoint>& breakpoints);

  // Attempts to find a breakpoint in the given file, at the given line.
  // If none is found, returns false and `bp` is unaltered.
  // If a breakpoint is fund, it is assigned to `bp`
  bool ReadBreakpoint(const std::string& fileName, uint32_t line, Breakpoint& bp) const;

 private:
  using FileNameHandle = std::shared_ptr<std::string>;

  [[nodiscard]] FileNameHandle FindFileNameHandle(const std::string& fileName) const;
  FileNameHandle EnsureFileNameHandle(const std::string& fileName);

  std::vector<FileNameHandle> fileNames_;
  std::unordered_map<FileNameHandle, std::unordered_map<uint32_t, Breakpoint>> breakpoints_;
};
}// namespace sdb

#endif// SDB_BREAKPOINT_MAP_H