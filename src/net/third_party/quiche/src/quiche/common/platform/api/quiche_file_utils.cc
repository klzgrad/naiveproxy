#include "quiche/common/platform/api/quiche_file_utils.h"

#include "quiche_platform_impl/quiche_file_utils_impl.h"

namespace quiche {

std::string JoinPath(absl::string_view a, absl::string_view b) {
  return JoinPathImpl(a, b);
}

absl::optional<std::string> ReadFileContents(absl::string_view file) {
  return ReadFileContentsImpl(file);
}

bool EnumerateDirectory(absl::string_view path,
                        std::vector<std::string>& directories,
                        std::vector<std::string>& files) {
  return EnumerateDirectoryImpl(path, directories, files);
}

bool EnumerateDirectoryRecursivelyInner(absl::string_view path,
                                        int recursion_limit,
                                        std::vector<std::string>& files) {
  if (recursion_limit < 0) {
    return false;
  }

  std::vector<std::string> local_files;
  std::vector<std::string> directories;
  if (!EnumerateDirectory(path, directories, local_files)) {
    return false;
  }
  for (const std::string& directory : directories) {
    if (!EnumerateDirectoryRecursivelyInner(JoinPath(path, directory),
                                            recursion_limit - 1, files)) {
      return false;
    }
  }
  for (const std::string& file : local_files) {
    files.push_back(JoinPath(path, file));
  }
  return true;
}

bool EnumerateDirectoryRecursively(absl::string_view path,
                                   std::vector<std::string>& files) {
  constexpr int kRecursionLimit = 20;
  return EnumerateDirectoryRecursivelyInner(path, kRecursionLimit, files);
}

}  // namespace quiche
