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
#include <atomic>
#include <future>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

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

            const std::string archivePathUtf8 = WideToUtf8(path);
            ZipReader reader(archivePathUtf8);
            RKByteArray data = reader.fileData(std::string("nl.darkwing.iRASPA_projectData"));
            reader.close();
            if (data.empty())
            {
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
                reason = L"the project tree could not be read: " +
                         std::wstring(winrt::to_hstring(ex.what()));
                return nullptr;
            }
            catch (...)
            {
                reason = L"the project tree could not be read";
                return nullptr;
            }

            auto local = libraryData->projectTreeController()->localProjects();
            if (!local)
            {
                reason = L"the project tree holds no local projects";
                return nullptr;
            }

            // Cocoa iRASPALoadDatabases.swift: load the ProjectTreeNode proxy hierarchy
            // from nl.darkwing.iRASPA_projectData at startup, and defer reading individual
            // nl.darkwing.iRASPA_Project_<UUID> entries on-demand when a structure is unwrapped.
            for (auto const& localNode : local->descendantNodes())
            {
                if (!localNode || !localNode->representedObject())
                    continue;
                localNode->representedObject()->setArchiveFileName(archivePathUtf8);
                localNode->setType(ProjectTreeNode::Type::gallery);
                localNode->setIsEditable(false);
            }

            // Pre-extract the 14 cached structural properties only for leaf nodes that do not
            // already have them serialized in nl.darkwing.iRASPA_projectData (e.g. local gallery).
            std::vector<std::shared_ptr<ProjectTreeNode>> uncachedLeaves;
            for (auto const& leaf : local->descendantLeafNodes())
            {
                if (leaf && leaf->representedObject() && !leaf->representedObject()->hasCachedPropertiesLoaded())
                    uncachedLeaves.push_back(leaf);
            }
            if (!uncachedLeaves.empty())
            {
                // Read the compressed project blobs once for the uncached leaves before parallel extraction.
                ZipReader leafReader(archivePathUtf8);
                for (auto const& leaf : uncachedLeaves)
                {
                    try
                    {
                        leaf->representedObject()->readData(leafReader);
                    }
                    catch (...)
                    {
                    }
                }
                leafReader.close();

                const unsigned int hw = std::max(1u, std::thread::hardware_concurrency());
                const size_t threadCount = std::min<size_t>(hw, uncachedLeaves.size());
                std::atomic<size_t> nextIndex{ 0 };
                std::vector<std::thread> workers;
                workers.reserve(threadCount);
                for (size_t t = 0; t < threadCount; ++t)
                {
                    workers.emplace_back([&]()
                    {
                        while (true)
                        {
                            const size_t idx = nextIndex.fetch_add(1, std::memory_order_relaxed);
                            if (idx >= uncachedLeaves.size())
                                break;
                            if (auto const& leaf = uncachedLeaves[idx]; leaf && leaf->representedObject())
                                leaf->representedObject()->ensureCachedPropertiesLoaded();
                        }
                    });
                }
                for (auto& w : workers)
                    w.join();
            }

            return libraryData;
        }
    }

    winrt::fire_and_forget MainWindow::LoadGalleryDatabaseAsync()
    {
        auto lifetime = get_strong();
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

        dispatcher.TryEnqueue([this, lifetime, database, error]()
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
        auto lifetime = get_strong();

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

        struct LoadedDatabase
        {
            std::shared_ptr<DocumentData> database;
            std::wstring error;
            std::wstring label;
        };

        std::vector<std::future<LoadedDatabase>> tasks;
        tasks.reserve(std::size(kDatabases));
        for (auto const& entry : kDatabases)
        {
            tasks.push_back(std::async(std::launch::async, [entry]() -> LoadedDatabase
            {
                LoadedDatabase out;
                out.label = entry.label;
                try
                {
                    const auto path = ResolveDatabasePath({entry.fileName});
                    if (path.empty())
                    {
                        out.error = out.label + L" not found (" + entry.fileName + L")";
                    }
                    else
                    {
                        std::wstring reason;
                        out.database = ReadDatabaseDocument(path, reason);
                        if (!out.database)
                            out.error = out.label + L" failed to load: " +
                                        (reason.empty() ? std::wstring(L"no reason given") : reason);
                        else if (!reason.empty())
                            out.error = out.label + L" loaded, but " + reason;
                    }
                }
                catch (std::exception const& ex)
                {
                    out.error = out.label + L" load error: " + std::wstring(winrt::to_hstring(ex.what()));
                }
                catch (...)
                {
                    out.error = out.label + L" load error";
                }
                return out;
            }));
        }

        for (auto& task : tasks)
        {
            LoadedDatabase res = task.get();
            dispatcher.TryEnqueue([this, lifetime, database = res.database, error = res.error, label = res.label]()
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
