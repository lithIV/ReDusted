#include "log.h"
#include <cstdarg>
#include <cstdio>

namespace logx {

static FILE* g_file = nullptr;
static CRITICAL_SECTION g_lock;

void Init(const wchar_t* path, bool enabled) {
  InitializeCriticalSection(&g_lock);
  if (enabled) g_file = _wfopen(path, L"w");
}

void Write(const char* fmt, ...) {
  if (!g_file) return;
  EnterCriticalSection(&g_lock);
  SYSTEMTIME t;
  GetLocalTime(&t);
  fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  va_list args;
  va_start(args, fmt);
  vfprintf(g_file, fmt, args);
  va_end(args);
  fputc('\n', g_file);
  fflush(g_file);
  LeaveCriticalSection(&g_lock);
}

}  // namespace logx
