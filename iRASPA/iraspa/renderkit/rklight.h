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

#include <foundationkit.h>
#include <symmetrykit.h>
#include <mathkit.h>
#include "rkcolor.h"
#include "binaryarchive.h"

#include <memory>
#include <vector>

/// Whether position() names a direction or a location, and which falloff applies. Mirrors
/// `lightType` in the HLSL Light struct; keep the raw values in step with it.
///
/// Archived, so the width is int64_t like every other enum a document holds: the reader takes
/// eight bytes for it, and Cocoa's Int is eight bytes wide.
enum class RKLightType: int64_t
{
  directional = 0, point = 1, spot = 2
};

/// A named set of lights. The emulations translate the documented defaults of other molecular
/// viewers into this model; they do not reproduce those renderers, so treat them as a close
/// starting point rather than a pixel match. `custom` is what one ends up with after editing a
/// light, not something one asks for, so it is absent from presets().
enum class RKLightStyle: int64_t
{
  custom = -1,

  /// Raw value zero has always stood for the bare camera light, so documents written before this
  /// style was named Camera still decode to the rig they were saved with.
  camera = 0,
  vmd = 1,
  chimeraX = 2,
  pymol = 3,
  jmol = 4,
  studio = 5,
  rembrandt = 6,
  publication = 7,
  standard = 8   // Cocoa's `default`, which is a keyword here
};

class RKLight
{
public:
  /// The photographic roles, in the slot order of a rig and of the inspector boxes.
  enum class Role: int
  {
    camera = 0, key = 1, fill = 2, side = 3, rim = 4, backlight = 5, hair = 6, butterfly = 7
  };

  static constexpr size_t numberOfRoles = 8;

  RKLight();
  RKLight(RKLightType type, bool isEnabled);

  RKLightType type() const {return _type;}
  void setType(RKLightType type) {_type = type;}
  /// Directional lights carry a direction in position(), the other two a location, which is the
  /// distinction the shaders make through position.w.
  bool isPositional() const {return _type != RKLightType::directional;}
  bool isEnabled() const {return _isEnabled;}
  void setEnabled(bool isEnabled) {_isEnabled = isEnabled;}

  /// Whether this light is able to put anything into shadow.
  ///
  /// A directional light shining along the view axis is not: it travels with the line of sight, so
  /// anything that would stand between it and a surface stands between the eye and that surface
  /// too, and is therefore what the eye sees instead. The legacy camera-light rig is made entirely
  /// of such lights, so asking this first lets the shadow pass be skipped for it rather than traced
  /// and found to have changed nothing.
  ///
  /// The tolerance scales with the distance because the position is a direction here, and only its
  /// bearing matters. Under perspective the off-axis pixels see a light of this kind at a slight
  /// angle, so a shadow of a pixel or two is given up in exchange for skipping the pass.
  bool castsShadows() const;

  double4 position() const {return _position;}
  void setPosition(double4 position) {_position = position;}
  RKColor ambientColor()  const {return _ambientColor;}
  void setAmbientColor(RKColor color) {_ambientColor = color;}
  RKColor diffuseColor()  const {return _diffuseColor;}
  void setDiffuseColor(RKColor color) {_diffuseColor = color;}
  RKColor specularColor()  const {return _specularColor;}
  void setSpecularColor(RKColor color) {_specularColor = color;}
  double ambientIntensity()  const {return _ambientIntensity;}
  void setAmbientIntensity(double intensity) {_ambientIntensity = intensity;}
  double diffuseIntensity()  const {return _diffuseIntensity;}
  void setDiffuseIntensity(double intensity) {_diffuseIntensity = intensity;}
  double specularIntensity()  const {return _specularIntensity;}
  void setSpecularIntensity(double intensity) {_specularIntensity = intensity;}
  double shininess() const {return _shininess;}
  void setShininess(double shininess) {_shininess = shininess;}
  double constantAttenuation()  const {return _constantAttenuation;}
  void setConstantAttenuation(double attenuation) {_constantAttenuation = attenuation;}
  double linearAttenuation() const {return _linearAttenuation;}
  void setLinearAttenuation(double attenuation) {_linearAttenuation = attenuation;}
  double quadraticAttenuation() const {return _quadraticAttenuation;}
  void setQuadraticAttenuation(double attenuation) {_quadraticAttenuation = attenuation;}
  double3 spotDirection() const {return _spotDirection;}
  void setSpotDirection(double3 direction) {_spotDirection = direction;}
  double spotCutoff() const {return _spotCutoff;}
  void setSpotCutoff(double cutoff) {_spotCutoff = cutoff;}
  double spotExponent() const {return _spotExponent;}
  void setSpotExponent(double exponent) {_spotExponent = exponent;}

  /// The eight lights a project starts with, one per Role. Positions are in eye space, so x runs to
  /// the right, y upwards and z towards the viewer, which puts a light behind the structure at
  /// negative z. Only the camera light starts on, being the look iRASPA has always opened with. The
  /// other seven are placed where they conventionally sit relative to the viewer, so that enabling
  /// one gives a usable result without first having to dial in a position. None of them carries
  /// ambient, which belongs to the scene instead.
  static std::vector<std::shared_ptr<RKLight>> standardRig();

  /// The lights of a named style, or an empty vector for `custom`, which stands for whatever is
  /// already there.
  static std::vector<std::shared_ptr<RKLight>> rig(RKLightStyle style);

  /// The rig a new document starts with.
  static std::vector<std::shared_ptr<RKLight>> defaultRig();

  /// The style that lights the scene the same way as these lights, scene ambient and occlusion
  /// strength, or `custom` when none does. Rechecked after every edit, the way the representation
  /// style in the appearance inspector is, so putting a value back by hand brings the name back
  /// instead of leaving it stuck on custom.
  static RKLightStyle styleMatching(const std::vector<std::shared_ptr<RKLight>> &lights,
                                    double sceneAmbientIntensity, RKColor sceneAmbientColor,
                                    double ambientOcclusionStrength);

  /// Whether two lights light the scene identically. A light that is off cannot, whatever it
  /// carries, so switching one on, playing with it and switching it off again returns to the named
  /// style. The ambient pair is left out because it no longer reaches the shaders.
  bool matchesInEffect(const RKLight &other) const;
private:
  [[maybe_unused]] int64_t _versionNumber = 2;
  RKLightType _type = RKLightType::directional;
  // Off by default: the uniform block holds a slot for every light the shaders can take, and a
  // project turns on the ones its rig actually uses.
  bool _isEnabled = false;
  /// Defined in eye space, so the light travels with the camera. w is derived from the type when the
  /// uniforms are built, so it is left at zero here.
  ///
  /// On the view axis, which is the camera light. standardRig() overrides this for every other role;
  /// note that a light sitting exactly at the camera reduces the diffuse term to N·V, so it flattens
  /// the surfaces whose shape one is trying to read. That is what the off-axis key light is for.
  double4 _position = double4(0, 0, 100.0, 0.0);
  // Not used for rendering, and not editable. Ambient describes the environment rather than one
  // lamp, so it is a property of the scene. The pair is kept because documents written before
  // ambient moved still hold it, and the move reads it to recover the ambient level such a document
  // was saved with.
  RKColor _ambientColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _diffuseColor = RKColor::fromRgb(255, 255, 255, 255);
  RKColor _specularColor = RKColor::fromRgb(255, 255, 255, 255);
  double _ambientIntensity = 0.0;
  double _diffuseIntensity = 1.0;
  double _specularIntensity = 1.0;
  double _shininess = 4.0;
  // No distance falloff by default. Structures are modelled in Angstrom, so a light placed a hundred
  // units from the origin would be attenuated to nothing by a quadratic term, and switching a light
  // from directional to point would simply turn it black.
  double _constantAttenuation = 1.0;
  double _linearAttenuation = 0.0;
  double _quadraticAttenuation = 0.0;
  // Aimed into the scene, away from the viewer, which is the useful direction for a spot placed in
  // front of the structure. Only consulted for the spot type.
  double3 _spotDirection = double3(0.0, 0.0, -1.0);
  // The half angle of the cone in degrees. A cutoff of one degree, which is what this was before the
  // spot type was reachable, gives a cone too narrow to see.
  double _spotCutoff = 45.0;
  double _spotExponent = 1.0;

  friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<RKLight> &);
  friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<RKLight> &);
};

BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<RKLight> &);
BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<RKLight> &);

/// The presets a style can be asked for, in the order the inspector lists them. `custom` is absent:
/// it is what one ends up with after editing a light rather than something one selects.
const std::vector<RKLightStyle> &RKLightStylePresets();

/// Part of a style, both applied when one is selected and required to match for the style to still
/// be named. Zero keeps ambient occlusion to the ambient term, which is the physically correct
/// treatment and what a rig with several lights wants. Camera asks for one, the legacy iRASPA
/// behaviour of letting occlusion darken the direct light too: a lone headlight leaves almost
/// nothing for occlusion to act on otherwise, since it lights very nearly what it sees.
double RKLightStyleAmbientOcclusionStrength(RKLightStyle style);

/// Part of a style in the same way the rig is. Every preset leaves ambient neutral at one: it
/// multiplies the material's own ambient, which the representation style owns, so anything less
/// would darken every representation style by that factor and take Fancy, which is lit by ambient
/// alone, close to black.
double RKLightStyleSceneAmbientIntensity(RKLightStyle style);
RKColor RKLightStyleSceneAmbientColor(RKLightStyle style);

const char *RKLightStyleDisplayName(RKLightStyle style);
const char *RKLightRoleDisplayName(RKLight::Role role);
