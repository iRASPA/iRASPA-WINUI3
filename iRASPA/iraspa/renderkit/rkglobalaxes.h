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

#include <cmath>
#include "rkcolor.h"
#include <vector>
#include <type_traits>
#include <foundationkit.h>
#include "rkrect.h"
#include "mathkit.h"

struct RKGlobalAxesUniforms;

class RKGlobalAxes
{
public:
  RKGlobalAxes();

  enum class Style: int64_t
  {
    defaultStyle = 0, thickRGB = 1, thick = 2, thinRGB = 3, thin = 4, beamArrowRGB = 5, beamArrow = 6,
    beamRGB = 7, beam = 8, squashedRGB = 9, squashed = 10
  };

  enum class Position: int64_t
  {
    none = 0, bottomLeft = 1, midLeft = 2, topLeft = 3, midTop = 4, topRight = 5,
    midRight = 6, bottomRight = 7, midBottom = 8, center = 9
  };

  enum class CenterType: int64_t
  {
    cube = 0, sphere = 1
  };

  enum class BackgroundStyle: int64_t
  {
    none = 0, filledCircle = 1, fillesSquare = 2, filledRoundedSquare = 3,
    circle = 4, square = 5, roundedSquare = 6
  };

  double totalAxesSize();

  RKGlobalAxes::Style style() {return _style;}
  void setStyle(RKGlobalAxes::Style style);
  RKGlobalAxes::Position position() {return _position;}
  void setPosition(RKGlobalAxes::Position position) {_position = position;}

  double borderOffsetScreenFraction() {return _borderOffsetScreenFraction;}
  void setBorderOffsetScreenFraction(double fraction) {_borderOffsetScreenFraction = fraction;}
  double sizeScreenFraction() {return _sizeScreenFraction;}
  void setSizeScreenFraction(double fraction) {_sizeScreenFraction = fraction;}

  RKColor axesBackgroundColor() {return _axesBackgroundColor;}
  void setAxesBackgroundColor(RKColor color) {_axesBackgroundColor = color;}
  double axesBackgroundAdditionalSize() {return _axesBackgroundAdditionalSize;}
  void setAxesBackgroundAdditionalSize(double size) {_axesBackgroundAdditionalSize = size;}
  RKGlobalAxes::BackgroundStyle axesBackgroundStyle() {return _axesBackgroundStyle;}
  void setAxesBackgroundStyle(RKGlobalAxes::BackgroundStyle style) {_axesBackgroundStyle = style;}

  double3 textScale() {return _textScale;}
  void setTextScaleX(double scale) {_textScale.x = scale;}
  void setTextScaleY(double scale) {_textScale.y = scale;}
  void setTextScaleZ(double scale) {_textScale.z = scale;}
  RKColor textColorX() {return _textColorX;}
  void setTextColorX(RKColor color) {_textColorX = color;}
  RKColor textColorY() {return _textColorY;}
  void setTextColorY(RKColor color) {_textColorY = color;}
  RKColor textColorZ() {return _textColorZ;}
  void setTextColorZ(RKColor color) {_textColorZ = color;}
  double3 textDisplacementX() {return _textDisplacementX;}
  void setXTextDisplacementX(double displacement) {_textDisplacementX.x = displacement;}
  void setXTextDisplacementY(double displacement) {_textDisplacementX.y = displacement;}
  void setXTextDisplacementZ(double displacement) {_textDisplacementX.z = displacement;}
  double3 textDisplacementY() {return _textDisplacementY;}
  void setYTextDisplacementX(double displacement) {_textDisplacementY.x = displacement;}
  void setYTextDisplacementY(double displacement) {_textDisplacementY.y = displacement;}
  void setYTextDisplacementZ(double displacement) {_textDisplacementY.z = displacement;}
  double3 textDisplacementZ() {return _textDisplacementZ;}
  void setZTextDisplacementX(double displacement) {_textDisplacementZ.x = displacement;}
  void setZTextDisplacementY(double displacement) {_textDisplacementZ.y = displacement;}
  void setZTextDisplacementZ(double displacement) {_textDisplacementZ.z = displacement;}

  double centerScale() {return _centerScale;}
  double textOffset() {return _textOffset;}
  double axisScale() {return _axisScale;}

  bool HDR() {return _HDR;}
  double exposure() {return _exposure;}

  int NumberOfSectors() {return _NumberOfSectors;}

  double shaftLength() {return _shaftLength;}
  double shaftWidth() {return _shaftWidth;}

  double tipLength() {return _tipLength;}
  double tipWidth() {return _tipWidth;}

  RKGlobalAxes::CenterType centerType() {return _centerType;}
  bool tipVisibility() {return _tipVisibility;}

  double aspectRatio() {return _aspectRatio;}

  RKColor centerAmbientColor() {return _centerAmbientColor;}
  RKColor centerDiffuseColor() {return _centerDiffuseColor;}
  RKColor centerSpecularColor() {return _centerSpecularColor;}
  double centerShininess() {return _centerShininess;}

  RKColor axisXAmbientColor() {return _axisXAmbientColor;}
  RKColor axisXDiffuseColor() {return _axisXDiffuseColor;}
  RKColor axisXSpecularColor() {return _axisXSpecularColor;}
  double axisXShininess() {return _axisXShininess;}

  RKColor axisYAmbientColor() {return _axisYAmbientColor;}
  RKColor axisYDiffuseColor() {return _axisYDiffuseColor;}
  RKColor axisYSpecularColor() {return _axisYSpecularColor;}
  double axisYShininess() {return _axisYShininess;}

  RKColor axisZAmbientColor() {return _axisZAmbientColor;}
  RKColor axisZDiffuseColor() {return _axisZDiffuseColor;}
  RKColor axisZSpecularColor() {return _axisZSpecularColor;}
  double axisZShininess() {return _axisZShininess;}
private:
  int64_t _versionNumber{1};

  RKGlobalAxes::Style _style  = RKGlobalAxes::Style::defaultStyle;
  RKGlobalAxes::Position _position  = RKGlobalAxes::Position::bottomLeft;

  double _borderOffsetScreenFraction = 1.0/32.0;
  double _sizeScreenFraction = 1.0/5.0;

  RKColor _axesBackgroundColor = RKColor::fromRgb(204, 204, 204, 48);
  double _axesBackgroundAdditionalSize = 0.0;
  BackgroundStyle _axesBackgroundStyle = BackgroundStyle::filledCircle;

  double3 _textScale = double3(1.0,1.0,1.0);
  RKColor _textColorX = RKColor(0,0,0,1.0);
  RKColor _textColorY= RKColor(0,0,0,1.0);
  RKColor _textColorZ = RKColor(0,0,0,1.0);
  double3 _textDisplacementX = double3(0.0,0.0,0.0);
  double3 _textDisplacementY = double3(0.0,0.0,0.0);
  double3 _textDisplacementZ = double3(0.0,0.0,0.0);

  bool _HDR = true;
  double _exposure = 1.5;

  int _NumberOfSectors = 41;

  double _centerScale = 0.5;
  double _textOffset = 0.0;
  double _axisScale = 5.0;

  double _shaftLength = 2.0/3.0;
  double _shaftWidth = 1.0/24.0;

  double _tipLength = 2.0/3.0;
  double _tipWidth = 1.0/24.0;

  RKGlobalAxes::CenterType _centerType = RKGlobalAxes::CenterType::cube;
  bool _tipVisibility = true;

  double _aspectRatio = 1.0;

  RKColor _centerAmbientColor = RKColor::fromRgb(51,51,51,255);
  RKColor _centerDiffuseColor = RKColor::fromRgb(0,255,255,255);
  RKColor _centerSpecularColor = RKColor::fromRgb(255,255,255,255);
  double _centerShininess = 4.0;

  RKColor _axisXAmbientColor = RKColor::fromRgb(51,51,51,255);
  RKColor _axisXDiffuseColor = RKColor(0.0,255,255,255);
  RKColor _axisXSpecularColor = RKColor::fromRgb(255,255,255,255);
  double _axisXShininess = 4.0;

  RKColor _axisYAmbientColor = RKColor::fromRgb(51,51,51,255);
  RKColor _axisYDiffuseColor = RKColor::fromRgb(0,255,255,255);
  RKColor _axisYSpecularColor = RKColor::fromRgb(255,255,255,255);
  double _axisYShininess = 4.0;

  RKColor _axisZAmbientColor = RKColor::fromRgb(51,51,51,255);
  RKColor _axisZDiffuseColor = RKColor(0.0,255,255,255);
  RKColor _axisZSpecularColor = RKColor::fromRgb(255,255,255,255);
  double _axisZShininess = 4.0;

  friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<RKGlobalAxes> &);
  friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<RKGlobalAxes> &);
};

