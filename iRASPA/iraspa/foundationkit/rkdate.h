/********************************************************************************************************************
    Qt-free calendar date (year/month/day). Wire format remains three ints when archived.
 ********************************************************************************************************************/

#pragma once

#include <ctime>

class RKDate
{
public:
  RKDate() = default;
  RKDate(int year, int month, int day) : _y(year), _m(month), _d(day) {}

  int year() const { return _y; }
  int month() const { return _m; }
  int day() const { return _d; }

  bool operator==(const RKDate &o) const { return _y == o._y && _m == o._m && _d == o._d; }
  bool operator!=(const RKDate &o) const { return !(*this == o); }

  static RKDate currentDate()
  {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    local = *std::localtime(&t);
#endif
    return RKDate(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  }

private:
  int _y{0};
  int _m{0};
  int _d{0};
};
