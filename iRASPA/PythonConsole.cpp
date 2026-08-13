#include "pch.h"
#include "MainWindow.xaml.h"

// Embedded Python console (port of Cocoa's InterpreterViewController).
// The embeddable CPython distribution ships inside the app, in a Python\ folder
// next to the executable, so nothing has to be installed on the user's machine.
// python314.dll is delay-loaded and EnsurePython() loads it explicitly from
// there; if the payload is missing the app still starts and the console just
// reports that Python is unavailable.

// pyconfig.h force-links python314_d.lib in _DEBUG builds; we always link the
// release Python (the standard approach for embedding).
#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG
#else
#include <Python.h>
#endif

#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.System.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        enum class PyState { NotStarted, Ready, Failed };
        PyState s_pyState = PyState::NotStarted;
        std::wstring s_pyError;

        // Must match the DelayLoadDLLs name in iRASPA.vcxproj: the delay-load stub
        // resolves the import by module name, so LoadLibrary has to produce a module
        // with exactly this base name. Bump both together with PythonAbi.
        constexpr wchar_t kPythonDll[] = L"python314.dll";
        constexpr wchar_t kPythonZip[] = L"python314.zip";

        // Cocoa bundles python3.9 inside PythonKit; the Windows equivalent is the
        // embeddable distribution copied into Python\ beside the executable.
        std::wstring BundledPythonDirectory()
        {
            wchar_t exePath[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            if (n == 0 || n >= MAX_PATH)
                return {};
            std::wstring dir(exePath, n);
            const size_t slash = dir.find_last_of(L'\\');
            if (slash == std::wstring::npos)
                return {};
            dir.resize(slash);
            return dir + L"\\Python";
        }

        // The bundled runtime wins; %PYTHONHOME% stays as a developer escape hatch
        // for pointing the console at a different interpreter. First match wins.
        std::vector<std::wstring> PythonHomeCandidates()
        {
            std::vector<std::wstring> homes;
            if (std::wstring bundled = BundledPythonDirectory(); !bundled.empty())
                homes.emplace_back(std::move(bundled));
            wchar_t env[MAX_PATH]{};
            if (GetEnvironmentVariableW(L"PYTHONHOME", env, MAX_PATH) > 0)
                homes.emplace_back(env);
            return homes;
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

        std::wstring Utf8ToWide(const char* s)
        {
            if (!s || !*s)
                return {};
            const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
            if (n <= 1)
                return {};
            std::wstring w(static_cast<size_t>(n - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
            return w;
        }

        // Same role as Cocoa's 'catch_out' module: swallow stdout/stderr into
        // a buffer the console can fetch after every command.
        constexpr char kBootstrapScript[] =
            "import sys, io\n"
            "class _IRaspaCatch:\n"
            "    def __init__(self):\n"
            "        self._buf = io.StringIO()\n"
            "    def write(self, s):\n"
            "        self._buf.write(s)\n"
            "    def flush(self):\n"
            "        pass\n"
            "    def fetch(self):\n"
            "        v = self._buf.getvalue()\n"
            "        self._buf = io.StringIO()\n"
            "        return v\n"
            "_iraspa_catch = _IRaspaCatch()\n"
            "sys.stdout = _iraspa_catch\n"
            "sys.stderr = _iraspa_catch\n";

        bool EnsurePython()
        {
            if (s_pyState == PyState::Ready)
                return true;
            if (s_pyState == PyState::Failed)
                return false;

            std::wstring home;
            HMODULE dll = nullptr;
            for (auto const& candidate : PythonHomeCandidates())
            {
                const std::wstring dllPath = candidate + L"\\" + kPythonDll;
                if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
                    continue;
                dll = LoadLibraryExW(dllPath.c_str(), nullptr,
                                     LOAD_WITH_ALTERED_SEARCH_PATH);
                if (dll)
                {
                    home = candidate;
                    break;
                }
            }
            if (!dll)
            {
                s_pyState = PyState::Failed;
                s_pyError = L"Python runtime not found (looked for ";
                s_pyError += kPythonDll;
                s_pyError += L" in the app's Python folder and %PYTHONHOME%).";
                return false;
            }

            PyConfig config;
            PyConfig_InitIsolatedConfig(&config);
            PyStatus status = PyConfig_SetString(&config, &config.home, home.c_str());
            // The embeddable distribution keeps the standard library in a zip beside
            // the DLL. Setting the search paths explicitly keeps initialization
            // independent of python314._pth and of any registry or environment state.
            if (!PyStatus_Exception(status))
            {
                config.module_search_paths_set = 1;
                status = PyWideStringList_Append(&config.module_search_paths,
                                                 (home + L"\\" + kPythonZip).c_str());
            }
            if (!PyStatus_Exception(status))
                status = PyWideStringList_Append(&config.module_search_paths, home.c_str());
            if (!PyStatus_Exception(status))
                status = Py_InitializeFromConfig(&config);
            PyConfig_Clear(&config);
            if (PyStatus_Exception(status))
            {
                s_pyState = PyState::Failed;
                s_pyError = L"Python initialization failed: " +
                            Utf8ToWide(status.err_msg ? status.err_msg : "unknown error");
                return false;
            }

            if (PyRun_SimpleString(kBootstrapScript) != 0)
            {
                s_pyState = PyState::Failed;
                s_pyError = L"Python output redirection failed.";
                Py_FinalizeEx();
                return false;
            }

            s_pyState = PyState::Ready;
            return true;
        }

        std::wstring FetchPythonOutput()
        {
            PyObject* main = PyImport_AddModule("__main__");
            if (!main)
                return {};
            PyObject* globals = PyModule_GetDict(main);
            PyObject* catcher = PyDict_GetItemString(globals, "_iraspa_catch");
            if (!catcher)
                return {};
            PyObject* text = PyObject_CallMethod(catcher, "fetch", nullptr);
            if (!text)
            {
                PyErr_Clear();
                return {};
            }
            std::wstring result = Utf8ToWide(PyUnicode_AsUTF8(text));
            Py_DECREF(text);
            return result;
        }

        // Py_single_input echoes expression results via sys.displayhook, so
        // `1+1` prints `2` like a real REPL (PyRun_SimpleString would not).
        std::wstring RunPythonLine(std::wstring const& command)
        {
            PyObject* main = PyImport_AddModule("__main__");
            if (!main)
                return L"<interpreter error>\n";
            PyObject* globals = PyModule_GetDict(main);
            const std::string utf8 = WideToUtf8(command);
            PyObject* result = PyRun_String(utf8.c_str(), Py_single_input, globals, globals);
            if (!result)
                PyErr_Print();
            else
                Py_DECREF(result);
            return FetchPythonOutput();
        }
    }

    Grid MainWindow::BuildPythonConsole()
    {
        auto grid = Grid();
        auto rows = grid.RowDefinitions();
        RowDefinition outRow;
        outRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        rows.Append(outRow);
        RowDefinition inRow;
        inRow.Height(GridLengthHelper::Auto());
        rows.Append(inRow);

        m_pythonOutputBox = TextBox();
        m_pythonOutputBox.IsReadOnly(true);
        m_pythonOutputBox.AcceptsReturn(true);
        m_pythonOutputBox.TextWrapping(TextWrapping::Wrap);
        m_pythonOutputBox.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
        Grid::SetRow(m_pythonOutputBox, 0);
        grid.Children().Append(m_pythonOutputBox);

        m_pythonInputBox = TextBox();
        m_pythonInputBox.PlaceholderText(L">>> Python (Enter runs, Up/Down history)");
        m_pythonInputBox.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(L"Consolas"));
        m_pythonInputBox.Margin(ThicknessHelper::FromLengths(0, 4, 0, 0));
        m_pythonInputBox.KeyDown({ this, &MainWindow::OnPythonInputKeyDown });
        Grid::SetRow(m_pythonInputBox, 1);
        grid.Children().Append(m_pythonInputBox);

        m_pythonOutput = L"Python console (embedded CPython)\n";
        m_pythonOutputBox.Text(m_pythonOutput);
        return grid;
    }

    void MainWindow::AppendPythonOutput(std::wstring const& text)
    {
        if (text.empty())
            return;
        m_pythonOutput += text;
        if (m_pythonOutputBox)
        {
            m_pythonOutputBox.Text(m_pythonOutput);
            // Keep the newest output visible.
            m_pythonOutputBox.SelectionStart(static_cast<int32_t>(m_pythonOutput.size()));
        }
    }

    void MainWindow::RunPythonConsoleCommand(std::wstring const& command)
    {
        AppendPythonOutput(L">>> " + command + L"\n");
        if (!EnsurePython())
        {
            AppendPythonOutput(s_pyError + L"\n");
            return;
        }
        AppendPythonOutput(RunPythonLine(command));
    }

    void MainWindow::OnPythonInputKeyDown(IInspectable const&, KeyRoutedEventArgs const& e)
    {
        if (!m_pythonInputBox)
            return;

        const auto key = e.Key();
        if (key == winrt::Windows::System::VirtualKey::Enter)
        {
            const std::wstring command{ m_pythonInputBox.Text() };
            if (!command.empty())
            {
                m_pythonHistory.push_back(command);
                m_pythonHistoryPos = static_cast<int>(m_pythonHistory.size());
                m_pythonInputBox.Text(L"");
                RunPythonConsoleCommand(command);
            }
            e.Handled(true);
        }
        else if (key == winrt::Windows::System::VirtualKey::Up)
        {
            if (!m_pythonHistory.empty() && m_pythonHistoryPos > 0)
            {
                --m_pythonHistoryPos;
                m_pythonInputBox.Text(m_pythonHistory[static_cast<size_t>(m_pythonHistoryPos)]);
                m_pythonInputBox.SelectionStart(static_cast<int32_t>(m_pythonInputBox.Text().size()));
            }
            e.Handled(true);
        }
        else if (key == winrt::Windows::System::VirtualKey::Down)
        {
            if (m_pythonHistoryPos < static_cast<int>(m_pythonHistory.size()) - 1)
            {
                ++m_pythonHistoryPos;
                m_pythonInputBox.Text(m_pythonHistory[static_cast<size_t>(m_pythonHistoryPos)]);
            }
            else
            {
                m_pythonHistoryPos = static_cast<int>(m_pythonHistory.size());
                m_pythonInputBox.Text(L"");
            }
            e.Handled(true);
        }
    }

    void MainWindow::ShutdownPython()
    {
        if (s_pyState == PyState::Ready)
        {
            Py_FinalizeEx();
            s_pyState = PyState::NotStarted;
        }
    }
}
