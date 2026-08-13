/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2026 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

// Float RGBA in memory [0,1]; archive matches Qt QColor RGB layout /
// Cocoa BinaryCodable NSColor encode (spec=1 + 5x uint16).
class RKColor
{
public:
  RKColor() = default;
  RKColor(double r, double g, double b, double a = 1.0)
      : _r(r), _g(g), _b(b), _a(a) {}
  // QColor(QRgb) / 0xRRGGBB compatibility (alpha forced to opaque).
  explicit RKColor(unsigned int rgb)
      : _r(((rgb >> 16) & 0xFFu) / 255.0),
        _g(((rgb >> 8) & 0xFFu) / 255.0),
        _b((rgb & 0xFFu) / 255.0),
        _a(1.0)
  {
  }

  static RKColor fromRgbF(double r, double g, double b, double a = 1.0)
  {
    return RKColor(r, g, b, a);
  }

  static RKColor fromRgb(int r, int g, int b, int a = 255)
  {
    return RKColor(r / 255.0, g / 255.0, b / 255.0, a / 255.0);
  }

  double redF() const { return _r; }
  double greenF() const { return _g; }
  double blueF() const { return _b; }
  double alphaF() const { return _a; }

  int red() const { return static_cast<int>(_r * 255.0 + 0.5); }
  int green() const { return static_cast<int>(_g * 255.0 + 0.5); }
  int blue() const { return static_cast<int>(_b * 255.0 + 0.5); }
  int alpha() const { return static_cast<int>(_a * 255.0 + 0.5); }

  void setRedF(double v) { _r = v; }
  void setGreenF(double v) { _g = v; }
  void setBlueF(double v) { _b = v; }
  void setAlphaF(double v) { _a = v; }

  void setRgbF(double r, double g, double b, double a = 1.0)
  {
    _r = r;
    _g = g;
    _b = b;
    _a = a;
  }

  bool operator==(const RKColor &o) const
  {
    return _r == o._r && _g == o._g && _b == o._b && _a == o._a;
  }

private:
  double _r{0.0};
  double _g{0.0};
  double _b{0.0};
  double _a{1.0};
};
