#include "pch.h"
#include "InfoDetailView.xaml.h"

#if __has_include("InfoDetailView.g.cpp")
#include "InfoDetailView.g.cpp"
#endif

#include <algorithm>
#include <ctime>
#include <string>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Foundation;

namespace winrt::iRASPA_WinUI::implementation
{
    namespace
    {
        // RKDate <-> WinRT DateTime for the CalendarDatePickers.
        DateTime DateTimeFromRKDate(RKDate const& d)
        {
            std::tm tm{};
            tm.tm_year = d.year() - 1900;
            tm.tm_mon = d.month() - 1;
            tm.tm_mday = d.day() > 0 ? d.day() : 1;
            tm.tm_hour = 12; // noon keeps the calendar date stable across zones
            return winrt::clock::from_time_t(_mkgmtime(&tm));
        }

        RKDate RKDateFromDateTime(DateTime const& dt)
        {
            const time_t t = winrt::clock::to_time_t(dt);
            std::tm tm{};
            gmtime_s(&tm, &t);
            return RKDate(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        }

        BOOL CALLBACK CountryEnumProc(PWSTR geoName, LPARAM lParam)
        {
            auto* names = reinterpret_cast<std::vector<std::wstring>*>(lParam);
            wchar_t friendly[256]{};
            if (GetGeoInfoEx(geoName, GEO_FRIENDLYNAME, friendly, ARRAYSIZE(friendly)) > 0)
                names->emplace_back(friendly);
            return TRUE;
        }

        // Localized country list for the affiliation popup (Cocoa uses
        // Locale.isoRegionCodes).
        std::vector<std::wstring> LocalizedCountryNames()
        {
            std::vector<std::wstring> names;
            EnumSystemGeoNames(GEOCLASS_NATION, CountryEnumProc, reinterpret_cast<LPARAM>(&names));
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            return names;
        }
    }

    InfoDetailView::InfoDetailView()
    {
        InitializeComponent();
        DetailControls::FitFixedColumns(*this);
        BuildFieldTable();
    }

    void InfoDetailView::SetController(DocumentController* controller)
    {
        m_controller = controller;
    }

    void InfoDetailView::AddText(TextBox const& box, RKString (Object::*get)(),
                                 void (Object::*set)(RKString))
    {
        m_textFields.push_back(TextField{
            box,
            [get](Object& o) { return (o.*get)(); },
            [set](Object& o, RKString const& v) { (o.*set)(v); } });
    }

    void InfoDetailView::AddText(TextBox const& box, RKString (InfoViewer::*get)(),
                                 void (InfoEditor::*set)(RKString))
    {
        m_textFields.push_back(TextField{
            box,
            [get](Object& o)
            {
                auto* viewer = dynamic_cast<InfoViewer*>(&o);
                return viewer ? (viewer->*get)() : RKString("");
            },
            [set](Object& o, RKString const& v)
            {
                if (auto* editor = dynamic_cast<InfoEditor*>(&o))
                    (editor->*set)(v);
            } });
    }

    void InfoDetailView::AddDate(CalendarDatePicker const& picker, RKDate (Object::*get)(),
                                 void (Object::*set)(RKDate))
    {
        m_dateFields.push_back(DateField{
            picker,
            [get](Object& o) { return (o.*get)(); },
            [set](Object& o, RKDate const& d) { (o.*set)(d); } });
    }

    void InfoDetailView::AddDate(CalendarDatePicker const& picker, RKDate (InfoViewer::*get)(),
                                 void (InfoEditor::*set)(RKDate))
    {
        m_dateFields.push_back(DateField{
            picker,
            [get](Object& o)
            {
                auto* viewer = dynamic_cast<InfoViewer*>(&o);
                return viewer ? (viewer->*get)() : RKDate();
            },
            [set](Object& o, RKDate const& d)
            {
                if (auto* editor = dynamic_cast<InfoEditor*>(&o))
                    (editor->*set)(d);
            } });
    }

    // The whole form, field by field: this is what the Cocoa storyboard expresses
    // as bindings from each cell view to its object property.
    void InfoDetailView::BuildFieldTable()
    {
        // Creator (Cocoa CreatorCell) — author and affiliation live on Object.
        AddText(AuthorFirstBox(), &Object::authorFirstName, &Object::setAuthorFirstName);
        AddText(AuthorMiddleBox(), &Object::authorMiddleName, &Object::setAuthorMiddleName);
        AddText(AuthorLastBox(), &Object::authorLastName, &Object::setAuthorLastName);
        AddText(OrcidBox(), &Object::authorOrchidID, &Object::setAuthorOrchidID);
        AddText(ResearcherBox(), &Object::authorResearcherID, &Object::setAuthorResearcherID);
        AddText(UniversityBox(), &Object::authorAffiliationUniversityName,
                &Object::setAuthorAffiliationUniversityName);
        AddText(FacultyBox(), &Object::authorAffiliationFacultyName,
                &Object::setAuthorAffiliationFacultyName);
        AddText(InstituteBox(), &Object::authorAffiliationInstituteName,
                &Object::setAuthorAffiliationInstituteName);
        AddText(CityBox(), &Object::authorAffiliationCityName, &Object::setAuthorAffiliationCityName);

        // Creation (Cocoa CreationCell) — the date is on Object, the rest on
        // InfoViewer/InfoEditor.
        AddDate(CreationDatePicker(), &Object::creationDate, &Object::setCreationDate);
        AddText(TemperatureBox(), &InfoViewer::creationTemperature,
                &InfoEditor::setCreationTemperature);
        AddEnum(TemperatureScaleCombo(), &InfoViewer::creationTemperatureScale,
                &InfoEditor::setCreationTemperatureScale);
        AddText(PressureBox(), &InfoViewer::creationPressure, &InfoEditor::setCreationPressure);
        AddEnum(PressureScaleCombo(), &InfoViewer::creationPressureScale,
                &InfoEditor::setCreationPressureScale);
        AddEnum(MethodCombo(), &InfoViewer::creationMethod, &InfoEditor::setCreationMethod);

        // Creation > Simulation (Cocoa CreationMethods tab 0).
        AddEnum(RelaxCombo(), &InfoViewer::creationUnitCellRelaxationMethod,
                &InfoEditor::setCreationUnitCellRelaxationMethod);
        AddText(PositionsSoftwareBox(), &InfoViewer::creationAtomicPositionsSoftwarePackage,
                &InfoEditor::setCreationAtomicPositionsSoftwarePackage);
        AddEnum(PositionsAlgorithmCombo(),
                &InfoViewer::creationAtomicPositionsIonsRelaxationAlgorithm,
                &InfoEditor::setCreationAtomicPositionsIonsRelaxationAlgorithm);
        AddEnum(EigenvaluesCombo(), &InfoViewer::creationAtomicPositionsIonsRelaxationCheck,
                &InfoEditor::setCreationAtomicPositionsIonsRelaxationCheck);
        AddText(PositionsForceFieldBox(), &InfoViewer::creationAtomicPositionsForcefield,
                &InfoEditor::setCreationAtomicPositionsForcefield);
        AddText(PositionsForceFieldDetailsBox(),
                &InfoViewer::creationAtomicPositionsForcefieldDetails,
                &InfoEditor::setCreationAtomicPositionsForcefieldDetails);
        AddText(ChargesSoftwareBox(), &InfoViewer::creationAtomicChargesSoftwarePackage,
                &InfoEditor::setCreationAtomicChargesSoftwarePackage);
        AddText(ChargesAlgorithmBox(), &InfoViewer::creationAtomicChargesAlgorithms,
                &InfoEditor::setCreationAtomicChargesAlgorithms);
        AddText(ChargesForceFieldBox(), &InfoViewer::creationAtomicChargesForcefield,
                &InfoEditor::setCreationAtomicChargesForcefield);
        AddText(ChargesForceFieldDetailsBox(), &InfoViewer::creationAtomicChargesForcefieldDetails,
                &InfoEditor::setCreationAtomicChargesForcefieldDetails);

        // Creation > Experimental (Cocoa CreationMethods tab 1).
        AddText(RadiationBox(), &InfoViewer::experimentalMeasurementRadiation,
                &InfoEditor::setExperimentalMeasurementRadiation);
        AddText(WaveLengthBox(), &InfoViewer::experimentalMeasurementWaveLength,
                &InfoEditor::setExperimentalMeasurementWaveLength);
        AddText(ThetaMinBox(), &InfoViewer::experimentalMeasurementThetaMin,
                &InfoEditor::setExperimentalMeasurementThetaMin);
        AddText(ThetaMaxBox(), &InfoViewer::experimentalMeasurementThetaMax,
                &InfoEditor::setExperimentalMeasurementThetaMax);
        AddText(HminBox(), &InfoViewer::experimentalMeasurementIndexLimitsHmin,
                &InfoEditor::setExperimentalMeasurementIndexLimitsHmin);
        AddText(HmaxBox(), &InfoViewer::experimentalMeasurementIndexLimitsHmax,
                &InfoEditor::setExperimentalMeasurementIndexLimitsHmax);
        AddText(KminBox(), &InfoViewer::experimentalMeasurementIndexLimitsKmin,
                &InfoEditor::setExperimentalMeasurementIndexLimitsKmin);
        AddText(KmaxBox(), &InfoViewer::experimentalMeasurementIndexLimitsKmax,
                &InfoEditor::setExperimentalMeasurementIndexLimitsKmax);
        AddText(LminBox(), &InfoViewer::experimentalMeasurementIndexLimitsLmin,
                &InfoEditor::setExperimentalMeasurementIndexLimitsLmin);
        AddText(LmaxBox(), &InfoViewer::experimentalMeasurementIndexLimitsLmax,
                &InfoEditor::setExperimentalMeasurementIndexLimitsLmax);
        AddText(ReflectionsBox(),
                &InfoViewer::experimentalMeasurementNumberOfSymmetryIndependentReflections,
                &InfoEditor::setExperimentalMeasurementNumberOfSymmetryIndependentReflections);
        AddText(RefinementSoftwareBox(), &InfoViewer::experimentalMeasurementSoftware,
                &InfoEditor::setExperimentalMeasurementSoftware);
        AddText(RefinementDetailsBox(), &InfoViewer::experimentalMeasurementRefinementDetails,
                &InfoEditor::setExperimentalMeasurementRefinementDetails);
        AddText(GoodnessOfFitBox(), &InfoViewer::experimentalMeasurementGoodnessOfFit,
                &InfoEditor::setExperimentalMeasurementGoodnessOfFit);
        AddText(RFactorGtBox(), &InfoViewer::experimentalMeasurementRFactorGt,
                &InfoEditor::setExperimentalMeasurementRFactorGt);
        AddText(RFactorAllBox(), &InfoViewer::experimentalMeasurementRFactorAll,
                &InfoEditor::setExperimentalMeasurementRFactorAll);

        // Chemical Information (Cocoa ChemicalCell).
        AddText(MoietyBox(), &InfoViewer::chemicalFormulaMoiety, &InfoEditor::setChemicalFormulaMoiety);
        AddText(FormulaSumBox(), &InfoViewer::chemicalFormulaSum, &InfoEditor::setChemicalFormulaSum);
        AddText(SystematicNameBox(), &InfoViewer::chemicalNameSystematic,
                &InfoEditor::setChemicalNameSystematic);

        // Citation (Cocoa PublicationCell).
        AddText(ArticleTitleBox(), &InfoViewer::citationArticleTitle,
                &InfoEditor::setCitationArticleTitle);
        AddText(ArticleAuthorsBox(), &InfoViewer::citationAuthors, &InfoEditor::setCitationAuthors);
        AddText(JournalTitleBox(), &InfoViewer::citationJournalTitle,
                &InfoEditor::setCitationJournalTitle);
        AddText(VolumeBox(), &InfoViewer::citationJournalVolume, &InfoEditor::setCitationJournalVolume);
        AddText(NumberBox(), &InfoViewer::citationJournalNumber, &InfoEditor::setCitationJournalNumber);
        AddDate(PublicationDatePicker(), &InfoViewer::citationPublicationDate,
                &InfoEditor::setCitationPublicationDate);
        AddText(DoiBox(), &InfoViewer::citationDOI, &InfoEditor::setCitationDOI);
        AddText(DatabaseCodesBox(), &InfoViewer::citationDatebaseCodes,
                &InfoEditor::setCitationDatebaseCodes);
    }

    void InfoDetailView::ApplyMethodVisibility(int method)
    {
        const bool experimental = (method == 2);
        SimulationPanel().Visibility(experimental ? Visibility::Collapsed : Visibility::Visible);
        ExperimentalPanel().Visibility(experimental ? Visibility::Visible : Visibility::Collapsed);
    }

    void InfoDetailView::Reload()
    {
        auto object = m_controller ? m_controller->FirstSelectedObject() : nullptr;
        Hint().Visibility(object ? Visibility::Collapsed : Visibility::Visible);
        Sections().Visibility(object ? Visibility::Visible : Visibility::Collapsed);
        if (!object)
            return;

        m_suppressEvents = true;
        try
        {
            for (auto const& field : m_textFields)
            {
                const auto value = Agreed<RKString>(field.read);
                DetailControls::SetTextOrMultiple(
                    field.box,
                    value ? std::optional<winrt::hstring>(value->toStdWString()) : std::nullopt);
            }

            // A popup lands on -1 (blank) when the model holds the enum's
            // multiple_values, and on the "Multiple Values" entry when the
            // selection itself disagrees.
            std::optional<int> method;
            for (auto const& field : m_enumFields)
            {
                auto value = Agreed<int>(field.read);
                const int count = static_cast<int>(field.box.Items().Size());
                if (value && (*value < 0 || *value >= count))
                    field.box.SelectedIndex(-1);
                else
                    DetailControls::SelectOrMultiple(field.box, value);
                if (field.box == MethodCombo())
                    method = value;
            }

            for (auto const& field : m_dateFields)
            {
                const auto date = Agreed<RKDate>(field.read);
                field.picker.PlaceholderText(date ? winrt::hstring{ L"pick a date" }
                                                  : DetailControls::MultipleValuesText());
                if (date && date->year() >= 1601) // WinRT DateTime cannot represent earlier
                    field.picker.Date(DateTimeFromRKDate(*date));
                else
                    field.picker.Date(nullptr);
            }

            // Country popup: the OS list, with the object's own value pushed in
            // front when it is not one of them.
            auto countries = LocalizedCountryNames();
            const auto country = Agreed<RKString>([](Object& o)
                                                  { return o.authorAffiliationCountryName(); });
            const std::wstring current = country ? country->toStdWString() : std::wstring{};
            if (!current.empty() &&
                std::find(countries.begin(), countries.end(), current) == countries.end())
                countries.insert(countries.begin(), current);
            CountryCombo().Items().Clear();
            std::optional<int> selected;
            for (size_t i = 0; i < countries.size(); ++i)
            {
                CountryCombo().Items().Append(box_value(winrt::hstring(countries[i])));
                if (country && countries[i] == current)
                    selected = static_cast<int>(i);
            }
            if (country && !selected)
                CountryCombo().SelectedIndex(-1);
            else
                DetailControls::SelectOrMultiple(CountryCombo(), selected);

            ApplyMethodVisibility(method.value_or(0));
        }
        catch (...)
        {
            if (m_controller)
                m_controller->Log(L"Info inspector reload error");
        }
        m_suppressEvents = false;
    }

    // Cocoa sendsActionOnEndEditing: the field commits when focus leaves it, to
    // every selected object.
    void InfoDetailView::OnTextCommit(IInspectable const& sender,
                                      [[maybe_unused]] RoutedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto box = sender.try_as<TextBox>();
        if (!box)
            return;
        // A field left empty while it stands for a mixed selection was not edited,
        // so it must not write that emptiness over the objects' values.
        if (box.Text().empty() && box.PlaceholderText() == DetailControls::MultipleValuesText())
            return;
        for (auto const& field : m_textFields)
        {
            if (field.box != box)
                continue;
            box.PlaceholderText(L"");
            const RKString value = RKString::fromStdWString(std::wstring(box.Text()));
            m_controller->ForEachSelectedObject([&field, &value](Object& o) { field.write(o, value); });
            return;
        }
    }

    void InfoDetailView::OnEnumCommit(IInspectable const& sender,
                                      [[maybe_unused]] SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo || combo.SelectedIndex() < 0)
            return;
        // The "Multiple Values" entry is there to be read, not picked.
        if (DetailControls::IsMultipleValuesSelected(combo))
            return;
        for (auto const& field : m_enumFields)
        {
            if (field.box != combo)
                continue;
            const int value = combo.SelectedIndex();
            m_controller->ForEachSelectedObject([&field, value](Object& o) { field.write(o, value); });
            if (combo == MethodCombo())
                ApplyMethodVisibility(value);
            return;
        }
    }

    void InfoDetailView::OnCountryCommit(IInspectable const& sender,
                                         [[maybe_unused]] SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto combo = sender.try_as<ComboBox>();
        if (!combo || combo.SelectedIndex() < 0)
            return;
        if (DetailControls::IsMultipleValuesSelected(combo))
            return;
        auto item = combo.SelectedItem();
        if (!item)
            return;
        const RKString name = RKString::fromStdWString(
            std::wstring(unbox_value<winrt::hstring>(item)));
        m_controller->ForEachSelectedObject([&name](Object& o)
        {
            o.setAuthorAffiliationCountryName(name);
        });
    }

    void InfoDetailView::OnDateCommit(CalendarDatePicker const& sender,
                                      CalendarDatePickerDateChangedEventArgs const& e)
    {
        if (m_suppressEvents || !m_controller)
            return;
        auto date = e.NewDate();
        if (!date)
            return;
        const RKDate value = RKDateFromDateTime(date.Value());
        for (auto const& field : m_dateFields)
        {
            if (field.picker != sender)
                continue;
            m_controller->ForEachSelectedObject([&field, &value](Object& o) { field.write(o, value); });
            return;
        }
    }
}
