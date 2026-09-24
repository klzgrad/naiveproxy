#include "perfetto/tracing/tracing.h"

#include <stdio.h>
#include <optional>

#include "perfetto/ext/base/thread_task_runner.h"
#include "perfetto/ext/base/waitable_event.h"
#include "perfetto/ext/tracing/ipc/service_ipc_host.h"
#include "perfetto/tracing/backend_type.h"
#include "protos/perfetto/config/trace_config.gen.h"
#include "protos/perfetto/trace/trace.gen.h"
#include "protos/perfetto/trace/trace_packet.gen.h"
#include "protos/perfetto/trace/trigger.gen.h"
#include "src/base/test/test_task_runner.h"
#include "src/base/test/tmp_dir_tree.h"
#include "test/gtest_and_gmock.h"

namespace perfetto {
namespace internal {
namespace {

using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Property;

class TracingMuxerImplIntegrationTest : public testing::Test {
 protected:
  void SetUp() override {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
    GTEST_SKIP() << "Unix sockets not supported on windows";
#endif
  }

  // Sets the environment variable `name` to `value`. Restores it to the
  // previous value when the test finishes.
  void SetEnvVar(const char* name, const char* value) {
    prev_state_.emplace();
    EnvVar& var = prev_state_.top();
    var.name = name;
    const char* prev_value = getenv(name);
    if (prev_value) {
      var.value.emplace(prev_value);
    }
    base::SetEnv(name, value);
  }

  ~TracingMuxerImplIntegrationTest() override {
    perfetto::Tracing::ResetForTesting();
    while (!prev_state_.empty()) {
      const EnvVar& var = prev_state_.top();
      if (var.value) {
        base::SetEnv(var.name, *var.value);
      } else {
        base::UnsetEnv(var.name);
      }
      prev_state_.pop();
    }
  }

  struct EnvVar {
    const char* name;
    std::optional<std::string> value;
  };
  // Stores previous values of environment variables overridden by tests. We
  // need to to this because some android integration tests need to talk to the
  // real system tracing service and need the PERFETTO_PRODUCER_SOCK_NAME and
  // PERFETTO_CONSUMER_SOCK_NAME to be set to their original value.
  std::stack<EnvVar> prev_state_;
};

class TracingServiceThread {
 public:
  TracingServiceThread(const std::string& producer_socket,
                       const std::string& consumer_socket)
      : runner_(base::ThreadTaskRunner::CreateAndStart("perfetto.svc")),
        producer_socket_(producer_socket),
        consumer_socket_(consumer_socket) {
    runner_.PostTaskAndWaitForTesting([this]() {
      svc_ = ServiceIPCHost::CreateInstance(&runner_);
      bool res =
          svc_->Start(producer_socket_.c_str(), consumer_socket_.c_str());
      if (!res) {
        PERFETTO_FATAL("Failed to start service listening on %s and %s",
                       producer_socket_.c_str(), consumer_socket_.c_str());
      }
    });
  }

  ~TracingServiceThread() {
    runner_.PostTaskAndWaitForTesting([this]() { svc_.reset(); });
  }

  const std::string& producer_socket() const { return producer_socket_; }
  const std::string& consumer_socket() const { return consumer_socket_; }

 private:
  base::ThreadTaskRunner runner_;

  std::string producer_socket_;
  std::string consumer_socket_;
  std::unique_ptr<ServiceIPCHost> svc_;
};

TEST_F(TracingMuxerImplIntegrationTest, ActivateTriggers) {
  base::TmpDirTree tmpdir_;

  base::TestTaskRunner task_runner;

  ASSERT_FALSE(perfetto::Tracing::IsInitialized());

  tmpdir_.TrackFile("producer2.sock");
  tmpdir_.TrackFile("consumer.sock");
  TracingServiceThread tracing_service(tmpdir_.AbsolutePath("producer2.sock"),
                                       tmpdir_.AbsolutePath("consumer.sock"));
  // Instead of being a unix socket, producer.sock is a regular empty file.
  tmpdir_.AddFile("producer.sock", "");

  // Wrong producer socket: the producer won't connect yet, but the consumer
  // will.
  SetEnvVar("PERFETTO_PRODUCER_SOCK_NAME",
            tmpdir_.AbsolutePath("producer.sock").c_str());
  SetEnvVar("PERFETTO_CONSUMER_SOCK_NAME",
            tmpdir_.AbsolutePath("consumer.sock").c_str());

  TracingInitArgs args;
  args.backends = perfetto::kSystemBackend;
  perfetto::Tracing::Initialize(args);

  // TracingMuxerImpl::ActivateTriggers will be called without the producer side
  // of the service being connected. It should store the trigger for 10000ms.
  perfetto::Tracing::ActivateTriggers({"trigger2", "trigger1"}, 10000);

  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  perfetto::TraceConfig::TriggerConfig* tr_cfg = cfg.mutable_trigger_config();
  tr_cfg->set_trigger_mode(perfetto::TraceConfig::TriggerConfig::STOP_TRACING);
  tr_cfg->set_trigger_timeout_ms(10000);
  perfetto::TraceConfig::TriggerConfig::Trigger* trigger =
      tr_cfg->add_triggers();
  trigger->set_name("trigger1");

  std::unique_ptr<TracingSession> session =
      perfetto::Tracing::NewTrace(perfetto::kSystemBackend);
  base::WaitableEvent on_stop;
  session->SetOnStopCallback([&on_stop] { on_stop.Notify(); });
  session->Setup(cfg);

  session->StartBlocking();

  // Swap producer.sock and producer2.sock. Now the client should connect to the
  // tracing service as a producer.
  ASSERT_EQ(rename(tmpdir_.AbsolutePath("producer2.sock").c_str(),
                   tmpdir_.AbsolutePath("producer3.sock").c_str()),
            0);
  ASSERT_EQ(rename(tmpdir_.AbsolutePath("producer.sock").c_str(),
                   tmpdir_.AbsolutePath("producer2.sock").c_str()),
            0);
  ASSERT_EQ(rename(tmpdir_.AbsolutePath("producer3.sock").c_str(),
                   tmpdir_.AbsolutePath("producer.sock").c_str()),
            0);

  on_stop.Wait();

  std::vector<char> bytes = session->ReadTraceBlocking();
  perfetto::protos::gen::Trace parsed_trace;
  ASSERT_TRUE(parsed_trace.ParseFromArray(bytes.data(), bytes.size()));
  EXPECT_THAT(
      parsed_trace,
      Property(&perfetto::protos::gen::Trace::packet,
               Contains(Property(
                   &perfetto::protos::gen::TracePacket::trigger,
                   Property(&perfetto::protos::gen::Trigger::trigger_name,
                            "trigger1")))));
}

}  // namespace
}  // namespace internal
}  // namespace perfetto
