#ifndef UTILS_HPP
#define UTILS_HPP
#include <cassert>
#include <cstdio>

#define panic(...)                     \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fprintf(stderr, "\n");        \
    std::fflush(stderr);               \
    assert(false);

#endif