/********************************************************************************************************************
    Qt-free RGBA8 image buffer (replaces QImage for WinUI / kit paths).
 ********************************************************************************************************************/

#include "rkimage.h"
#include "ImageExport.h"

#include <windows.h>

namespace
{
  std::wstring widenPath(const std::string &path)
  {
    if (path.empty())
      return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
                                           nullptr, 0);
    if (needed <= 0)
      return {};
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), wide.data(), needed);
    return wide;
  }
}

bool RKImage::load(const std::string &path)
{
  return load(widenPath(path));
}

bool RKImage::load(const std::wstring &path)
{
  _pixels.clear();
  _w = 0;
  _h = 0;
  _format = Format_Invalid;
  if (path.empty())
    return false;

  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  if (!ImageExport::loadImageFile(path, pixels, width, height))
    return false;

  _w = width;
  _h = height;
  _format = Format_RGBA8888;
  _pixels = std::move(pixels);
  return true;
}

bool RKImage::loadFromPng(const std::vector<uint8_t> &pngBytes)
{
  _pixels.clear();
  _w = 0;
  _h = 0;
  _format = Format_Invalid;
  if (pngBytes.empty())
    return false;

  std::vector<uint8_t> pixels;
  int width = 0;
  int height = 0;
  if (!ImageExport::decodeImage(pngBytes.data(), pngBytes.size(), pixels, width, height))
    return false;

  _w = width;
  _h = height;
  _format = Format_RGBA8888;
  _pixels = std::move(pixels);
  return true;
}

bool RKImage::saveToPng(std::vector<uint8_t> &pngBytes) const
{
  pngBytes.clear();
  if (isNull())
    return false;
  return ImageExport::encodePng(_pixels.data(), _w, _h, pngBytes);
}
