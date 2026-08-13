/********************************************************************************************************************
    Simple integer size/rect used instead of QSize/QRect in RenderKit.
 ********************************************************************************************************************/

#pragma once

#include <algorithm>

struct RKSize
{
  int w{0};
  int h{0};
  RKSize() = default;
  RKSize(int width, int height) : w(width), h(height) {}
  int width() const { return w; }
  int height() const { return h; }
};

struct RKRect
{
  int x{0};
  int y{0};
  int w{0};
  int h{0};
  RKRect() = default;
  RKRect(int x_, int y_, int width, int height) : x(x_), y(y_), w(width), h(height) {}
  explicit RKRect(RKSize s) : w(s.w), h(s.h) {}
  int left() const { return x; }
  int top() const { return y; }
  int width() const { return w; }
  int height() const { return h; }
  RKSize size() const { return RKSize(w, h); }
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool contains(int px, int py) const
  {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

struct RKPointF
{
  double xf{0};
  double yf{0};
  RKPointF() = default;
  RKPointF(double x_, double y_) : xf(x_), yf(y_) {}
  double x() const { return xf; }
  double y() const { return yf; }
};

struct RKRectF
{
  double x{0};
  double y{0};
  double w{0};
  double h{0};
  RKRectF() = default;
  RKRectF(double x_, double y_, double width, double height) : x(x_), y(y_), w(width), h(height) {}
  RKPointF center() const { return RKPointF(x + w * 0.5, y + h * 0.5); }
  RKRectF united(const RKRectF &other) const
  {
    if (w <= 0 && h <= 0)
      return other;
    if (other.w <= 0 && other.h <= 0)
      return *this;
    const double x1 = (std::min)(x, other.x);
    const double y1 = (std::min)(y, other.y);
    const double x2 = (std::max)(x + w, other.x + other.w);
    const double y2 = (std::max)(y + h, other.y + other.h);
    return RKRectF(x1, y1, x2 - x1, y2 - y1);
  }
};
