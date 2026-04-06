/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/profiling/memory/client.h"

#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <new>

#include <unwindstack/Regs.h>
#include <unwindstack/RegsGetLocal.h>

#include "perfetto/base/build_config.h"
#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/thread_utils.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/unix_socket.h"
#include "perfetto/ext/base/utils.h"
#include "src/profiling/memory/sampler.h"
#include "src/profiling/memory/scoped_spinlock.h"
#include "src/profiling/memory/shared_ring_buffer.h"
#include "src/profiling/memory/wire_protocol.h"

namespace perfetto {
namespace profiling {
namespace {

const char kSingleByte[1] = {'x'};
constexpr auto kResendBackoffUs = 100;

inline bool IsMainThread() {
  return getpid() == base::GetThreadId();
}

int UnsetDumpable(int) {
  prctl(PR_SET_DUMPABLE, 0);
  return 0;
}

bool Contained(const StackRange& base, const char* ptr) {
  return (ptr >= base.begin && ptr < base.end);
}

}  // namespace

uint64_t GetMaxTries(const ClientConfiguration& client_config) {
  if (!client_config.block_client)
    return 1u;
  if (client_config.block_client_timeout_us == 0)
    return kInfiniteTries;
  return std::max<uint64_t>(
      1ul, client_config.block_client_timeout_us / kResendBackoffUs);
}

StackRange GetThreadStackRange() {
  // In glibc pthread_getattr_np can call realloc, even for a non-main-thread.
  // This is fine, because the heapprofd wrapper for glibc prevents re-entering
  // malloc.
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr) != 0)
    return {nullptr, nullptr};
  base::ScopedResource<pthread_attr_t*, pthread_attr_destroy, nullptr> cleanup(
      &attr);

  char* stackaddr;
  size_t stacksize;
  if (pthread_attr_getstack(&attr, reinterpret_cast<void**>(&stackaddr),
                            &stacksize) != 0)
    return {nullptr, nullptr};
  return {stackaddr, stackaddr + stacksize};
}

StackRange GetSigAltStackRange() {
  stack_t altstack;

  if (sigaltstack(nullptr, &altstack) == -1) {
    PERFETTO_PLOG("sigaltstack");
    return {nullptr, nullptr};
  }

  if ((altstack.ss_flags & SS_ONSTACK) == 0) {
    return {nullptr, nullptr};
  }

  return {static_cast<char*>(altstack.ss_sp),
          static_cast<char*>(altstack.ss_sp) + altstack.ss_size};
}

// The implementation of pthread_getattr_np for the main thread on bionic uses
// malloc, so we cannot use it in GetStackEnd, which we use inside of
// RecordMalloc (which is called from malloc). We would re-enter malloc if we
// used it.
//
// This is why we find the stack base for the main-thread when constructing
// the client and remember it.
StackRange GetMainThreadStackRange() {
  base::ScopedFstream maps(fopen("/proc/self/maps", base::kFopenReadFlag));
  if (!maps) {
    return {nullptr, nullptr};
  }
  while (!feof(*maps)) {
    char line[1024];
    char* data = fgets(line, sizeof(line), *maps);
    if (data != nullptr && strstr(data, "[stack]")) {
      char* sep = strstr(data, "-");
      if (sep == nullptr)
        continue;

      char* min = reinterpret_cast<char*>(strtoll(data, nullptr, 16));
      char* max = reinterpret_cast<char*>(strtoll(sep + 1, nullptr, 16));
      return {min, max};
    }
  }
  return {nullptr, nullptr};
}

// static
std::optional<base::UnixSocketRaw> Client::ConnectToHeapprofd(
    const std::string& sock_name) {
  auto sock = base::UnixSocketRaw::CreateMayFail(base::SockFamily::kUnix,
                                                 base::SockType::kStream);
  if (!sock || !sock.Connect(sock_name)) {
    PERFETTO_PLOG("Failed to connect to %s", sock_name.c_str());
    return std::nullopt;
  }
  if (!sock.SetTxTimeout(kClientSockTimeoutMs)) {
    PERFETTO_PLOG("Failed to set send timeout for %s", sock_name.c_str());
    return std::nullopt;
  }
  if (!sock.SetRxTimeout(kClientSockTimeoutMs)) {
    PERFETTO_PLOG("Failed to set receive timeout for %s", sock_name.c_str());
    return std::nullopt;
  }
  return std::move(sock);
}

// static
std::shared_ptr<Client> Client::CreateAndHandshake(
    base::UnixSocketRaw sock,
    UnhookedAllocator<Client> unhooked_allocator) {
  if (!sock) {
    PERFETTO_DFATAL_OR_ELOG("Socket not connected.");
    return nullptr;
  }

  sock.DcheckIsBlocking(true);

  // We might be running in a process that is not dumpable (such as app
  // processes on user builds), in which case the /proc/self/mem will be chown'd
  // to root:root, and will not be accessible even to the process itself (see
  // man 5 proc). In such situations, temporarily mark the process dumpable to
  // be able to open the files, unsetting dumpability immediately afterwards.
  int orig_dumpable = prctl(PR_GET_DUMPABLE);

  enum { kNop, kDoUnset };
  base::ScopedResource<int, UnsetDumpable, kNop, false> unset_dumpable(kNop);
  if (orig_dumpable == 0) {
    unset_dumpable.reset(kDoUnset);
    prctl(PR_SET_DUMPABLE, 1);
  }

  base::ScopedFile maps(base::OpenFile("/proc/self/maps", O_RDONLY));
  if (!maps) {
    PERFETTO_DFATAL_OR_ELOG("Failed to open /proc/self/maps");
    return nullptr;
  }
  base::ScopedFile mem(base::OpenFile("/proc/self/mem", O_RDONLY));
  if (!mem) {
    PERFETTO_DFATAL_OR_ELOG("Failed to open /proc/self/mem");
    return nullptr;
  }

  // Restore original dumpability value if we overrode it.
  unset_dumpable.reset();

  int fds[kHandshakeSize];
  fds[kHandshakeMaps] = *maps;
  fds[kHandshakeMem] = *mem;

  // Send an empty record to transfer fds for /proc/self/maps and
  // /proc/self/mem.
  if (sock.Send(kSingleByte, sizeof(kSingleByte), fds, kHandshakeSize) !=
      sizeof(kSingleByte)) {
    PERFETTO_DFATAL_OR_ELOG("Failed to send file descriptors.");
    return nullptr;
  }

  ClientConfiguration client_config;
  base::ScopedFile shmem_fd;
  size_t recv = 0;
  while (recv < sizeof(client_config)) {
    size_t num_fds = 0;
    base::ScopedFile* fd = nullptr;
    if (!shmem_fd) {
      num_fds = 1;
      fd = &shmem_fd;
    }
    ssize_t rd = sock.Receive(reinterpret_cast<char*>(&client_config) + recv,
                              sizeof(client_config) - recv, fd, num_fds);
    if (rd == -1) {
      PERFETTO_PLOG("Failed to receive ClientConfiguration.");
      return nullptr;
    }
    if (rd == 0) {
      PERFETTO_LOG("Server disconnected while sending ClientConfiguration.");
      return nullptr;
    }
    recv += static_cast<size_t>(rd);
  }

  if (!shmem_fd) {
    PERFETTO_DFATAL_OR_ELOG("Did not receive shmem fd.");
    return nullptr;
  }

  auto shmem = SharedRingBuffer::Attach(std::move(shmem_fd));
  if (!shmem || !shmem->is_valid()) {
    PERFETTO_DFATAL_OR_ELOG("Failed to attach to shmem.");
    return nullptr;
  }

  sock.SetBlocking(false);
  // note: the shared_ptr will retain a copy of the unhooked_allocator
  return std::allocate_shared<Client>(unhooked_allocator, std::move(sock),
                                      client_config, std::move(shmem.value()),
                                      getpid(), GetMainThreadStackRange());
}

Client::Client(base::UnixSocketRaw sock,
               ClientConfiguration client_config,
               SharedRingBuffer shmem,
               pid_t pid_at_creation,
               StackRange main_thread_stack_range)
    : client_config_(client_config),
      max_shmem_tries_(GetMaxTries(client_config_)),
      sock_(std::move(sock)),
      main_thread_stack_range_(main_thread_stack_range),
      shmem_(std::move(shmem)),
      pid_at_creation_(pid_at_creation) {}

Client::~Client() {
  // This is work-around for code like the following:
  // https://android.googlesource.com/platform/libcore/+/4ecb71f94378716f88703b9f7548b5d24839262f/ojluni/src/main/native/UNIXProcess_md.c#427
  // They fork, close all fds by iterating over /proc/self/fd using opendir.
  // Unfortunately closedir calls free, which detects the fork, and then tries
  // to destruct this Client.
  //
  // ScopedResource crashes on failure to close, so we explicitly ignore
  // failures here.
  int fd = sock_.ReleaseFd().release();
  if (fd != -1)
    close(fd);
}

const char* Client::GetStackEnd(const char* stackptr) {
  StackRange thread_stack_range;
  bool is_main_thread = IsMainThread();
  if (is_main_thread) {
    thread_stack_range = main_thread_stack_range_;
  } else {
    thread_stack_range = GetThreadStackRange();
  }
  if (Contained(thread_stack_range, stackptr)) {
    return thread_stack_range.end;
  }
  StackRange sigalt_stack_range = GetSigAltStackRange();
  if (Contained(sigalt_stack_range, stackptr)) {
    return sigalt_stack_range.end;
  }
  // The main thread might have expanded since we read its bounds. We now know
  // it is not the sigaltstack, so it has to be the main stack.
  // TODO(fmayer): We should reparse maps here, because now we will keep
  //               hitting the slow-path that calls the sigaltstack syscall.
  if (is_main_thread && stackptr < thread_stack_range.end) {
    return thread_stack_range.end;
  }
  return nullptr;
}

// Best-effort detection of whether we're continuing work in a forked child of
// the profiled process, in which case we want to stop. Note that due to
// malloc_hooks.cc's atfork handler, the proper fork calls should leak the child
// before reaching this point. Therefore this logic exists primarily to handle
// clone and vfork.
// TODO(rsavitski): rename/delete |disable_fork_teardown| config option if this
// logic sticks, as the option becomes more clone-specific, and quite narrow.
bool Client::IsPostFork() {
  if (PERFETTO_UNLIKELY(getpid() != pid_at_creation_)) {
    // Only print the message once, even if we do not shut down the client.
    if (!detected_fork_) {
      detected_fork_ = true;
      const char* vfork_detected = "";

      // We use the fact that vfork does not update Bionic's TID cache, so
      // we will have a mismatch between the actual TID (from the syscall)
      // and the cached one.
      //
      // What we really want to check is if we are sharing virtual memory space
      // with the original process. This would be
      // syscall(__NR_kcmp, syscall(__NR_getpid), pid_at_creation_,
      //         KCMP_VM, 0, 0),
      //  but that is not compiled into our kernels and disallowed by seccomp.
      if (!client_config_.disable_vfork_detection &&
          syscall(__NR_gettid) != base::GetThreadId()) {
        postfork_return_value_ = true;
        vfork_detected = " (vfork detected)";
      } else {
        postfork_return_value_ = client_config_.disable_fork_teardown;
      }
      const char* action =
          postfork_return_value_ ? "Not shutting down" : "Shutting down";
      const char* force =
          postfork_return_value_ ? " (fork teardown disabled)" : "";
      PERFETTO_LOG(
          "Detected post-fork child situation. Not profiling the child. "
          "%s client%s%s",
          action, force, vfork_detected);
    }
    return true;
  }
  return false;
}

#if PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_RISCV) && \
    !PERFETTO_HAS_BUILTIN_STACK_ADDRESS()
ssize_t Client::GetStackRegister(unwindstack::ArchEnum arch) {
  ssize_t reg_sp, reg_size;
  switch (arch) {
    case unwindstack::ARCH_X86:
      reg_sp = unwindstack::X86_REG_SP;
      reg_size = sizeof(uint32_t);
      break;
    case unwindstack::ARCH_X86_64:
      reg_sp = unwindstack::X86_64_REG_SP;
      reg_size = sizeof(uint64_t);
      break;
    case unwindstack::ARCH_ARM:
      reg_sp = unwindstack::ARM_REG_SP;
      reg_size = sizeof(uint32_t);
      break;
    case unwindstack::ARCH_ARM64:
      reg_sp = unwindstack::ARM64_REG_SP;
      reg_size = sizeof(uint64_t);
      break;
    case unwindstack::ARCH_RISCV64:
      reg_sp = unwindstack::RISCV64_REG_SP;
      reg_size = sizeof(uint64_t);
      break;
    case unwindstack::ARCH_UNKNOWN:
      return -1;
  }
  return reg_sp * reg_size;
}

uintptr_t Client::GetStackAddress(char* reg_data, unwindstack::ArchEnum arch) {
  ssize_t reg = GetStackRegister(arch);
  if (reg < 0)
    return reinterpret_cast<uintptr_t>(nullptr);
  return *reinterpret_cast<uintptr_t*>(&reg_data[reg]);
}
#endif /* PERFETTO_ARCH_CPU_RISCV && !PERFETTO_HAS_BUILTIN_STACK_ADDRESS() */

// The stack grows towards numerically smaller addresses, so the stack layout
// of main calling malloc is as follows.
//
//               +------------+
//               |SendWireMsg |
// stackptr +--> +------------+ 0x1000
//               |RecordMalloc|    +
//               +------------+    |
//               | malloc     |    |
//               +------------+    |
//               |  main      |    v
// stackend  +-> +------------+ 0xffff
bool Client::RecordMalloc(uint32_t heap_id,
                          uint64_t sample_size,
                          uint64_t alloc_size,
                          uint64_t alloc_address) {
  if (PERFETTO_UNLIKELY(IsPostFork())) {
    return postfork_return_value_;
  }

  AllocMetadata metadata;
  // By the difference between calling conventions, the frame pointer might
  // include the current frame or not. So, using __builtin_frame_address()
  // on specific architectures such as riscv can make stack unwinding failed.
  // Thus, using __builtin_stack_address() or reading the stack pointer in
  // register data directly instead of using __builtin_frame_address() on riscv.
#if PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_RISCV)
#if PERFETTO_HAS_BUILTIN_STACK_ADDRESS()
  const char* stackptr = reinterpret_cast<char*>(__builtin_stack_address());
  unwindstack::AsmGetRegs(metadata.register_data);
#else
  char* register_data = metadata.register_data;
  unwindstack::AsmGetRegs(register_data);
  const char* stackptr = reinterpret_cast<char*>(
      GetStackAddress(register_data, unwindstack::Regs::CurrentArch()));
  if (!stackptr) {
    PERFETTO_ELOG("Failed to get stack address.");
    shmem_.SetErrorState(SharedRingBuffer::kInvalidStackBounds);
    return false;
  }
#endif /* PERFETTO_HAS_BUILTIN_STACK_ADDRESS() */
#else
  const char* stackptr = reinterpret_cast<char*>(__builtin_frame_address(0));
  unwindstack::AsmGetRegs(metadata.register_data);
#endif /* PERFETTO_BUILDFLAG(PERFETTO_ARCH_CPU_RISCV) */
  const char* stackend = GetStackEnd(stackptr);
  if (!stackend) {
    PERFETTO_ELOG("Failed to find stackend.");
    shmem_.SetErrorState(SharedRingBuffer::kInvalidStackBounds);
    return false;
  }
  uint64_t stack_size = static_cast<uint64_t>(stackend - stackptr);
  metadata.sample_size = sample_size;
  metadata.alloc_size = alloc_size;
  metadata.alloc_address = alloc_address;
  metadata.stack_pointer = reinterpret_cast<uint64_t>(stackptr);
  metadata.arch = unwindstack::Regs::CurrentArch();
  metadata.sequence_number =
      1 + sequence_number_[heap_id].fetch_add(1, std::memory_order_acq_rel);
  metadata.heap_id = heap_id;

  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC_COARSE, &ts) == 0) {
    metadata.clock_monotonic_coarse_timestamp =
        static_cast<uint64_t>(base::FromPosixTimespec(ts).count());
  } else {
    metadata.clock_monotonic_coarse_timestamp = 0;
  }

  WireMessage msg{};
  msg.record_type = RecordType::Malloc;
  msg.alloc_header = &metadata;
  msg.payload = const_cast<char*>(stackptr);
  msg.payload_size = static_cast<size_t>(stack_size);

  if (SendWireMessageWithRetriesIfBlocking(msg) == -1)
    return false;

  if (!shmem_.GetAndResetReaderPaused())
    return true;
  return SendControlSocketByte();
}

int64_t Client::SendWireMessageWithRetriesIfBlocking(const WireMessage& msg) {
  for (uint64_t i = 0;
       max_shmem_tries_ == kInfiniteTries || i < max_shmem_tries_; ++i) {
    if (shmem_.shutting_down())
      return -1;
    int64_t res = SendWireMessage(&shmem_, msg);
    if (PERFETTO_LIKELY(res >= 0))
      return res;
    // retry if in blocking mode and still connected
    if (client_config_.block_client && base::IsAgain(errno) && IsConnected()) {
      usleep(kResendBackoffUs);
    } else {
      break;
    }
  }
  if (IsConnected())
    shmem_.SetErrorState(SharedRingBuffer::kHitTimeout);
  PERFETTO_PLOG("Failed to write to shared ring buffer. Disconnecting.");
  return -1;
}

bool Client::RecordFree(uint32_t heap_id, const uint64_t alloc_address) {
  if (PERFETTO_UNLIKELY(IsPostFork())) {
    return postfork_return_value_;
  }

  FreeEntry current_entry;
  current_entry.sequence_number =
      1 + sequence_number_[heap_id].fetch_add(1, std::memory_order_acq_rel);
  current_entry.addr = alloc_address;
  current_entry.heap_id = heap_id;
  WireMessage msg = {};
  msg.record_type = RecordType::Free;
  msg.free_header = &current_entry;
  // Do not send control socket byte, as frees are very cheap to handle, so we
  // just delay to the next alloc. Sending the control socket byte is ~10x the
  // rest of the client overhead.
  int64_t bytes_free = SendWireMessageWithRetriesIfBlocking(msg);
  if (bytes_free == -1)
    return false;
  // Seems like we are filling up the shmem with frees. Flush.
  if (static_cast<uint64_t>(bytes_free) < shmem_.size() / 2 &&
      shmem_.GetAndResetReaderPaused()) {
    return SendControlSocketByte();
  }
  return true;
}

bool Client::RecordHeapInfo(uint32_t heap_id,
                            const char* heap_name,
                            uint64_t interval) {
  if (PERFETTO_UNLIKELY(IsPostFork())) {
    return postfork_return_value_;
  }

  HeapName hnr;
  hnr.heap_id = heap_id;
  base::StringCopy(&hnr.heap_name[0], heap_name, sizeof(hnr.heap_name));
  hnr.sample_interval = interval;

  WireMessage msg = {};
  msg.record_type = RecordType::HeapName;
  msg.heap_name_header = &hnr;
  return SendWireMessageWithRetriesIfBlocking(msg);
}

bool Client::IsConnected() {
  sock_.DcheckIsBlocking(false);
  char buf[1];
  ssize_t recv_bytes = sock_.Receive(buf, sizeof(buf), nullptr, 0);
  if (recv_bytes == 0)
    return false;
  // This is not supposed to happen because currently heapprofd does not send
  // data to the client. Here for generality's sake.
  if (recv_bytes > 0)
    return true;
  return base::IsAgain(errno);
}

bool Client::SendControlSocketByte() {
  // If base::IsAgain(errno), the socket buffer is full, so the service will
  // pick up the notification even without adding another byte.
  // In other error cases (usually EPIPE) we want to disconnect, because that
  // is how the service signals the tracing session was torn down.
  if (sock_.Send(kSingleByte, sizeof(kSingleByte)) == -1 &&
      !base::IsAgain(errno)) {
    if (shmem_.shutting_down()) {
      PERFETTO_LOG("Profiling session ended.");
    } else {
      PERFETTO_PLOG("Failed to send control socket byte.");
    }
    return false;
  }
  return true;
}

}  // namespace profiling
}  // namespace perfetto
