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

#pragma once

#include "rkstring.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "rkstring.h"
#include <mathkit.h>
#include "rkrenderuniforms.h"

struct FontCharacter
{
  int ID;
  float xadv;   // glyph->advance.x
  float yadv;   // glyph->advance.y
  int width;    // ft_face->glyph->bitmap.width
  int height;   // ft_face->glyph->bitmap.height
  int x;        // x-position in the font-atlas
  int y;        // y-position in the font-atlas
  float xoff;   // glyph->bitmap_left
  float yoff;   // glyph->bitmap_top
};

struct RKFontAtlas
{
  RKFontAtlas(const std::wstring &fontFamily, int texture_size);
  RKFontAtlas(const RKString &fontName, int texture_size);
  ~RKFontAtlas();

  int texture = 0;

  float width;
  float height;

  std::vector<unsigned char> textureData;
  std::vector<unsigned char> _pdata;
  std::vector<FontCharacter> characters;
  std::array<int, 256> characterIndex;

  bool renderSignedDistanceFont(int textureSize);

  int fontSizeForTextureSize(int texture_size, const std::vector<int> &render_list,
                             std::vector<FontCharacter> &characters);
  bool gen_pack_list(int pixel_size, int pack_tex_size, const std::vector<int> &render_list,
                     std::vector<FontCharacter> &packed_glyphs);

  int save_png_SDFont(const char *orig_filename, const char *font_name, int img_width, int img_height,
                      const std::vector<unsigned char> &img_data,
                      const std::vector<FontCharacter> &packed_glyphs);

  unsigned char get_SDF_radial(unsigned char *fontmap, int w, int h, int x, int y, int max_radius);

  std::vector<RKInPerInstanceAttributesText> buildMeshWithString(float4 position, float4 scale,
                                                                 RKString text, RKTextAlignment alignment);
  std::vector<RKInPerInstanceAttributesText> buildMeshWithString(float4 position, float4 scale,
                                                                 const std::wstring &text,
                                                                 RKTextAlignment alignment);

private:
  struct GlyphBitmap
  {
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    float advanceX = 0.0f;
    float advanceY = 0.0f;
    std::vector<unsigned char> pixels;
  };

  bool initDirectWrite(const std::wstring &fontFamily);
  void setPixelSize(float pixelSize);
  std::uint16_t glyphIndexForCodepoint(std::uint32_t codepoint) const;
  GlyphBitmap mapForGlyph(std::uint16_t glyphIndex) const;
  bool glyphMetrics(std::uint16_t glyphIndex, int &outWidth, int &outHeight, int &outBearingX,
                    int &outBearingY, float &outAdvanceX, float &outAdvanceY) const;

  struct DirectWriteState;
  DirectWriteState *_dwrite = nullptr;
  float _pixelSize = 0.0f;
};
