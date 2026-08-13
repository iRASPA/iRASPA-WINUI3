/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
    D.Dubbeldam@uva.nl            https://www.uva.nl/en/profile/d/u/d.dubbeldam/d.dubbeldam.html
    S.Calero@tue.nl               https://www.tue.nl/en/research/researchers/sofia-calero/
    t.j.h.vlugt@tudelft.nl        http://homepage.tudelft.nl/v9k6y

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ********************************************************************************************************************/

#include "rkfontatlas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>

#include "foundationkit.h"

#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace
{
const int scaler = 16;

int roundToInt(double v)
{
  return static_cast<int>(std::rint(v));
}
} // namespace

struct RKFontAtlas::DirectWriteState
{
  ComPtr<IDWriteFactory> factory;
  ComPtr<IDWriteFontFace> fontFace;
  UINT32 designUnitsPerEm = 0;
};

RKFontAtlas::RKFontAtlas(const RKString &fontName, int texture_size)
  : RKFontAtlas([&]() -> std::wstring {
      RKString family = fontName;
      // Accept legacy QFont::toString() blobs by taking the family field.
      const int comma = family.indexOf(",");
      if (comma >= 0)
        family = family.left(comma).trimmed();
      if (family.isEmpty())
        family = RKString("Segoe UI");
      return family.toStdWString();
    }(), texture_size)
{
}

RKFontAtlas::RKFontAtlas(const std::wstring &fontFamily, int texture_size)
  : width(static_cast<float>(texture_size)),
    height(static_cast<float>(texture_size)),
    textureData(static_cast<size_t>(texture_size) * static_cast<size_t>(texture_size)),
    _pdata(4 * static_cast<size_t>(texture_size) * static_cast<size_t>(texture_size), 0),
    characters()
{
  characterIndex.fill(-1);

  if (!initDirectWrite(fontFamily))
  {
    std::fprintf(stderr, "RKFontAtlas: DirectWrite failed to load font family\n");
    return;
  }

  renderSignedDistanceFont(texture_size);

  for (size_t i = 0; i < characters.size(); ++i)
  {
    const int index = characters[i].ID;
    if (index >= 0 && index < 256)
      characterIndex[static_cast<size_t>(index)] = static_cast<int>(i);
  }
}

RKFontAtlas::~RKFontAtlas()
{
  delete _dwrite;
  _dwrite = nullptr;
}

bool RKFontAtlas::initDirectWrite(const std::wstring &fontFamily)
{
  _dwrite = new DirectWriteState();

  HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(_dwrite->factory.GetAddressOf()));
  if (FAILED(hr) || !_dwrite->factory)
    return false;

  ComPtr<IDWriteFontCollection> collection;
  hr = _dwrite->factory->GetSystemFontCollection(&collection);
  if (FAILED(hr) || !collection)
    return false;

  auto tryFamily = [&](const std::wstring &name) -> bool {
    UINT32 index = 0;
    BOOL exists = FALSE;
    hr = collection->FindFamilyName(name.c_str(), &index, &exists);
    if (FAILED(hr) || !exists)
      return false;

    ComPtr<IDWriteFontFamily> family;
    hr = collection->GetFontFamily(index, &family);
    if (FAILED(hr) || !family)
      return false;

    ComPtr<IDWriteFont> font;
    hr = family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                      DWRITE_FONT_STYLE_NORMAL, &font);
    if (FAILED(hr) || !font)
      return false;

    hr = font->CreateFontFace(&_dwrite->fontFace);
    if (FAILED(hr) || !_dwrite->fontFace)
      return false;

    DWRITE_FONT_METRICS metrics{};
    _dwrite->fontFace->GetMetrics(&metrics);
    _dwrite->designUnitsPerEm = metrics.designUnitsPerEm;
    return _dwrite->designUnitsPerEm > 0;
  };

  if (!fontFamily.empty() && tryFamily(fontFamily))
    return true;
  if (tryFamily(L"Segoe UI"))
    return true;
  return tryFamily(L"Arial");
}

void RKFontAtlas::setPixelSize(float pixelSize)
{
  _pixelSize = pixelSize;
}

std::uint16_t RKFontAtlas::glyphIndexForCodepoint(std::uint32_t codepoint) const
{
  if (!_dwrite || !_dwrite->fontFace)
    return 0;

  UINT16 glyph = 0;
  const HRESULT hr = _dwrite->fontFace->GetGlyphIndices(&codepoint, 1, &glyph);
  if (FAILED(hr))
    return 0;
  return glyph;
}

bool RKFontAtlas::glyphMetrics(std::uint16_t glyphIndex, int &outWidth, int &outHeight, int &outBearingX,
                               int &outBearingY, float &outAdvanceX, float &outAdvanceY) const
{
  outWidth = outHeight = outBearingX = outBearingY = 0;
  outAdvanceX = outAdvanceY = 0.0f;

  if (!_dwrite || !_dwrite->factory || !_dwrite->fontFace || _pixelSize <= 0.0f || glyphIndex == 0)
    return false;

  const UINT16 glyph = glyphIndex;
  DWRITE_GLYPH_RUN run{};
  run.fontFace = _dwrite->fontFace.Get();
  run.fontEmSize = _pixelSize;
  run.glyphIndices = &glyph;
  run.glyphCount = 1;
  run.isSideways = FALSE;
  run.bidiLevel = 0;

  ComPtr<IDWriteGlyphRunAnalysis> analysis;
  HRESULT hr = _dwrite->factory->CreateGlyphRunAnalysis(
      &run, 1.0f, nullptr, DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL, 0.0f, 0.0f,
      &analysis);
  if (FAILED(hr) || !analysis)
    return false;

  RECT bounds{};
  hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds);
  if (FAILED(hr))
    return false;

  outWidth = bounds.right - bounds.left;
  outHeight = bounds.bottom - bounds.top;
  outBearingX = bounds.left;
  outBearingY = bounds.top;

  DWRITE_GLYPH_METRICS gm{};
  hr = _dwrite->fontFace->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE);
  if (SUCCEEDED(hr) && _dwrite->designUnitsPerEm > 0)
  {
    const float scale = _pixelSize / static_cast<float>(_dwrite->designUnitsPerEm);
    outAdvanceX = static_cast<float>(gm.advanceWidth) * scale;
    // Horizontal layout only — match Qt QRawFont advances().y() == 0.
    // advanceHeight is the vertical-text advance (≈ em size) and must not move the pen.
    outAdvanceY = 0.0f;
  }

  return outWidth > 0 && outHeight > 0;
}

RKFontAtlas::GlyphBitmap RKFontAtlas::mapForGlyph(std::uint16_t glyphIndex) const
{
  GlyphBitmap bitmap;
  if (!_dwrite || !_dwrite->factory || !_dwrite->fontFace || _pixelSize <= 0.0f || glyphIndex == 0)
    return bitmap;

  const UINT16 glyph = glyphIndex;
  DWRITE_GLYPH_RUN run{};
  run.fontFace = _dwrite->fontFace.Get();
  run.fontEmSize = _pixelSize;
  run.glyphIndices = &glyph;
  run.glyphCount = 1;
  run.isSideways = FALSE;
  run.bidiLevel = 0;

  ComPtr<IDWriteGlyphRunAnalysis> analysis;
  HRESULT hr = _dwrite->factory->CreateGlyphRunAnalysis(
      &run, 1.0f, nullptr, DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL, 0.0f, 0.0f,
      &analysis);
  if (FAILED(hr) || !analysis)
    return bitmap;

  RECT bounds{};
  hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &bounds);
  if (FAILED(hr))
    return bitmap;

  const int w = bounds.right - bounds.left;
  const int h = bounds.bottom - bounds.top;
  if (w <= 0 || h <= 0)
    return bitmap;

  std::vector<BYTE> alpha(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
  hr = analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &bounds, alpha.data(),
                                    static_cast<UINT32>(alpha.size()));
  if (FAILED(hr))
    return bitmap;

  bitmap.width = w;
  bitmap.height = h;
  bitmap.bearingX = bounds.left;
  bitmap.bearingY = bounds.top;
  bitmap.pixels.resize(alpha.size());
  for (size_t i = 0; i < alpha.size(); ++i)
    bitmap.pixels[i] = alpha[i] ? 255 : 0;

  DWRITE_GLYPH_METRICS gm{};
  hr = _dwrite->fontFace->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE);
  if (SUCCEEDED(hr) && _dwrite->designUnitsPerEm > 0)
  {
    const float scale = _pixelSize / static_cast<float>(_dwrite->designUnitsPerEm);
    bitmap.advanceX = static_cast<float>(gm.advanceWidth) * scale;
    bitmap.advanceY = 0.0f; // horizontal layout; see glyphMetrics()
  }

  return bitmap;
}

bool RKFontAtlas::renderSignedDistanceFont(int texture_size)
{
  if (!_dwrite || !_dwrite->fontFace)
    return false;

  const int max_unicode_char = 255;
  std::vector<int> render_list;
  render_list.reserve(max_unicode_char + 1);
  for (int char_idx = 0; char_idx <= max_unicode_char; ++char_idx)
    render_list.push_back(char_idx);

  characters.clear();
  const int sz = fontSizeForTextureSize(texture_size, render_list, characters);
  if (sz <= 0)
    return false;

  setPixelSize(static_cast<float>(sz * scaler));

  std::printf("Rendering characters into a packed %d image\n", texture_size);
  int packed_glyph_index = 0;
  const clock_t tin = clock();
  for (unsigned int char_index = 0; char_index < render_list.size(); ++char_index)
  {
    const std::uint16_t glyph_index = glyphIndexForCodepoint(static_cast<std::uint32_t>(render_list[char_index]));
    if (!glyph_index)
      continue;

    // Must match gen_pack_list skip rules — otherwise packed_glyph_index desyncs and
    // characters[] is accessed out of range (FAST_FAIL_INVALID_ARG in Debug STL).
    int mw = 0, mh = 0, bx = 0, by = 0;
    float ax = 0.0f, ay = 0.0f;
    if (!glyphMetrics(glyph_index, mw, mh, bx, by, ax, ay))
      continue;

    if (packed_glyph_index >= static_cast<int>(characters.size()))
    {
      std::fprintf(stderr, "RKFontAtlas: packed glyph index out of range (%d >= %zu)\n",
                   packed_glyph_index, characters.size());
      break;
    }

    const GlyphBitmap image = mapForGlyph(glyph_index);
    const int w = image.width;
    const int h = image.height;

    if (w > 0 && h > 0 && !image.pixels.empty())
    {
      const int sw = w + scaler * 4;
      const int sh = h + scaler * 4;
      std::vector<unsigned char> smooth_buf(static_cast<size_t>(sw) * static_cast<size_t>(sh), 0);

      for (int j = 0; j < h; ++j)
      {
        for (int i = 0; i < w; ++i)
        {
          smooth_buf[static_cast<size_t>(scaler * 2 + i + (j + scaler * 2) * sw)] =
              image.pixels[static_cast<size_t>(i + j * w)];
        }
      }

      const int sdfw = characters[static_cast<size_t>(packed_glyph_index)].width;
      const int sdfx = characters[static_cast<size_t>(packed_glyph_index)].x;
      const int sdfh = characters[static_cast<size_t>(packed_glyph_index)].height;
      const int sdfy = characters[static_cast<size_t>(packed_glyph_index)].y;
      for (int j = 0; j < sdfh; ++j)
      {
        for (int i = 0; i < sdfw; ++i)
        {
          const int pd_idx = (i + sdfx + (j + sdfy) * texture_size) * 4;
          if (pd_idx < 0 || static_cast<size_t>(pd_idx + 3) >= _pdata.size())
            continue;
          _pdata[static_cast<size_t>(pd_idx)] = get_SDF_radial(
              smooth_buf.data(), sw, sh, i * scaler + (scaler >> 1), j * scaler + (scaler >> 1), 2 * scaler);
          _pdata[static_cast<size_t>(pd_idx + 1)] = _pdata[static_cast<size_t>(pd_idx)];
          _pdata[static_cast<size_t>(pd_idx + 2)] = _pdata[static_cast<size_t>(pd_idx)];
          _pdata[static_cast<size_t>(pd_idx + 3)] = _pdata[static_cast<size_t>(pd_idx)];
          const size_t texIdx = static_cast<size_t>(i + sdfx + (j + sdfy) * texture_size);
          if (texIdx < textureData.size())
            textureData[texIdx] = _pdata[static_cast<size_t>(pd_idx)];
        }
      }
    }
    ++packed_glyph_index;
    std::printf("%i ", render_list[char_index]);
  }
  const clock_t elapsed = clock() - tin;
  std::printf("\nRendering took %1.3f seconds\n\n", 0.001f * static_cast<float>(elapsed));

  return true;
}

int RKFontAtlas::fontSizeForTextureSize(int texture_size, const std::vector<int> &render_list,
                                        std::vector<FontCharacter> &c)
{
  int sz = 4;
  bool keep_going = true;
  while (keep_going)
  {
    sz <<= 1;
    std::printf(" %i", sz);
    keep_going = gen_pack_list(sz, texture_size, render_list, c);
  }
  int sz_step = sz >> 2;
  while (sz_step)
  {
    if (keep_going)
      sz += sz_step;
    else
      sz -= sz_step;
    std::printf(" %i", sz);
    sz_step >>= 1;
    keep_going = gen_pack_list(sz, texture_size, render_list, c);
  }
  while ((!keep_going) && (sz > 1))
  {
    --sz;
    std::printf(" %i", sz);
    keep_going = gen_pack_list(sz, texture_size, render_list, c);
  }
  std::printf("\nResult number of pixels: %i\n", sz);

  if (!keep_going)
    return -1;

  return sz;
}

bool RKFontAtlas::gen_pack_list(int pixel_size, int pack_tex_size, const std::vector<int> &render_list,
                                std::vector<FontCharacter> &packed_glyphs)
{
  packed_glyphs.clear();
  setPixelSize(static_cast<float>(pixel_size * scaler));

  std::vector<int> rectangle_info;
  std::vector<std::vector<int>> packed_glyph_info;
  for (unsigned int char_index = 0; char_index < render_list.size(); ++char_index)
  {
    const std::uint16_t glyph_index =
        glyphIndexForCodepoint(static_cast<std::uint32_t>(render_list[char_index]));
    if (!glyph_index)
      continue;

    int w = 0;
    int h = 0;
    int bearingX = 0;
    int bearingY = 0;
    float advanceX = 0.0f;
    float advanceY = 0.0f;
    if (!glyphMetrics(glyph_index, w, h, bearingX, bearingY, advanceX, advanceY))
      continue;

    FontCharacter add_me{};

    const int sw = w + scaler * 4;
    const int sh = h + scaler * 4;
    const int sdfw = sw / scaler;
    const int sdfh = sh / scaler;
    rectangle_info.push_back(sdfw);
    rectangle_info.push_back(sdfh);

    add_me.ID = render_list[char_index];
    add_me.width = sdfw;
    add_me.height = sdfh;
    add_me.x = -1;
    add_me.y = -1;
    add_me.xoff = static_cast<float>(roundToInt(bearingX));
    add_me.yoff = static_cast<float>(-roundToInt(bearingY));
    add_me.xadv = static_cast<float>(roundToInt(advanceX));
    add_me.yadv = 0.0f; // horizontal runs; ignore DirectWrite advanceHeight

    add_me.xoff = add_me.xoff / scaler - 1.5f;
    add_me.yoff = add_me.yoff / scaler + 1.5f;
    add_me.xadv = add_me.xadv / scaler;
    packed_glyphs.push_back(add_me);
  }

  const bool dont_allow_rotation = false;
  BinPacker bp;
  bp.Pack(rectangle_info, packed_glyph_info, pack_tex_size, dont_allow_rotation);
  if (packed_glyph_info.size() == 1)
  {
    const unsigned int lim = static_cast<unsigned int>(packed_glyph_info[0].size());
    for (unsigned int i = 0; i < lim; i += 4)
    {
      const unsigned int idx = static_cast<unsigned int>(packed_glyph_info[0][i + 0]);
      packed_glyphs[idx].x = packed_glyph_info[0][i + 1];
      packed_glyphs[idx].y = packed_glyph_info[0][i + 2];
    }
    return true;
  }
  return false;
}

int RKFontAtlas::save_png_SDFont(const char *orig_filename, const char *font_name, int img_width,
                                 int img_height, const std::vector<unsigned char> &img_data,
                                 const std::vector<FontCharacter> &packed_glyphs)
{
  (void)orig_filename;
  (void)font_name;
  (void)img_width;
  (void)img_height;
  (void)img_data;
  (void)packed_glyphs;
  return 0;
}

unsigned char RKFontAtlas::get_SDF_radial(unsigned char *fontmap, int w, int h, int x, int y,
                                          int max_radius)
{
  float d2 = max_radius * max_radius + 1.0f;
  unsigned char v = fontmap[x + y * w];
  for (int radius = 1; (radius <= max_radius) && (radius * radius < d2); ++radius)
  {
    int line, lo, hi;
    line = y - radius;
    if ((line >= 0) && (line < h))
    {
      lo = x - radius;
      hi = x + radius;
      if (lo < 0)
        lo = 0;
      if (hi >= w)
        hi = w - 1;
      int idx = line * w + lo;
      for (int i = lo; i <= hi; ++i)
      {
        if (fontmap[idx] != v)
        {
          const float nx = static_cast<float>(i - x);
          const float ny = static_cast<float>(line - y);
          const float nd2 = nx * nx + ny * ny;
          if (nd2 < d2)
            d2 = nd2;
        }
        ++idx;
      }
    }
    line = y + radius;
    if ((line >= 0) && (line < h))
    {
      lo = x - radius;
      hi = x + radius;
      if (lo < 0)
        lo = 0;
      if (hi >= w)
        hi = w - 1;
      int idx = line * w + lo;
      for (int i = lo; i <= hi; ++i)
      {
        if (fontmap[idx] != v)
        {
          const float nx = static_cast<float>(i - x);
          const float ny = static_cast<float>(line - y);
          const float nd2 = nx * nx + ny * ny;
          if (nd2 < d2)
            d2 = nd2;
        }
        ++idx;
      }
    }
    line = x - radius;
    if ((line >= 0) && (line < w))
    {
      lo = y - radius + 1;
      hi = y + radius - 1;
      if (lo < 0)
        lo = 0;
      if (hi >= h)
        hi = h - 1;
      int idx = lo * w + line;
      for (int i = lo; i <= hi; ++i)
      {
        if (fontmap[idx] != v)
        {
          const float nx = static_cast<float>(line - x);
          const float ny = static_cast<float>(i - y);
          const float nd2 = nx * nx + ny * ny;
          if (nd2 < d2)
            d2 = nd2;
        }
        idx += w;
      }
    }
    line = x + radius;
    if ((line >= 0) && (line < w))
    {
      lo = y - radius + 1;
      hi = y + radius - 1;
      if (lo < 0)
        lo = 0;
      if (hi >= h)
        hi = h - 1;
      int idx = lo * w + line;
      for (int i = lo; i <= hi; ++i)
      {
        if (fontmap[idx] != v)
        {
          const float nx = static_cast<float>(line - x);
          const float ny = static_cast<float>(i - y);
          const float nd2 = nx * nx + ny * ny;
          if (nd2 < d2)
            d2 = nd2;
        }
        idx += w;
      }
    }
  }
  d2 = std::sqrt(d2);
  if (v == 0)
    d2 = -d2;
  d2 *= 127.5f / max_radius;
  d2 += 127.5f;
  if (d2 < 0.0f)
    d2 = 0.0f;
  if (d2 > 255.0f)
    d2 = 255.0f;
  return static_cast<unsigned char>(d2 + 0.5f);
}

std::vector<RKInPerInstanceAttributesText> RKFontAtlas::buildMeshWithString(float4 position, float4 scale,
                                                                            RKString text,
                                                                            RKTextAlignment alignment)
{
  return buildMeshWithString(position, scale, text.toStdWString(), alignment);
}

std::vector<RKInPerInstanceAttributesText> RKFontAtlas::buildMeshWithString(float4 position, float4 scale,
                                                                            const std::wstring &text,
                                                                            RKTextAlignment alignment)
{
  float x = 0.0f;
  float y = 0.0f;
  const float sx = 1.0f;
  const float sy = 1.0f;

  std::vector<RKInPerInstanceAttributesText> subdata{};
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
  bool hasRect = false;

  for (wchar_t ch : text)
  {
    const int code = static_cast<int>(ch);
    if (code < 0 || code > 255)
      continue;
    const int id = characterIndex[static_cast<size_t>(code)];
    if (id < 0 || id > 255)
      continue;

    const float x2 = x + characters[static_cast<size_t>(id)].xoff * sx;
    const float y2 = -y - characters[static_cast<size_t>(id)].yoff * sy;
    const float w = characters[static_cast<size_t>(id)].width * sx;
    const float h = characters[static_cast<size_t>(id)].height * sy;

    x += characters[static_cast<size_t>(id)].xadv * sx;
    y += characters[static_cast<size_t>(id)].yadv * sy;

    const float4 vertex = float4(x2, y2, w, h);
    if (!hasRect)
    {
      minX = x2;
      minY = y2;
      maxX = x2 + w;
      maxY = y2 + h;
      hasRect = true;
    }
    else
    {
      minX = std::min(minX, x2);
      minY = std::min(minY, y2);
      maxX = std::max(maxX, x2 + w);
      maxY = std::max(maxY, y2 + h);
    }

    const float4 uv = float4(characters[static_cast<size_t>(id)].x / width,
                             characters[static_cast<size_t>(id)].y / height,
                             characters[static_cast<size_t>(id)].width / width,
                             characters[static_cast<size_t>(id)].height / height);

    subdata.push_back(RKInPerInstanceAttributesText(position, scale, vertex, uv));
  }

  const float centerX = hasRect ? 0.5f * (minX + maxX) : 0.0f;
  const float centerY = hasRect ? 0.5f * (minY + maxY) : 0.0f;

  float2 shift(0.0f, 0.0f);
  switch (alignment)
  {
  case RKTextAlignment::center:
  case RKTextAlignment::multiple_values:
    shift = float2(0.0f, 0.0f);
    break;
  case RKTextAlignment::left:
    shift = float2(-centerX, 0.0f);
    break;
  case RKTextAlignment::right:
    shift = float2(centerX, 0.0f);
    break;
  case RKTextAlignment::top:
    shift = float2(0.0f, centerY);
    break;
  case RKTextAlignment::bottom:
    shift = float2(0.0f, -centerY);
    break;
  case RKTextAlignment::topLeft:
    shift = float2(-centerX, centerY);
    break;
  case RKTextAlignment::topRight:
    shift = float2(centerX, centerY);
    break;
  case RKTextAlignment::bottomLeft:
    shift = float2(-centerX, -centerY);
    break;
  case RKTextAlignment::bottomRight:
    shift = float2(centerX, -centerY);
    break;
  }

  for (RKInPerInstanceAttributesText &subdataText : subdata)
  {
    subdataText.vertexCoordinatesData.x -= centerX;
    subdataText.vertexCoordinatesData.y -= centerY;
    subdataText.vertexCoordinatesData.x += shift.x;
    subdataText.vertexCoordinatesData.y += shift.y;

    subdataText.vertexCoordinatesData.x /= 50.0f;
    subdataText.vertexCoordinatesData.y /= 50.0f;
    subdataText.vertexCoordinatesData.z /= 50.0f;
    subdataText.vertexCoordinatesData.w /= 50.0f;
  }

  return subdata;
}
