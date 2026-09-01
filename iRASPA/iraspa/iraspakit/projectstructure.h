#pragma once

#include "rkstring.h"
#include "rkcolor.h"
#include "binaryarchive.h"
#include <renderkit.h>
#include <symmetrykit.h>
#include <filesystem>
#include <vector>
#include "project.h"
#include "scenelist.h"
#include "iraspaobject.h"

class ProjectStructure: public Project, public RKRenderDataSource
{
public:
  enum class MovieType: int64_t
  {
    frames = 0, rotationY = 1, rotationXYlemniscate = 2
  };

  /// Cocoa's ProjectStructureNode class version, whose archive this is: the two are written and read
  /// field for field so that a document travels between them. Public because the export job carries
  /// it, which is how a helper built against a different format is told apart from a corrupt file.
  static constexpr int64_t archiveVersion = 13;

  ProjectStructure();
  ProjectStructure(RKString filename, SKColorSets& colorSets, ForceFieldSets& forcefieldSets, bool proteinOnlyAsymmetricUnit, bool asMolecule, bool separatePolymerChains = false) noexcept(false);
  ProjectStructure(std::vector<std::filesystem::path> paths, SKColorSets& colorSets, ForceFieldSets& forcefieldSets, SKParser::ImportType importType, bool proteinOnlyAsymmetricUnit, bool asMolecule, bool separatePolymerChains = false) noexcept(false);
  ~ProjectStructure() override;

  std::vector<size_t> numberOfScenes() const override final;
  int numberOfMovies(int sceneIndex) const override final;
  std::vector<std::shared_ptr<RKRenderObject>> renderStructuresForScene(size_t i) const;

  std::vector<std::shared_ptr<RKLight>>& renderLights() override final {return _renderLights;}

  /// The named style renderLights() currently amounts to, kept in step by recheckLightStyle().
  RKLightStyle renderLightStyle() const {return _renderLightStyle;}
  /// Replaces the whole rig, the occlusion strength and the scene ambient with what \a style asks
  /// for. `custom` names whatever is already there, so it changes nothing but the name.
  void setLightStyle(RKLightStyle style);
  /// Called after any light, the scene ambient or the occlusion strength is edited, so the style name
  /// follows the lighting rather than going stale.
  void recheckLightStyle();

  double renderSceneAmbientIntensity() const override final {return _renderSceneAmbientIntensity;}
  void setSceneAmbientIntensity(double intensity) {_renderSceneAmbientIntensity = intensity;}
  RKColor renderSceneAmbientColor() const override final {return _renderSceneAmbientColor;}
  void setSceneAmbientColor(RKColor color) {_renderSceneAmbientColor = color;}

  double renderAmbientOcclusionStrength() const override final {return _renderAmbientOcclusionStrength;}
  void setAmbientOcclusionStrength(double strength) {_renderAmbientOcclusionStrength = strength;}

  bool renderShadows() const override final {return _renderShadows;}
  void setShadows(bool shadows) {_renderShadows = shadows;}

  std::vector<RKInPerInstanceAttributesAtoms> renderMeasurementPoints() const override final;
  std::vector<RKRenderObject> renderMeasurementStructure() const override final;

  bool hasSelectedObjects() const override final;

  RKBackgroundType renderBackgroundType() const override final;
  void setBackgroundType(RKBackgroundType type);
  RKColor renderBackgroundColor() const override final;
  void setBackgroundColor(RKColor color);

  RKColor linearGradientFromColor() const {return _backgroundLinearGradientFromColor;}
  void setLinearGradientFromColor(RKColor color) {_backgroundLinearGradientFromColor = color;}
  RKColor linearGradientToColor() const {return _backgroundLinearGradientToColor;}
  void setLinearGradientToColor(RKColor color) {_backgroundLinearGradientToColor = color;}
  double linearGradientAngle() const {return _backgroundLinearGradientAngle;}
  void setLinearGradientAngle(double angle) {_backgroundLinearGradientAngle = angle;}

  RKColor radialGradientFromColor() const {return _backgroundRadialGradientFromColor;}
  void setRadialGradientFromColor(RKColor color) {_backgroundRadialGradientFromColor = color;}
  RKColor radialGradientToColor() const {return _backgroundRadialGradientToColor;}
  void setRadialGradientToColor(RKColor color) {_backgroundRadialGradientToColor = color;}
  double radialGradientRoundness() const {return _backgroundRadialGradientRoundness;}
  void setRadialGradientRoundness(double value) {_backgroundRadialGradientRoundness = value;}
  void loadBackgroundImage(RKString filename);
  RKString backgroundImageFilename() {return _backgroundImageFilename;}

  const RKImage renderBackgroundCachedImage() override final;

  int renderImageNumberOfPixels() const override final {return _renderImageNumberOfPixels;}
  void setImageNumberOfPixels(int width) {_renderImageNumberOfPixels = width;}
  void setImageAspectRatio(double ratio) {_aspectRatio = ratio;}
  double imageAspectRatio() {return _aspectRatio;}
  double renderImagePhysicalSizeInInches() const override final {return _renderImagePhysicalSizeInInches;}
  void setImagePhysicalSizeInInches(double width) {_renderImagePhysicalSizeInInches = width;}
  RKImageDPI imageDPI() {return _imageDPI;}
  void setImageDPI(RKImageDPI dpi) {_imageDPI = dpi;}
  RKImageUnits imageUnits() {return _imageUnits;}
  void setImageUnits(RKImageUnits units) {_imageUnits = units;}
  RKImageDimensions imageDimensions() {return _imageDimensions;}
  void setImageDimensions(RKImageDimensions dimensions) {_imageDimensions = dimensions;}
  RKImageQuality renderImageQuality() {return _renderImageQuality;}
  void setImageQuality(RKImageQuality quality) {_renderImageQuality = quality;}

  /// How exported pictures and movies are rendered. These sit with the other export settings rather
  /// than in the application settings, because unlike the interactive sample counts, which say what
  /// this machine can keep up with, they are choices about the output.
  bool renderPictureRayTracing() const override final {return _renderPictureRayTracing;}
  void setPictureRayTracing(bool tracing) {_renderPictureRayTracing = tracing;}
  int renderPictureSampleCount() const override final {return static_cast<int>(_renderPictureSampleCount);}
  void setPictureSampleCount(int count) {_renderPictureSampleCount = count;}
  int renderPictureMaximumBounces() const override final {return static_cast<int>(_renderPictureMaximumBounces);}
  void setPictureMaximumBounces(int bounces) {_renderPictureMaximumBounces = bounces;}

  int movieFramesPerSecond() {return _movieFramesPerSecond;}
  void setMovieFramesPerSecond(int fps) {_movieFramesPerSecond = fps;}
  MovieType movieType() {return _movieType;}
  void setMovieType(MovieType value) {_movieType = value;}
  double imageDotsPerInchValue();

  size_t maxNumberOfMoviesFrames();

  void setShowBoundingBox(bool show) {_showBoundingBox = show;}
  SKBoundingBox renderBoundingBox() const override final;
  bool showBoundingBox() const override final;
  std::vector<RKInPerInstanceAttributesAtoms> renderBoundingBoxSpheres() const override final;
  std::vector<RKInPerInstanceAttributesBonds> renderBoundingBoxCylinders() const override final;
  std::shared_ptr<RKCamera> camera() const override final {return _camera;}

  std::shared_ptr<SceneList> sceneList() {return _sceneList;}

  void setInitialSelectionIfNeeded() override final;
  std::shared_ptr<RKGlobalAxes> axes() const override final {return _renderAxes;}
private:
  int64_t _versionNumber{archiveVersion};

  SKBoundingBox _boundingBox = SKBoundingBox();
  bool _showBoundingBox{false};

  RKBackgroundType _backgroundType = RKBackgroundType::color;
  RKImage _backgroundImage;
  RKString _backgroundImageFilename;
  RKColor _backgroundColor = RKColor::fromRgb(255,255,255,255);
  RKColor _backgroundLinearGradientFromColor = RKColor::fromRgb(0,0,0,255);
  RKColor _backgroundLinearGradientToColor = RKColor::fromRgb(255,255,255,255);
  RKColor _backgroundRadialGradientFromColor = RKColor::fromRgb(0,0,0,255);
  RKColor _backgroundRadialGradientToColor = RKColor::fromRgb(255,255,255,255);
  double _backgroundLinearGradientAngle = 45.0;
  double _backgroundRadialGradientRoundness = 0.4;

  double _renderImagePhysicalSizeInInches = 6.5;
  int64_t _renderImageNumberOfPixels = 1200;
  double _aspectRatio = 1.0;
  RKImageDPI _imageDPI = RKImageDPI::dpi_300;
  RKImageUnits _imageUnits = RKImageUnits::cm;
  RKImageDimensions _imageDimensions = RKImageDimensions::pixels;
  RKImageQuality _renderImageQuality = RKImageQuality::rgb_8_bits;
  // Off by default: tracing an image takes minutes rather than the moment rasterizing it takes, so
  // it is asked for rather than assumed. The sample count is high because an export is not waited on
  // interactively, and noise is what one notices in a picture one has kept.
  bool _renderPictureRayTracing = false;
  int64_t _renderPictureSampleCount = 1024;
  int64_t _renderPictureMaximumBounces = 2;
  int64_t _movieFramesPerSecond = 10;
  ProjectStructure::MovieType _movieType = ProjectStructure::MovieType::rotationY;
  // One light per RKLight::Role, of which a rig switches on the few it uses.
  std::vector<std::shared_ptr<RKLight>> _renderLights = RKLight::defaultRig();
  RKLightStyle _renderLightStyle = RKLightStyle::standard;
  double _renderSceneAmbientIntensity = 1.0;
  RKColor _renderSceneAmbientColor = RKColor::fromRgb(255, 255, 255, 255);
  double _renderAmbientOcclusionStrength = 0.0;
  bool _renderShadows{true};
  std::shared_ptr<RKCamera> _camera;
  std::shared_ptr<RKGlobalAxes> _renderAxes = std::make_shared<RKGlobalAxes>();

  std::shared_ptr<SceneList> _sceneList = std::make_shared<SceneList>();

  static RKImage makeSolidBackgroundImage(const RKColor &color, int size = 1024);

  friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<ProjectStructure> &);
  friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<ProjectStructure> &);
};
