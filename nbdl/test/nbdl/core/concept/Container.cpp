// Copyright Jason Rice 2026

#include <nbdl/concept/Container.hpp>

#include <catch.hpp>
#include <list>
#include <string>
#include <vector>

static_assert(nbdl::Container<std::list<int>>);
static_assert(!nbdl::ContiguousContainer<std::list<int>>);
static_assert(!nbdl::ContiguousByteContainer<std::list<int>>);
static_assert(!nbdl::FixedSizeContainer<std::list<int>>);

static_assert(nbdl::Container<std::vector<int>>);
static_assert(nbdl::ContiguousContainer<std::vector<int>>);
static_assert(!nbdl::ContiguousByteContainer<std::vector<int>>);
static_assert(!nbdl::FixedSizeContainer<std::vector<int>>);

static_assert(nbdl::Container<std::vector<char>>);
static_assert(nbdl::ContiguousContainer<std::vector<char>>);
static_assert(nbdl::ContiguousByteContainer<std::vector<char>>);
static_assert(!nbdl::FixedSizeContainer<std::vector<char>>);

static_assert(nbdl::Container<std::array<int, 5>>);
static_assert(nbdl::ContiguousContainer<std::array<int, 5>>);
static_assert(!nbdl::ContiguousByteContainer<std::array<int, 2>>);
static_assert(nbdl::FixedSizeContainer<std::array<int, 5>>);

static_assert(nbdl::Container<std::array<char, 2>>);
static_assert(nbdl::ContiguousContainer<std::array<char, 2>>);
static_assert(nbdl::ContiguousByteContainer<std::array<char, 2>>);
static_assert(nbdl::FixedSizeContainer<std::array<char, 2>>);

static_assert(nbdl::Container<std::string>);
static_assert(nbdl::ContiguousContainer<std::string>);
static_assert(nbdl::ContiguousByteContainer<std::string>);
static_assert(!nbdl::FixedSizeContainer<std::string>);
