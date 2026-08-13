#pragma once

#include "MovieWriter.h"
#include "projectstructure.h"
#include "rkrenderuniforms.h"

#include <cstdint>
#include <memory>
#include <string>

// One picture or movie for iRASPA.Export.exe. The order the fields are written in is
// fixed by readExportJob in Export\ExportJob.cpp, which is the authority on the format;
// the two only ever change together.
struct ExportJobRequest
{
    bool movie{ false };
    int width{ 1 };
    int height{ 1 };
    double dotsPerInch{ 72.0 };
    int framesPerSecond{ 10 };
    ProjectStructure::MovieType movieType{ ProjectStructure::MovieType::frames };
    MovieWriter::Format movieFormat{ MovieWriter::Format::h264 };
    RKRenderQuality renderQuality{ RKRenderQuality::picture };
    std::wstring outputPath;
    // Adapter the live view is on, so the helper takes a different GPU where there is
    // one. Zero means no preference.
    int64_t avoidAdapterLuid{ 0 };
    std::shared_ptr<ProjectStructure> project;
};

// Serialises the whole request, project included, to a file the helper can be pointed
// at. Reads the project, so it belongs on the thread that owns the document.
bool WriteExportJobFile(std::wstring const& path, ExportJobRequest const& request,
                        std::wstring& error);

// A file in the user's temp directory no other job is using. Empty on failure.
std::wstring UniqueExportJobPath();

// iRASPA.Export.exe beside the running executable, which is where it sits both loose and
// inside the MSIX. Empty if it is not there.
std::wstring ExportHelperPath();

// Where the helper builds a file before moving it onto outputPath, so that being killed
// mid-encode cannot leave a half-written file at the destination. The marker goes before
// the extension rather than after it: WIC and the Media Foundation sink writer both pick
// their container from the extension, so a name ending in ".part" would quietly write PNG
// bytes into a file the user asked to be a JPEG. Both processes derive the staging name
// from this, the app to sweep it and the helper to write it.
std::wstring ExportStagingPath(std::wstring const& outputPath);
