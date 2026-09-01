/********************************************************************************************************************
   iRASPA: GPU-accelated visualisation software for materials scientists
 ********************************************************************************************************************/

#include "ExportJob.h"

#include "rkstring.h"

#include <algorithm>

namespace
{
  bool fail(std::wstring &error, const wchar_t *what)
  {
    error = what;
    return false;
  }
}

bool readExportJob(BinaryArchive &archive, ExportJob &job, std::wstring &error)
{
  uint32_t magic = 0;
  uint32_t version = 0;
  archive >> magic;
  archive >> version;
  if (magic != ExportJob::kMagic)
    return fail(error, L"not an iRASPA export job file");
  if (version != ExportJob::kVersion)
    return fail(error, L"unsupported export job version");

  uint32_t mode = 0;
  archive >> mode;
  if (mode > static_cast<uint32_t>(ExportJob::Mode::movie))
    return fail(error, L"unknown export mode");
  job.mode = static_cast<ExportJob::Mode>(mode);

  archive >> job.width;
  archive >> job.height;
  archive >> job.dotsPerInch;
  archive >> job.framesPerSecond;

  int32_t movieType = 0;
  archive >> movieType;
  job.movieType = static_cast<ProjectStructure::MovieType>(movieType);

  int32_t movieFormat = 0;
  archive >> movieFormat;
  job.movieFormat = static_cast<MovieWriter::Format>(movieFormat);

  int32_t renderQuality = 0;
  archive >> renderQuality;
  job.renderQuality = static_cast<RKRenderQuality>(renderQuality);

  RKString outputPath;
  archive >> outputPath;
  job.outputPath = outputPath.toStdWString();

  int64_t avoidAdapterLuid = 0;
  archive >> avoidAdapterLuid;
  job.hasAvoidAdapter = (avoidAdapterLuid != 0);
  job.avoidAdapter.LowPart = static_cast<DWORD>(static_cast<uint64_t>(avoidAdapterLuid) & 0xffffffffull);
  job.avoidAdapter.HighPart = static_cast<LONG>(avoidAdapterLuid >> 32);

  // A project written in a format this build does not read means the two executables came from
  // different builds, which is worth saying outright: read on, and the first nested version number
  // the misalignment lands on would be blamed instead.
  int64_t projectFormat = 0;
  archive >> projectFormat;
  if (projectFormat != ProjectStructure::archiveVersion)
  {
    error = L"the application and the export helper are from different builds (the project is in "
            L"format " + std::to_wstring(projectFormat) + L", this helper reads format " +
            std::to_wstring(ProjectStructure::archiveVersion) +
            L"): rebuild both and restart iRASPA";
    return false;
  }

  if (archive.status() != BinaryArchive::Status::Ok)
    return fail(error, L"the export job header is truncated");

  if (job.width <= 0 || job.height <= 0)
    return fail(error, L"the export job asks for an empty image");
  if (job.outputPath.empty())
    return fail(error, L"the export job names no output file");
  job.framesPerSecond = (std::max)(1, job.framesPerSecond);

  // The reader writes through the pointee, so the project has to exist first.
  job.project = std::make_shared<ProjectStructure>();
  try
  {
    archive >> job.project;
  }
  // Which class refused the archive and where, since "invalid archive version" on its own says
  // nothing about which of the dozens of nested versions it was, and a reader that has lost its
  // place reports the failure at whichever nested version it stumbles into next.
  catch (const InvalidArchiveVersionException &ex)
  {
    error = L"the project could not be read: " + RKString(ex.what()).toStdWString() + L" (" +
            RKString(ex.get_func()).toStdWString() + L", " + RKString(ex.get_file()).toStdWString() +
            L":" + std::to_wstring(ex.get_line()) + L")";
    return false;
  }
  catch (const std::exception &ex)
  {
    error = L"the project could not be read: " + RKString(ex.what()).toStdWString();
    return false;
  }
  catch (...)
  {
    return fail(error, L"the project could not be read");
  }

  if (archive.status() != BinaryArchive::Status::Ok)
    return fail(error, L"the export job project is truncated");

  return true;
}
