#include "zipreader.h"
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

#include "projectstructure.h"
#include "rkstring.h"
#include "rkcolor.h"
#include <cfloat>
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

RKImage makeSolidImage(const RKColor &color, int size)
{
  RKImage image(size, size, RKImage::Format_ARGB32);
  image.fill(static_cast<uint8_t>(color.red()),
             static_cast<uint8_t>(color.green()),
             static_cast<uint8_t>(color.blue()),
             static_cast<uint8_t>(color.alpha()));
  return image;
}

// Qt and Cocoa each hand this off to a 2D drawing library (QPainter, NSGradient); there is
// no equivalent underneath RKImage, which is a bare RGBA8 buffer, so the two ramps are
// rasterised here. Keeping it that way is deliberate: the kit is linked by the export
// helper as well as the app, and neither Direct2D nor XAML may be dragged into it.
void writeStop(uint8_t *pixel, const RKColor &from, const RKColor &to, double t)
{
  const double clamped = std::clamp(t, 0.0, 1.0);
  // RKColor holds unclamped doubles, so the channels are pinned rather than trusted: a
  // colour outside 0..1 would otherwise wrap round into an unrelated shade.
  const auto mix = [clamped](double a, double b) {
    return static_cast<uint8_t>(std::clamp(std::lround(a + (b - a) * clamped), 0L, 255L));
  };
  pixel[0] = mix(static_cast<double>(from.red()), static_cast<double>(to.red()));
  pixel[1] = mix(static_cast<double>(from.green()), static_cast<double>(to.green()));
  pixel[2] = mix(static_cast<double>(from.blue()), static_cast<double>(to.blue()));
  pixel[3] = mix(static_cast<double>(from.alpha()), static_cast<double>(to.alpha()));
}

// Angle handling matches the Qt build exactly, so that a project moved between the two
// ports keeps the background it was saved with: the start point steps round the corners in
// 90 degree quadrants while the end point sweeps the circle inscribed in the square.
RKImage makeLinearGradientImage(const RKColor &from, const RKColor &to, double angleInDegrees, int size)
{
  constexpr double pi = 3.14159265358979323846;
  RKImage image(size, size, RKImage::Format_ARGB32);
  const double extent = static_cast<double>(size);

  double angle = std::fmod(angleInDegrees, 360.0);
  if (angle < 0.0)
    angle += 360.0;

  double startX = 0.0;
  double startY = 0.0;
  double radians = 0.0;
  if (angle < 90.0)
  {
    startX = 0.0; startY = 0.0;
    radians = (angle * 2.0 - 45.0) / 180.0 * pi;
  }
  else if (angle < 180.0)
  {
    startX = extent; startY = 0.0;
    radians = ((angle - 90.0) * 2.0 + 45.0) / 180.0 * pi;
  }
  else if (angle < 270.0)
  {
    startX = extent; startY = extent;
    radians = ((angle - 180.0) * 2.0 + 135.0) / 180.0 * pi;
  }
  else
  {
    startX = 0.0; startY = extent;
    radians = ((angle - 270.0) * 2.0 + 225.0) / 180.0 * pi;
  }

  const double axisX = (0.5 + std::cos(radians) / std::sqrt(2.0)) * extent - startX;
  const double axisY = (0.5 + std::sin(radians) / std::sqrt(2.0)) * extent - startY;
  const double axisLengthSquared = axisX * axisX + axisY * axisY;
  if (axisLengthSquared <= 0.0)
    return makeSolidImage(to, size);

  uint8_t *pixels = image.bits();
  for (int y = 0; y < size; ++y)
  {
    for (int x = 0; x < size; ++x)
    {
      // Distance along the axis, as a fraction of its length: the projection of this
      // pixel onto the line from start to end.
      const double t = ((static_cast<double>(x) + 0.5 - startX) * axisX +
                        (static_cast<double>(y) + 0.5 - startY) * axisY) / axisLengthSquared;
      writeStop(pixels + (static_cast<size_t>(y) * size + x) * 4u, from, to, t);
    }
  }
  return image;
}

// Two circles of equal radius whose centres sit above and below the image, again as in the
// Qt build. Roundness moves the "from" circle: at 1 it sits one image height below the top,
// and smaller values push it further away, flattening the falloff into a broader wash.
RKImage makeRadialGradientImage(const RKColor &from, const RKColor &to, double roundness, int size)
{
  RKImage image(size, size, RKImage::Format_ARGB32);
  const double extent = static_cast<double>(size);
  const double radius = extent / 2.0;
  const double centerX = radius;
  const double fromCenterY = extent / (std::max)(roundness, 0.0001);
  const double toCenterY = -radius;

  const double driftY = toCenterY - fromCenterY;
  const double drift = driftY * driftY;

  uint8_t *pixels = image.bits();
  for (int y = 0; y < size; ++y)
  {
    for (int x = 0; x < size; ++x)
    {
      // Largest t for which the pixel lies on the circle interpolated between the two,
      // which is the standard two-point radial gradient. Pixels the sweep never reaches
      // (the discriminant going negative) take the far colour, matching pad spread.
      const double offsetX = static_cast<double>(x) + 0.5 - centerX;
      const double offsetY = static_cast<double>(y) + 0.5 - fromCenterY;
      const double halfB = offsetY * driftY;
      const double c = offsetX * offsetX + offsetY * offsetY - radius * radius;
      const double discriminant = halfB * halfB - drift * c;

      const double t = discriminant < 0.0 ? 1.0 : (halfB + std::sqrt(discriminant)) / drift;
      writeStop(pixels + (static_cast<size_t>(y) * size + x) * 4u, from, to, t);
    }
  }
  return image;
}

} // namespace

RKImage ProjectStructure::makeSolidBackgroundImage(const RKColor &color, int size)
{
  return makeSolidImage(color, size);
}

ProjectStructure::ProjectStructure(): _camera(std::make_shared<RKCamera>())
{
  _backgroundImage = makeSolidBackgroundImage(_backgroundColor);
}

ProjectStructure::ProjectStructure(RKString filename, SKColorSets& colorSets, ForceFieldSets& forcefieldSets,
                                   bool proteinOnlyAsymmetricUnit, bool asMolecule, bool separatePolymerChains) noexcept(false): _camera(std::make_shared<RKCamera>())
{
  const std::filesystem::path path(filename.toStdString());
  if (std::filesystem::exists(path))
  {
    std::shared_ptr<Scene> scene = std::make_shared<Scene>(path, colorSets, forcefieldSets, proteinOnlyAsymmetricUnit, asMolecule, separatePolymerChains);
    for(std::shared_ptr<Movie> movie : scene->movies())
    {
      movie->setParent(scene);
    }
    _sceneList->appendScene(scene);
    _camera->resetForNewBoundingBox(this->renderBoundingBox());
  }

  _backgroundImage = makeSolidBackgroundImage(_backgroundColor);
}

ProjectStructure::ProjectStructure(std::vector<std::filesystem::path> paths, SKColorSets& colorSets, ForceFieldSets& forcefieldSets,
                                   SKParser::ImportType importType, bool onlyAsymmetricUnit, bool asMolecule, bool separatePolymerChains) noexcept(false): _camera(std::make_shared<RKCamera>())
{
  for (const std::filesystem::path &path : paths)
  {
    if (std::filesystem::exists(path))
    {
      std::shared_ptr<Scene> scene = std::make_shared<Scene>(path, colorSets, forcefieldSets, onlyAsymmetricUnit, asMolecule, separatePolymerChains);
      for(std::shared_ptr<Movie> movie : scene->movies())
      {
        movie->setParent(scene);
      }
      _sceneList->appendScene(scene);
    }
  }

  if(importType == SKParser::ImportType::asMovieFrames)
  {
    std::vector<std::shared_ptr<iRASPAObject>> iraspaStructures = _sceneList->flattenedAllIRASPAStructures();
    _sceneList = std::make_shared<SceneList>("NewMovie", iraspaStructures);
  }

  _camera->resetForNewBoundingBox(this->renderBoundingBox());
  _backgroundImage = makeSolidBackgroundImage(_backgroundColor);
}

ProjectStructure::~ProjectStructure()
{

}

void ProjectStructure::setInitialSelectionIfNeeded()
{
  if(!_sceneList->selectedScene())
  {
    if(!_sceneList->scenes().empty())
    {
      _sceneList->setSelectedScene(_sceneList->scenes().front());
      _sceneList->setSelectedFrameIndex(0);
    }
  }

  for(std::shared_ptr<Scene> scene : _sceneList->selectedScenes())
  {
    if(!scene->selectedMovie())
    {
      if(!scene->movies().empty())
      {
        scene->setSelectedMovie(scene->movies().front());
      }
      else
      {
        scene->setSelectedMovie(nullptr);
      }
    }
  }
}

std::vector<size_t> ProjectStructure::numberOfScenes() const
{
  std::vector<size_t> v = std::vector<size_t>(_sceneList->scenes().size());

  for(size_t i=0;i<_sceneList->scenes().size();i++)
  {
    v[i] = renderStructuresForScene(i).size();
  }

  return v;
}

int ProjectStructure::numberOfMovies([[maybe_unused]] int sceneIndex) const
{
  return 0;
}

std::vector<std::shared_ptr<RKRenderObject>> ProjectStructure::renderStructuresForScene(size_t i) const
{
  std::vector<std::shared_ptr<RKRenderObject>> structures = std::vector<std::shared_ptr<RKRenderObject>>();

  std::optional<size_t> selectedFrameIndex = _sceneList->selectedFrameIndex();
  if(selectedFrameIndex)
  {
    std::shared_ptr<Scene> scene = _sceneList->scenes()[i];
    for(std::shared_ptr<Movie> movie: scene->movies())
    {
      std::shared_ptr<iRASPAObject> selectedFrame = movie->frameAtIndex(*selectedFrameIndex);
      if(selectedFrame)
      {
        if(std::shared_ptr<Object> object = selectedFrame->object())
        {
          if(std::shared_ptr<RKRenderObject> structure = std::dynamic_pointer_cast<RKRenderObject>(object))
          {
            structures.push_back(structure);
          }
        }
      }
    }
  }
  return structures;
}

std::vector<RKInPerInstanceAttributesAtoms> ProjectStructure::renderMeasurementPoints() const
{
  return std::vector<RKInPerInstanceAttributesAtoms>();
}

std::vector<RKRenderObject> ProjectStructure::renderMeasurementStructure() const
{
  return std::vector<RKRenderObject>();
}

SKBoundingBox ProjectStructure::renderBoundingBox() const
{
  // What is switched off is not on screen, and what is not on screen has no say in where the camera
  // is put: a protein imported with its solvent hidden should be framed on the protein.
  std::vector<std::shared_ptr<iRASPAObject>> flattenedRenderStructures{};
  for(const std::shared_ptr<Scene> &scene : _sceneList->scenes())
  {
    for(const std::shared_ptr<Movie> &movie : scene->movies())
    {
      if(!movie->isVisible()) { continue; }

      for(const std::shared_ptr<iRASPAObject> &frame : movie->selectedFrames())
      {
        if(frame->object() && !frame->object()->isVisible()) { continue; }
        flattenedRenderStructures.push_back(frame);
      }
    }
  }

   if(flattenedRenderStructures.empty())
   {
     return SKBoundingBox();
   }

   double3 minimum = double3(DBL_MAX, DBL_MAX, DBL_MAX);
   double3 maximum = double3(-DBL_MAX, -DBL_MAX, -DBL_MAX);

   for(const std::shared_ptr<iRASPAObject> &frame: flattenedRenderStructures)
   {
     // for rendering the bounding-box is in the global coordinate space (adding the frame origin)
     SKBoundingBox currentBoundingBox  = frame->object()->transformedBoundingBox() + frame->object()->origin();

     SKBoundingBox transformedBoundingBox = currentBoundingBox;

     minimum.x = std::min(minimum.x, transformedBoundingBox.minimum().x);
     minimum.y = std::min(minimum.y, transformedBoundingBox.minimum().y);
     minimum.z = std::min(minimum.z, transformedBoundingBox.minimum().z);
     maximum.x = std::max(maximum.x, transformedBoundingBox.maximum().x);
     maximum.y = std::max(maximum.y, transformedBoundingBox.maximum().y);
     maximum.z = std::max(maximum.z, transformedBoundingBox.maximum().z);
   }

   return SKBoundingBox(minimum, maximum);
}

bool ProjectStructure::hasSelectedObjects() const
{
  for (const std::vector<std::shared_ptr<iRASPAObject>> &iraspa_structures: _sceneList->selectediRASPAStructures())
  {
    for(const std::shared_ptr<iRASPAObject> &iraspa_structure: iraspa_structures)
    {
      if (std::shared_ptr<Structure> structure = std::dynamic_pointer_cast<Structure>(iraspa_structure->object()))
      {
        if(structure->hasSelectedAtoms())
        {
         return true;
        }
      }
    }
  }
  return false;
}

RKBackgroundType ProjectStructure::renderBackgroundType() const
{
  return _backgroundType;
}

void ProjectStructure::setBackgroundType(RKBackgroundType type)
{
  _backgroundType = type;
}

RKColor ProjectStructure::renderBackgroundColor() const
{
   return _backgroundColor;
}

void ProjectStructure::setBackgroundColor(RKColor color)
{
 _backgroundColor = color;
}

const RKImage ProjectStructure::renderBackgroundCachedImage()
{
  switch(_backgroundType)
  {
    case RKBackgroundType::color:
    default:
      return makeSolidBackgroundImage(_backgroundColor);
    case RKBackgroundType::linearGradient:
      return makeLinearGradientImage(_backgroundLinearGradientFromColor, _backgroundLinearGradientToColor,
                                     _backgroundLinearGradientAngle, 1024);
    case RKBackgroundType::radialGradient:
      return makeRadialGradientImage(_backgroundRadialGradientFromColor, _backgroundRadialGradientToColor,
                                     _backgroundRadialGradientRoundness, 1024);
    case RKBackgroundType::image:
      if (!_backgroundImage.isNull())
        return _backgroundImage;
      return makeSolidBackgroundImage(_backgroundColor);
  }
}

void ProjectStructure::loadBackgroundImage(RKString filename)
{
  _backgroundImageFilename = RKString(std::filesystem::path(filename.toStdString()).filename().string());
  _backgroundImage = RKImage();
  if (!_backgroundImage.load(filename.toStdWString()) &&
      !_backgroundImage.load(filename.toStdString()))
  {
    _backgroundImage = makeSolidBackgroundImage(_backgroundColor);
  }
}

bool ProjectStructure::showBoundingBox() const
{
  return _showBoundingBox;
}

std::vector<RKInPerInstanceAttributesAtoms> ProjectStructure::renderBoundingBoxSpheres() const
{
  std::vector<RKInPerInstanceAttributesAtoms> data;

  double3 boundingBoxWidths = renderBoundingBox().widths();
  std::array<double3,8> corners = renderBoundingBox().corners();

  double scale = 0.0025 * std::max({boundingBoxWidths.x,boundingBoxWidths.y,boundingBoxWidths.z});
  for(double3 corner: corners)
  {
    RKInPerInstanceAttributesAtoms sphere = RKInPerInstanceAttributesAtoms(
                float4(corner.x,corner.y,corner.z,1.0),
                float4(1.0,1.0,1.0,1.0),
                float4(1.0,1.0,1.0,1.0),
                float4(1.0,1.0,1.0,1.0),
                float4(scale,scale,scale,1.0),
                0);

    data.push_back(sphere);
  }
  return data;
}

std::vector<RKInPerInstanceAttributesBonds> ProjectStructure::renderBoundingBoxCylinders() const
{
  std::vector<RKInPerInstanceAttributesBonds> data;

  double3 boundingBoxWidths = renderBoundingBox().widths();
  std::array<std::pair<double3,double3>,12> sides = renderBoundingBox().sides();

  double scale = 0.0025 * std::max({boundingBoxWidths.x,boundingBoxWidths.y,boundingBoxWidths.z});
  for(std::pair<double3,double3> side: sides)
  {
    RKInPerInstanceAttributesBonds bondData = RKInPerInstanceAttributesBonds(
                float4(side.first,1.0),
                float4(side.second,1.0),
                float4(1.0,1.0,1.0,1.0),
                float4(1.0,1.0,1.0,1.0),
                float4(scale,1.0,scale,1.0),
                0,
                0);
    data.push_back(bondData);
  }

  return data;
}

double ProjectStructure::imageDotsPerInchValue()
{
  switch(imageDPI())
  {
  case RKImageDPI::dpi_72:
    return 72.0;
  case RKImageDPI::dpi_75:
    return 75.0;
  case RKImageDPI::dpi_150:
    return 150.0;
  case RKImageDPI::dpi_300:
  default:
    return 300.0;
  case RKImageDPI::dpi_600:
    return 600.0;
  case RKImageDPI::dpi_1200:
    return 1200.0;
  }
}

void ProjectStructure::setLightStyle(RKLightStyle style)
{
  std::vector<std::shared_ptr<RKLight>> rig = RKLight::rig(style);
  if(!rig.empty())
  {
    _renderLights = rig;
    _renderAmbientOcclusionStrength = RKLightStyleAmbientOcclusionStrength(style);
    _renderSceneAmbientIntensity = RKLightStyleSceneAmbientIntensity(style);
    _renderSceneAmbientColor = RKLightStyleSceneAmbientColor(style);
  }
  _renderLightStyle = style;
}

void ProjectStructure::recheckLightStyle()
{
  _renderLightStyle = RKLight::styleMatching(_renderLights, _renderSceneAmbientIntensity,
                                             _renderSceneAmbientColor,
                                             _renderAmbientOcclusionStrength);
}

size_t ProjectStructure::maxNumberOfMoviesFrames()
{
  size_t maxNumberOfFrames=0;
  for(const std::shared_ptr<Scene> &scene : _sceneList->scenes())
  {
    for(const std::shared_ptr<Movie> &movie : scene->movies())
    {
      maxNumberOfFrames = std::max(movie->frames().size(), maxNumberOfFrames);
    }
  }
  return maxNumberOfFrames;
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::shared_ptr<ProjectStructure>& node)
{
  stream << node->_versionNumber;

  stream << node->_showBoundingBox;

  stream << static_cast<typename std::underlying_type<RKBackgroundType>::type>(node->_backgroundType);

  // Cocoa archives the picture as a PNG blob (empty Data when there is none). Colour and
  // gradient backgrounds regenerate on the fly and must not bloat the file with raw pixels.
  RKByteArray imageByteArray;
  if (node->_backgroundType == RKBackgroundType::image && !node->_backgroundImage.isNull())
    node->_backgroundImage.saveToPng(imageByteArray);
  stream << imageByteArray;

  stream << node->_backgroundImageFilename;
  stream << node->_backgroundColor;
  stream << node->_backgroundLinearGradientFromColor;
  stream << node->_backgroundLinearGradientToColor;
  stream << node->_backgroundRadialGradientFromColor;
  stream << node->_backgroundRadialGradientToColor;
  stream << node->_backgroundLinearGradientAngle;
  stream << node->_backgroundRadialGradientRoundness;

  stream << node->_renderImagePhysicalSizeInInches;
  stream << node->_renderImageNumberOfPixels;
  stream << node->_aspectRatio;
  stream << static_cast<typename std::underlying_type<RKImageDPI>::type>(node->_imageDPI);
  stream << static_cast<typename std::underlying_type<RKImageUnits>::type>(node->_imageUnits);
  stream << static_cast<typename std::underlying_type<RKImageDimensions>::type>(node->_imageDimensions);
  stream << static_cast<typename std::underlying_type<RKImageQuality>::type>(node->_renderImageQuality);

  // Cocoa's version 6 block, occlusion strength included: it sits with the export settings rather
  // than with the lights it grades, and the order here is the one Cocoa writes.
  stream << node->_renderPictureRayTracing;
  stream << node->_renderPictureSampleCount;
  stream << node->_renderPictureMaximumBounces;
  stream << node->_renderAmbientOcclusionStrength;

  stream << node->_renderShadows;

  stream << node->_movieFramesPerSecond;
  stream << static_cast<typename std::underlying_type<ProjectStructure::MovieType>::type>(node->_movieType);

  stream << node->_camera;
  stream << node->_renderAxes;

  // Between the axes and the scene list, which is where Cocoa writes them.
  stream << int64_t(node->_renderLights.size());
  for(const std::shared_ptr<RKLight> &light : node->_renderLights)
  {
    stream << light;
  }
  stream << static_cast<typename std::underlying_type<RKLightStyle>::type>(node->_renderLightStyle);
  stream << node->_renderSceneAmbientIntensity;
  stream << node->_renderSceneAmbientColor;

  stream << node->_sceneList;

  stream << int64_t(0x6f6b6180);

  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::shared_ptr<ProjectStructure>& node)
{
  int64_t versionNumber;
  stream >> versionNumber;
  if(versionNumber > node->_versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "ProjectStructure");
  }

  stream >> node->_showBoundingBox;
  int64_t backgroundType;
  stream >> backgroundType;
  node->_backgroundType = RKBackgroundType(backgroundType);

  // Cocoa: UInt32 length, then PNG bytes (0xffffffff = no picture).
  RKByteArray imageByteArray;
  stream >> imageByteArray;

  stream >> node->_backgroundImageFilename;
  stream >> node->_backgroundColor;
  stream >> node->_backgroundLinearGradientFromColor;
  stream >> node->_backgroundLinearGradientToColor;
  stream >> node->_backgroundRadialGradientFromColor;
  stream >> node->_backgroundRadialGradientToColor;
  stream >> node->_backgroundLinearGradientAngle;
  stream >> node->_backgroundRadialGradientRoundness;

  node->_backgroundImage = RKImage();
  if (!imageByteArray.empty())
    node->_backgroundImage.loadFromPng(imageByteArray);
  if (node->_backgroundImage.isNull())
    node->_backgroundImage = ProjectStructure::makeSolidBackgroundImage(node->_backgroundColor);

  stream >> node->_renderImagePhysicalSizeInInches;
  stream >> node->_renderImageNumberOfPixels;
  stream >> node->_aspectRatio;
  int64_t imageDPI;
  stream >> imageDPI;
  node->_imageDPI = RKImageDPI(imageDPI);
  int64_t imageUnits;
  stream >> imageUnits;
  node->_imageUnits = RKImageUnits(imageUnits);
  int64_t imageDimensions;
  stream >> imageDimensions;
  node->_imageDimensions = RKImageDimensions(imageDimensions);
  int64_t renderImageQuality;
  stream >> renderImageQuality;
  node->_renderImageQuality = RKImageQuality(renderImageQuality);

  if(versionNumber >= 6) // introduced in version 6
  {
    stream >> node->_renderPictureRayTracing;
    stream >> node->_renderPictureSampleCount;
    stream >> node->_renderPictureMaximumBounces;
    stream >> node->_renderAmbientOcclusionStrength;
  }

  if(versionNumber >= 13) // introduced in version 13
  {
    stream >> node->_renderShadows;
  }

  stream >> node->_movieFramesPerSecond;
  if(versionNumber >= 5) // introduced in version 5
  {
    int64_t movieType;
    stream >> movieType;
    node->_movieType = ProjectStructure::MovieType(movieType);
  }

  stream >> node->_camera;

  if(versionNumber >= 3) // introduced in version 3
  {
    stream >> node->_renderAxes;
  }

  if(versionNumber >= 7) // introduced in version 7
  {
    // The count is stored rather than assumed, so the number of roles can change without a version
    // bump.
    int64_t numberOfLights;
    stream >> numberOfLights;

    // Read into the slots of the current rig, so a document holding fewer lights than there are roles
    // keeps the placement of the roles it says nothing about.
    std::vector<std::shared_ptr<RKLight>> lights = RKLight::standardRig();
    for(int64_t i = 0; i < numberOfLights; i++)
    {
      std::shared_ptr<RKLight> light = std::make_shared<RKLight>();
      stream >> light;
      if(i < int64_t(lights.size()))
      {
        lights[size_t(i)] = light;
      }
    }
    if(numberOfLights > 0)
    {
      node->_renderLights = lights;
    }
  }

  bool readLightStyle = false;
  int64_t lightStyle = 0;
  if(versionNumber >= 9) // introduced in version 9
  {
    stream >> lightStyle;
    readLightStyle = true;
  }

  if(versionNumber >= 10) // introduced in version 10
  {
    stream >> node->_renderSceneAmbientIntensity;
    stream >> node->_renderSceneAmbientColor;
  }

  if(readLightStyle)
  {
    node->_renderLightStyle = RKLightStyle(lightStyle);
  }
  else
  {
    // A document from before the lighting was named: the name is worked out from the lighting itself,
    // which is why this waits until the ambient above has been settled. With no lights stored either,
    // what it matches is the default rig, which is what such a document was rendered with.
    node->recheckLightStyle();
  }

  if(versionNumber == 11)
  {
    // Version 11 held one setting for the whole scene. It moved onto the structures, which each carry
    // their own now, so the value is read past and dropped.
    int64_t discarded;
    stream >> discarded;
  }

  stream >> node->_sceneList;

  if(versionNumber >= 4) // introduced in version 4
  {
    int64_t magicNumber;
    stream >> magicNumber;
    if(magicNumber != int64_t(0x6f6b6180))
    {
      throw InvalidArchiveVersionException(__FILE__, __LINE__, "ProjectStructure invalid magic-number");
    }
  }

  return stream;
}

