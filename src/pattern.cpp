#include "pattern.h"
#include <vector>
#include <cstdlib>

static bool Parse(const char* pattern, std::vector<int>& out) {
  out.clear();
  for (const char* p = pattern; *p;) {
    if (*p == ' ') { ++p; continue; }
    if (*p == '?') {
      out.push_back(-1);
      ++p;
      if (*p == '?') ++p;
      continue;
    }
    char* end = nullptr;
    long v = strtol(p, &end, 16);
    if (end == p || v < 0 || v > 0xFF) return false;
    out.push_back(static_cast<int>(v));
    p = end;
  }
  return !out.empty();
}

static uint8_t* ScanRange(uint8_t* begin, size_t size, const std::vector<int>& pat, int& matches) {
  uint8_t* found = nullptr;
  if (size < pat.size()) return nullptr;
  for (size_t i = 0; i <= size - pat.size(); ++i) {
    size_t j = 0;
    while (j < pat.size() && (pat[j] < 0 || begin[i + j] == pat[j])) ++j;
    if (j == pat.size()) {
      if (++matches == 1) found = begin + i;
    }
  }
  return found;
}

uint8_t* FindPattern(HMODULE module, const char* pattern) {
  std::vector<int> pat;
  if (!module || !Parse(pattern, pat)) return nullptr;

  auto* base = reinterpret_cast<uint8_t*>(module);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  auto* sec = IMAGE_FIRST_SECTION(nt);

  int matches = 0;
  uint8_t* result = nullptr;
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
    if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
    uint8_t* hit = ScanRange(base + sec->VirtualAddress, sec->Misc.VirtualSize, pat, matches);
    if (hit && !result) result = hit;
  }
  return matches == 1 ? result : nullptr;
}
