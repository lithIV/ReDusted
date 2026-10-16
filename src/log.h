#pragma once
#include <windows.h>

namespace logx {
void Init(const wchar_t* path, bool enabled);
void Write(const char* fmt, ...);
}

#define LOG(...) ::logx::Write(__VA_ARGS__)
