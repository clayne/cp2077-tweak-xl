#pragma once

// Generated by xmake from config/Project.hpp.in

#include <semver.hpp>

namespace App::Project
{
constexpr auto Name = "TweakXL";
constexpr auto Author = "psiberx";

constexpr auto NameW = L"TweakXL";
constexpr auto AuthorW = L"psiberx";

constexpr auto Version = semver::from_string_noexcept("1.5.0").value();
}
