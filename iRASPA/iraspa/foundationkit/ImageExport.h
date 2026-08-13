#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

/// Writes rendered frames to still-image files through WIC, and encodes/decodes
/// PNG payloads for the Cocoa-compatible project archive.
///
/// Threading: the calling thread must have COM initialised. Nothing here touches the
/// apartment state, which belongs to whoever owns the thread.
namespace ImageExport
{
  /// Writes width * height tightly packed RGBA8888 pixels, top row first, to \a filename.
  /// The container is chosen from the file extension (.png, .jpg/.jpeg, .tif/.tiff, .bmp),
  /// defaulting to PNG. \a dotsPerInch is recorded in the file's metadata so that printing
  /// and placing reproduce the physical size the user asked for; it does not resample.
  /// On failure returns false and, when \a error is non-null, fills it with a message.
  bool saveImage(const std::wstring &filename, const uint8_t *pixels, int width, int height,
                 double dotsPerInch, std::wstring *error = nullptr);

  /// Encodes tightly packed RGBA8888 pixels as a PNG byte blob (Cocoa NSBitmapImageRep PNG).
  bool encodePng(const uint8_t *pixels, int width, int height, std::vector<uint8_t> &out,
                 std::wstring *error = nullptr);

  /// Decodes a PNG (or any WIC-readable) blob into tightly packed RGBA8888 pixels.
  bool decodeImage(const uint8_t *bytes, size_t size, std::vector<uint8_t> &pixels, int &width,
                   int &height, std::wstring *error = nullptr);

  /// Loads an image file into tightly packed RGBA8888 pixels via WIC.
  bool loadImageFile(const std::wstring &filename, std::vector<uint8_t> &pixels, int &width,
                     int &height, std::wstring *error = nullptr);
}
