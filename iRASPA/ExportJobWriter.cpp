#include "ExportJobWriter.h"

#include "binaryarchive.h"
#include "rkstring.h"

#include <windows.h>

#include <exception>
#include <fstream>
#include <vector>

namespace
{
    constexpr uint32_t kJobMagic = 0x49525845;
    constexpr uint32_t kJobVersion = 2;
}

std::wstring ExportStagingPath(std::wstring const& outputPath)
{
    const size_t slash = outputPath.find_last_of(L"\\/");
    const size_t dot = outputPath.find_last_of(L'.');
    const bool hasExtension = dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
    if (!hasExtension)
        return outputPath + L".part";
    return outputPath.substr(0, dot) + L".part" + outputPath.substr(dot);
}

bool WriteExportJobFile(std::wstring const& path, ExportJobRequest const& request,
                        std::wstring& error)
{
    if (!request.project)
    {
        error = L"there is no project to export";
        return false;
    }

    BinaryArchive archive;
    try
    {
        archive << kJobMagic;
        archive << kJobVersion;
        archive << static_cast<uint32_t>(request.movie ? 1 : 0);
        archive << static_cast<int32_t>(request.width);
        archive << static_cast<int32_t>(request.height);
        archive << request.dotsPerInch;
        archive << static_cast<int32_t>(request.framesPerSecond);
        archive << static_cast<int32_t>(request.movieType);
        archive << static_cast<int32_t>(request.movieFormat);
        archive << static_cast<int32_t>(request.renderQuality);
        archive << RKString::fromStdWString(request.outputPath);
        archive << request.avoidAdapterLuid;

        // The format the project below is written in. The helper is a separate executable and can be
        // left behind by a build, or outlive one in a window opened before it; without this the reader
        // walks off the end of a field it does not know about and blames whichever nested version it
        // lands on, which says nothing about what is actually wrong.
        archive << ProjectStructure::archiveVersion;
        archive << request.project;
    }
    catch (std::exception const& ex)
    {
        error = L"the project could not be written: " + RKString(ex.what()).toStdWString();
        return false;
    }
    catch (...)
    {
        error = L"the project could not be written";
        return false;
    }

    if (archive.status() != BinaryArchive::Status::Ok)
    {
        error = L"the export job could not be assembled";
        return false;
    }

    std::vector<uint8_t> const& bytes = archive.buffer();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file)
        file.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
    {
        error = L"could not write the export job file " + path;
        return false;
    }
    file.close();
    if (!file)
    {
        error = L"could not write the export job file " + path;
        return false;
    }
    return true;
}

std::wstring UniqueExportJobPath()
{
    wchar_t directory[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH + 1, directory);
    if (length == 0 || length > MAX_PATH)
        return {};

    wchar_t path[MAX_PATH + 1]{};
    if (GetTempFileNameW(directory, L"irx", 0, path) == 0)
        return {};
    return path;
}

std::wstring FailedExportJobPath()
{
    wchar_t directory[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH + 1, directory);
    if (length == 0 || length > MAX_PATH)
        return {};
    return std::wstring(directory) + L"iraspa-failed-export.irjob";
}

std::wstring ExportHelperPath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD written = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (written == 0)
            return {};
        if (written < path.size())
        {
            path.resize(written);
            break;
        }
        path.resize(path.size() * 2);
    }

    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    path.replace(slash + 1, std::wstring::npos, L"iRASPA.Export.exe");
    return path;
}
