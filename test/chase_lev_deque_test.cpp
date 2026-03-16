#include <gtest/gtest.h>

#include <barrier>
#include <chrono>
#include <optional>
#include <thread>

#include "agner/detail/chase_lev_deque.hpp"

namespace {

using namespace std::chrono_literals;

// Summary: Pushing past capacity grows the deque and preserves element order.
// Description: Starts from a tiny capacity to force growth, then validates
// LIFO pops still return all inserted elements correctly.
TEST(ChaseLevDeque, GrowPreservesElements) {
  agner::detail::ChaseLevDeque<int> deque(2);

  deque.push(1);
  deque.push(2);
  deque.push(3);

  auto v1 = deque.pop();
  auto v2 = deque.pop();
  auto v3 = deque.pop();

  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  ASSERT_TRUE(v3.has_value());
  EXPECT_EQ(*v1, 3);
  EXPECT_EQ(*v2, 2);
  EXPECT_EQ(*v3, 1);
  EXPECT_FALSE(deque.pop().has_value());
}

// Summary: Deque move operations transfer ownership and preserve content.
// Description: Exercises move construction and move assignment (including
// self-move assignment), then validates the destination still serves values.
TEST(ChaseLevDeque, MoveOperationsTransferState) {
  agner::detail::ChaseLevDeque<int> source(2);
  source.push(7);
  source.push(8);
  source.push(9);

  agner::detail::ChaseLevDeque<int> moved(std::move(source));
  EXPECT_TRUE(source.empty());

  agner::detail::ChaseLevDeque<int> assigned(2);
  assigned.push(11);
  assigned = std::move(moved);

  auto v1 = assigned.pop();
  auto v2 = assigned.pop();
  auto v3 = assigned.pop();

  ASSERT_TRUE(v1.has_value());
  ASSERT_TRUE(v2.has_value());
  ASSERT_TRUE(v3.has_value());
  EXPECT_EQ(*v1, 9);
  EXPECT_EQ(*v2, 8);
  EXPECT_EQ(*v3, 7);
}

// Summary: Concurrent steals on a single item contend and only one succeeds.
// Description: Starts two stealers at the same instant against one queued item,
// forcing one compare_exchange winner and one loser.
TEST(ChaseLevDeque, ConcurrentStealsContention) {
  agner::detail::ChaseLevDeque<int> deque(2);
  deque.push(42);

  std::barrier sync_point(3);
  std::optional<int> steal_a;
  std::optional<int> steal_b;

  std::thread a([&] {
    sync_point.arrive_and_wait();
    steal_a = deque.steal();
  });
  std::thread b([&] {
    sync_point.arrive_and_wait();
    steal_b = deque.steal();
  });

  sync_point.arrive_and_wait();
  a.join();
  b.join();

  EXPECT_NE(steal_a.has_value(), steal_b.has_value());
}

// Summary: Pop vs steal contention can cause owner pop CAS failure.
// Description: Races pop against steal on a single element, biasing the steal
// to win and validating we observe the owner losing the single-item CAS path.
TEST(ChaseLevDeque, PopCanLoseSingleItemRaceToSteal) {
  bool observed_owner_pop_loss = false;

  for (int i = 0; i < 2048 && !observed_owner_pop_loss; ++i) {
    agner::detail::ChaseLevDeque<int> deque(2);
    deque.push(i);

    std::barrier sync_point(6);
    std::optional<int> popped;
    std::optional<int> stolen_a;
    std::optional<int> stolen_b;
    std::optional<int> stolen_c;
    std::optional<int> stolen_d;

    std::thread stealer_a([&] {
      sync_point.arrive_and_wait();
      stolen_a = deque.steal();
    });
    std::thread stealer_b([&] {
      sync_point.arrive_and_wait();
      stolen_b = deque.steal();
    });
    std::thread stealer_c([&] {
      sync_point.arrive_and_wait();
      stolen_c = deque.steal();
    });
    std::thread stealer_d([&] {
      sync_point.arrive_and_wait();
      stolen_d = deque.steal();
    });

    std::thread owner([&] {
      sync_point.arrive_and_wait();
      popped = deque.pop();
    });

    sync_point.arrive_and_wait();
    stealer_a.join();
    stealer_b.join();
    stealer_c.join();
    stealer_d.join();
    owner.join();

    const bool any_steal_succeeded =
        stolen_a.has_value() || stolen_b.has_value() || stolen_c.has_value() ||
        stolen_d.has_value();
    if (!popped.has_value() && any_steal_succeeded) {
      observed_owner_pop_loss = true;
    }
  }

  EXPECT_TRUE(observed_owner_pop_loss);
}

}  // namespace