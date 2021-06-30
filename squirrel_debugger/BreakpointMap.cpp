#include "BreakpointMap.h"

#include <sdb/LogInterface.h>

#include <algorithm>
#include <string>

#ifdef WIN32
void StrToLower(std::string& str)
{
  // Converts using the current system locale
  _strlwr_s(str.data(), str.size() + 1);
}
#endif

using sdb::Breakpoint;
using sdb::BreakpointMap;

constexpr bool kCaseInsensitivePaths = true;// need this on windows
const char* const kTag = "BreakpointMap";

void BreakpointMap::Clear(const FileNameHandle& handle)
{
  if (handle == nullptr) {
    SDB_LOGE(kTag, "Clear: Null FileNameHandle provided");
    return;
  }

  const auto bpMapPos = breakpoints_.find(handle);
  if (bpMapPos == breakpoints_.end()) {
    return;
  }

  bpMapPos->second.clear();
}

void BreakpointMap::AddAll(const FileNameHandle& handle, std::vector<Breakpoint>& breakpoints)
{
  if (handle == nullptr) {
    SDB_LOGE(kTag, "AddAll: Null FileNameHandle provided");
    return;
  }

  auto& lineToBpMap = breakpoints_[handle];
  for (const auto& bp : breakpoints) {
    lineToBpMap[bp.line] = bp;
  }
}

bool BreakpointMap::ReadBreakpoint(const FileNameHandle& handle, const uint32_t line, Breakpoint& bp) const
{
  if (handle == nullptr) {
    SDB_LOGE(kTag, "ReadBreakpoint: Null FileNameHandle provided");
    return false;
  }

  const auto bpMapPos = breakpoints_.find(handle);
  if (bpMapPos == breakpoints_.end()) {
    return false;
  }
  const auto& bpMap = bpMapPos->second;

  const auto bpPos = bpMap.find(line);
  if (bpPos == bpMap.end()) {
    return false;
  }

  bp = bpPos->second;
  return true;
}

BreakpointMap::FileNameHandle BreakpointMap::FindFileNameHandle(const std::string& fileName) const
{
  FileNameHandle handle;
  decltype(fileNames_)::const_iterator handlePos;

  if constexpr (kCaseInsensitivePaths) {
    std::string fileNameLower = fileName;
    StrToLower(fileNameLower);
    handlePos = std::find_if(fileNames_.begin(), fileNames_.end(), [&fileNameLower](const FileNameHandle& f) {
      return *f == fileNameLower;
    });
  }
  else {
    handlePos = std::find_if(
            fileNames_.begin(), fileNames_.end(), [&fileName](const FileNameHandle& f) { return *f == fileName; });
  }

  if (handlePos == fileNames_.end()) {
    return nullptr;
  }
  return *handlePos;
}

BreakpointMap::FileNameHandle BreakpointMap::EnsureFileNameHandle(const std::string& fileName)
{
  FileNameHandle handle = FindFileNameHandle(fileName);

  if (handle == nullptr) {
    if constexpr (kCaseInsensitivePaths) {
      // technically doing a StrToLower twice in this flow. But it makes the code flow much better, and it's an uncommon code path.
      std::string fileNameLower = fileName;
      StrToLower(fileNameLower);
      handle = std::make_shared<std::string>(fileNameLower);
    }
    else {
      handle = std::make_shared<std::string>(fileName);
    }
    fileNames_.emplace_back(handle);
  }

  return handle;
}
