/********************************************************************************************************************
    Generate UUID strings without Qt (matches legacy QUuid uppercase hyphenated form).
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"

#if defined(_WIN32)
#include <objbase.h>
#endif

#include <random>
#include <sstream>
#include <iomanip>

inline RKString generateUuidString()
{
#if defined(_WIN32)
  GUID guid{};
  if (SUCCEEDED(CoCreateGuid(&guid)))
  {
    char buf[37];
    std::snprintf(buf, sizeof(buf),
                  "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                  static_cast<unsigned>(guid.Data1),
                  static_cast<unsigned>(guid.Data2),
                  static_cast<unsigned>(guid.Data3),
                  static_cast<unsigned>(guid.Data4[0]),
                  static_cast<unsigned>(guid.Data4[1]),
                  static_cast<unsigned>(guid.Data4[2]),
                  static_cast<unsigned>(guid.Data4[3]),
                  static_cast<unsigned>(guid.Data4[4]),
                  static_cast<unsigned>(guid.Data4[5]),
                  static_cast<unsigned>(guid.Data4[6]),
                  static_cast<unsigned>(guid.Data4[7]));
    return RKString(buf);
  }
#endif
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);
  const uint32_t a = dist(rng);
  const uint32_t b = dist(rng);
  const uint32_t c = dist(rng);
  const uint32_t d = dist(rng);
  std::ostringstream os;
  os << std::uppercase << std::hex << std::setfill('0')
     << std::setw(8) << a << '-'
     << std::setw(4) << ((b >> 16) & 0xFFFFu) << '-'
     << std::setw(4) << (b & 0xFFFFu) << '-'
     << std::setw(4) << ((c >> 16) & 0xFFFFu) << '-'
     << std::setw(12) << (((static_cast<uint64_t>(c & 0xFFFFu) << 32) | d) & 0xFFFFFFFFFFFFull);
  return RKString(os.str());
}
