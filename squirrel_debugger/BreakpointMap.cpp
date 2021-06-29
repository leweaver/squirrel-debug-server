#include "BreakpointMap.h"

using sdb::Breakpoint;
using sdb::BreakpointMap;

void BreakpointMap::Clear(const std::string& fileName)
{
  const auto handle = FindFileNameHandle(fileName);
  if (handle == nullptr) { return; }

  const auto bpMapPos = breakpoints_.find(handle);
  if (bpMapPos == breakpoints_.end()) { return; }

  bpMapPos->second.clear();
}

void BreakpointMap::AddAll(const std::string& fileName, std::vector<Breakpoint>& breakpoints)
{
  const auto handle = EnsureFileNameHandle(fileName);

  auto& lineToBpMap = breakpoints_[handle];
  for (const auto& bp : breakpoints) { lineToBpMap[bp.line] = bp; }
}

bool BreakpointMap::ReadBreakpoint(const std::string& fileName, const uint32_t line, Breakpoint& bp) const
{
  const auto handle = FindFileNameHandle(fileName);
  if (handle == nullptr) { return false; }

  const auto bpMapPos = breakpoints_.find(handle);
  if (bpMapPos == breakpoints_.end()) { return false; }
  const auto& bpMap = bpMapPos->second;

  const auto bpPos = bpMap.find(line);
  if (bpPos == bpMap.end()) { return false; }

  bp = bpPos->second;
  return true;
}

BreakpointMap::FileNameHandle BreakpointMap::FindFileNameHandle(const std::string& fileName) const
{
  const auto handlePos = std::find_if(fileNames_.begin(), fileNames_.end(),
                                      [&fileName](const FileNameHandle& f) { return *f == fileName; });
  if (handlePos == fileNames_.end()) { return nullptr; }
  return *handlePos;
}

BreakpointMap::FileNameHandle BreakpointMap::EnsureFileNameHandle(const std::string& fileName)
{
  const auto handlePos = std::find_if(fileNames_.begin(), fileNames_.end(),
                                      [&fileName](const FileNameHandle& f) { return *f == fileName; });
  FileNameHandle handle;
  if (handlePos == fileNames_.end()) {
    handle = std::make_shared<std::string>(fileName);
    fileNames_.emplace_back(handle);
  } else {
    handle = *handlePos;
  }
  return handle;
}
