#include "agner/asio_scheduler.hpp"

#include <gtest/gtest.h>

#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/thread/future.hpp>

#include "agner/genserver.hpp"
#include "agner/supervisor.hpp"

#include <atomic>
#include <chrono>
#include <cerrno>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
#include <boost/asio/buffer.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "test_support.hpp"

namespace {

using namespace std::chrono_literals;
using namespace agner::test_support;

struct MoveTracked {
  explicit MoveTracked(std::shared_ptr<std::atomic<int>> move_count)
      : move_count(std::move(move_count)) {}

  MoveTracked(const MoveTracked&) = delete;
  MoveTracked& operator=(const MoveTracked&) = delete;

  MoveTracked(MoveTracked&& other) noexcept
      : move_count(std::move(other.move_count)) {
    if (move_count) {
      move_count->fetch_add(1, std::memory_order_relaxed);
    }
  }

  MoveTracked& operator=(MoveTracked&& other) noexcept {
    move_count = std::move(other.move_count);
    if (move_count) {
      move_count->fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  std::shared_ptr<std::atomic<int>> move_count;
};

boost::asio::awaitable<int> asio_timer_value() {
  auto executor = co_await boost::asio::this_coro::executor;
  boost::asio::steady_timer timer(executor);
  timer.expires_after(1ms);
  co_await timer.async_wait(boost::asio::use_awaitable);
  co_return 5;
}

agner::task<void> await_successes(agner::AsioScheduler& scheduler,
                                  boost::future<int> boost_future,
                                  boost::shared_future<int> shared_future,
                                  std::future<int> std_future, int* out) {
  int total = 0;
  total += co_await scheduler.await(std::move(boost_future));
  total += co_await scheduler.await(std::move(shared_future));
  total += co_await scheduler.await(std::move(std_future));
  total += co_await scheduler.await(asio_timer_value());
  *out = total;
  co_return;
}

agner::task<void> await_exception(agner::AsioScheduler& scheduler,
                                  std::future<int> future, bool* caught) {
  try {
    (void)co_await scheduler.await(std::move(future));
  } catch (const std::runtime_error&) {
    *caught = true;
  }
  co_return;
}

agner::task<void> await_pending_std_future(agner::AsioScheduler& scheduler,
                                            std::future<int> future, int* out) {
  *out = co_await scheduler.await(std::move(future));
  co_return;
}

#if defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)

struct WriteFile {
  std::string contents;
};

struct ReadFile {
  std::size_t bytes;
};

struct FileRoundTrip {
  std::size_t bytes_written = 0;
  std::string contents;
};

using FileHandlers =
    agner::Handlers<std::size_t(WriteFile), std::string(ReadFile)>;
using FileWriteCall = agner::CallMessage<WriteFile>;
using FileReadCall = agner::CallMessage<ReadFile>;

int open_file_descriptor(const std::string& path, int flags, mode_t mode = 0) {
  int descriptor = ::open(path.c_str(), flags, mode);
  if (descriptor == -1) {
    throw std::system_error(errno, std::generic_category());
  }
  return descriptor;
}

boost::asio::awaitable<std::size_t> asio_write_file(std::string path,
                                                    std::string contents) {
  auto executor = co_await boost::asio::this_coro::executor;
  boost::asio::posix::stream_descriptor file(
      executor, open_file_descriptor(path, O_WRONLY | O_CREAT | O_TRUNC,
                                     S_IRUSR | S_IWUSR));
  co_return co_await boost::asio::async_write(
      file, boost::asio::buffer(contents), boost::asio::use_awaitable);
}

boost::asio::awaitable<std::string> asio_read_file(std::string path,
                                                   std::size_t bytes) {
  auto executor = co_await boost::asio::this_coro::executor;
  boost::asio::posix::stream_descriptor file(
      executor, open_file_descriptor(path, O_RDONLY));
  std::string contents(bytes, '\0');
  auto bytes_read = co_await boost::asio::async_read(
      file, boost::asio::buffer(contents), boost::asio::use_awaitable);
  contents.resize(bytes_read);
  co_return contents;
}

class AsioFileServer
    : public agner::GenServer<agner::AsioScheduler, AsioFileServer,
                              FileHandlers> {
 public:
  using Base =
      agner::GenServer<agner::AsioScheduler, AsioFileServer, FileHandlers>;

  AsioFileServer(agner::AsioScheduler& scheduler, std::string path)
      : Base(scheduler), path_(std::move(path)) {}

  agner::task<void> run() {
    while (true) {
      auto action = co_await this->receive(
          [this](FileWriteCall msg) {
            return handle_write(std::move(msg));
          },
          [this](FileReadCall msg) { return handle_read(std::move(msg)); },
          [this](agner::ExitSignal signal) { return handle_exit(signal); });
      if (co_await action) {
        break;
      }
    }
  }

 private:
  agner::task<bool> handle_write(FileWriteCall msg) {
    auto bytes = co_await this->scheduler().await(
        asio_write_file(path_, std::move(msg.request.contents)));
    this->send(msg.caller, agner::Reply{msg.request_id, std::any(bytes)});
    co_return false;
  }

  agner::task<bool> handle_read(FileReadCall msg) {
    auto contents =
        co_await this->scheduler().await(asio_read_file(path_,
                                                        msg.request.bytes));
    this->send(msg.caller,
               agner::Reply{msg.request_id, std::any(std::move(contents))});
    co_return false;
  }

  agner::task<bool> handle_exit(agner::ExitSignal) { co_return true; }

  std::string path_;
};

class AsioFileClient
    : public agner::GenServer<agner::AsioScheduler, AsioFileClient,
                              FileHandlers> {
 public:
  using Base =
      agner::GenServer<agner::AsioScheduler, AsioFileClient, FileHandlers>;

  AsioFileClient(agner::AsioScheduler& scheduler,
                 agner::ActorRef server,
                 std::string contents,
                 FileRoundTrip* result)
      : Base(scheduler),
        server_(server),
        contents_(std::move(contents)),
        result_(result) {}

  agner::task<void> run() {
    auto bytes_written =
        co_await this->call(server_, WriteFile{contents_}, 100ms);
    auto contents =
        co_await this->call(server_, ReadFile{bytes_written}, 100ms);

    result_->bytes_written = bytes_written;
    result_->contents = std::move(contents);
    co_return;
  }

 private:
  agner::ActorRef server_{};
  std::string contents_;
  FileRoundTrip* result_;
};

class AsioFileSupervisor
    : public agner::Supervisor<
          agner::AsioScheduler, AsioFileSupervisor,
          agner::ChildSpec<AsioFileServer, std::string>,
          agner::ChildSpec<AsioFileClient, agner::ActorRef, std::string,
                           FileRoundTrip*>> {
 public:
  using Base = agner::Supervisor<
      agner::AsioScheduler, AsioFileSupervisor,
      agner::ChildSpec<AsioFileServer, std::string>,
      agner::ChildSpec<AsioFileClient, agner::ActorRef, std::string,
                       FileRoundTrip*>>;

  AsioFileSupervisor(agner::AsioScheduler& scheduler,
                     std::string path,
                     std::string contents,
                     FileRoundTrip* result)
      : Base(scheduler),
        path_(std::move(path)),
        contents_(std::move(contents)),
        result_(result) {}

  static Specification specification() {
    return {
        .strategy = agner::Strategy::one_for_one,
        .intensity = {3, 100ms},
        .children = std::make_tuple(
            agner::child<AsioFileServer, std::string>(
                {"file-server"}, agner::Restart::permanent, 0ms, {}),
            agner::child<AsioFileClient, agner::ActorRef, std::string,
                         FileRoundTrip*>(
                {"file-client"}, agner::Restart::temporary, 0ms,
                agner::ActorRef{}, {}, nullptr))};
  }

  agner::task<void> run() {
    this->template set_child_args<0>(std::make_tuple(path_));
    auto server = co_await this->template start_child<agner::ChildIndex<0>>();
    co_await this->template start_child<agner::ChildIndex<1>>(
        server, contents_, result_);
    co_await Base::supervise_loop();
  }

 private:
  std::string path_;
  std::string contents_;
  FileRoundTrip* result_;
};

#endif

}  // namespace

// Summary: When an actor runs on AsioScheduler, it shall receive sent messages.
// Description: This test spawns a generic Collector actor on AsioScheduler,
// sends `Ping{42}`, and runs the Asio event loop. The stored value confirms
// spawn, send, and receive all work through the optional scheduler.
TEST(AsioScheduler, SpawnSendReceiveActor) {
  agner::AsioScheduler scheduler;
  int value = 0;
  auto actor = scheduler.spawn<CollectorT<agner::AsioScheduler>>(&value);

  scheduler.send(actor, Ping{42});
  scheduler.run();

  EXPECT_EQ(value, 42);
}

// Summary: When delayed callbacks are scheduled, AsioScheduler shall fire them.
// Description: This test schedules an immediate callback and a short timer, then
// runs the Asio event loop. The observed sequence confirms `schedule_after`
// keeps timer storage alive and invokes callbacks in deadline order.
TEST(AsioScheduler, ScheduleAfterFiresTimers) {
  agner::AsioScheduler scheduler;
  std::vector<int> values;

  scheduler.schedule_after(agner::AsioScheduler::Clock::duration::zero(),
                           [&] { values.push_back(1); });
  scheduler.schedule_after(1ms, [&] { values.push_back(2); });
  scheduler.run();

  EXPECT_EQ(values, (std::vector<int>{1, 2}));
}

// Summary: When public await helpers receive ready values, they shall resume.
// Description: This test awaits a Boost.Thread future, Boost.Thread
// shared_future, std::future, and Boost.Asio awaitable through
// `scheduler.await(...)`. The summed result locks in successful public await
// syntax for each supported asynchronous family.
TEST(AsioScheduler, AwaitSupportedFamilies) {
  agner::AsioScheduler scheduler;
  boost::promise<int> boost_promise;
  boost::promise<int> shared_promise;
  std::promise<int> std_promise;
  int result = 0;

  auto shared_future = shared_promise.get_future().share();
  await_successes(scheduler, boost_promise.get_future(), shared_future,
                  std_promise.get_future(), &result)
      .detach(scheduler);
  scheduler.schedule_after(
      agner::AsioScheduler::Clock::duration::zero(), [&] {
        boost_promise.set_value(11);
        shared_promise.set_value(13);
        std_promise.set_value(17);
      });
  scheduler.run();

  EXPECT_EQ(result, 46);
}

// Summary: When a future completes exceptionally, await shall rethrow it.
// Description: This test awaits a std::future through `scheduler.await(...)`
// and completes the promise with a runtime_error. The coroutine catches that
// exception, proving exceptional future completion propagates to await_resume.
TEST(AsioScheduler, ExceptionalFutureAwaitPropagatesException) {
  agner::AsioScheduler scheduler;
  std::promise<int> promise;
  bool caught = false;

  await_exception(scheduler, promise.get_future(), &caught).detach(scheduler);
  scheduler.schedule_after(agner::AsioScheduler::Clock::duration::zero(), [&] {
    promise.set_exception(std::make_exception_ptr(std::runtime_error("boom")));
  });
  scheduler.run();

  EXPECT_TRUE(caught);
}

// Summary: While a std::future is pending, AsioScheduler shall keep running.
// Description: This test starts a coroutine blocked on an unset std::future,
// schedules an immediate timer, then completes the future from a later timer.
// The immediate timer flag confirms the event loop remains live instead of
// blocking or exiting while the future waiter is pending.
TEST(AsioScheduler, EventLoopRemainsLiveWhileFutureIsPending) {
  agner::AsioScheduler scheduler;
  std::promise<int> promise;
  bool timer_ran = false;
  int result = 0;

  await_pending_std_future(scheduler, promise.get_future(), &result)
      .detach(scheduler);
  scheduler.schedule_after(agner::AsioScheduler::Clock::duration::zero(),
                           [&] { timer_ran = true; });
  scheduler.schedule_after(1ms, [&] { promise.set_value(23); });
  scheduler.run();

  EXPECT_TRUE(timer_ran);
  EXPECT_EQ(result, 23);
}

// Summary: Future completion after scheduler destruction shall not touch it.
// Description: This test suspends an awaiter on a pending std::future, destroys
// the scheduler, then completes the future. The waiter thread still consumes the
// value and posts through the retained internal Asio state rather than
// dereferencing the destroyed scheduler object.
TEST(AsioScheduler, StdFutureCompletionSurvivesSchedulerDestruction) {
  auto move_count = std::make_shared<std::atomic<int>>(0);
  std::promise<MoveTracked> promise;
  auto future = promise.get_future();

  {
    agner::AsioScheduler scheduler;
    auto awaiter = agner::asio_await(scheduler, std::move(future));
    awaiter.await_suspend(std::noop_coroutine());
  }

  promise.set_value(MoveTracked{move_count});
  const int moves_after_set = move_count->load(std::memory_order_relaxed);
  for (int tries = 0; tries < 50 &&
                       move_count->load(std::memory_order_relaxed) <=
                           moves_after_set;
       ++tries) {
    std::this_thread::sleep_for(1ms);
  }

  EXPECT_GT(move_count->load(std::memory_order_relaxed), moves_after_set);
}

// Summary: A supervised GenServer tree shall integrate with Asio file I/O.
// Description: This test starts a supervisor tree with a file GenServer and a
// GenServer client under AsioScheduler. The client calls the server to write and
// then read a temporary file using Boost.Asio POSIX file descriptor operations,
// proving actor calls, supervision, and Asio file awaitables work together.
TEST(AsioScheduler, SupervisedGenServerPerformsAsioFileRoundTrip) {
#if defined(BOOST_ASIO_HAS_POSIX_STREAM_DESCRIPTOR)
  agner::AsioScheduler scheduler;
  FileRoundTrip result;
  auto path =
      std::filesystem::temp_directory_path() /
      ("agner-asio-file-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  const std::string contents = "agner asio supervised genserver file io";

  std::filesystem::remove(path);
  scheduler.spawn<AsioFileSupervisor>(path.string(), contents, &result);
  scheduler.run();

  EXPECT_EQ(result.bytes_written, contents.size());
  EXPECT_EQ(result.contents, contents);

  std::filesystem::remove(path);
#else
  GTEST_SKIP() << "Boost.Asio POSIX file descriptor I/O is not available";
#endif
}
