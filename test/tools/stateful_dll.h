#pragma once

#include <cstdint>

extern "C" std::uint32_t protected_mix32(std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                         std::uint32_t d, std::uint32_t e, std::uint32_t f,
                                         std::uint32_t g, std::uint32_t h);
extern "C" std::uint64_t protected_mix64(std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                         std::uint64_t d, std::uint64_t e, std::uint64_t f,
                                         std::uint64_t g, std::uint64_t h);

using MixCallback = void (*)(std::uint64_t);
extern "C" void invoke_mix(MixCallback callback, const std::uint64_t *operands, std::uint64_t x,
                           std::uint64_t y, std::uint32_t *destructions);
