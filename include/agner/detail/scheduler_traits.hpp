#pragma once

#include <mutex>
#include <type_traits>

#include "agner/detail/null_mutex.hpp"

namespace agner::detail {

/// @brief Trait to mark a scheduler as concurrent (multi-threaded).
///
/// Specialize to std::true_type for schedulers that run actors on
/// multiple threads. When true, Actor and SchedulerBase use std::mutex
/// for internal synchronization; when false they use null_mutex (no-op).
template <typename SchedulerType>
struct is_concurrent_scheduler : std::false_type {};

template <typename SchedulerType>
inline constexpr bool is_concurrent_scheduler_v =
    is_concurrent_scheduler<SchedulerType>::value;

/// @brief Resolves to std::mutex for concurrent schedulers, null_mutex otherwise.
template <typename SchedulerType>
using scheduler_mutex_t =
    std::conditional_t<is_concurrent_scheduler_v<SchedulerType>,
                       std::mutex, null_mutex>;

}  // namespace agner::detail
