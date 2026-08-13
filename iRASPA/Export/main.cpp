/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists

   Out-of-process picture and movie renderer, the Windows counterpart of the Cocoa build's
   PictureCreationService and MovieCreationService XPC services. iRASPA.exe writes a job
   file holding the whole project and starts this program on it; nothing is shared with the
   live process, so the export gets its own D3D12 device (on a second GPU when the machine
   has one), its own VRAM, and cannot take the window down with it. Cancellation is the
   parent killing the process, which is why the output file is only moved into place after
   the encoder has been flushed.
 ********************************************************************************************************************/

#include "ExportJob.h"
#include "ExportJobWriter.h"

#include "ImageExport.h"
#include "MovieWriter.h"
#include "binaryarchive.h"
#include "directxrenderer.h"
#include "rkcamera.h"
#include "rkimage.h"
#include "scenelist.h"

#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <io.h>
#include <memory>
#include <string>
#include <vector>

namespace
{
  constexpr double kRadiansPerDegree = M_PI / 180.0;

  // The camera path has to match CameraDetailView's in-process export exactly, or the same
  // project would give a different movie depending on which one produced it: one full turn
  // in 120 frames of three degrees, and a Gerono lemniscate about the starting orientation.
  constexpr int kCameraMovieFrames = 120;
  constexpr double kRotationYStep = -3.0 * kRadiansPerDegree;
  constexpr double kLemniscateYaw = M_PI;
  constexpr double kLemniscatePitch = M_PI / 2.0;

  std::string toUtf8(const std::wstring &text)
  {
    if (text.empty())
      return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
      return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), utf8.data(), size,
                        nullptr, nullptr);
    return utf8;
  }

  // The parent reads these lines as they appear, so every one of them is flushed. Binary
  // mode keeps the bytes exactly as written: the protocol is UTF-8 with '\n' separators,
  // not whatever the console code page and text-mode translation would make of it.
  void configureStandardOutput()
  {
    _setmode(_fileno(stdout), _O_BINARY);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
  }

  void writeLine(const std::string &line)
  {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
  }

  void reportError(const std::wstring &message)
  {
    writeLine("ERROR " + toUtf8(message));
  }

  void reportProgress(int completed, int total)
  {
    writeLine("PROGRESS " + std::to_string(completed) + " " + std::to_string(total));
  }

  bool readFile(const std::wstring &path, std::vector<uint8_t> &bytes)
  {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
      return false;
    const std::streamoff size = file.tellg();
    if (size < 0)
      return false;
    bytes.resize(static_cast<size_t>(size));
    file.seekg(0);
    return static_cast<bool>(file.read(reinterpret_cast<char *>(bytes.data()), size));
  }

  bool publish(const std::wstring &stagingPath, const std::wstring &outputPath, std::wstring &error)
  {
    if (MoveFileExW(stagingPath.c_str(), outputPath.c_str(), MOVEFILE_REPLACE_EXISTING))
      return true;
    error = L"could not move the finished file to " + outputPath + L" (error "
            + std::to_wstring(GetLastError()) + L")";
    DeleteFileW(stagingPath.c_str());
    return false;
  }

  int movieFrameCount(const ExportJob &job)
  {
    if (job.movieType != ProjectStructure::MovieType::frames)
      return kCameraMovieFrames;
    return (std::max)(1, static_cast<int>(job.project->maxNumberOfMoviesFrames()));
  }

  /// One frame of the movie. The camera path is written as an absolute rotation from where
  /// the camera started rather than a step per frame, so a long movie cannot drift and the
  /// last frame meets the first.
  RKImage renderMovieFrame(DirectXRenderer &renderer, const ExportJob &job, int frame,
                           int frameCount, int width, int height, const simd_quatd &startRotation)
  {
    std::shared_ptr<RKCamera> camera = job.project->camera();
    const double t = 2.0 * M_PI * static_cast<double>(frame) / static_cast<double>((std::max)(1, frameCount));
    switch (job.movieType)
    {
    case ProjectStructure::MovieType::frames:
      // Scrubbing every movie to the same frame changes which structures the scene list
      // hands out, so the renderer has to be given the new set rather than only redrawn.
      if (std::shared_ptr<SceneList> sceneList = job.project->sceneList())
      {
        sceneList->setSelectedFrameIndex(static_cast<size_t>(frame));
        renderer.setRenderStructures(sceneList->selectediRASPARenderStructures());
        renderer.reloadData();
      }
      break;
    case ProjectStructure::MovieType::rotationY:
      if (camera)
      {
        camera->setWorldRotation(startRotation);
        camera->rotateCameraAroundAxisY(kRotationYStep * frame);
      }
      break;
    case ProjectStructure::MovieType::rotationXYlemniscate:
      if (camera)
      {
        camera->setWorldRotation(startRotation);
        camera->rotateCameraAroundAxisY(kLemniscateYaw * std::sin(t));
        camera->rotateCameraAroundAxisX(kLemniscatePitch * std::sin(t) * std::cos(t));
      }
      break;
    }

    return renderer.renderSceneToImage(width, height, job.renderQuality);
  }

  bool loadProject(DirectXRenderer &renderer, const ExportJob &job, std::wstring &error)
  {
    job.project->setInitialSelectionIfNeeded();

    std::shared_ptr<SceneList> sceneList = job.project->sceneList();
    if (!sceneList)
    {
      error = L"the project has no scene list";
      return false;
    }

    // The picture is of the view the user is looking at, so the snapshot's camera is the
    // one to draw with. setRenderDataSource frames the structure the way opening a project
    // does -- resetForNewBoundingBox followed by resetCameraToDirection -- which would
    // throw that orientation away, so it is put back afterwards.
    std::shared_ptr<RKCamera> camera = job.project->camera();
    const RKCamera savedCamera = camera ? *camera : RKCamera();

    renderer.setRenderStructures(sceneList->selectediRASPARenderStructures());
    renderer.setRenderDataSource(job.project);

    if (camera)
    {
      *camera = savedCamera;
      camera->updateCameraForWindowResize(job.width, job.height);
      // The eye and the view matrix are derived from the archived fields rather than
      // archived themselves, so a camera that has only ever been read back still looks
      // along an identity view matrix and would render an empty frame.
      camera->updateViewMatrix();
    }
    return true;
  }

  int runPicture(DirectXRenderer &renderer, const ExportJob &job)
  {
    const RKImage image = renderer.renderSceneToImage(job.width, job.height, job.renderQuality);
    if (image.isNull())
    {
      reportError(L"the renderer produced no image");
      return 1;
    }

    const std::wstring stagingPath = ExportStagingPath(job.outputPath);
    DeleteFileW(stagingPath.c_str());

    std::wstring error;
    if (!ImageExport::saveImage(stagingPath, image.constBits(), image.width(), image.height(),
                                job.dotsPerInch, &error))
    {
      DeleteFileW(stagingPath.c_str());
      reportError(error);
      return 1;
    }
    reportProgress(1, 1);

    if (!publish(stagingPath, job.outputPath, error))
    {
      reportError(error);
      return 1;
    }

    writeLine("DONE " + toUtf8(job.outputPath));
    return 0;
  }

  int runMovie(DirectXRenderer &renderer, const ExportJob &job)
  {
    MovieWriter writer(static_cast<unsigned int>(job.width), static_cast<unsigned int>(job.height),
                       job.framesPerSecond, job.movieFormat);
    // MovieWriter rounds to the even extents H.264 and HEVC insist on and then reads frames
    // of exactly that size on trust, so the renderer is asked for its size rather than the
    // one the job named.
    const int width = static_cast<int>(writer.width());
    const int height = static_cast<int>(writer.height());

    const std::wstring stagingPath = ExportStagingPath(job.outputPath);
    DeleteFileW(stagingPath.c_str());

    if (!writer.initialize(stagingPath))
    {
      DeleteFileW(stagingPath.c_str());
      reportError(writer.lastError());
      return 1;
    }

    const int frameCount = movieFrameCount(job);
    std::shared_ptr<RKCamera> camera = job.project->camera();
    const simd_quatd startRotation =
        camera ? camera->worldRotation() : simd_quatd(1.0, double3(0.0, 0.0, 0.0));

    std::wstring failure;
    for (int frame = 0; frame < frameCount; ++frame)
    {
      const RKImage image = renderMovieFrame(renderer, job, frame, frameCount, width, height,
                                             startRotation);
      if (image.isNull())
      {
        failure = L"the renderer produced no image";
        break;
      }
      // The renderer caps its targets at the D3D12 maximum, so an over-large request comes
      // back smaller than the encoder expects. Refusing beats reading off the end.
      if (image.width() != width || image.height() != height)
      {
        failure = L"the renderer could not produce a frame at " + std::to_wstring(width) + L"x"
                  + std::to_wstring(height);
        break;
      }
      if (!writer.addFrame(image.constBits(), static_cast<size_t>(frame)))
      {
        failure = writer.lastError();
        break;
      }
      reportProgress(frame + 1, frameCount);
    }

    if (failure.empty() && !writer.finalize())
      failure = writer.lastError();

    if (!failure.empty())
    {
      DeleteFileW(stagingPath.c_str());
      reportError(failure);
      return 1;
    }

    if (!publish(stagingPath, job.outputPath, failure))
    {
      reportError(failure);
      return 1;
    }

    writeLine("DONE " + toUtf8(job.outputPath));
    return 0;
  }

  int runJob(const ExportJob &job)
  {
    auto renderer = std::make_unique<DirectXRenderer>();
    if (!renderer->initializeOffscreen(static_cast<UINT>(job.width), static_cast<UINT>(job.height),
                                       job.hasAvoidAdapter ? &job.avoidAdapter : nullptr))
    {
      reportError(renderer->statusMessage());
      return 1;
    }
    writeLine("ADAPTER " + toUtf8(renderer->deviceContext().adapterDescription()));

    // The live view bakes ambient occlusion at low quality because it has to keep up with
    // the user; an exported frame is baked once and then looked at closely, so it is worth
    // the wait here.
    renderer->setAmbientOcclusionQuality(job.renderQuality);

    std::wstring error;
    if (!loadProject(*renderer, job, error))
    {
      reportError(error);
      return 1;
    }

    return (job.mode == ExportJob::Mode::movie) ? runMovie(*renderer, job)
                                                : runPicture(*renderer, job);
  }
}

int wmain(int argc, wchar_t **argv)
{
  configureStandardOutput();

  std::wstring jobPath;
  for (int i = 1; i < argc; ++i)
  {
    if (std::wstring(argv[i]) == L"--job" && i + 1 < argc)
      jobPath = argv[++i];
  }
  if (jobPath.empty())
  {
    reportError(L"usage: iRASPA.Export.exe --job <path-to-job-file>");
    return 2;
  }

  std::vector<uint8_t> bytes;
  if (!readFile(jobPath, bytes))
  {
    reportError(L"could not read the job file " + jobPath);
    return 2;
  }

  BinaryArchive archive(std::move(bytes));
  ExportJob job;
  std::wstring error;
  if (!readExportJob(archive, job, error))
  {
    reportError(error);
    return 2;
  }

  // MovieWriter's sink writer and ImageExport's WIC factory both want an initialised
  // apartment, and neither touches the apartment state itself.
  const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr))
  {
    reportError(L"CoInitializeEx failed (hr=" + std::to_wstring(static_cast<long>(hr)) + L")");
    return 3;
  }

  int result = 1;
  try
  {
    result = runJob(job);
  }
  catch (const std::exception &ex)
  {
    reportError(RKString(ex.what()).toStdWString());
  }
  catch (...)
  {
    reportError(L"the export failed with an unknown error");
  }

  CoUninitialize();
  return result;
}
