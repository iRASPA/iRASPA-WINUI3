#include "pch.h"
#include "MainWindow.xaml.h"

#include "documentdata.h"
#include "iraspaobject.h"
#include "iraspaproject.h"
#include "projecttreecontroller.h"
#include "projecttreenode.h"
#include "zipreader.h"

#include "binaryarchive.h"
#include <Windows.h>
#include <initializer_list>
#include <string>

using namespace winrt;

// Reading the shipped gallery database (Qt MainWindow::readDatabase). The
// grafting of what is read onto the project tree belongs to the document layer;
// only the file work and the thread hop are here.

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        std::wstring ApplicationDirectory()
        {
            wchar_t buf[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH)
                return {};
            std::wstring path(buf, n);
            const auto slash = path.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
                path.resize(slash);
            return path;
        }

        // The first of the candidate names that is actually present next to the executable.
        std::wstring ResolveDatabasePath(std::initializer_list<wchar_t const*> names)
        {
            const auto dir = ApplicationDirectory();
            if (dir.empty())
                return {};
            for (auto const* name : names)
            {
                const auto path = dir + L"\\" + name;
                if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
                    return path;
            }
            return {};
        }

        std::wstring ResolveGalleryDatabasePath()
        {
            return ResolveDatabasePath({
                L"libraryofstructures.irspdoc",
                L"LibraryOfStructures.irspdoc",
                L"Gallery.irspdoc",
            });
        }

        std::string WideToUtf8(std::wstring const& w)
        {
            if (w.empty())
                return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1)
                return {};
            std::string s(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
            return s;
        }

        // ZIP tree + lazy project blobs; do not unwrap yet. Qt reads the gallery and the
        // structure databases with one and the same routine, down to marking every node
        // read-only, and only parts company over which section they are grafted under.
        // The reason is reported rather than discarded. A database that will not open used to say
        // only that it had not opened, which for a file that is read before the window is even
        // usable leaves nothing to go on: whether it was absent, not an archive, of a version
        // this build does not know, or one project in it that could not be unwrapped.
        std::shared_ptr<DocumentData> ReadDatabaseDocument(std::wstring const& path,
                                                          std::wstring& reason)
        {
            if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                reason = L"no such file";
                return nullptr;
            }

            ZipReader reader(WideToUtf8(path));
            RKByteArray data = reader.fileData(std::string("nl.darkwing.iRASPA_projectData"));
            if (data.empty())
            {
                reader.close();
                reason = L"the archive holds no nl.darkwing.iRASPA_projectData";
                return nullptr;
            }

            BinaryArchive stream(std::move(data));
            auto libraryData = std::make_shared<DocumentData>();
            try
            {
                stream >> libraryData;
            }
            catch (std::exception const& ex)
            {
                reader.close();
                reason = L"the project tree could not be read: " +
                         std::wstring(winrt::to_hstring(ex.what()));
                return nullptr;
            }
            catch (...)
            {
                reader.close();
                reason = L"the project tree could not be read";
                return nullptr;
            }

            auto local = libraryData->projectTreeController()->localProjects();
            if (!local)
            {
                reader.close();
                reason = L"the project tree holds no local projects";
                return nullptr;
            }

            // One project that will not unwrap costs that project, not the whole database. It used
            // to cost the whole database: the throw left here and the caller kept nothing.
            std::wstring failures;
            int failed = 0;
            for (auto const& localNode : local->descendantNodes())
            {
                if (!localNode || !localNode->representedObject())
                    continue;
                try
                {
                    localNode->representedObject()->readData(reader);
                }
                catch (std::exception const& ex)
                {
                    if (++failed <= 3)
                    {
                        failures += L"\n  " + localNode->displayName().toStdWString() + L": " +
                                    std::wstring(winrt::to_hstring(ex.what()));
                    }
                    continue;
                }
                catch (...)
                {
                    ++failed;
                    continue;
                }
                localNode->setType(ProjectTreeNode::Type::gallery);
                localNode->setIsEditable(false);
            }
            reader.close();

            if (failed > 0)
            {
                reason = std::to_wstring(failed) + L" of " +
                         std::to_wstring(local->descendantNodes().size()) +
                         L" projects could not be read:" + failures;
            }
            return libraryData;
        }
    }

    winrt::fire_and_forget MainWindow::LoadGalleryDatabaseAsync()
    {
        auto dispatcher = DispatcherQueue();
        co_await winrt::resume_background();

        std::shared_ptr<DocumentData> database;
        std::wstring error;
        try
        {
            const auto path = ResolveGalleryDatabasePath();
            if (path.empty())
            {
                error = L"Gallery database not found (libraryofstructures.irspdoc)";
            }
            else
            {
                std::wstring reason;
                database = ReadDatabaseDocument(path, reason);
                if (!database)
                    error = L"Gallery database failed to load: " +
                            (reason.empty() ? std::wstring(L"no reason given") : reason);
                else if (!reason.empty())
                    error = L"Gallery database loaded, but " + reason;
            }
        }
        catch (std::exception const& ex)
        {
            error = std::wstring(L"Gallery load error: ") +
                    std::wstring(winrt::to_hstring(ex.what()));
        }
        catch (...)
        {
            error = L"Gallery load error";
        }

        dispatcher.TryEnqueue([this, database, error]()
        {
            try
            {
                if (!error.empty())
                    AppendLog(error);
                if (database)
                    m_controller.InsertGalleryData(database);
            }
            catch (...)
            {
                AppendLog(L"Gallery insert error");
            }
        });
    }

    // Qt runs one worker per database; here they are read in turn on a single background
    // hop instead. Reading only builds the ZIP tree and attaches the still-compressed
    // project blobs, which costs a few milliseconds per file, and going in order keeps the
    // section's rows in a fixed order rather than in whichever sequence the reads finish.
    winrt::fire_and_forget MainWindow::LoadStructureDatabasesAsync()
    {
        struct Database
        {
            wchar_t const* fileName;
            wchar_t const* label;
        };
        static constexpr Database kDatabases[] = {
            { L"databasecoremof.irspdoc",     L"CoRE MOF database" },
            { L"databasecoremofddec.irspdoc", L"CoRE MOF DDEC database" },
            { L"databaseiza.irspdoc",         L"IZA database" },
        };

        auto dispatcher = DispatcherQueue();
        co_await winrt::resume_background();

        for (auto const& entry : kDatabases)
        {
            std::shared_ptr<DocumentData> database;
            std::wstring error;
            const std::wstring label = entry.label;
            try
            {
                const auto path = ResolveDatabasePath({entry.fileName});
                if (path.empty())
                    error = label + L" not found (" + entry.fileName + L")";
                else
                {
                    std::wstring reason;
                    database = ReadDatabaseDocument(path, reason);
                    if (!database)
                        error = label + L" failed to load: " +
                                (reason.empty() ? std::wstring(L"no reason given") : reason);
                    else if (!reason.empty())
                        error = label + L" loaded, but " + reason;
                }
            }
            catch (std::exception const& ex)
            {
                error = label + L" load error: " + std::wstring(winrt::to_hstring(ex.what()));
            }
            catch (...)
            {
                error = label + L" load error";
            }

            dispatcher.TryEnqueue([this, database, error, label]()
            {
                try
                {
                    if (!error.empty())
                        AppendLog(error);
                    if (database)
                        m_controller.InsertDatabaseData(database, label);
                }
                catch (...)
                {
                    AppendLog(label + L" insert error");
                }
            });
        }
    }
}
