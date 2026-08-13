/********************************************************************************************************************
    Qt-free RGBA8 image buffer (replaces QImage for WinUI / kit paths).
 ********************************************************************************************************************/

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class RKImage
{
public:
  enum Format { Format_Invalid = 0, Format_RGBA8888 = 1, Format_ARGB32 = 2 };

  RKImage() = default;
  RKImage(int width, int height, Format format = Format_RGBA8888)
    : _w(width), _h(height), _format(format)
  {
    if (_w > 0 && _h > 0)
      _pixels.assign(static_cast<size_t>(_w) * static_cast<size_t>(_h) * 4u, 0);
  }

  bool isNull() const { return _pixels.empty() || _w <= 0 || _h <= 0; }
  int width() const { return _w; }
  int height() const { return _h; }
  Format format() const { return _format; }
  int bytesPerLine() const { return _w * 4; }

  uint8_t *bits() { return _pixels.data(); }
  const uint8_t *bits() const { return _pixels.data(); }
  const uint8_t *constBits() const { return _pixels.data(); }
  std::vector<uint8_t> &pixels() { return _pixels; }
  const std::vector<uint8_t> &pixels() const { return _pixels; }

  void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
  {
    for (size_t i = 0; i + 3 < _pixels.size(); i += 4)
    {
      _pixels[i] = r;
      _pixels[i + 1] = g;
      _pixels[i + 2] = b;
      _pixels[i + 3] = a;
    }
  }

  RKImage convertToFormat(Format fmt) const
  {
    RKImage out(*this);
    out._format = fmt;
    // Stored as RGBA8888 always; ARGB32 callers still get the same byte order for DX uploads.
    return out;
  }

  /// Loads any WIC-supported image file into tightly packed RGBA8888.
  bool load(const std::string &path);
  bool load(const std::wstring &path);

  /// Cocoa archives the project background as a PNG blob; these round-trip that payload.
  bool loadFromPng(const std::vector<uint8_t> &pngBytes);
  bool saveToPng(std::vector<uint8_t> &pngBytes) const;

private:
  int _w{0};
  int _h{0};
  Format _format{Format_Invalid};
  std::vector<uint8_t> _pixels;
};
