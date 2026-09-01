#include "pch.h"
#include "CameraDetailView.xaml.h"
#if __has_include("CameraDetailView.g.cpp")
#include "CameraDetailView.g.cpp"
#endif

#include "DetailControls.h"
#include "ExportJobWriter.h"

#include "mathkit.h"
#include "rkglobalaxes.h"
#include "rkrenderuniforms.h"
#include "rkstring.h"

#include <Shobjidl.h>
#include <cmath>
#include <string>

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Globalization.NumberFormatting.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>

// C++/WinRT only ships resume_foreground for the Windows.System dispatcher, not WinUI's
// Microsoft.UI.Dispatching one; WIL supplies that overload, and only sees it if the
// projection header above is included first.
#include <wil/cppwinrt_helpers.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        constexpr double kRadiansPerDegree = M_PI / 180.0;
        constexpr double kDegreesPerRadian = 180.0 / M_PI;

        hstring FormatDouble(double value, int decimals)
        {
            wchar_t buffer[48];
            swprintf_s(buffer, L"%.*f", decimals, value);
            return hstring(buffer);
        }

        // Cocoa retitles the rotate buttons when the step changes.
        hstring RotateTitle(bool plus, double degrees)
        {
            wchar_t buffer[40];
            swprintf_s(buffer, L"Rotate %s%.4g\u00B0", plus ? L"+" : L"-", degrees);
            return hstring(buffer);
        }

        void Configure(NumberBox const& box, double minimum, double maximum, double step, int decimals)
        {
            box.Minimum(minimum);
            box.Maximum(maximum);
            box.SmallChange(step);
            box.LargeChange(step * 5.0);
            winrt::Windows::Globalization::NumberFormatting::DecimalFormatter formatter;
            formatter.IntegerDigits(1);
            formatter.FractionDigits(decimals);
            formatter.IsGrouped(false);
            box.NumberFormatter(formatter);
        }

        void ConfigureSlider(Slider const& slider, double minimum, double maximum, double step)
        {
            slider.Minimum(minimum);
            slider.Maximum(maximum);
            slider.StepFrequency(step);
        }

        // The light model names its styles and roles in ASCII, the form needs them wide.
        hstring ToHstring(char const* text)
        {
            return hstring(std::wstring(text, text + std::strlen(text)));
        }

        // Where a style sits in the picker: the presets in their listed order, Custom last.
        int StyleComboIndex(RKLightStyle style)
        {
            auto const& presets = RKLightStylePresets();
            for (size_t i = 0; i < presets.size(); ++i)
            {
                if (presets[i] == style)
                    return static_cast<int>(i);
            }
            return static_cast<int>(presets.size());
        }

        RKLightStyle StyleForComboIndex(int index)
        {
            auto const& presets = RKLightStylePresets();
            if (index >= 0 && index < static_cast<int>(presets.size()))
                return presets[static_cast<size_t>(index)];
            return RKLightStyle::custom;
        }

        void Fill(ComboBox const& combo, std::initializer_list<wchar_t const*> items)
        {
            for (auto const* name : items)
                combo.Items().Append(box_value(hstring(name)));
        }

        void Select(ComboBox const& combo, int index)
        {
            if (index >= 0 && index < static_cast<int>(combo.Items().Size()))
                combo.SelectedIndex(index);
        }

        // H.264 and HEVC only accept even extents; MovieWriter rounds the same way and
        // then insists on frames of exactly that size.
        int NearestEvenInt(int value)
        {
            return (value % 2 == 0) ? value : value + 1;
        }

        hstring FrameStatus(int frame, int frameCount)
        {
            wchar_t buffer[64];
            swprintf_s(buffer, L"Frame %d of %d", frame, frameCount);
            return hstring(buffer);
        }

        // Rendering an export in this process meant a second D3D12 workload on the very
        // adapter drawing the window, sharing the live view's command list and its VRAM,
        // and a driver fault in the middle of a 4K frame took the window with it. The
        // helper is the Windows counterpart of the Cocoa build's PictureCreationService
        // and MovieCreationService XPC services: its own process, its own device, on a
        // second GPU where the machine has one, and cancelling is killing it.
        struct ExportProcess
        {
            HANDLE process{ nullptr };
            HANDLE output{ nullptr };
        };

        bool StartExportHelper(std::wstring const& helperPath, std::wstring const& jobPath,
                               ExportProcess& started, std::wstring& error)
        {
            SECURITY_ATTRIBUTES inheritable{};
            inheritable.nLength = sizeof(inheritable);
            inheritable.bInheritHandle = TRUE;

            HANDLE readEnd = nullptr;
            HANDLE writeEnd = nullptr;
            if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
            {
                error = L"could not open a pipe to the export helper";
                return false;
            }
            SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = writeEnd;

            std::wstring commandLine = L"\"" + helperPath + L"\" --job \"" + jobPath + L"\"";
            PROCESS_INFORMATION process{};
            const BOOL launched = CreateProcessW(helperPath.c_str(), commandLine.data(), nullptr,
                                                 nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                                 &startup, &process);
            const DWORD lastError = GetLastError();
            // The child owns the writing end now; a copy left here would keep the pipe
            // open and the read below would never reach end of file.
            CloseHandle(writeEnd);
            if (!launched)
            {
                CloseHandle(readEnd);
                error = L"could not start " + helperPath + L" (error " + std::to_wstring(lastError) + L")";
                return false;
            }
            CloseHandle(process.hThread);

            started.process = process.hProcess;
            started.output = readEnd;
            return true;
        }

        // Blocks until the helper has written a whole line or closed its end.
        bool ReadHelperLine(HANDLE output, std::string& pending, std::string& line)
        {
            for (;;)
            {
                const size_t newline = pending.find('\n');
                if (newline != std::string::npos)
                {
                    line.assign(pending, 0, newline);
                    pending.erase(0, newline + 1);
                    return true;
                }
                char buffer[512];
                DWORD read = 0;
                if (!ReadFile(output, buffer, static_cast<DWORD>(sizeof(buffer)), &read, nullptr) || read == 0)
                {
                    if (pending.empty())
                        return false;
                    line = pending;
                    pending.clear();
                    return true;
                }
                pending.append(buffer, read);
            }
        }

        bool LineStartsWith(std::string const& line, char const* prefix, size_t length)
        {
            return line.size() >= length && line.compare(0, length, prefix) == 0;
        }

        bool ParseProgress(std::string const& line, int& completed, int& total)
        {
            const size_t space = line.find(' ', 9);
            if (space == std::string::npos)
                return false;
            try
            {
                completed = std::stoi(line.substr(9, space - 9));
                total = std::stoi(line.substr(space + 1));
            }
            catch (...)
            {
                return false;
            }
            return true;
        }

        std::wstring FromUtf8(std::string const& text)
        {
            if (text.empty())
                return {};
            const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                                 static_cast<int>(text.size()), nullptr, 0);
            if (size <= 0)
                return {};
            std::wstring wide(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                wide.data(), size);
            return wide;
        }
    }

    CameraDetailView::CameraDetailView()
    {
        InitializeComponent();
        DetailControls::FitFixedColumns(*this);

        BuildComboItems();
        BuildMovieFormats();
        ConfigureRanges();
        BuildViewMatrix();
        BuildLightSlots();
        WireSliderRows();
        WireColorWells();
    }

    void CameraDetailView::BuildComboItems()
    {
        Fill(AxesPosition(), { L"None", L"Bottom-left", L"Mid-left", L"Top-left", L"Mid-top",
                               L"Top-right", L"Mid-right", L"Bottom-right", L"Mid-bottom", L"Center" });
        Fill(AxesStyle(), { L"Default", L"Thick RGB", L"Thick", L"Thin RGB", L"Thin",
                            L"Beam Arrow RGB", L"Beam Arrow", L"Beam RGB", L"Beam",
                            L"Squashed RGB", L"Squashed" });
        Fill(AxesBackgroundStyle(), { L"None", L"Filled Circle", L"Filled Box", L"Filled Rounded Box",
                                      L"Circle", L"Box", L"Rounded Box" });
        Fill(ImageDpi(), { L"72 dpi", L"75 dpi", L"150 dpi", L"300 dpi", L"600 dpi", L"1200 dpi" });
        Fill(ImageQuality(), { L"8-bits, RGB", L"16-bits, RGB", L"8-bits, CMYK", L"16-bits, CMYK" });
        Fill(MovieType(), { L"Frames", L"Rotation Around Y", L"Rotation XY Lemniscate" });

        // The presets, then Custom, which stands for whatever editing a light has left behind and so
        // is shown rather than asked for.
        for (RKLightStyle style : RKLightStylePresets())
            LightStyle().Items().Append(box_value(ToHstring(RKLightStyleDisplayName(style))));
        LightStyle().Items().Append(box_value(ToHstring(RKLightStyleDisplayName(RKLightStyle::custom))));
    }

    void CameraDetailView::BuildMovieFormats()
    {
        m_movieFormats = MovieWriter::availableFormats();
        for (auto const format : m_movieFormats)
            MovieFormat().Items().Append(box_value(hstring(MovieWriter::displayName(format))));

        if (m_movieFormats.empty())
        {
            MovieFormat().Items().Append(box_value(L"No video encoder on this machine"));
            MovieFormat().IsEnabled(false);
            MakeMovieButton().IsEnabled(false);
        }
        MovieFormat().SelectedIndex(0);
    }

    void CameraDetailView::ConfigureRanges()
    {
        Configure(ResetFraction(), 1.0, 100.0, 1.0, 0);
        Configure(AngleOfView(), 1.0, 179.0, 1.0, 1);
        Configure(RotationAngle(), 0.1, 180.0, 1.0, 2);

        // Yaw and pitch turn all the way round; roll is the narrower Cocoa range.
        Configure(EulerXBox(), -180.0, 180.0, 1.0, 2);
        Configure(EulerZBox(), -180.0, 180.0, 1.0, 2);
        Configure(EulerYBox(), -90.0, 90.0, 1.0, 2);
        ConfigureSlider(EulerXSlider(), -180.0, 180.0, 1.0);
        ConfigureSlider(EulerZSlider(), -180.0, 180.0, 1.0);
        ConfigureSlider(EulerYSlider(), -90.0, 90.0, 1.0);

        Configure(BloomBox(), 0.0, 2.0, 0.05, 2);
        ConfigureSlider(BloomSlider(), 0.0, 2.0, 0.05);

        Configure(AxesSize(), 1.0, 50.0, 1.0, 1);
        Configure(AxesOffset(), 0.0, 25.0, 0.5, 1);
        Configure(AxesBackgroundSize(), 0.0, 100.0, 0.5, 2);
        for (auto const& box : { TextScaleX(), TextScaleY(), TextScaleZ() })
            Configure(box, 0.1, 5.0, 0.1, 2);
        for (auto const& box : { TextXDispX(), TextXDispY(), TextXDispZ(),
                                 TextYDispX(), TextYDispY(), TextYDispZ(),
                                 TextZDispX(), TextZDispY(), TextZDispZ() })
            Configure(box, -10.0, 10.0, 0.1, 2);

        for (auto const& box : { SceneAmbientBox(), OcclusionStrengthBox() })
            Configure(box, 0.0, 1.0, 0.01, 2);
        for (auto const& slider : { SceneAmbientSlider(), OcclusionStrengthSlider() })
            ConfigureSlider(slider, 0.0, 1.0, 0.01);

        // Whole paths and whole bounces, and capped at what an interactive frame can afford: render
        // time grows about linearly in both, so a value entered by mistake here stalls the frame loop.
        Configure(InteractiveSampleCount(), 1.0,
                  double(RKRenderSettings::maximumSupportedInteractiveSamples), 1.0, 0);
        Configure(InteractiveRotatingSampleCount(), 1.0,
                  double(RKRenderSettings::maximumSupportedInteractiveSamples), 1.0, 0);
        Configure(InteractiveMaximumBounces(), 0.0,
                  double(RKRenderSettings::maximumSupportedInteractiveBounces), 1.0, 0);

        // An export can be asked for far more of both than a frame can, nobody waiting on it a frame
        // at a time, but not for more than the tracer accepts.
        Configure(PictureSampleCount(), 1.0,
                  double(RKRenderSettings::maximumSupportedPictureSamples), 16.0, 0);
        Configure(PictureMaximumBounces(), 0.0,
                  double(RKRenderSettings::maximumSupportedPictureBounces), 1.0, 0);

        Configure(PhysicalWidth(), 0.1, 1000.0, 0.1, 2);
        Configure(PixelWidth(), 16.0, 16384.0, 16.0, 0);
        Configure(FramesPerSecond(), 1.0, 120.0, 1.0, 0);

        Configure(LinearAngleBox(), 0.0, 360.0, 1.0, 0);
        ConfigureSlider(LinearAngleSlider(), 0.0, 360.0, 1.0);
        Configure(RadialRoundnessBox(), 0.0, 5.0, 0.05, 2);
        ConfigureSlider(RadialRoundnessSlider(), 0.0, 5.0, 0.05);
    }

    void CameraDetailView::BuildViewMatrix()
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                TextBox box;
                box.Style(Resources().Lookup(box_value(L"Readout")).as<Microsoft::UI::Xaml::Style>());
                Grid::SetRow(box, row);
                Grid::SetColumn(box, column);
                ViewMatrix().Children().Append(box);
                m_matrix[row * 4 + column] = box;
            }
        }
    }

    // One bordered box per photographic role, all eight alike but for their title, so they are
    // generated from the form's own styles rather than written out eight times in the XAML.
    void CameraDetailView::BuildLightSlots()
    {
        auto const style = [this](wchar_t const* key)
        {
            return Resources().Lookup(box_value(key)).as<Microsoft::UI::Xaml::Style>();
        };
        const auto titledBox = style(L"TitledBox");
        const auto boxHeader = style(L"BoxHeader");
        const auto rowStyle = style(L"Row");
        const auto rowLabel = style(L"RowLabel");
        const auto rowSlider = style(L"RowSlider");
        const auto numStyle = style(L"Num");
        const auto colorWell = style(L"ColorWell");
        const auto swatchStyle = style(L"Swatch");

        // The label, slider and value columns of the rows written out in the XAML, so the generated
        // rows line up with the scene rows above them.
        auto const makeRow = [&](wchar_t const* label, bool withWell)
        {
            Grid row;
            row.Style(rowStyle);
            for (double width : { 150.0, -1.0, 148.0 })
            {
                ColumnDefinition column;
                column.Width(width < 0.0 ? GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star)
                                         : GridLengthHelper::FromPixels(width));
                row.ColumnDefinitions().Append(column);
            }
            if (withWell)
            {
                ColumnDefinition column;
                column.Width(GridLengthHelper::FromPixels(56.0));
                row.ColumnDefinitions().Append(column);
            }

            TextBlock caption;
            caption.Style(rowLabel);
            caption.Text(label);
            row.Children().Append(caption);
            return row;
        };

        // A slider, a number box and optionally a colour well, in the three trailing columns.
        auto const fillRow = [&](Grid const& row, Slider& slider, NumberBox& box,
                                 DropDownButton* well, Border* swatch)
        {
            slider = Slider();
            slider.Style(rowSlider);
            Grid::SetColumn(slider, 1);
            row.Children().Append(slider);

            box = NumberBox();
            box.Style(numStyle);
            box.MinWidth(64.0);
            Grid::SetColumn(box, 2);
            row.Children().Append(box);

            if (well && swatch)
            {
                *swatch = Border();
                swatch->Style(swatchStyle);
                *well = DropDownButton();
                well->Style(colorWell);
                well->Content(*swatch);
                Grid::SetColumn(*well, 3);
                row.Children().Append(*well);
            }
        };

        for (size_t index = 0; index < RKLight::numberOfRoles; ++index)
        {
            LightSlot& slot = m_lightSlots[index];

            StackPanel rows;
            rows.Spacing(2.0);

            // Enabled and the light type, which is all a light that is off has to offer.
            Grid header;
            header.Style(rowStyle);
            for (double width : { 150.0, -1.0 })
            {
                ColumnDefinition column;
                column.Width(width < 0.0 ? GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star)
                                         : GridLengthHelper::FromPixels(width));
                header.ColumnDefinitions().Append(column);
            }
            slot.enabled = CheckBox();
            slot.enabled.Content(box_value(L"Enabled"));
            header.Children().Append(slot.enabled);
            slot.type = ComboBox();
            slot.type.HorizontalAlignment(HorizontalAlignment::Stretch);
            Fill(slot.type, { L"Directional", L"Point", L"Spot" });
            Grid::SetColumn(slot.type, 1);
            header.Children().Append(slot.type);
            rows.Children().Append(header);

            Grid diffuseRow = makeRow(L"Diffuse Light Intensity", true);
            fillRow(diffuseRow, slot.diffuseSlider, slot.diffuseBox, &slot.diffuseWell, &slot.diffuseSwatch);
            rows.Children().Append(diffuseRow);

            Grid specularRow = makeRow(L"Specular Light Intensity", true);
            fillRow(specularRow, slot.specularSlider, slot.specularBox, &slot.specularWell, &slot.specularSwatch);
            rows.Children().Append(specularRow);

            Grid shininessRow = makeRow(L"Shininess", false);
            fillRow(shininessRow, slot.shininessSlider, slot.shininessBox, nullptr, nullptr);
            rows.Children().Append(shininessRow);

            Configure(slot.diffuseBox, 0.0, 1.0, 0.01, 2);
            ConfigureSlider(slot.diffuseSlider, 0.0, 1.0, 0.01);
            Configure(slot.specularBox, 0.0, 1.0, 0.01, 2);
            ConfigureSlider(slot.specularSlider, 0.0, 1.0, 0.01);
            Configure(slot.shininessBox, 0.1, 128.0, 0.1, 1);
            ConfigureSlider(slot.shininessSlider, 0.1, 128.0, 0.1);

            TextBlock title;
            title.Style(boxHeader);
            title.Text(ToHstring(RKLightRoleDisplayName(RKLight::Role(index))));

            StackPanel body;
            body.Spacing(4.0);
            body.Children().Append(title);
            body.Children().Append(rows);

            Border box;
            box.Style(titledBox);
            box.Child(body);
            LightSlots().Children().Append(box);

            slot.enabled.Checked([this, index](IInspectable const&, RoutedEventArgs const&)
            {
                ApplyToLight(index, [](RKLight& light) { light.setEnabled(true); });
            });
            slot.enabled.Unchecked([this, index](IInspectable const&, RoutedEventArgs const&)
            {
                ApplyToLight(index, [](RKLight& light) { light.setEnabled(false); });
            });
            slot.type.SelectionChanged([this, index](IInspectable const& sender, SelectionChangedEventArgs const&)
            {
                const int selected = sender.as<ComboBox>().SelectedIndex();
                if (selected < 0)
                    return;
                ApplyToLight(index, [selected](RKLight& light) { light.setType(RKLightType(selected)); });
            });

            DetailControls::SyncSliderAndBox(slot.diffuseSlider, slot.diffuseBox, [this, index](double value)
            {
                ApplyToLight(index, [value](RKLight& light)
                             { light.setDiffuseIntensity(std::clamp(value, 0.0, 1.0)); });
            });
            DetailControls::SyncSliderAndBox(slot.specularSlider, slot.specularBox, [this, index](double value)
            {
                ApplyToLight(index, [value](RKLight& light)
                             { light.setSpecularIntensity(std::clamp(value, 0.0, 1.0)); });
            });
            DetailControls::SyncSliderAndBox(slot.shininessSlider, slot.shininessBox, [this, index](double value)
            {
                ApplyToLight(index, [value](RKLight& light)
                             { light.setShininess(std::clamp(value, 0.1, 128.0)); });
            });
            DetailControls::AttachColorWell(slot.diffuseWell, slot.diffuseSwatch, [this, index](RKColor color)
            {
                ApplyToLight(index, [color](RKLight& light) { light.setDiffuseColor(color); });
            });
            DetailControls::AttachColorWell(slot.specularWell, slot.specularSwatch, [this, index](RKColor color)
            {
                ApplyToLight(index, [color](RKLight& light) { light.setSpecularColor(color); });
            });
        }
    }

    void CameraDetailView::WireSliderRows()
    {
        // The three Euler rows write one component of the camera's rotation. Which
        // control the value came from does not matter, so both halves of the row
        // share the one callback.
        struct EulerRow { Slider slider; NumberBox box; int axis; };
        const EulerRow rows[3] = {
            { EulerXSlider(), EulerXBox(), 0 },
            { EulerZSlider(), EulerZBox(), 2 },
            { EulerYSlider(), EulerYBox(), 1 },
        };
        for (auto const& row : rows)
        {
            DetailControls::SyncSliderAndBox(row.slider, row.box, [this, axis = row.axis](double degrees)
            {
                if (m_suppressEvents || !m_host)
                    return;
                auto camera = m_host->PaneCamera();
                if (!camera)
                    return;
                double3 euler = camera->EulerAngles();
                const double radians = degrees * kRadiansPerDegree;
                if (axis == 0)
                    euler.x = radians;
                else if (axis == 1)
                    euler.y = radians;
                else
                    euler.z = radians;
                camera->setWorldRotation(simd_quatd(euler));
                m_host->RedrawRenderer();
                ReloadReadouts();
            });
        }

        DetailControls::SyncSliderAndBox(BloomSlider(), BloomBox(), [this](double value)
        {
            if (m_suppressEvents || !m_host)
                return;
            if (auto camera = m_host->PaneCamera())
                camera->setBloomLevel(value);
            m_host->ReloadRenderer();
        });

        DetailControls::SyncSliderAndBox(SceneAmbientSlider(), SceneAmbientBox(), [this](double value)
        {
            ApplySceneLighting([value](ProjectStructure& project)
                               { project.setSceneAmbientIntensity(std::clamp(value, 0.0, 1.0)); },
                               false);
        });
        DetailControls::SyncSliderAndBox(OcclusionStrengthSlider(), OcclusionStrengthBox(), [this](double value)
        {
            ApplySceneLighting([value](ProjectStructure& project)
                               { project.setAmbientOcclusionStrength(std::clamp(value, 0.0, 1.0)); },
                               true);
        });

        DetailControls::SyncSliderAndBox(LinearAngleSlider(), LinearAngleBox(), [this](double value)
        {
            if (m_suppressEvents || !m_host)
                return;
            if (auto project = m_host->PaneProject())
            {
                project->setLinearGradientAngle(value);
                m_host->ReloadRenderer();
            }
        });
        DetailControls::SyncSliderAndBox(RadialRoundnessSlider(), RadialRoundnessBox(), [this](double value)
        {
            if (m_suppressEvents || !m_host)
                return;
            if (auto project = m_host->PaneProject())
            {
                project->setRadialGradientRoundness(value);
                m_host->ReloadRenderer();
            }
        });
    }

    void CameraDetailView::WireColorWells()
    {
        DetailControls::AttachColorWell(AxesBackgroundWell(), AxesBackgroundSwatch(), [this](RKColor color)
        {
            if (!m_host)
                return;
            auto project = m_host->PaneProject();
            if (!project || !project->axes())
                return;
            project->axes()->setAxesBackgroundColor(color);
            m_host->ReloadRenderer();
        });

        struct TextWell
        {
            DropDownButton button;
            Border swatch;
            void (RKGlobalAxes::*set)(RKColor);
        };
        const TextWell wells[3] = {
            { TextXWell(), TextXSwatch(), &RKGlobalAxes::setTextColorX },
            { TextYWell(), TextYSwatch(), &RKGlobalAxes::setTextColorY },
            { TextZWell(), TextZSwatch(), &RKGlobalAxes::setTextColorZ },
        };
        for (auto const& well : wells)
        {
            DetailControls::AttachColorWell(well.button, well.swatch, [this, set = well.set](RKColor color)
            {
                if (!m_host)
                    return;
                auto project = m_host->PaneProject();
                if (!project || !project->axes())
                    return;
                ((*project->axes()).*set)(color);
                m_host->ReloadRenderer();
            });
        }

        DetailControls::AttachColorWell(SceneAmbientWell(), SceneAmbientSwatch(), [this](RKColor color)
        {
            ApplySceneLighting([color](ProjectStructure& project) { project.setSceneAmbientColor(color); },
                               false);
        });

        struct BackgroundWell
        {
            DropDownButton button;
            Border swatch;
            void (ProjectStructure::*set)(RKColor);
        };
        const BackgroundWell backgroundWells[5] = {
            { BgColorWell(), BgColorSwatch(), &ProjectStructure::setBackgroundColor },
            { LinearFromWell(), LinearFromSwatch(), &ProjectStructure::setLinearGradientFromColor },
            { LinearToWell(), LinearToSwatch(), &ProjectStructure::setLinearGradientToColor },
            { RadialFromWell(), RadialFromSwatch(), &ProjectStructure::setRadialGradientFromColor },
            { RadialToWell(), RadialToSwatch(), &ProjectStructure::setRadialGradientToColor },
        };
        for (auto const& well : backgroundWells)
        {
            DetailControls::AttachColorWell(well.button, well.swatch, [this, set = well.set](RKColor color)
            {
                if (!m_host)
                    return;
                if (auto project = m_host->PaneProject())
                {
                    ((*project).*set)(color);
                    m_host->ReloadRenderer();
                }
            });
        }
    }

    std::shared_ptr<ProjectStructure> CameraDetailView::LightProject() const
    {
        return m_host ? m_host->PaneProject() : nullptr;
    }

    std::shared_ptr<RKLight> CameraDetailView::LightAt(size_t slot) const
    {
        auto project = LightProject();
        if (!project || slot >= project->renderLights().size())
            return nullptr;
        return project->renderLights()[slot];
    }

    // The light uniforms are rebuilt from the project every frame, so a redraw is all an edit needs;
    // none of this is undoable, as in Cocoa.
    void CameraDetailView::ApplyToLight(size_t slot, std::function<void(RKLight&)> change)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto project = LightProject();
        auto light = LightAt(slot);
        if (!project || !light)
            return;

        change(*light);
        project->recheckLightStyle();

        // Enabling a light changes which of its own controls are usable, so the whole box follows.
        m_suppressEvents = true;
        ReloadLightSlot(slot);
        ReloadLightStyle();
        m_suppressEvents = false;

        m_host->RedrawRenderer();
    }

    void CameraDetailView::ApplySceneLighting(std::function<void(ProjectStructure&)> change,
                                              bool rebakesOcclusion)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto project = LightProject();
        if (!project)
            return;

        change(*project);
        project->recheckLightStyle();

        m_suppressEvents = true;
        ReloadLightStyle();
        m_suppressEvents = false;

        // The occlusion strength only grades occlusion that is already baked, so even that is a
        // redraw rather than a reload.
        (void)rebakesOcclusion;
        m_host->RedrawRenderer();
    }

    void CameraDetailView::OnLightStyleChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto project = LightProject();
        if (!project)
            return;

        // Custom stands for the lights that are already there, so choosing it changes nothing.
        const RKLightStyle style = StyleForComboIndex(LightStyle().SelectedIndex());
        if (style == RKLightStyle::custom)
        {
            m_suppressEvents = true;
            ReloadLightStyle();
            m_suppressEvents = false;
            return;
        }

        project->setLightStyle(style);

        // A style rewrites every light, the scene ambient and the occlusion strength. Whether any of
        // the new lights casts a shadow decides what an export costs, so that is restated too.
        m_suppressEvents = true;
        ReloadLights();
        ReloadPictures();
        m_suppressEvents = false;

        m_host->RedrawRenderer();
    }

    // How an export is rendered, which travels with the document rather than with the machine. The
    // render view is left alone by all of it save the shadow setting, which the rasterizer reads.
    void CameraDetailView::OnPictureRayTracingToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        if (auto project = m_host->PaneProject())
        {
            auto const state = PictureRayTracing().IsChecked();
            project->setPictureRayTracing(state && state.Value());

            // The counts and the shadow box follow the checkbox.
            m_suppressEvents = true;
            ReloadPictures();
            m_suppressEvents = false;
        }
    }

    void CameraDetailView::OnPictureShadowsToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        if (auto project = m_host->PaneProject())
        {
            auto const state = PictureShadows().IsChecked();
            project->setShadows(state && state.Value());

            // What an export now costs has changed with it.
            m_suppressEvents = true;
            ReloadPictures();
            m_suppressEvents = false;

            // The render view reads the same setting, so it has something to show for it.
            m_host->RedrawRenderer();
        }
    }

    // Both clamp on the way in, so what the box shows afterwards is what was stored rather than what
    // was typed.
    void CameraDetailView::OnPictureSampleCountChanged(NumberBox const& sender,
                                                       NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        if (auto project = m_host->PaneProject())
        {
            project->setPictureSampleCount(int(value));

            m_suppressEvents = true;
            sender.Value(double(project->picturePathTracerSampleCount()));
            m_suppressEvents = false;
        }
    }

    void CameraDetailView::OnPictureMaximumBouncesChanged(NumberBox const& sender,
                                                          NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        if (auto project = m_host->PaneProject())
        {
            project->setPictureMaximumBounces(int(value));

            m_suppressEvents = true;
            sender.Value(double(project->picturePathTracerMaximumBounces()));
            m_suppressEvents = false;
        }
    }

    void CameraDetailView::Reload()
    {
        if (!m_host)
            return;

        auto project = m_host->PaneProject();
        auto camera = m_host->PaneCamera();
        Hint().Visibility((!project && !camera) ? Visibility::Visible : Visibility::Collapsed);

        m_suppressEvents = true;
        try
        {
            ReloadCameraSection();
            ReloadAxes();
            ReloadLights();
            ReloadRaytracing();
            ReloadPictures();
            ReloadBackground();
        }
        catch (...)
        {
        }
        m_suppressEvents = false;

        ReloadReadouts();
    }

    void CameraDetailView::ReloadCameraSection()
    {
        auto camera = m_host ? m_host->PaneCamera() : nullptr;

        ResetFraction().Value(camera ? camera->resetFraction() * 100.0 : 85.0);

        const int direction = camera ? static_cast<int>(camera->resetCameraDirection()) : 0;
        RadioButton directions[6] = { DirPlusX(), DirPlusY(), DirPlusZ(),
                                      DirMinusX(), DirMinusY(), DirMinusZ() };
        for (int i = 0; i < 6; ++i)
            directions[i].IsChecked(i == direction);

        Orthographic().IsChecked(camera ? camera->isOrthographic() : true);
        Perspective().IsChecked(camera ? camera->isPerspective() : false);
        AngleOfView().Value(camera ? camera->angleOfView() * kDegreesPerRadian : 60.0);

        const double delta = camera ? camera->rotationAngle() : 10.0;
        RotationAngle().Value(delta);
        Button buttons[6] = { YawPlus(), YawMinus(), PitchPlus(), PitchMinus(), RollPlus(), RollMinus() };
        for (int i = 0; i < 6; ++i)
            buttons[i].Content(box_value(RotateTitle((i % 2) == 0, delta)));
    }

    void CameraDetailView::ReloadReadouts()
    {
        auto camera = m_host ? m_host->PaneCamera() : nullptr;
        if (!camera)
            return;

        const bool wasSuppressed = m_suppressEvents;
        m_suppressEvents = true;

        const double3 euler = camera->EulerAngles();
        DetailControls::SetSliderAndBox(EulerXSlider(), EulerXBox(), euler.x * kDegreesPerRadian);
        DetailControls::SetSliderAndBox(EulerYSlider(), EulerYBox(), euler.y * kDegreesPerRadian);
        DetailControls::SetSliderAndBox(EulerZSlider(), EulerZBox(), euler.z * kDegreesPerRadian);

        const double3 center = camera->centerOfScene();
        Center0().Text(FormatDouble(center.x, 3));
        Center1().Text(FormatDouble(center.y, 3));
        Center2().Text(FormatDouble(center.z, 3));

        const double4x4 view = camera->modelViewMatrix();
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                if (auto box = m_matrix[row * 4 + column])
                    box.Text(FormatDouble(view.mm[column][row], 4));
            }
        }

        const double3 position = camera->position();
        Position0().Text(FormatDouble(position.x, 3));
        Position1().Text(FormatDouble(position.y, 3));
        Position2().Text(FormatDouble(position.z, 3));
        Distance().Text(FormatDouble(std::sqrt(position.x * position.x + position.y * position.y +
                                               position.z * position.z), 3));

        m_suppressEvents = wasSuppressed;
    }

    void CameraDetailView::ReloadAxes()
    {
        auto project = m_host ? m_host->PaneProject() : nullptr;
        auto axes = project ? project->axes() : nullptr;
        AxesHint().Visibility(axes ? Visibility::Collapsed : Visibility::Visible);
        AxesBody().Visibility(axes ? Visibility::Visible : Visibility::Collapsed);
        if (!axes)
            return;

        Select(AxesPosition(), static_cast<int>(axes->position()));
        Select(AxesStyle(), static_cast<int>(axes->style()));
        AxesSize().Value(axes->sizeScreenFraction() * 100.0);
        AxesOffset().Value(axes->borderOffsetScreenFraction() * 100.0);
        Select(AxesBackgroundStyle(), static_cast<int>(axes->axesBackgroundStyle()));
        DetailControls::SetColorWell(AxesBackgroundSwatch(), axes->axesBackgroundColor());
        AxesBackgroundSize().Value(axes->axesBackgroundAdditionalSize());

        const double3 scale = axes->textScale();
        TextScaleX().Value(scale.x);
        TextScaleY().Value(scale.y);
        TextScaleZ().Value(scale.z);

        struct TextRow
        {
            double3 displacement;
            RKColor color;
            NumberBox boxes[3];
            Border swatch;
        };
        const TextRow rows[3] = {
            { axes->textDisplacementX(), axes->textColorX(),
              { TextXDispX(), TextXDispY(), TextXDispZ() }, TextXSwatch() },
            { axes->textDisplacementY(), axes->textColorY(),
              { TextYDispX(), TextYDispY(), TextYDispZ() }, TextYSwatch() },
            { axes->textDisplacementZ(), axes->textColorZ(),
              { TextZDispX(), TextZDispY(), TextZDispZ() }, TextZSwatch() },
        };
        for (auto const& row : rows)
        {
            row.boxes[0].Value(row.displacement.x);
            row.boxes[1].Value(row.displacement.y);
            row.boxes[2].Value(row.displacement.z);
            DetailControls::SetColorWell(row.swatch, row.color);
        }
    }

    void CameraDetailView::ReloadLightStyle()
    {
        auto project = LightProject();
        LightStyle().IsEnabled(project != nullptr);
        Select(LightStyle(), StyleComboIndex(project ? project->renderLightStyle()
                                                    : RKLightStyle::standard));
    }

    void CameraDetailView::ReloadLightSlot(size_t slot)
    {
        LightSlot const& controls = m_lightSlots[slot];
        auto light = LightAt(slot);

        // Everything but the checkbox is pointless while the light is off, so it follows the checkbox.
        const bool isOn = light && light->isEnabled();
        controls.enabled.IsEnabled(light != nullptr);
        controls.enabled.IsChecked(isOn);

        for (auto const& control : { controls.diffuseSlider, controls.specularSlider,
                                     controls.shininessSlider })
            control.IsEnabled(isOn);
        for (auto const& control : { controls.diffuseBox, controls.specularBox, controls.shininessBox })
            control.IsEnabled(isOn);
        controls.type.IsEnabled(isOn);
        controls.diffuseWell.IsEnabled(isOn);
        controls.specularWell.IsEnabled(isOn);

        if (!light)
            return;

        Select(controls.type, static_cast<int>(light->type()));
        DetailControls::SetSliderAndBox(controls.diffuseSlider, controls.diffuseBox,
                                        light->diffuseIntensity());
        DetailControls::SetSliderAndBox(controls.specularSlider, controls.specularBox,
                                        light->specularIntensity());
        DetailControls::SetSliderAndBox(controls.shininessSlider, controls.shininessBox,
                                        light->shininess());
        DetailControls::SetColorWell(controls.diffuseSwatch, light->diffuseColor());
        DetailControls::SetColorWell(controls.specularSwatch, light->specularColor());
    }

    void CameraDetailView::ReloadLights()
    {
        auto project = LightProject();
        LightsHint().Visibility(project ? Visibility::Collapsed : Visibility::Visible);
        LightsBody().Visibility(project ? Visibility::Visible : Visibility::Collapsed);

        ReloadLightStyle();
        for (size_t slot = 0; slot < RKLight::numberOfRoles; ++slot)
            ReloadLightSlot(slot);

        if (!project)
            return;

        DetailControls::SetSliderAndBox(SceneAmbientSlider(), SceneAmbientBox(),
                                        project->renderSceneAmbientIntensity());
        DetailControls::SetColorWell(SceneAmbientSwatch(), project->renderSceneAmbientColor());
        DetailControls::SetSliderAndBox(OcclusionStrengthSlider(), OcclusionStrengthBox(),
                                        project->renderAmbientOcclusionStrength());
    }

    void CameraDetailView::ReloadRaytracing()
    {
        RKRenderSettings& settings = RKRenderSettings::shared();

        // These describe the machine, so unlike everything else in this pane they are readable
        // without a project. Only acting on them needs one, there being nothing to render otherwise.
        const bool canTrace = RKRenderSettings::isRayTracingSupported();
        const bool hasProject = m_host && m_host->PaneProject() != nullptr;
        const bool isRayTracing = settings.interactiveRenderMode() == RKRenderMode::rayTracing;

        InteractiveRayTracing().IsEnabled(hasProject && canTrace);
        InteractiveRayTracing().IsChecked(isRayTracing);

        // The counts and the bounce limit only mean anything to the tracer, so they follow it.
        const bool tracerSettingsApply = hasProject && canTrace && isRayTracing;
        InteractiveSampleCount().IsEnabled(tracerSettingsApply);
        InteractiveSampleCount().Value(double(settings.interactiveSampleCount()));
        InteractiveRotatingSampleCount().IsEnabled(tracerSettingsApply);
        InteractiveRotatingSampleCount().Value(double(settings.interactiveRotatingSampleCount()));
        InteractiveMaximumBounces().IsEnabled(tracerSettingsApply);
        InteractiveMaximumBounces().Value(double(settings.interactiveMaximumBounces()));

        // A path-traced frame works out its own shadows, one ray per light per hit, so there is
        // nothing left to ask for and the box says so rather than appearing to be ignored.
        if (isRayTracing)
        {
            InteractiveShadows().IsEnabled(false);
            InteractiveShadows().IsChecked(true);
        }
        else
        {
            InteractiveShadows().IsEnabled(hasProject && canTrace);
            InteractiveShadows().IsChecked(settings.interactiveShadows());
        }

        if (!canTrace)
        {
            RaytracingHint().Visibility(Visibility::Visible);
            RaytracingHint().Text(m_host ? m_host->LiveRaytracingStatus() + L"."
                                         : L"There is no render view to trace with.");
        }
        else if (!RKRenderSettings::tracesRaysInHardware())
        {
            // Worth saying: on this adapter every frame pays for the traversal in a shader, which is
            // why the shadow setting starts off here where it starts on elsewhere.
            RaytracingHint().Visibility(Visibility::Visible);
            RaytracingHint().Text(L"This adapter traces rays in a shader rather than in hardware, so "
                                  L"every frame pays for them. Pictures and movies are rendered "
                                  L"elsewhere and are unaffected.");
        }
        else
        {
            RaytracingHint().Visibility(Visibility::Collapsed);
        }
    }

    void CameraDetailView::OnInteractiveRayTracingToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto const state = InteractiveRayTracing().IsChecked();
        RKRenderSettings::shared().setInteractiveRenderMode(
            (state && state.Value()) ? RKRenderMode::rayTracing : RKRenderMode::rasterization);

        // The counts and the shadow box follow the checkbox.
        m_suppressEvents = true;
        ReloadRaytracing();
        m_suppressEvents = false;

        m_host->RedrawRenderer();
    }

    void CameraDetailView::OnInteractiveShadowsToggled(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto const state = InteractiveShadows().IsChecked();
        RKRenderSettings::shared().setInteractiveShadows(state && state.Value());
        m_host->RedrawRenderer();
    }

    // The three counts clamp on the way in, so what the box shows afterwards is what was stored
    // rather than what was typed.
    void CameraDetailView::OnInteractiveSampleCountChanged(NumberBox const& sender,
                                                           NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        RKRenderSettings& settings = RKRenderSettings::shared();
        settings.setInteractiveSampleCount(int(value));

        m_suppressEvents = true;
        sender.Value(double(settings.interactiveSampleCount()));
        m_suppressEvents = false;

        m_host->RedrawRenderer();
    }

    void CameraDetailView::OnInteractiveRotatingSampleCountChanged(NumberBox const& sender,
                                                                   NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        RKRenderSettings& settings = RKRenderSettings::shared();
        settings.setInteractiveRotatingSampleCount(int(value));

        m_suppressEvents = true;
        sender.Value(double(settings.interactiveRotatingSampleCount()));
        m_suppressEvents = false;

        // No redraw: this only takes effect while the camera is being moved, which redraws anyway.
    }

    void CameraDetailView::OnInteractiveMaximumBouncesChanged(NumberBox const& sender,
                                                              NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        RKRenderSettings& settings = RKRenderSettings::shared();
        settings.setInteractiveMaximumBounces(int(value));

        m_suppressEvents = true;
        sender.Value(double(settings.interactiveMaximumBounces()));
        m_suppressEvents = false;

        m_host->RedrawRenderer();
    }

    void CameraDetailView::ReloadPictures()
    {
        auto project = m_host ? m_host->PaneProject() : nullptr;
        PicturesHint().Visibility(project ? Visibility::Collapsed : Visibility::Visible);
        PicturesBody().Visibility(project ? Visibility::Visible : Visibility::Collapsed);
        if (!project)
            return;

        // Nothing here is disabled for want of a graphics card that can trace, the way Cocoa disables
        // it: there an export is rendered on the same device as the render view, and here it is
        // rendered by a separate process which falls back to the software adapter. Slowly, but an
        // export is not a frame anyone is waiting on.
        const bool tracing = project->renderPictureRayTracing();
        PictureRayTracing().IsChecked(tracing);
        PictureSampleCount().IsEnabled(tracing);
        PictureSampleCount().Value(double(project->picturePathTracerSampleCount()));
        PictureMaximumBounces().IsEnabled(tracing);
        PictureMaximumBounces().Value(double(project->picturePathTracerMaximumBounces()));

        // On by default and never taken away: shadows are traced, and a machine that cannot trace
        // them in its graphics card can still trace them in software, which for an export is only a
        // question of waiting. It stays switchable while ray-tracing is on, where it no longer
        // decides anything about the export -- a traced image works its own shadows out, one ray per
        // light per hit -- because the render view reads the same setting.
        PictureShadows().IsEnabled(true);
        PictureShadows().IsChecked(project->renderShadows());

        std::wstring hint = tracing
            ? L"Traced, which takes minutes rather than moments, and works its own shadows out."
            : L"Rasterized, in a moment.";
        if (!project->wantsShadows())
        {
            // Either switched off, or on under a rig whose lights all sit on the camera axis, where
            // nothing can stand between a light and anything visible to it.
            hint += project->renderShadows()
                ? L" These lights cast no shadow, so nothing is traced for them."
                : L"";
        }
        else if (!tracing)
        {
            hint += RKRenderSettings::tracesRaysInHardware()
                ? L" Its shadows are traced by the graphics card, which costs little."
                : L" Its shadows are traced as well, which the card drawing this view cannot do, so"
                  L" the export falls back to software and takes longer.";
        }
        PictureRayTracingHint().Text(hint);

        Select(ImageDpi(), static_cast<int>(project->imageDPI()));
        Select(ImageQuality(), static_cast<int>(project->renderImageQuality()));

        const bool physical = (project->imageDimensions() == RKImageDimensions::physical);
        DimPhysical().IsChecked(physical);
        DimPixels().IsChecked(!physical);
        const bool inch = (project->imageUnits() == RKImageUnits::inch);
        UnitInch().IsChecked(inch);
        UnitCm().IsChecked(!inch);

        const double unitFactor = inch ? 1.0 : 2.54;
        const double ratio = (project->imageAspectRatio() > 0.0) ? project->imageAspectRatio() : 1.0;
        const double inches = project->renderImagePhysicalSizeInInches();
        const double pixels = static_cast<double>(project->renderImageNumberOfPixels());
        PhysicalWidth().Value(inches * unitFactor);
        PhysicalWidth().IsEnabled(physical);
        PixelWidth().Value(pixels);
        PixelWidth().IsEnabled(!physical);
        PhysicalHeight().Text(FormatDouble(inches * unitFactor / ratio, 2));
        PixelHeight().Text(FormatDouble(pixels / ratio, 0));

        FramesPerSecond().Value(static_cast<double>(project->movieFramesPerSecond()));
        Select(MovieType(), static_cast<int>(project->movieType()));
    }

    void CameraDetailView::ReloadBackground()
    {
        auto project = m_host ? m_host->PaneProject() : nullptr;
        const int type = project ? static_cast<int>(project->renderBackgroundType()) : 0;
        RadioButton radios[4] = { BgColorRadio(), BgLinearRadio(), BgRadialRadio(), BgImageRadio() };
        for (int i = 0; i < 4; ++i)
            radios[i].IsChecked(i == type);
        ShowBackgroundPanel(type);

        const RKColor white(1.0, 1.0, 1.0, 1.0);
        DetailControls::SetColorWell(BgColorSwatch(), project ? project->renderBackgroundColor() : white);
        DetailControls::SetColorWell(LinearFromSwatch(), project ? project->linearGradientFromColor() : white);
        DetailControls::SetColorWell(LinearToSwatch(), project ? project->linearGradientToColor() : white);
        DetailControls::SetColorWell(RadialFromSwatch(), project ? project->radialGradientFromColor() : white);
        DetailControls::SetColorWell(RadialToSwatch(), project ? project->radialGradientToColor() : white);
        DetailControls::SetSliderAndBox(LinearAngleSlider(), LinearAngleBox(),
                                        project ? project->linearGradientAngle() : 90.0);
        DetailControls::SetSliderAndBox(RadialRoundnessSlider(), RadialRoundnessBox(),
                                        project ? project->radialGradientRoundness() : 0.4);

        const bool named = project && !project->backgroundImageFilename().isEmpty();
        BackgroundImageName().Visibility(named ? Visibility::Visible : Visibility::Collapsed);
        if (named)
            BackgroundImageName().Text(winrt::to_hstring(project->backgroundImageFilename().toStdString()));
    }

    void CameraDetailView::ShowBackgroundPanel(int type)
    {
        StackPanel panels[4] = { BgColorPanel(), BgLinearPanel(), BgRadialPanel(), BgImagePanel() };
        for (int i = 0; i < 4; ++i)
            panels[i].Visibility(i == type ? Visibility::Visible : Visibility::Collapsed);
    }

    // ---- Camera section -----------------------------------------------------

    void CameraDetailView::OnResetFractionChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        if (auto camera = m_host->PaneCamera())
            camera->setResetFraction(value / 100.0);
    }

    void CameraDetailView::OnResetDirectionChecked(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto camera = m_host->PaneCamera();
        if (!camera)
            return;
        auto radio = sender.try_as<RadioButton>();
        RadioButton directions[6] = { DirPlusX(), DirPlusY(), DirPlusZ(),
                                      DirMinusX(), DirMinusY(), DirMinusZ() };
        for (int i = 0; i < 6; ++i)
        {
            if (radio == directions[i])
            {
                camera->setResetDirectionType(static_cast<ResetDirectionType>(i));
                return;
            }
        }
    }

    void CameraDetailView::OnResetCamera(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_host)
            return;
        auto camera = m_host->PaneCamera();
        if (!camera)
            return;
        if (auto project = m_host->PaneProject())
            camera->resetForNewBoundingBox(project->renderBoundingBox());
        camera->resetCameraToDirection();
        camera->resetCameraDistance();
        m_host->ResetRendererCameraView();
        m_host->RedrawRenderer();
        ReloadReadouts();
        m_host->Log(L"Camera reset");
    }

    void CameraDetailView::OnProjectionChecked(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto camera = m_host->PaneCamera();
        if (!camera)
            return;
        const bool orthographic = (sender.try_as<RadioButton>() == Orthographic());
        if (orthographic)
            camera->setCameraToOrthographic();
        else
            camera->setCameraToPerspective();
        m_host->SetRendererCameraOrthographic(orthographic);
        m_host->RedrawRenderer();
    }

    void CameraDetailView::OnAngleOfViewChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        if (auto camera = m_host->PaneCamera())
        {
            camera->setAngleOfView(value * kRadiansPerDegree);
            m_host->RedrawRenderer();
        }
    }

    void CameraDetailView::OnRotationAngleChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (!m_host)
            return;
        const double value = sender.Value();
        if (!std::isfinite(value))
            return;
        if (auto camera = m_host->PaneCamera())
            camera->setRotationAngle(value);
        Button buttons[6] = { YawPlus(), YawMinus(), PitchPlus(), PitchMinus(), RollPlus(), RollMinus() };
        for (int i = 0; i < 6; ++i)
            buttons[i].Content(box_value(RotateTitle((i % 2) == 0, value)));
    }

    void CameraDetailView::OnRotateClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (!m_host)
            return;
        auto camera = m_host->PaneCamera();
        if (!camera)
            return;
        auto button = sender.try_as<Button>();
        struct Rotation { Button button; int operation; double sign; };
        const Rotation rotations[6] = {
            { YawPlus(), 0, 1.0 },   { YawMinus(), 0, -1.0 },
            { PitchPlus(), 1, 1.0 }, { PitchMinus(), 1, -1.0 },
            { RollPlus(), 2, 1.0 },  { RollMinus(), 2, -1.0 },
        };
        for (auto const& rotation : rotations)
        {
            if (button != rotation.button)
                continue;
            const double angle = rotation.sign * camera->rotationAngle();
            const simd_quatd delta = (rotation.operation == 0) ? simd_quatd::yaw(angle)
                                   : (rotation.operation == 1) ? simd_quatd::pitch(angle)
                                                               : simd_quatd::roll(angle);
            camera->setWorldRotation(camera->worldRotation() * delta);
            m_host->RedrawRenderer();
            ReloadReadouts();
            return;
        }
    }

    // RKCamera::increaseDistance subtracts its argument from the camera distance, so a
    // positive amount moves the camera towards the scene however its name reads. Three is
    // what one wheel notch delivers there (a 120-unit delta over 40), so a click of these
    // steps by the same amount as a notch of the wheel.
    void CameraDetailView::OnZoomIn(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_host)
            return;
        m_host->ZoomRendererCamera(3.0);
        ReloadReadouts();
    }

    void CameraDetailView::OnZoomOut(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_host)
            return;
        m_host->ZoomRendererCamera(-3.0);
        ReloadReadouts();
    }

    // ---- Axes ---------------------------------------------------------------

    void CameraDetailView::OnAxesPositionChanged(IInspectable const& sender, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto combo = sender.try_as<ComboBox>();
        auto project = m_host->PaneProject();
        if (!combo || combo.SelectedIndex() < 0 || !project || !project->axes())
            return;
        project->axes()->setPosition(static_cast<RKGlobalAxes::Position>(combo.SelectedIndex()));
        m_host->ReloadRenderer();
    }

    void CameraDetailView::OnAxesStyleChanged(IInspectable const& sender, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto combo = sender.try_as<ComboBox>();
        auto project = m_host->PaneProject();
        if (!combo || combo.SelectedIndex() < 0 || !project || !project->axes())
            return;
        project->axes()->setStyle(static_cast<RKGlobalAxes::Style>(combo.SelectedIndex()));
        m_host->ReloadRenderer();
    }

    void CameraDetailView::OnAxesSizeChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        auto project = m_host->PaneProject();
        if (!std::isfinite(value) || !project || !project->axes())
            return;
        project->axes()->setSizeScreenFraction(value / 100.0);
        m_host->ReloadRenderer();
    }

    void CameraDetailView::OnAxesOffsetChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        auto project = m_host->PaneProject();
        if (!std::isfinite(value) || !project || !project->axes())
            return;
        project->axes()->setBorderOffsetScreenFraction(value / 100.0);
        m_host->ReloadRenderer();
    }

    void CameraDetailView::OnAxesBackgroundStyleChanged(IInspectable const& sender, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto combo = sender.try_as<ComboBox>();
        auto project = m_host->PaneProject();
        if (!combo || combo.SelectedIndex() < 0 || !project || !project->axes())
            return;
        project->axes()->setAxesBackgroundStyle(
            static_cast<RKGlobalAxes::BackgroundStyle>(combo.SelectedIndex()));
        m_host->ReloadRenderer();
    }

    void CameraDetailView::OnAxesBackgroundSizeChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        auto project = m_host->PaneProject();
        if (!std::isfinite(value) || !project || !project->axes())
            return;
        project->axes()->setAxesBackgroundAdditionalSize(value);
        m_host->ReloadRenderer();
    }

    void CameraDetailView::OnTextScaleChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        auto project = m_host->PaneProject();
        if (!std::isfinite(value) || !project || !project->axes())
            return;
        auto axes = project->axes();
        if (sender == TextScaleX())
            axes->setTextScaleX(value);
        else if (sender == TextScaleY())
            axes->setTextScaleY(value);
        else
            axes->setTextScaleZ(value);
        m_host->ReloadRenderer();
    }

    int CameraDetailView::TextRowOf(IInspectable const& sender, int& outAxis)
    {
        NumberBox const boxes[3][3] = {
            { TextXDispX(), TextXDispY(), TextXDispZ() },
            { TextYDispX(), TextYDispY(), TextYDispZ() },
            { TextZDispX(), TextZDispY(), TextZDispZ() },
        };
        for (int row = 0; row < 3; ++row)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (sender == boxes[row][axis])
                {
                    outAxis = axis;
                    return row;
                }
            }
        }
        outAxis = 0;
        return -1;
    }

    void CameraDetailView::OnTextDisplacementChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        auto project = m_host->PaneProject();
        if (!std::isfinite(value) || !project || !project->axes())
            return;
        int axis = 0;
        const int row = TextRowOf(sender, axis);
        if (row < 0)
            return;

        // Which axis label the row belongs to, then which component of its
        // displacement the box holds.
        using Setter = void (RKGlobalAxes::*)(double);
        const Setter setters[3][3] = {
            { &RKGlobalAxes::setXTextDisplacementX, &RKGlobalAxes::setXTextDisplacementY,
              &RKGlobalAxes::setXTextDisplacementZ },
            { &RKGlobalAxes::setYTextDisplacementX, &RKGlobalAxes::setYTextDisplacementY,
              &RKGlobalAxes::setYTextDisplacementZ },
            { &RKGlobalAxes::setZTextDisplacementX, &RKGlobalAxes::setZTextDisplacementY,
              &RKGlobalAxes::setZTextDisplacementZ },
        };
        ((*project->axes()).*setters[row][axis])(value);
        m_host->ReloadRenderer();
    }

    // ---- Pictures / movies ---------------------------------------------------

    void CameraDetailView::OnImageDpiChanged(IInspectable const& sender, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto combo = sender.try_as<ComboBox>();
        auto project = m_host->PaneProject();
        if (!combo || combo.SelectedIndex() < 0 || !project)
            return;
        project->setImageDPI(static_cast<RKImageDPI>(combo.SelectedIndex()));
    }

    void CameraDetailView::OnImageQualityChanged(IInspectable const& sender, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto combo = sender.try_as<ComboBox>();
        auto project = m_host->PaneProject();
        if (!combo || combo.SelectedIndex() < 0 || !project)
            return;
        project->setImageQuality(static_cast<RKImageQuality>(combo.SelectedIndex()));
    }

    void CameraDetailView::OnPhysicalWidthChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        auto project = m_host->PaneProject();
        if (!std::isfinite(value) || !project)
            return;
        const double unitFactor = (project->imageUnits() == RKImageUnits::cm) ? 2.54 : 1.0;
        project->setImagePhysicalSizeInInches(value / unitFactor);
        m_suppressEvents = true;
        ReloadPictures();
        m_suppressEvents = false;
    }

    void CameraDetailView::OnPixelWidthChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        auto project = m_host->PaneProject();
        if (!std::isfinite(value) || !project)
            return;
        project->setImageNumberOfPixels(static_cast<int>(value));
        m_suppressEvents = true;
        ReloadPictures();
        m_suppressEvents = false;
    }

    void CameraDetailView::OnImageDimensionsChecked(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto project = m_host->PaneProject();
        if (!project)
            return;
        project->setImageDimensions((sender.try_as<RadioButton>() == DimPhysical())
                                        ? RKImageDimensions::physical
                                        : RKImageDimensions::pixels);
        m_suppressEvents = true;
        ReloadPictures();
        m_suppressEvents = false;
    }

    void CameraDetailView::OnImageUnitsChecked(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto project = m_host->PaneProject();
        if (!project)
            return;
        project->setImageUnits((sender.try_as<RadioButton>() == UnitInch()) ? RKImageUnits::inch
                                                                           : RKImageUnits::cm);
        m_suppressEvents = true;
        ReloadPictures();
        m_suppressEvents = false;
    }

    void CameraDetailView::OnFramesPerSecondChanged(NumberBox const& sender, NumberBoxValueChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        const double value = sender.Value();
        auto project = m_host->PaneProject();
        if (!std::isfinite(value) || !project)
            return;
        project->setMovieFramesPerSecond(static_cast<int>(value));
    }

    void CameraDetailView::OnMovieTypeChanged(IInspectable const& sender, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_host)
            return;
        auto combo = sender.try_as<ComboBox>();
        auto project = m_host->PaneProject();
        if (!combo || combo.SelectedIndex() < 0 || !project)
            return;
        project->setMovieType(static_cast<ProjectStructure::MovieType>(combo.SelectedIndex()));
    }

    bool CameraDetailView::CollectExportSettings(ExportSettings& settings) const
    {
        auto project = m_host ? m_host->PaneProject() : nullptr;
        if (!project)
            return false;

        const double ratio = (project->imageAspectRatio() > 0.0) ? project->imageAspectRatio() : 1.0;
        settings.width = (std::max)(1, project->renderImageNumberOfPixels());
        settings.height = (std::max)(1, static_cast<int>(std::lround(settings.width / ratio)));
        settings.dotsPerInch = project->imageDotsPerInchValue();
        settings.framesPerSecond = (std::max)(1, project->movieFramesPerSecond());
        settings.movieType = project->movieType();
        return true;
    }

    bool CameraDetailView::BuildExportJob(ExportSettings const& settings, std::wstring const& outputPath,
                                          bool movie, MovieWriter::Format format,
                                          ExportJobRequest& request) const
    {
        auto project = m_host ? m_host->PaneProject() : nullptr;
        if (!project)
            return false;

        request.movie = movie;
        // H.264 and HEVC only accept even extents; MovieWriter rounds the same way and
        // then insists on frames of exactly that size.
        request.width = movie ? NearestEvenInt(settings.width) : settings.width;
        request.height = movie ? NearestEvenInt(settings.height) : settings.height;
        request.dotsPerInch = settings.dotsPerInch;
        request.framesPerSecond = settings.framesPerSecond;
        request.movieType = settings.movieType;
        request.movieFormat = format;
        request.renderQuality = RKRenderQuality::picture;
        request.outputPath = outputPath;
        request.avoidAdapterLuid = m_host->LiveAdapterLuid();
        // The helper draws with project->camera(), and that is the same object the render
        // view moves: DirectXRenderer::setRenderDataSource adopts the project's camera, so
        // the viewpoint that goes into the job file is the one on screen.
        request.project = project;
        return true;
    }

    // A picture is one render and one write, neither of which can be interrupted part
    // way, so its panel shows the progress without offering the cancel.
    void CameraDetailView::BeginExport(std::wstring const& status, bool cancellable)
    {
        ExportProgress().Minimum(0.0);
        // The helper decides how many frames a movie has and says so with its first
        // progress line; until then there is one step to take.
        ExportProgress().Maximum(1.0);
        ExportProgress().Value(0.0);
        ExportStatus().Text(hstring(status));
        CancelExportButton().IsEnabled(cancellable);
        ExportPanel().Visibility(Visibility::Visible);
    }

    void CameraDetailView::ReportExportProgress(int completed, int total)
    {
        const int frameCount = (std::max)(1, total);
        ExportProgress().Maximum(static_cast<double>(frameCount));
        ExportProgress().Value(static_cast<double>((std::min)(completed, frameCount)));
        if (frameCount > 1)
            ExportStatus().Text(FrameStatus(completed, frameCount));
    }

    void CameraDetailView::EndExport(std::wstring const& message)
    {
        ExportPanel().Visibility(Visibility::Collapsed);
        MakePictureButton().IsEnabled(true);
        MakeMovieButton().IsEnabled(!m_movieFormats.empty());
        m_exportRunning = false;
        m_cancelExport = false;
        if (m_host)
            m_host->Log(message);
    }

    IAsyncOperation<hstring> CameraDetailView::PickExportPath(
        std::vector<std::pair<hstring, hstring>> choices, hstring suggestedName)
    {
        auto lifetime = get_strong();

        FileSavePicker picker;
        picker.SuggestedStartLocation(PickerLocationId::PicturesLibrary);
        for (auto const& choice : choices)
        {
            auto extensions = single_threaded_vector<hstring>();
            extensions.Append(choice.second);
            picker.FileTypeChoices().Insert(choice.first, extensions);
        }
        picker.SuggestedFileName(suggestedName);

        // A desktop picker has to be parented on the window, which a UserControl only
        // reaches through the island it is hosted in.
        if (auto root = XamlRoot())
        {
            const HWND hwnd =
                winrt::Microsoft::UI::GetWindowFromWindowId(root.ContentIslandEnvironment().AppWindowId());
            if (auto init = picker.as<IInitializeWithWindow>())
                init->Initialize(hwnd);
        }

        StorageFile file = co_await picker.PickSaveFileAsync();
        co_return file ? file.Path() : hstring();
    }

    IAsyncAction CameraDetailView::RunExportJob(ExportJobRequest request, std::wstring label)
    {
        auto lifetime = get_strong();
        auto ui = DispatcherQueue();

        const std::wstring outputPath = request.outputPath;
        // A killed helper cannot delete its own staging file, so sweeping it belongs to
        // whoever writes that output next.
        const std::wstring stagingPath = ExportStagingPath(outputPath);
        DeleteFileW(stagingPath.c_str());

        const std::wstring helperPath = ExportHelperPath();
        if (helperPath.empty() || GetFileAttributesW(helperPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            EndExport(label + L" export failed: iRASPA.Export.exe is not next to iRASPA.exe");
            co_return;
        }

        const std::wstring jobPath = UniqueExportJobPath();
        if (jobPath.empty())
        {
            EndExport(label + L" export failed: there is no temporary directory to write the job to");
            co_return;
        }

        // The project is serialised here rather than on the worker: nothing locks it
        // against the form editing it, and the form runs on this thread.
        std::wstring error;
        const bool written = WriteExportJobFile(jobPath, request, error);
        request.project.reset();
        if (!written)
        {
            DeleteFileW(jobPath.c_str());
            EndExport(label + L" export failed: " + error);
            co_return;
        }

        ExportProcess helper;
        if (!StartExportHelper(helperPath, jobPath, helper, error))
        {
            DeleteFileW(jobPath.c_str());
            EndExport(label + L" export failed: " + error);
            co_return;
        }
        m_exportProcess = helper.process;

        // The helper writes one line per frame and blocks on the pipe in between, so the
        // read has to be off the UI thread for the window to stay usable while a movie
        // encodes.
        co_await winrt::resume_background();

        std::string pending;
        std::string line;
        std::wstring failure;
        bool finished = false;
        while (ReadHelperLine(helper.output, pending, line))
        {
            if (LineStartsWith(line, "PROGRESS ", 9))
            {
                int completed = 0;
                int total = 0;
                if (ParseProgress(line, completed, total))
                {
                    co_await wil::resume_foreground(ui);
                    ReportExportProgress(completed, total);
                    co_await winrt::resume_background();
                }
            }
            else if (LineStartsWith(line, "ADAPTER ", 8))
            {
                std::wstring const adapter = FromUtf8(line.substr(8));
                co_await wil::resume_foreground(ui);
                if (m_host)
                    m_host->Log(label + L" export rendering on " + adapter);
                co_await winrt::resume_background();
            }
            else if (LineStartsWith(line, "ERROR ", 6))
            {
                failure = FromUtf8(line.substr(6));
            }
            else if (LineStartsWith(line, "DONE ", 5))
            {
                finished = true;
            }
        }

        WaitForSingleObject(helper.process, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(helper.process, &exitCode);
        CloseHandle(helper.output);

        co_await wil::resume_foreground(ui);

        CloseHandle(m_exportProcess);
        m_exportProcess = nullptr;

        const bool cancelled = m_cancelExport;
        const bool succeeded = !cancelled && finished && failure.empty() && exitCode == 0;
        if (!succeeded)
        {
            DeleteFileW(outputPath.c_str());
            DeleteFileW(stagingPath.c_str());
        }

        // An export that worked leaves nothing behind. One that did not keeps the job it was given,
        // under a name that does not change, so the helper can be run against it again by hand:
        // a failure inside the renderer is otherwise only reproducible by exporting all over again.
        if (succeeded || cancelled)
        {
            DeleteFileW(jobPath.c_str());
        }
        else
        {
            const std::wstring kept = FailedExportJobPath();
            if (kept.empty() || !MoveFileExW(jobPath.c_str(), kept.c_str(), MOVEFILE_REPLACE_EXISTING))
                DeleteFileW(jobPath.c_str());
        }

        if (succeeded)
            EndExport(label + L" saved to " + outputPath);
        else if (cancelled)
            EndExport(label + L" export cancelled");
        else if (!failure.empty())
            EndExport(label + L" export failed: " + failure);
        else
            EndExport(label + L" export failed: the export helper stopped without writing a file");
    }

    winrt::fire_and_forget CameraDetailView::RunPictureExport(ExportSettings settings)
    {
        auto lifetime = get_strong();

        m_exportRunning = true;
        m_cancelExport = false;
        MakePictureButton().IsEnabled(false);
        MakeMovieButton().IsEnabled(false);

        std::wstring path;
        try
        {
            path = std::wstring(co_await PickExportPath({ { L"PNG image", L".png" },
                                                          { L"JPEG image", L".jpg" },
                                                          { L"TIFF image", L".tiff" } },
                                                        L"picture"));
        }
        catch (hresult_error const& ex)
        {
            EndExport(std::wstring(L"Picture export failed: ") + std::wstring(ex.message()));
            co_return;
        }
        if (path.empty())
        {
            EndExport(L"Picture export cancelled");
            co_return;
        }

        ExportJobRequest request;
        if (!BuildExportJob(settings, path, false, MovieWriter::Format::h264, request))
        {
            EndExport(L"Picture export failed: no project is loaded");
            co_return;
        }

        BeginExport(L"Rendering", false);
        co_await RunExportJob(std::move(request), L"Picture");
    }

    winrt::fire_and_forget CameraDetailView::RunMovieExport(ExportSettings settings, MovieWriter::Format format)
    {
        auto lifetime = get_strong();

        m_exportRunning = true;
        m_cancelExport = false;
        MakePictureButton().IsEnabled(false);
        MakeMovieButton().IsEnabled(false);

        std::wstring path;
        try
        {
            path = std::wstring(co_await PickExportPath({ { L"MPEG-4 video", L".mp4" } }, L"movie"));
        }
        catch (hresult_error const& ex)
        {
            EndExport(std::wstring(L"Movie export failed: ") + std::wstring(ex.message()));
            co_return;
        }
        if (path.empty())
        {
            EndExport(L"Movie export cancelled");
            co_return;
        }

        ExportJobRequest request;
        if (!BuildExportJob(settings, path, true, format, request))
        {
            EndExport(L"Movie export failed: no project is loaded");
            co_return;
        }

        BeginExport(L"Encoding", true);
        co_await RunExportJob(std::move(request), L"Movie");
    }

    void CameraDetailView::OnMakePicture(IInspectable const&, RoutedEventArgs const&)
    {
        ExportSettings settings;
        if (m_exportRunning || !CollectExportSettings(settings))
            return;
        RunPictureExport(settings);
    }

    void CameraDetailView::OnMakeMovie(IInspectable const&, RoutedEventArgs const&)
    {
        ExportSettings settings;
        if (m_exportRunning || m_movieFormats.empty() || !CollectExportSettings(settings))
            return;
        const int index = MovieFormat().SelectedIndex();
        if (index < 0 || index >= static_cast<int>(m_movieFormats.size()))
            return;
        RunMovieExport(settings, m_movieFormats[index]);
    }

    void CameraDetailView::OnCancelExport(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_exportRunning)
            return;
        m_cancelExport = true;
        CancelExportButton().IsEnabled(false);
        // Killing the helper is the whole of cancellation: it closes the pipe, which ends
        // the read, and what it leaves on disk is swept once the read has finished.
        if (m_exportProcess)
            TerminateProcess(m_exportProcess, 1);
    }

    // ---- Background ----------------------------------------------------------

    void CameraDetailView::OnBackgroundTypeChecked(IInspectable const& sender, RoutedEventArgs const&)
    {
        auto radio = sender.try_as<RadioButton>();
        RadioButton radios[4] = { BgColorRadio(), BgLinearRadio(), BgRadialRadio(), BgImageRadio() };
        int type = -1;
        for (int i = 0; i < 4; ++i)
        {
            if (radio == radios[i])
                type = i;
        }
        if (type < 0)
            return;
        // The panel follows the radio even while the form is being filled in; only
        // the model edit is a user action.
        ShowBackgroundPanel(type);
        if (m_suppressEvents || !m_host)
            return;
        if (auto project = m_host->PaneProject())
        {
            project->setBackgroundType(static_cast<RKBackgroundType>(type));
            m_host->ReloadRenderer();
        }
    }

    void CameraDetailView::OnSelectBackgroundImage(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_host)
            m_host->PickBackgroundImage();
    }
}
