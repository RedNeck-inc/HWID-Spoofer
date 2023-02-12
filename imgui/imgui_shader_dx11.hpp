#pragma once

#include "../include/auto.hpp"
#include "../include/base.hpp"
#include "../include/win32.hpp"

constexpr std::uint8_t g_vs_key = 0xEB;
constexpr std::uint8_t g_ps_key = 0xEB;

extern const std::size_t g_vs_size;
extern const std::size_t g_ps_size;

extern const std::uint8_t g_vs_code[];
extern const std::uint8_t g_ps_code[];