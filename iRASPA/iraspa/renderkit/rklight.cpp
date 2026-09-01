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

#include "rklight.h"

#include <algorithm>
#include <cmath>

RKLight::RKLight()
{

}

RKLight::RKLight(RKLightType type, bool isEnabled): _type(type), _isEnabled(isEnabled)
{

}

bool RKLight::castsShadows() const
{
  if(!_isEnabled)
  {
    return false;
  }
  if(_type != RKLightType::directional)
  {
    return true;
  }

  // eye space: z is the view axis, so a light with no lateral offset shines straight down it
  const double lateral = std::max(std::abs(_position.x), std::abs(_position.y));
  return !(_position.z > 0.0 && lateral < 1.0e-3 * _position.z);
}

namespace
{
  /// One switched-on slot of a lighting style: the role it fills, where it sits in eye space, and
  /// the two levels plus the specular exponent it contributes. Ambient is absent because it belongs
  /// to the scene, and a style names it through RKLightStyleSceneAmbientIntensity.
  struct StyleLight
  {
    RKLight::Role role;
    double x;
    double y;
    double z;
    double diffuse;
    double specular;
    double shininess;
    // Only the Default rig tints its lights; every emulation leaves these white because the
    // programs being emulated light with white.
    RKColor diffuseColor = RKColor::fromRgb(255, 255, 255, 255);
    RKColor specularColor = RKColor::fromRgb(255, 255, 255, 255);
  };

  RKColor colorFromF(double red, double green, double blue)
  {
    const auto channel = [](double value) {
      return int(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };
    return RKColor::fromRgb(channel(red), channel(green), channel(blue), 255);
  }

  /// The slots a style switches on; every other role stays off. Where another program's light is
  /// given as the direction it shines, the position here is that direction negated, and the sign
  /// conventions have been resolved so the highlight lands where that program puts it, upper left in
  /// every case.
  ///
  /// Note that a light's shininess adds to the material's own, so these exponents land near rather
  /// than exactly on the published figure.
  ///
  /// No light here carries ambient: ambient describes the environment, so it is a property of the
  /// scene and a style sets it through RKLightStyleSceneAmbientIntensity. Note also that the ambient
  /// level each emulated program publishes is not reproducible at all, because in this renderer the
  /// amount of ambient a surface takes belongs to its material and the representation style owns
  /// that: Default asks for 0.2 while Fancy asks for 1.0 and zeroes diffuse outright, so a Fancy
  /// atom takes its whole colour from ambient. What distinguishes these styles is therefore the
  /// direction, level and specular of their key, fill and rim.
  std::vector<StyleLight> styleLights(RKLightStyle style)
  {
    switch(style)
    {
    case RKLightStyle::custom:
    case RKLightStyle::camera:
      return {};

    // A four-light rig meant to look good rather than to imitate anything. The key sits high and to
    // the left for shape, a dim fill opposite it keeps the shadow side readable, and a rim from
    // behind draws a bright edge that lifts the structure off the background. The key is warmed and
    // the fill and rim cooled by a few percent, which reads as depth; the tint is kept small on
    // purpose because atom colours carry meaning and must stay recognisable. The camera light
    // carries only a little diffuse, so a face turned straight at the viewer is never unlit while it
    // is rotated.
    case RKLightStyle::standard:
    {
      const RKColor warm = colorFromF(1.00, 0.96, 0.90);
      const RKColor cool = colorFromF(0.88, 0.93, 1.00);
      const RKColor coolNeutral = colorFromF(0.94, 0.97, 1.00);
      const RKColor white = colorFromF(1.0, 1.0, 1.0);

      return {{RKLight::Role::camera,   0.0,   0.0, 100.0, 0.12, 0.00,  4.0},
              {RKLight::Role::key,    -45.0,  55.0,  85.0, 0.80, 0.55, 28.0, warm, white},
              {RKLight::Role::fill,    70.0, -20.0,  65.0, 0.28, 0.00,  4.0, cool, white},
              {RKLight::Role::rim,     55.0,  30.0, -78.0, 0.32, 0.70, 44.0, coolNeutral, coolNeutral}};
    }

    // VMD's Opaque material is diffuse 0.65 and specular 0.50 at a Phong exponent of 40, and lights
    // 0 and 1 are the two of its four that start on. Its material ambient of 0.00, which is what
    // makes the far side of a VMD structure go black, is not something a light can express here.
    case RKLightStyle::vmd:
      return {{RKLight::Role::camera,  -10.0,  10.0, 100.0, 0.65, 0.50, 40.0},
              {RKLight::Role::key,    -100.0, 200.0,  50.0, 0.65, 0.50, 40.0}};

    // ChimeraX simple lighting is key 1.0 and fill 0.5. Its key shines along (1,-1,-1), that is from
    // the upper left front, and its fill along (-0.2,-0.2,-0.959). Only the key carries specular, at
    // the default material shininess of 30.
    case RKLightStyle::chimeraX:
      return {{RKLight::Role::key,  -58.0, 58.0, 58.0, 1.0, 0.3, 30.0},
              {RKLight::Role::fill,  20.0, 20.0, 96.0, 0.5, 0.0, 30.0}};

    // PyMOL is direct 0.45 from the camera and reflect 0.45 for its one movable light, with
    // shininess 55 and specular_intensity 0.5. spec_direct is 0, so the camera light carries no
    // highlight.
    case RKLightStyle::pymol:
      return {{RKLight::Role::camera,   0.0,  0.0, 100.0, 0.45, 0.0, 55.0},
              {RKLight::Role::key,    -40.0, 40.0, 100.0, 0.45, 0.5, 55.0}};

    // Jmol is diffusePercent 84 and specularPercent 22, lit from the upper left front. Its
    // specularExponent of 6 is a power of two, hence 64.
    case RKLightStyle::jmol:
      return {{RKLight::Role::key, -60.0, 60.0, 80.0, 0.84, 0.22, 64.0}};

    // The photographic three-point setup this rig is named for, with the camera light out of the way.
    case RKLightStyle::studio:
      return {{RKLight::Role::key,  -30.0, 40.0, 100.0, 1.00, 0.8, 8.0},
              {RKLight::Role::fill,  60.0, 10.0,  80.0, 0.35, 0.1, 8.0},
              {RKLight::Role::rim,   60.0, 25.0, -70.0, 0.60, 0.9, 8.0}};

    // A single hard key well off axis, with no fill, for high contrast.
    case RKLightStyle::rembrandt:
      return {{RKLight::Role::key, -70.0, 70.0, 60.0, 1.0, 0.6, 16.0}};

    // Even and matte with no highlights at all, for figures where shading and glare hide detail.
    case RKLightStyle::publication:
      return {{RKLight::Role::camera, 0.0, 0.0, 100.0, 0.6, 0.0, 4.0}};
    }
    return {};
  }

  bool colorsMatch(RKColor first, RKColor second)
  {
    const double tolerance = 1.0e-3;
    return std::abs(first.redF() - second.redF()) < tolerance &&
           std::abs(first.greenF() - second.greenF()) < tolerance &&
           std::abs(first.blueF() - second.blueF()) < tolerance;
  }
}

std::vector<std::shared_ptr<RKLight>> RKLight::standardRig()
{
  struct RigLight { double x, y, z, diffuse, specular; };
  const RigLight rig[numberOfRoles] = {
      {   0.0,   0.0,  100.0, 1.0, 1.0},  // camera, straight down the view axis
      { -30.0,  40.0,  100.0, 1.0, 1.0},  // key, up and to the left
      {  60.0,  10.0,   80.0, 0.4, 0.2},  // fill, opposite the key and dim
      { 100.0,   0.0,    0.0, 0.7, 0.3},  // side, level with the structure and square on
      {  60.0,  25.0,  -70.0, 0.6, 0.8},  // rim, behind and to the side, mostly specular
      {   0.0,   0.0, -100.0, 0.8, 0.4},  // backlight, straight behind
      {   0.0, 100.0,  -25.0, 0.6, 0.6},  // hair, above and a little behind
      {   0.0,  80.0,   60.0, 0.9, 0.5}}; // butterfly, high and in front

  std::vector<std::shared_ptr<RKLight>> lights;
  lights.reserve(numberOfRoles);
  for(size_t i = 0; i < numberOfRoles; i++)
  {
    std::shared_ptr<RKLight> light = std::make_shared<RKLight>();
    light->setEnabled(i == 0);
    // w is derived from the light type when the uniforms are built, so it is left at zero here
    light->setPosition(double4(rig[i].x, rig[i].y, rig[i].z, 0.0));
    light->setAmbientIntensity(0.0);
    light->setDiffuseIntensity(rig[i].diffuse);
    light->setSpecularIntensity(rig[i].specular);
    lights.push_back(light);
  }
  return lights;
}

std::vector<std::shared_ptr<RKLight>> RKLight::rig(RKLightStyle style)
{
  if(style == RKLightStyle::custom)
  {
    return {};
  }
  if(style == RKLightStyle::camera)
  {
    return RKLight::standardRig();
  }

  std::vector<std::shared_ptr<RKLight>> lights = RKLight::standardRig();
  for(const std::shared_ptr<RKLight> &light : lights)
  {
    light->setEnabled(false);
  }

  for(const StyleLight &entry : styleLights(style))
  {
    const size_t slot = size_t(entry.role);
    if(slot >= lights.size())
    {
      continue;
    }
    const std::shared_ptr<RKLight> &light = lights[slot];
    light->setEnabled(true);
    light->setType(RKLightType::directional);

    // w is derived from the light type when the uniforms are built, so it is left at zero here
    light->setPosition(double4(entry.x, entry.y, entry.z, 0.0));
    light->setDiffuseIntensity(entry.diffuse);
    light->setSpecularIntensity(entry.specular);
    light->setShininess(entry.shininess);
    light->setDiffuseColor(entry.diffuseColor);
    light->setSpecularColor(entry.specularColor);
  }
  return lights;
}

std::vector<std::shared_ptr<RKLight>> RKLight::defaultRig()
{
  return RKLight::rig(RKLightStyle::standard);
}

bool RKLight::matchesInEffect(const RKLight &other) const
{
  if(_isEnabled != other._isEnabled)
  {
    return false;
  }
  if(!_isEnabled)
  {
    return true;
  }

  const double tolerance = 1.0e-3;
  bool matches = _type == other._type &&
                 std::abs(_position.x - other._position.x) < tolerance &&
                 std::abs(_position.y - other._position.y) < tolerance &&
                 std::abs(_position.z - other._position.z) < tolerance &&
                 std::abs(_diffuseIntensity - other._diffuseIntensity) < tolerance &&
                 std::abs(_specularIntensity - other._specularIntensity) < tolerance &&
                 std::abs(_shininess - other._shininess) < tolerance &&
                 colorsMatch(_diffuseColor, other._diffuseColor) &&
                 colorsMatch(_specularColor, other._specularColor);

  if(matches && _type != RKLightType::directional)
  {
    // only a light placed at a location falls off with distance
    matches = std::abs(_constantAttenuation - other._constantAttenuation) < tolerance &&
              std::abs(_linearAttenuation - other._linearAttenuation) < tolerance &&
              std::abs(_quadraticAttenuation - other._quadraticAttenuation) < tolerance;
  }

  if(matches && _type == RKLightType::spot)
  {
    matches = std::abs(_spotCutoff - other._spotCutoff) < tolerance &&
              std::abs(_spotExponent - other._spotExponent) < tolerance &&
              std::abs(_spotDirection.x - other._spotDirection.x) < tolerance &&
              std::abs(_spotDirection.y - other._spotDirection.y) < tolerance &&
              std::abs(_spotDirection.z - other._spotDirection.z) < tolerance;
  }

  return matches;
}

RKLightStyle RKLight::styleMatching(const std::vector<std::shared_ptr<RKLight>> &lights,
                                    double sceneAmbientIntensity, RKColor sceneAmbientColor,
                                    double ambientOcclusionStrength)
{
  for(RKLightStyle style : RKLightStylePresets())
  {
    const std::vector<std::shared_ptr<RKLight>> candidate = RKLight::rig(style);
    if(candidate.size() != lights.size())
    {
      continue;
    }
    if(std::abs(RKLightStyleAmbientOcclusionStrength(style) - ambientOcclusionStrength) >= 1.0e-3 ||
       std::abs(RKLightStyleSceneAmbientIntensity(style) - sceneAmbientIntensity) >= 1.0e-3 ||
       !colorsMatch(RKLightStyleSceneAmbientColor(style), sceneAmbientColor))
    {
      continue;
    }

    bool everyLightMatches = true;
    for(size_t i = 0; i < candidate.size() && everyLightMatches; i++)
    {
      everyLightMatches = lights[i] && candidate[i]->matchesInEffect(*lights[i]);
    }
    if(everyLightMatches)
    {
      return style;
    }
  }
  return RKLightStyle::custom;
}

const std::vector<RKLightStyle> &RKLightStylePresets()
{
  static const std::vector<RKLightStyle> presets{
      RKLightStyle::standard, RKLightStyle::camera, RKLightStyle::vmd, RKLightStyle::chimeraX,
      RKLightStyle::pymol, RKLightStyle::jmol, RKLightStyle::studio, RKLightStyle::rembrandt,
      RKLightStyle::publication};
  return presets;
}

double RKLightStyleAmbientOcclusionStrength(RKLightStyle style)
{
  return (style == RKLightStyle::camera) ? 1.0 : 0.0;
}

double RKLightStyleSceneAmbientIntensity([[maybe_unused]] RKLightStyle style)
{
  return 1.0;
}

RKColor RKLightStyleSceneAmbientColor([[maybe_unused]] RKLightStyle style)
{
  return RKColor::fromRgb(255, 255, 255, 255);
}

const char *RKLightStyleDisplayName(RKLightStyle style)
{
  switch(style)
  {
  case RKLightStyle::custom: return "Custom";
  case RKLightStyle::standard: return "Default";
  case RKLightStyle::camera: return "Camera";
  case RKLightStyle::vmd: return "VMD";
  case RKLightStyle::chimeraX: return "ChimeraX";
  case RKLightStyle::pymol: return "PyMOL";
  case RKLightStyle::jmol: return "Jmol";
  case RKLightStyle::studio: return "Three-Point Studio";
  case RKLightStyle::rembrandt: return "Rembrandt";
  case RKLightStyle::publication: return "Publication";
  }
  return "Custom";
}

// The field order is Cocoa's RKRenderLight, so that a document written by either reads in both.
BinaryArchive &operator<<(BinaryArchive &stream, const std::shared_ptr<RKLight> &light)
{
  stream << light->_versionNumber;

  stream << light->_isEnabled;
  stream << static_cast<typename std::underlying_type<RKLightType>::type>(light->_type);
  stream << light->_position;
  stream << light->_ambientColor;
  stream << light->_diffuseColor;
  stream << light->_specularColor;
  stream << light->_ambientIntensity;
  stream << light->_diffuseIntensity;
  stream << light->_specularIntensity;
  stream << light->_shininess;
  stream << light->_constantAttenuation;
  stream << light->_linearAttenuation;
  stream << light->_quadraticAttenuation;
  stream << light->_spotDirection;
  stream << light->_spotCutoff;
  stream << light->_spotExponent;

  return stream;
}

BinaryArchive &operator>>(BinaryArchive &stream, std::shared_ptr<RKLight> &light)
{
  int64_t versionNumber;
  stream >> versionNumber;
  if(versionNumber > light->_versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "RKLight");
  }

  if(versionNumber >= 2) // introduced in version 2
  {
    stream >> light->_isEnabled;
    int64_t type;
    stream >> type;
    light->_type = RKLightType(type);
  }

  stream >> light->_position;
  stream >> light->_ambientColor;
  stream >> light->_diffuseColor;
  stream >> light->_specularColor;
  stream >> light->_ambientIntensity;
  stream >> light->_diffuseIntensity;
  stream >> light->_specularIntensity;
  stream >> light->_shininess;
  stream >> light->_constantAttenuation;
  stream >> light->_linearAttenuation;
  stream >> light->_quadraticAttenuation;
  stream >> light->_spotDirection;
  stream >> light->_spotCutoff;
  stream >> light->_spotExponent;

  return stream;
}

const char *RKLightRoleDisplayName(RKLight::Role role)
{
  switch(role)
  {
  case RKLight::Role::camera: return "Camera";
  case RKLight::Role::key: return "Key";
  case RKLight::Role::fill: return "Fill";
  case RKLight::Role::side: return "Side/Split";
  case RKLight::Role::rim: return "Rim/Kicker";
  case RKLight::Role::backlight: return "Backlight/Silhouette";
  case RKLight::Role::hair: return "Hair/Top";
  case RKLight::Role::butterfly: return "Butterfly/Paramount";
  }
  return "";
}
