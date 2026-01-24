#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <vector>

#include "agner/actor.hpp"

namespace {

using Clock = std::chrono::high_resolution_clock;

struct TimedPayload {
  Clock::time_point sent_at{};
  std::array<std::uint64_t, 2> words{};
};

static_assert(sizeof(TimedPayload) >= 16 && sizeof(TimedPayload) <= 32);

std::size_t percentile_index(double percentile, std::size_t count) {
  if (count == 0) {
    return 0;
  }
  auto position = static_cast<std::size_t>(
      std::ceil(percentile * static_cast<double>(count)));
  if (position == 0) {
    return 0;
  }
  return std::min(position - 1, count - 1);
}

struct SampleStats {
  double mean = 0.0;
  double p50 = 0.0;
  double p90 = 0.0;
  double p99 = 0.0;
};

struct SampleCollector {
  std::vector<std::uint64_t> values;
  std::mutex mutex;

  void reserve(std::size_t count) { values.reserve(count); }

  void add(std::uint64_t value) {
    std::lock_guard<std::mutex> guard(mutex);
    values.push_back(value);
  }

  SampleStats summarize() {
    std::lock_guard<std::mutex> guard(mutex);
    SampleStats stats{};
    if (values.empty()) {
      return stats;
    }
    std::sort(values.begin(), values.end());
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    auto count = values.size();
    stats.mean = sum / static_cast<double>(count);
    stats.p50 = static_cast<double>(values[percentile_index(0.50, count)]);
    stats.p90 = static_cast<double>(values[percentile_index(0.90, count)]);
    stats.p99 = static_cast<double>(values[percentile_index(0.99, count)]);
    return stats;
  }

  void clear() {
    std::lock_guard<std::mutex> guard(mutex);
    values.clear();
  }
};

class LatencyActor : public agner::Actor<agner::Scheduler, LatencyActor,
                                         agner::Messages<TimedPayload>> {
 public:
  LatencyActor(agner::Scheduler& scheduler, std::size_t expected,
               SampleCollector* latency_samples, SampleCollector* depth_samples,
               std::atomic<std::uint64_t>* sent_count,
               std::atomic<std::uint64_t>* received_count)
      : Actor(scheduler),
        expected_(expected),
        latency_samples_(latency_samples),
        depth_samples_(depth_samples),
        sent_count_(sent_count),
        received_count_(received_count) {}

  agner::task<void> run() {
    latency_samples_->reserve(expected_);
    for (std::size_t i = 0; i < expected_; ++i) {
      auto latency = co_await receive([&](TimedPayload& payload) {
        auto now = Clock::now();
        auto received =
            received_count_->fetch_add(1, std::memory_order_relaxed) + 1;
        auto sent = sent_count_->load(std::memory_order_relaxed);
        depth_samples_->add(sent - received);
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - payload.sent_at)
                .count());
      });
      latency_samples_->add(latency);
    }
    co_return;
  }

 private:
  std::size_t expected_;
  SampleCollector* latency_samples_;
  SampleCollector* depth_samples_;
  std::atomic<std::uint64_t>* sent_count_;
  std::atomic<std::uint64_t>* received_count_;
};

void add_latency_counters(benchmark::State& state, const SampleStats& stats) {
  state.counters["lat_mean_ns"] = stats.mean;
  state.counters["lat_p50_ns"] = stats.p50;
  state.counters["lat_p90_ns"] = stats.p90;
  state.counters["lat_p99_ns"] = stats.p99;
}

void add_depth_counters(benchmark::State& state, const SampleStats& stats) {
  state.counters["queue_depth_mean"] = stats.mean;
  state.counters["queue_depth_p50"] = stats.p50;
  state.counters["queue_depth_p90"] = stats.p90;
  state.counters["queue_depth_p99"] = stats.p99;
}

void add_throughput_counters(benchmark::State& state,
                             std::size_t message_count) {
  state.counters["messages"] =
      benchmark::Counter(static_cast<double>(message_count),
                         benchmark::Counter::kIsIterationInvariant);
  state.counters["messages_per_second"] = benchmark::Counter(
      static_cast<double>(message_count), benchmark::Counter::kIsRate);
}

void register_message_counts(benchmark::internal::Benchmark* benchmark) {
  for (int count : {1, 64, 256, 1024, 8192, 65536}) {
    benchmark->Arg(count);
  }
}

// Benchmark: enqueue a full burst before running the scheduler.
// Measures end-to-end latency (send timestamp to receive), queue depth
// distribution from sent/received counters, and throughput from fixed
// message_count.
// Latency uses a timestamp taken immediately before send and sampled on
// receive; queue depth uses atomics so backlog is exact, and throughput
// counters use iteration-invariant counts and benchmark rate.
void UseCase_Burst(benchmark::State& state) {
  auto message_count = static_cast<std::size_t>(state.range(0));
  SampleStats latency_stats{};
  SampleStats depth_stats{};
  for (auto _ : state) {
    agner::Scheduler scheduler;
    SampleCollector latency_samples;
    SampleCollector depth_samples;
    latency_samples.reserve(message_count);
    depth_samples.reserve(message_count * 2);
    std::atomic<std::uint64_t> sent_count{0};
    std::atomic<std::uint64_t> received_count{0};
    auto actor = scheduler.spawn<LatencyActor>(message_count, &latency_samples,
                                               &depth_samples, &sent_count,
                                               &received_count);
    for (std::size_t i = 0; i < message_count; ++i) {
      TimedPayload payload{};
      payload.sent_at = Clock::now();
      auto sent = sent_count.fetch_add(1, std::memory_order_relaxed) + 1;
      auto received = received_count.load(std::memory_order_relaxed);
      depth_samples.add(sent - received);
      scheduler.send(actor, payload);
    }
    scheduler.run();
    state.PauseTiming();
    latency_stats = latency_samples.summarize();
    depth_stats = depth_samples.summarize();
    state.ResumeTiming();
  }
  add_throughput_counters(state, message_count);
  add_latency_counters(state, latency_stats);
  add_depth_counters(state, depth_stats);
}

// Benchmark: send one message, run scheduler, repeat (steady-state
// processing).
// Measures per-message latency and queue depth while keeping the queue
// mostly drained, plus throughput for total messages.
// Latency is computed from a send-time timestamp to receive time; queue
// depth uses atomic sent/received counts, and throughput uses the exact
// message_count with benchmark rate.
void UseCase_SteadyState(benchmark::State& state) {
  auto message_count = static_cast<std::size_t>(state.range(0));
  SampleStats latency_stats{};
  SampleStats depth_stats{};
  for (auto _ : state) {
    agner::Scheduler scheduler;
    SampleCollector latency_samples;
    SampleCollector depth_samples;
    latency_samples.reserve(message_count);
    depth_samples.reserve(message_count * 2);
    std::atomic<std::uint64_t> sent_count{0};
    std::atomic<std::uint64_t> received_count{0};
    auto actor = scheduler.spawn<LatencyActor>(message_count, &latency_samples,
                                               &depth_samples, &sent_count,
                                               &received_count);
    for (std::size_t i = 0; i < message_count; ++i) {
      TimedPayload payload{};
      payload.sent_at = Clock::now();
      auto sent = sent_count.fetch_add(1, std::memory_order_relaxed) + 1;
      auto received = received_count.load(std::memory_order_relaxed);
      depth_samples.add(sent - received);
      scheduler.send(actor, payload);
      scheduler.run();
    }
    state.PauseTiming();
    latency_stats = latency_samples.summarize();
    depth_stats = depth_samples.summarize();
    state.ResumeTiming();
  }
  add_throughput_counters(state, message_count);
  add_latency_counters(state, latency_stats);
  add_depth_counters(state, depth_stats);
}

// Benchmark: send in batches, run scheduler between batches to
// simulate backlog.
// Measures latency under partial backpressure, queue depth distribution
// from sent/received counters, and throughput for total batch processing.
// Latency uses send timestamps and receive times; queue depth uses atomic
// counters so backlog across bursts is accurate; throughput counters use
// fixed message_count and benchmark rate.
void UseCase_Backlog(benchmark::State& state) {
  auto message_count = static_cast<std::size_t>(state.range(0));
  auto batch_size = std::min<std::size_t>(64u, message_count);
  SampleStats latency_stats{};
  SampleStats depth_stats{};
  for (auto _ : state) {
    agner::Scheduler scheduler;
    SampleCollector latency_samples;
    SampleCollector depth_samples;
    latency_samples.reserve(message_count);
    depth_samples.reserve(message_count * 2);
    std::atomic<std::uint64_t> sent_count{0};
    std::atomic<std::uint64_t> received_count{0};
    auto actor = scheduler.spawn<LatencyActor>(message_count, &latency_samples,
                                               &depth_samples, &sent_count,
                                               &received_count);
    std::size_t sent = 0;
    while (sent < message_count) {
      auto chunk = std::min(batch_size, message_count - sent);
      for (std::size_t i = 0; i < chunk; ++i) {
        TimedPayload payload{};
        payload.sent_at = Clock::now();
        auto send_value =
            sent_count.fetch_add(1, std::memory_order_relaxed) + 1;
        auto received = received_count.load(std::memory_order_relaxed);
        depth_samples.add(send_value - received);
        scheduler.send(actor, payload);
      }
      sent += chunk;
      scheduler.run();
    }
    state.PauseTiming();
    latency_stats = latency_samples.summarize();
    depth_stats = depth_samples.summarize();
    state.ResumeTiming();
  }
  add_throughput_counters(state, message_count);
  add_latency_counters(state, latency_stats);
  add_depth_counters(state, depth_stats);
}

BENCHMARK(UseCase_Burst)->Apply(register_message_counts);
BENCHMARK(UseCase_SteadyState)->Apply(register_message_counts);
BENCHMARK(UseCase_Backlog)->Apply(register_message_counts);

}  // namespace
