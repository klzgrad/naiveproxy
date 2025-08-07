#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_THREAD_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_THREAD_IMPL_H_

#include <optional>
#include <string>
#include <thread>  // NOLINT: only used outside of google3

#include "quiche/common/platform/api/quiche_export.h"

class QUICHE_NO_EXPORT QuicheThreadImpl {
 public:
  QuicheThreadImpl(const std::string&) {}
  virtual ~QuicheThreadImpl() {}

  virtual void Run() = 0;

  void Start() {
    thread_.emplace([this]() { Run(); });
  }
  void Join() { thread_->join(); }

 private:
  std::optional<std::thread> thread_;
};

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_THREAD_IMPL_H_
