#include "pch.h"
#include "MainWindow.xaml.h"
#include "ProjectView.xaml.h"

#include "binaryarchive.h"
#include "documentdata.h"
#include "iraspaproject.h"
#include "projecttreecontroller.h"
#include "projecttreenode.h"
#include "zipreader.h"
#include "zipwriter.h"

#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <microsoft.ui.xaml.window.h>
#include <Shobjidl.h>

#include <exception>
#include <string>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        std::string ToUtf8(std::wstring const& text)
        {
            if (text.empty())
                return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1)
                return {};
            std::string result(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), n, nullptr, nullptr);
            return result;
        }

        std::wstring FileNameOf(std::wstring const& path)
        {
            const auto pos = path.find_last_of(L"\\/");
            return pos == std::wstring::npos ? path : path.substr(pos + 1);
        }
    }

    // Cocoa iRASPADocument.write(to:ofType:): a zip holding the project-tree
    // index, the color and force-field sets, and one blob per local project.
    // Runs off the UI thread, so it only touches the document model.
    bool MainWindow::WriteDocumentToFile(std::wstring const& path, std::wstring& error)
    {
        auto document = m_document;
        if (!document)
        {
            error = L"No document to save";
            return false;
        }

        try
        {
            ZipWriter writer(ToUtf8(path));
            if (!writer.isWritable())
            {
                error = L"Cannot write " + FileNameOf(path);
                return false;
            }

            BinaryArchive tree;
            tree << document;
            writer.addFile("nl.darkwing.iRASPA_projectData", tree.buffer());

            BinaryArchive colors;
            colors << document->colorSets();
            writer.addFile("nl.darkwing.iRASPA_colorData", colors.buffer());

            BinaryArchive forceFields;
            forceFields << document->forceFieldSets();
            writer.addFile("nl.darkwing.iRASPA_forceFieldData", forceFields.buffer());

            // Only the local-projects subtree is persisted (DocumentData encodes
            // just that); the gallery comes from its own database on startup.
            // Projects still lazy are written back as the bytes they were read
            // as, loaded ones are re-encoded and xz-compressed by saveData.
            int projects = 0;
            if (auto controller = document->projectTreeController())
            {
                if (auto local = controller->localProjects())
                {
                    for (auto const& node : local->descendantNodes())
                    {
                        if (!node)
                            continue;
                        if (auto project = node->representedObject())
                        {
                            project->saveData(writer);
                            ++projects;
                        }
                    }
                }
            }

            writer.close();
            if (writer.status() != ZipWriter::NoError)
            {
                error = L"Write error while saving " + FileNameOf(path);
                return false;
            }

            m_savedProjectCount = projects;
            return true;
        }
        catch (std::exception const& ex)
        {
            error = std::wstring(L"Save failed: ") + winrt::to_hstring(ex.what()).c_str();
            return false;
        }
        catch (...)
        {
            error = L"Save failed";
            return false;
        }
    }

    // Cocoa saveDocument: / saveDocumentAs:. A document that has no file yet
    // asks for one, so plain Save on an untitled document behaves as Save As.
    winrt::fire_and_forget MainWindow::SaveDocumentAsync(bool saveAs)
    {
        auto lifetime = get_strong();

        if (m_saveInProgress)
        {
            AppendLog(L"Save already in progress");
            co_return;
        }

        std::wstring path = saveAs ? std::wstring() : m_documentPath;

        if (path.empty())
        {
            try
            {
                FileSavePicker picker;
                picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
                auto extensions = single_threaded_vector<hstring>();
                extensions.Append(L".irspdoc");
                picker.FileTypeChoices().Insert(L"iRASPA document", extensions);
                picker.DefaultFileExtension(L".irspdoc");
                picker.SuggestedFileName(m_documentPath.empty()
                                             ? hstring{ L"Untitled" }
                                             : hstring{ FileNameOf(m_documentPath) });

                HWND hwnd{};
                if (auto native = try_as<IWindowNative>())
                {
                    native->get_WindowHandle(&hwnd);
                    if (auto init = picker.as<IInitializeWithWindow>())
                        init->Initialize(hwnd);
                }

                StorageFile file = co_await picker.PickSaveFileAsync();
                if (!file)
                {
                    AppendLog(L"Save cancelled");
                    co_return;
                }
                path = std::wstring(file.Path());
            }
            catch (hresult_error const& ex)
            {
                AppendLog(std::wstring(L"Save failed: ") + std::wstring(ex.message()));
                co_return;
            }
        }

        m_saveInProgress = true;
        auto dispatcher = DispatcherQueue();
        co_await winrt::resume_background();

        std::wstring error;
        const bool ok = WriteDocumentToFile(path, error);

        dispatcher.TryEnqueue([this, lifetime, ok, path, error]()
        {
            m_saveInProgress = false;
            if (ok)
            {
                m_documentPath = path;
                Title(L"iRASPA - " + hstring{ FileNameOf(path) });
                AppendLog(L"Saved " + FileNameOf(path) + L" (" +
                          std::to_wstring(m_savedProjectCount) + L" project(s))");
            }
            else
            {
                AppendLog(error.empty() ? std::wstring(L"Save failed") : error);
            }
        });
    }

    // Cocoa iRASPADocument.readDocumentFileFormat(url:) — the counterpart of
    // the writer above. Projects are left lazy: only their compressed bytes are
    // read, they get decoded when the project is selected.
    std::shared_ptr<DocumentData> MainWindow::ReadDocumentFromFile(std::wstring const& path,
                                                                   std::wstring& error)
    {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            error = L"File not found: " + FileNameOf(path);
            return nullptr;
        }

        try
        {
            ZipReader reader(ToUtf8(path));
            RKByteArray treeData = reader.fileData(std::string("nl.darkwing.iRASPA_projectData"));
            if (treeData.empty())
            {
                reader.close();
                error = L"Not an iRASPA document: " + FileNameOf(path);
                return nullptr;
            }

            auto document = std::make_shared<DocumentData>();
            {
                BinaryArchive stream(std::move(treeData));
                stream >> document;
            }

            // The element colors and force fields travel with the document, but
            // an older file without them is still perfectly readable.
            try
            {
                RKByteArray colorData = reader.fileData(std::string("nl.darkwing.iRASPA_colorData"));
                if (!colorData.empty())
                {
                    BinaryArchive stream(std::move(colorData));
                    stream >> document->colorSets();
                }
            }
            catch (...)
            {
            }
            try
            {
                RKByteArray forceFieldData = reader.fileData(std::string("nl.darkwing.iRASPA_forceFieldData"));
                if (!forceFieldData.empty())
                {
                    BinaryArchive stream(std::move(forceFieldData));
                    stream >> document->forceFieldSets();
                }
            }
            catch (...)
            {
            }

            if (auto controller = document->projectTreeController())
            {
                if (auto local = controller->localProjects())
                {
                    for (auto const& node : local->descendantNodes())
                    {
                        if (node && node->representedObject())
                            node->representedObject()->readData(reader);
                    }
                }
            }

            reader.close();
            return document;
        }
        catch (std::exception const& ex)
        {
            error = std::wstring(L"Open failed: ") + winrt::to_hstring(ex.what()).c_str();
            return nullptr;
        }
        catch (...)
        {
            error = L"Open failed";
            return nullptr;
        }
    }

    // The opened file becomes this window's local projects; the gallery that was
    // loaded at startup stays where it is (Cocoa rebuilds it the same way).
    void MainWindow::ApplyOpenedDocument(std::shared_ptr<DocumentData> opened, std::wstring const& path)
    {
        if (!opened || !m_document)
            return;

        auto controller = m_document->projectTreeController();
        auto local = controller ? controller->localProjects() : nullptr;
        auto openedController = opened->projectTreeController();
        auto openedLocal = openedController ? openedController->localProjects() : nullptr;
        if (!local || !openedLocal)
        {
            AppendLog(L"Open failed: document has no projects");
            return;
        }

        // Nothing of the previous document survives, so drop what points into
        // it before the tree changes underneath.
        ClearProjectFromRenderer();
        controller->clearSelection();
        DocumentUndoStack().clear();
        if (auto *view = ProjectViewImpl())
            view->ClearExpandedNodes();
        local->childNodes().clear();

        // Copy then clear, so the temporary document does not keep ownership.
        auto children = openedLocal->childNodes();
        openedLocal->childNodes().clear();

        int inserted = 0;
        for (auto const& child : children)
        {
            if (!child)
                continue;
            controller->insertNodeInParent(child, local, inserted);
            ++inserted;
        }

        m_document->colorSets() = opened->colorSets();
        m_document->forceFieldSets() = opened->forceFieldSets();

        m_documentPath = path;
        Title(L"iRASPA - " + hstring{ FileNameOf(path) });
        UpdateEditMenuLabels();
        if (auto *view = ProjectViewImpl())
            view->ExpandSectionRoots();
        m_controller.RefreshSceneAndFrameRows();

        AppendLog(L"Opened " + FileNameOf(path) + L" (" + std::to_wstring(inserted) + L" project(s))");
    }

    winrt::fire_and_forget MainWindow::OpenDocumentAsync()
    {
        auto lifetime = get_strong();

        std::wstring path;
        try
        {
            FileOpenPicker picker;
            picker.ViewMode(PickerViewMode::List);
            picker.SuggestedStartLocation(PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L".irspdoc");

            HWND hwnd{};
            if (auto native = try_as<IWindowNative>())
            {
                native->get_WindowHandle(&hwnd);
                if (auto init = picker.as<IInitializeWithWindow>())
                    init->Initialize(hwnd);
            }

            StorageFile file = co_await picker.PickSingleFileAsync();
            if (!file)
            {
                AppendLog(L"Open cancelled");
                co_return;
            }
            path = std::wstring(file.Path());

            // Cocoa opens a second window; here the projects of this one are
            // replaced, so anything unsaved would be lost without asking.
            auto controller = m_document ? m_document->projectTreeController() : nullptr;
            auto local = controller ? controller->localProjects() : nullptr;
            if (local && local->childCount() > 0)
            {
                auto dialog = ContentDialog();
                dialog.XamlRoot(Content().XamlRoot());
                dialog.Title(box_value(L"Open Document"));
                dialog.Content(box_value(L"Opening " + hstring{ FileNameOf(path) } +
                                         L" replaces the projects in this window. Unsaved changes are lost."));
                dialog.PrimaryButtonText(L"Open");
                dialog.CloseButtonText(L"Cancel");
                dialog.DefaultButton(ContentDialogButton::Primary);
                if (co_await dialog.ShowAsync() != ContentDialogResult::Primary)
                {
                    AppendLog(L"Open cancelled");
                    co_return;
                }
            }
        }
        catch (hresult_error const& ex)
        {
            AppendLog(std::wstring(L"Open failed: ") + std::wstring(ex.message()));
            co_return;
        }

        auto dispatcher = DispatcherQueue();
        co_await winrt::resume_background();

        std::wstring error;
        auto opened = ReadDocumentFromFile(path, error);

        dispatcher.TryEnqueue([this, lifetime, opened, path, error]()
        {
            try
            {
                if (!opened)
                {
                    AppendLog(error.empty() ? std::wstring(L"Open failed") : error);
                    return;
                }
                ApplyOpenedDocument(opened, path);
            }
            catch (...)
            {
                AppendLog(L"Open failed");
            }
        });
    }

    void MainWindow::OnSaveClick([[maybe_unused]] IInspectable const&,
                                 [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        SaveDocumentAsync(false);
    }

    void MainWindow::OnSaveAsClick([[maybe_unused]] IInspectable const&,
                                   [[maybe_unused]] Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        SaveDocumentAsync(true);
    }
}
