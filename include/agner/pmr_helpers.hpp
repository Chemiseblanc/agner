#pragma once

#include <array>
#include <cstddef>
#include <memory_resource>

namespace agner {

class PmrBuffer {
 public:
  static constexpr std::size_t kInlineBytes = 512;

  std::pmr::memory_resource* resource() noexcept { return &pool_; }

 private:
  std::array<std::byte, kInlineBytes> buffer_{};
  std::pmr::monotonic_buffer_resource upstream_{
      buffer_.data(), buffer_.size(), std::pmr::new_delete_resource()};
  std::pmr::unsynchronized_pool_resource pool_{std::pmr::pool_options{},
                                               &upstream_};
};

}  // namespace agner
