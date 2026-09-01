/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#pragma once

#include <windows.h>

#include "MovieWriter.h"
#include "binaryarchive.h"
#include "projectstructure.h"
#include "rkrenderuniforms.h"

#include <cstdint>
#include <memory>
#include <string>

/// One picture or movie to render, as iRASPA.exe hands it to iRASPA.Export.exe.
///
/// The whole job, project included, travels as a single BinaryArchive written to a file
/// and named on the command line. The project crosses the process boundary through the
/// archive operators it already has for documents, so the two processes cannot disagree
/// about what a project is; a hand-written copy would have to be kept in step with every
/// field anyone ever adds.
struct ExportJob
{
  enum class Mode : uint32_t
  {
    picture = 0, movie = 1
  };

  static constexpr uint32_t kMagic = 0x49525845;  // 'IRXE'
  static constexpr uint32_t kVersion = 2;

  Mode mode = Mode::picture;
  int32_t width = 1;
  int32_t height = 1;
  double dotsPerInch = 72.0;
  int32_t framesPerSecond = 10;
  ProjectStructure::MovieType movieType = ProjectStructure::MovieType::rotationY;
  MovieWriter::Format movieFormat = MovieWriter::Format::h264;
  RKRenderQuality renderQuality = RKRenderQuality::picture;
  std::wstring outputPath;

  /// The adapter the live view runs on, so the export picks a different GPU when the
  /// machine has one. Absent means no preference.
  LUID avoidAdapter = {};
  bool hasAvoidAdapter = false;

  std::shared_ptr<ProjectStructure> project;
};

/// Fills \a job from \a archive. On failure returns false and describes the first thing
/// that did not fit in \a error.
bool readExportJob(BinaryArchive &archive, ExportJob &job, std::wstring &error);
