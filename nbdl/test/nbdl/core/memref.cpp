//
// Copyright Jason Rice 2026
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include <nbdl/bind_memref.hpp>
#include <catch.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace {
  // Mock the prototype of a FuncOp with a memref argument lowered to LLVM IR.
  int32_t sum_memref(
    int32_t* allocated,
    int32_t* aligned,
    std::intptr_t offset,
    std::intptr_t size,
    std::intptr_t stride
  ) {
    (void)allocated;
    int32_t total = 0;
    for (std::intptr_t i = 0; i < size; ++i) {
      total += aligned[offset + i * stride];
    }
    return total;
  }

  void fill_memref(
    int32_t* allocated,
    int32_t* aligned,
    std::intptr_t offset,
    std::intptr_t size,
    std::intptr_t stride,
    int32_t value
  ) {
    (void)allocated;
    for (std::intptr_t i = 0; i < size; ++i)
      aligned[offset + i * stride] = value;
  }

  int32_t sum_memref_2d(int32_t* allocated,
                        int32_t* aligned,
                        std::intptr_t offset,
                        std::intptr_t size0,
                        std::intptr_t size1,
                        std::intptr_t stride0,
                        std::intptr_t stride1) {
    (void)allocated;
    int32_t total = 0;
    for (std::intptr_t i = 0; i < size0; ++i)
      for (std::intptr_t j = 0; j < size1; ++j)
        total += aligned[offset + i * stride0 + j * stride1];
    return total;
  }

}

TEST_CASE("Bind std::vector to memref.", "[memref]") {
  std::vector<int32_t> xs{1, 2, 3, 4};

  [&](auto&& mr) {
    auto&& [...args] = mr;
    CHECK(sum_memref(args...) == 10);
  }(nbdl::bind_memref(xs));

  // The binding is a view, not a copy: writing through the memref's
  // pointers mutates the original vector's storage.
  [&](auto&& mr) {
    auto&& [...args] = mr;
    fill_memref(args..., 42);
  }(nbdl::bind_memref(xs));
  CHECK(xs == (std::vector<int32_t>{42, 42, 42, 42}));
}

TEST_CASE("Bind std::array to memref.", "[memref]") {
  std::array<int32_t, 3> xs{5, 10, 15};

  [&](auto&& mr) {
    auto&& [...args] = mr;
    CHECK(sum_memref(args...) == 30);
  }(nbdl::bind_memref(xs));

  [&](auto&& mr) {
    auto&& [...args] = mr;
    fill_memref(args..., 7);
  }(nbdl::bind_memref(xs));
  CHECK(xs == (std::array<int32_t, 3>{7, 7, 7}));
}

TEST_CASE("Bind raw array to memref.", "[memref]") {
  int32_t xs[4] = {1, 1, 1, 1};

  [&](auto&& mr) {
    auto&& [...args] = mr;
    CHECK(sum_memref(args...) == 4);
  }(nbdl::bind_memref(xs));

  [&](auto&& mr) {
    auto&& [...args] = mr;
    fill_memref(args..., 9);
  }(nbdl::bind_memref(xs));
  CHECK(xs[0] == 9);
  CHECK(xs[1] == 9);
  CHECK(xs[2] == 9);
  CHECK(xs[3] == 9);
}

TEST_CASE("Bind std::mdspan to a rank-1 memref.", "[memref]") {
  std::vector<int32_t> buf{2, 4, 6, 8};
  std::mdspan<int32_t, std::dextents<std::size_t, 1>> m(buf.data(), 4);

  [&](auto&& mr) {
    auto&& [...args] = mr;
    CHECK(sum_memref(args...) == 20);
  }(nbdl::bind_memref(m));

  [&](auto&& mr) {
    auto&& [...args] = mr;
    fill_memref(args..., 3);
  }(nbdl::bind_memref(m));
  CHECK(buf == (std::vector<int32_t>{3, 3, 3, 3}));
}

TEST_CASE("Bind std::mdspan to a rank-2 memref.", "[memref]") {
  std::vector<int32_t> buf{1, 2, 3, 4, 5, 6};
  std::mdspan<int32_t, std::dextents<std::size_t, 2>> m(buf.data(), 2, 3);

  [&](auto&& mr) {
    auto&& [...args] = mr;
    CHECK(sum_memref_2d(args...) == 21);
  }(nbdl::bind_memref(m));
}
