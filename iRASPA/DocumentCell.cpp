#include "pch.h"
#include "DocumentController.h"
#include "StructureTypeTable.h"

#include "atomstructureviewer.h"
#include "atomviewer.h"
#include "bondviewer.h"
#include "forcefieldsets.h"
#include "iraspaobject.h"
#include "primitive.h"
#include "spacegroupviewer.h"
#include "structuralpropertyviewer.h"
#include "structure.h"
#include "skspacegroup.h"
#include "skapolloniusporeanalysis.h"
#include "skgeometricsurface.h"

// The document-level half of the Cell tab (Cocoa
// StructureCellDetailViewController): the undo snapshots a cell edit records,
// and the structure-type conversion behind the material-type popup. Nothing
// here touches XAML.

namespace
{
    std::shared_ptr<Object> MakeObjectOfType(ObjectType type, std::shared_ptr<Object> const& source)
    {
        switch (type)
        {
        case ObjectType::crystal:                        return std::make_shared<Crystal>(source);
        case ObjectType::molecularCrystal:               return std::make_shared<MolecularCrystal>(source);
        case ObjectType::molecule:                       return std::make_shared<Molecule>(source);
        case ObjectType::protein:                        return std::make_shared<Protein>(source);
        case ObjectType::proteinCrystal:                 return std::make_shared<ProteinCrystal>(source);
        case ObjectType::crystalEllipsoidPrimitive:      return std::make_shared<CrystalEllipsoidPrimitive>(source);
        case ObjectType::crystalCylinderPrimitive:       return std::make_shared<CrystalCylinderPrimitive>(source);
        case ObjectType::crystalPolygonalPrismPrimitive: return std::make_shared<CrystalPolygonalPrismPrimitive>(source);
        case ObjectType::ellipsoidPrimitive:             return std::make_shared<EllipsoidPrimitive>(source);
        case ObjectType::cylinderPrimitive:              return std::make_shared<CylinderPrimitive>(source);
        case ObjectType::polygonalPrismPrimitive:        return std::make_shared<PolygonalPrismPrimitive>(source);
        case ObjectType::RASPADensityVolume:             return std::make_shared<RASPADensityVolume>(source);
        case ObjectType::VTKDensityVolume:               return std::make_shared<VTKDensityVolume>(source);
        case ObjectType::VASPDensityVolume:              return std::make_shared<VASPDensityVolume>(source);
        case ObjectType::GaussianCubeVolume:             return std::make_shared<GaussianCubeVolume>(source);
        default:                                         return nullptr;
        }
    }
}

DocumentController::CellUndoState
DocumentController::SnapshotCellState(std::shared_ptr<Object> const& object) const
{
    CellUndoState state;
    state.object = object;
    if (!object)
        return state;
    if (auto cell = object->cell())
        state.cell = std::make_shared<SKCell>(*cell);
    state.orientation = object->orientation();
    state.rotationDelta = object->rotationDelta();
    state.origin = object->origin();
    if (auto spaceGroup = std::dynamic_pointer_cast<SpaceGroupViewer>(object))
        state.hallNumber = static_cast<int>(spaceGroup->spaceGroup().spaceGroupSetting().HallNumber());
    if (auto viewer = std::dynamic_pointer_cast<StructuralPropertyViewer>(object))
    {
        CellStructuralUndoState properties;
        properties.materialType = viewer->structureMaterialType();
        properties.probeMolecule = viewer->frameworkProbeMolecule();
        properties.mass = viewer->structureMass();
        properties.density = viewer->structureDensity();
        properties.heliumVoidFraction = viewer->structureHeliumVoidFraction();
        properties.specificVolume = viewer->structureSpecificVolume();
        properties.accessiblePoreVolume = viewer->structureAccessiblePoreVolume();
        properties.volumetricSurfaceArea = viewer->structureVolumetricNitrogenSurfaceArea();
        properties.gravimetricSurfaceArea = viewer->structureGravimetricNitrogenSurfaceArea();
        properties.volumetricWellSurfaceArea = viewer->structureVolumetricWellSurfaceArea();
        properties.gravimetricWellSurfaceArea = viewer->structureGravimetricWellSurfaceArea();
        properties.numberOfChannelSystems = viewer->structureNumberOfChannelSystems();
        properties.numberOfInaccessiblePockets = viewer->structureNumberOfInaccessiblePockets();
        properties.dimensionalityOfPoreSystem = viewer->structureDimensionalityOfPoreSystem();
        properties.largestCavityDiameter = viewer->structureLargestCavityDiameter();
        properties.restrictingPoreLimitingDiameter = viewer->structureRestrictingPoreLimitingDiameter();
        properties.largestCavityDiameterAlongAViablePath =
            viewer->structureLargestCavityDiameterAlongAViablePath();
        properties.restrictingPoreLimitingDiameter2 = viewer->structureRestrictingPoreLimitingDiameter2();
        properties.largestCavityDiameterAlongAViablePath2 =
            viewer->structureLargestCavityDiameterAlongAViablePath2();
        properties.window1Size = viewer->structureWindow1Size();
        properties.window2Size = viewer->structureWindow2Size();
        properties.window3Size = viewer->structureWindow3Size();
        properties.window1RingAtoms = viewer->structureWindow1RingAtoms();
        properties.window2RingAtoms = viewer->structureWindow2RingAtoms();
        properties.window3RingAtoms = viewer->structureWindow3RingAtoms();
        if (auto structure = std::dynamic_pointer_cast<Structure>(object))
        {
            properties.blockingPocketProbeMolecule = structure->blockingPocketProbeMolecule();
            properties.blockingPocketProbeEpsilon = structure->blockingPocketProbeEpsilon();
            properties.blockingPocketProbeSigma = structure->blockingPocketProbeSigma();
            properties.hasBlockingPocketProbe = true;
        }
        state.structural = properties;
    }
    return state;
}

std::vector<DocumentController::CellUndoState> DocumentController::SnapshotCellStates() const
{
    std::vector<CellUndoState> states;
    for (auto const& target : TargetStructures())
    {
        if (auto object = target ? target->object() : nullptr)
            states.push_back(SnapshotCellState(object));
    }
    return states;
}

void DocumentController::RegisterCellUndo(std::wstring const& actionName,
                                          std::vector<CellUndoState> const& before)
{
    if (before.empty())
        return;
    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, actionName, before]() { RestoreCellStates(actionName, before); });
}

void DocumentController::RestoreCellStates(std::wstring const& actionName,
                                           std::vector<CellUndoState> const& states)
{
    std::vector<CellUndoState> inverse;
    for (auto const& state : states)
    {
        auto object = state.object;
        if (!object)
            continue;
        inverse.push_back(SnapshotCellState(object));

        bool cellChanged = false;
        if (state.cell)
        {
            auto current = object->cell();
            cellChanged = !current || !(current->unitCell() == state.cell->unitCell());
            object->setCell(std::make_shared<SKCell>(*state.cell));
        }
        object->setOrientation(state.orientation);
        object->setRotationDelta(state.rotationDelta);
        object->setOrigin(state.origin);

        // The space group is only re-imposed when it actually differs, because
        // that regenerates the symmetry copies and the bonds.
        bool regenerate = cellChanged;
        if (state.hallNumber)
        {
            if (auto spaceGroup = std::dynamic_pointer_cast<SpaceGroupViewer>(object);
                spaceGroup &&
                static_cast<int>(spaceGroup->spaceGroup().spaceGroupSetting().HallNumber()) != *state.hallNumber)
            {
                spaceGroup->setSpaceGroupHallNumber(*state.hallNumber);
                regenerate = true;
            }
        }
        // A different unit cell (undoing a cell edit) also moves the copies and
        // changes the bond distances, so the same regeneration applies.
        if (regenerate)
        {
            if (auto atoms = std::dynamic_pointer_cast<AtomViewer>(object))
                atoms->expandSymmetry();
            if (auto bonds = std::dynamic_pointer_cast<BondViewer>(object))
                bonds->computeBonds();
        }

        if (state.structural)
        {
            if (auto editor = std::dynamic_pointer_cast<StructuralPropertyEditor>(object))
            {
                auto const& p = *state.structural;
                editor->setStructureMaterialType(p.materialType);
                editor->setFrameworkProbeMolecule(p.probeMolecule);
                editor->setStructureMass(p.mass);
                editor->setStructureDensity(p.density);
                editor->setStructureHeliumVoidFraction(p.heliumVoidFraction);
                editor->setStructureSpecificVolume(p.specificVolume);
                editor->setStructureAccessiblePoreVolume(p.accessiblePoreVolume);
                editor->setStructureVolumetricNitrogenSurfaceArea(p.volumetricSurfaceArea);
                editor->setStructureGravimetricNitrogenSurfaceArea(p.gravimetricSurfaceArea);
                editor->setStructureVolumetricWellSurfaceArea(p.volumetricWellSurfaceArea);
                editor->setStructureGravimetricWellSurfaceArea(p.gravimetricWellSurfaceArea);
                editor->setStructureNumberOfChannelSystems(p.numberOfChannelSystems);
                editor->setStructureNumberOfInaccessiblePockets(p.numberOfInaccessiblePockets);
                editor->setStructureDimensionalityOfPoreSystem(p.dimensionalityOfPoreSystem);
                editor->setStructureLargestCavityDiameter(p.largestCavityDiameter);
                editor->setStructureRestrictingPoreLimitingDiameter(p.restrictingPoreLimitingDiameter);
                editor->setStructureLargestCavityDiameterAlongAViablePath(
                    p.largestCavityDiameterAlongAViablePath);
                editor->setStructureRestrictingPoreLimitingDiameter2(p.restrictingPoreLimitingDiameter2);
                editor->setStructureLargestCavityDiameterAlongAViablePath2(
                    p.largestCavityDiameterAlongAViablePath2);
                editor->setStructureWindow1Size(p.window1Size);
                editor->setStructureWindow2Size(p.window2Size);
                editor->setStructureWindow3Size(p.window3Size);
                editor->setStructureWindow1RingAtoms(p.window1RingAtoms);
                editor->setStructureWindow2RingAtoms(p.window2RingAtoms);
                editor->setStructureWindow3RingAtoms(p.window3RingAtoms);
                if (p.hasBlockingPocketProbe)
                {
                    if (auto structure = std::dynamic_pointer_cast<Structure>(object))
                    {
                        structure->applyBlockingPocketProbeMolecule(p.blockingPocketProbeMolecule);
                        structure->setBlockingPocketProbeEpsilon(p.blockingPocketProbeEpsilon);
                        structure->setBlockingPocketProbeSigma(p.blockingPocketProbeSigma);
                    }
                }
            }
        }

        object->reComputeBoundingBox();
    }
    if (inverse.empty())
        return;

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, actionName, inverse]() { RestoreCellStates(actionName, inverse); });
    if (m_host)
    {
        if (auto scene = SelectedScene())
        {
            m_host->InvalidateSceneAmbientOcclusion(scene);
            m_host->InvalidateSceneIsosurfaces(scene);
        }
        m_host->ReloadAfterCellEdit();
    }
    if (m_cellPane)
        m_cellPane->Reload();
}

void DocumentController::EditCells(std::wstring const& actionName,
                                   std::function<void(Object&)> const& fn,
                                   CellReload reload)
{
    if (!fn)
        return;
    auto before = SnapshotCellStates();
    for (auto const& target : TargetStructures())
    {
        if (auto object = target ? target->object() : nullptr)
            fn(*object);
    }
    RegisterCellUndo(actionName, before);
    ReloadAfterCellEdit(reload);
}

void DocumentController::EditCellsWithoutUndo(std::function<void(Object&)> const& fn, CellReload reload)
{
    if (!fn)
        return;
    for (auto const& target : TargetStructures())
    {
        if (auto object = target ? target->object() : nullptr)
            fn(*object);
    }
    ReloadAfterCellEdit(reload);
}

void DocumentController::ReloadAfterCellEdit(CellReload reload)
{
    if (!m_host)
        return;
    switch (reload)
    {
    case CellReload::None:
        break;
    case CellReload::Renderer:
        if (auto scene = SelectedScene())
        {
            m_host->InvalidateSceneAmbientOcclusion(scene);
            m_host->InvalidateSceneIsosurfaces(scene);
        }
        m_host->ReloadRendererData();
        break;
    case CellReload::CameraAndRenderer:
        if (auto scene = SelectedScene())
        {
            m_host->InvalidateSceneAmbientOcclusion(scene);
            m_host->InvalidateSceneIsosurfaces(scene);
        }
        m_host->ReloadAfterCellEdit();
        break;
    }
}

void DocumentController::ApplyForceFieldIdentifiers(
    std::vector<std::pair<std::shared_ptr<AtomStructureEditor>, RKString>> const& assignments)
{
    if (!m_document)
        return;
    auto& forceFieldSets = m_document->forceFieldSets();

    std::vector<std::pair<std::shared_ptr<AtomStructureEditor>, RKString>> inverse;
    for (auto const& [editor, name] : assignments)
    {
        if (!editor)
            continue;
        inverse.emplace_back(editor, editor->atomForceFieldIdentifier());
        editor->setAtomForceFieldIdentifier(name, forceFieldSets);
        editor->recheckRepresentationStyle();
    }
    if (inverse.empty())
        return;

    RegisterUndo(ObjectUndoStack(), L"Change Force Field",
                 [this, inverse]() { ApplyForceFieldIdentifiers(inverse); });
    // Cocoa invalidates the energy surface: Lennard-Jones parameters drive the grid.
    ReloadRendererInvalidatingIsosurfaces();
    if (m_cellPane)
        m_cellPane->Reload();
}

void DocumentController::SetSpaceGroupHallNumber(int hall)
{
    auto before = SnapshotCellStates();
    for (auto const& target : TargetStructures())
    {
        auto object = target ? target->object() : nullptr;
        if (!object)
            continue;
        if (auto spaceGroup = std::dynamic_pointer_cast<SpaceGroupViewer>(object))
        {
            spaceGroup->setSpaceGroupHallNumber(hall);
            if (auto atoms = std::dynamic_pointer_cast<AtomViewer>(object))
                atoms->expandSymmetry();
            if (auto bonds = std::dynamic_pointer_cast<BondViewer>(object))
                bonds->computeBonds();
            object->reComputeBoundingBox();
        }
    }
    RegisterCellUndo(L"Change Space Group", before);
    ReloadAfterCellEdit(CellReload::CameraAndRenderer);
    Log(L"Space group changed");
}

void DocumentController::ComputeHeliumVoidFraction()
{
    auto before = SnapshotCellStates();
    for (auto const& target : TargetStructures())
    {
        auto editor = target ? std::dynamic_pointer_cast<StructuralPropertyEditor>(target->object()) : nullptr;
        if (!editor)
            continue;
        try
        {
            editor->setStructureHeliumVoidFraction(editor->computeVoidFractionAccelerated());
            editor->recomputeDensityProperties();
        }
        catch (...)
        {
            Log(L"Void-fraction computation failed");
        }
    }
    RegisterCellUndo(L"Compute Helium Void Fraction", before);
}

void DocumentController::ComputeNitrogenSurfaceArea()
{
    auto before = SnapshotCellStates();
    for (auto const& target : TargetStructures())
    {
        auto object = target ? target->object() : nullptr;
        auto editor = std::dynamic_pointer_cast<StructuralPropertyEditor>(object);
        if (!editor)
            continue;
        try
        {
            const double area = editor->computeNitrogenSurfaceAreaAccelerated();
            // Converts to gravimetric/volumetric units internally.
            if (auto structure = std::dynamic_pointer_cast<Structure>(object))
                structure->setStructureNitrogenSurfaceArea(area);
        }
        catch (...)
        {
            Log(L"Surface-area computation failed");
        }
    }
    RegisterCellUndo(L"Compute Nitrogen Surface Area", before);
}

void DocumentController::ComputeWellSurfaceArea()
{
    auto before = SnapshotCellStates();
    for (auto const& target : TargetStructures())
    {
        auto object = target ? target->object() : nullptr;
        auto editor = std::dynamic_pointer_cast<StructuralPropertyEditor>(object);
        if (!editor)
            continue;
        try
        {
            const double area = editor->computeWellSurfaceAreaAccelerated();
            // Converts to gravimetric/volumetric units internally.
            if (auto structure = std::dynamic_pointer_cast<Structure>(object))
                structure->setStructureWellSurfaceArea(area);
        }
        catch (...)
        {
            Log(L"Well-surface-area computation failed");
        }
    }
    RegisterCellUndo(L"Compute Well Surface Area", before);
}

void DocumentController::ComputeGeometricSurfaceArea()
{
    auto before = SnapshotCellStates();
    std::vector<SKFrameworkSnapshot> snapshots;
    std::vector<std::shared_ptr<Structure>> structures;
    for (auto const& target : TargetStructures())
    {
        auto structure = target ? std::dynamic_pointer_cast<Structure>(target->object()) : nullptr;
        if (!structure)
            continue;
        structures.push_back(structure);
        snapshots.push_back(structure->frameworkSnapshot(/*applyingBlockingPockets=*/true));
    }
    try
    {
        const auto results = SKGeometricSurface::surfaceAreas(snapshots);
        for (size_t i = 0; i < results.size() && i < structures.size(); ++i)
            structures[i]->setStructureGeometricSurfaceArea(results[i].area);
    }
    catch (...)
    {
        Log(L"Geometric-surface-area computation failed");
    }
    RegisterCellUndo(L"Compute Geometric Surface Area", before);
}

void DocumentController::ComputeVanDerWaalsGeometricSurfaceArea()
{
    auto before = SnapshotCellStates();
    std::vector<SKFrameworkSnapshot> snapshots;
    std::vector<std::shared_ptr<Structure>> structures;
    for (auto const& target : TargetStructures())
    {
        auto structure = target ? std::dynamic_pointer_cast<Structure>(target->object()) : nullptr;
        if (!structure)
            continue;
        structures.push_back(structure);
        snapshots.push_back(structure->frameworkSnapshot(/*applyingBlockingPockets=*/true));
    }
    try
    {
        const auto results = SKGeometricSurface::vanDerWaalsSurfaceAreas(snapshots);
        for (size_t i = 0; i < results.size() && i < structures.size(); ++i)
            structures[i]->setStructureVanDerWaalsGeometricSurfaceArea(results[i].area);
    }
    catch (...)
    {
        Log(L"Van-der-Waals geometric-surface-area computation failed");
    }
    RegisterCellUndo(L"Compute Van der Waals Geometric Surface Area", before);
}

namespace
{
    // Cocoa recomputeApolloniusPoreProperties: build the diagram once, then copy
    // only the fields the button asked for.
    void ComputeApolloniusPoreProperties(DocumentController& controller,
                                         bool fillChannels, bool fillDiameters, bool fillWindows,
                                         std::wstring const& actionName)
    {
        auto before = controller.SnapshotCellStates();
        std::vector<SKFrameworkSnapshot> snapshots;
        std::vector<std::shared_ptr<StructuralPropertyEditor>> editors;
        for (auto const& target : controller.TargetStructures())
        {
            auto object = target ? target->object() : nullptr;
            auto structure = std::dynamic_pointer_cast<Structure>(object);
            auto editor = std::dynamic_pointer_cast<StructuralPropertyEditor>(object);
            if (!structure || !editor)
                continue;
            editors.push_back(editor);
            snapshots.push_back(structure->frameworkSnapshot(/*applyingBlockingPockets=*/false));
        }
        try
        {
            const auto results = SKApolloniusPoreAnalysis::compute(snapshots);
            for (size_t i = 0; i < results.size() && i < editors.size(); ++i)
            {
                auto& editor = *editors[i];
                auto const& result = results[i];
                if (fillChannels)
                {
                    editor.setStructureNumberOfChannelSystems(result.channels.numberOfChannels);
                    editor.setStructureNumberOfInaccessiblePockets(result.channels.numberOfPockets);
                    editor.setStructureDimensionalityOfPoreSystem(result.channels.dimensionality);
                }
                if (fillDiameters)
                {
                    editor.setStructureLargestCavityDiameter(result.diameters.includedSphereDiameter);
                    editor.setStructureRestrictingPoreLimitingDiameter(result.diameters.freeSphereDiameter);
                    editor.setStructureLargestCavityDiameterAlongAViablePath(
                        result.diameters.includedAlongFreePathDiameter);
                    editor.setStructureRestrictingPoreLimitingDiameter2(result.diameters.freeSphereDiameter2);
                    editor.setStructureLargestCavityDiameterAlongAViablePath2(
                        result.diameters.includedAlongFreePathDiameter2);
                }
                if (fillWindows)
                {
                    auto const& sizes = result.windows.sizes;
                    auto const& rings = result.windows.ringAtomCounts;
                    editor.setStructureWindow1Size(sizes.size() > 0 ? sizes[0] : double2());
                    editor.setStructureWindow2Size(sizes.size() > 1 ? sizes[1] : double2());
                    editor.setStructureWindow3Size(sizes.size() > 2 ? sizes[2] : double2());
                    editor.setStructureWindow1RingAtoms(rings.size() > 0 ? rings[0] : 0);
                    editor.setStructureWindow2RingAtoms(rings.size() > 1 ? rings[1] : 0);
                    editor.setStructureWindow3RingAtoms(rings.size() > 2 ? rings[2] : 0);
                }
            }
        }
        catch (...)
        {
            controller.Log(L"Apollonius pore-property computation failed");
        }
        controller.RegisterCellUndo(actionName, before);
    }
}

void DocumentController::ComputeApolloniusChannels()
{
    ComputeApolloniusPoreProperties(*this, true, false, false, L"Compute Channel Systems");
}

void DocumentController::ComputeApolloniusDiameters()
{
    ComputeApolloniusPoreProperties(*this, false, true, false, L"Compute Pore Diameters");
}

void DocumentController::ComputeApolloniusWindows()
{
    ComputeApolloniusPoreProperties(*this, false, false, true, L"Compute Pore Windows");
}

std::vector<double4> DocumentController::BlockingPockets() const
{
    for (auto const& target : TargetStructures())
    {
        if (auto structure = target ? std::dynamic_pointer_cast<Structure>(target->object()) : nullptr)
            return structure->blockingPockets();
    }
    return {};
}

void DocumentController::SetBlockingPockets(std::vector<std::vector<double4>> const& pockets,
                                            std::wstring const& actionName)
{
    std::vector<std::shared_ptr<Structure>> structures;
    for (auto const& target : TargetStructures())
    {
        if (auto structure = target ? std::dynamic_pointer_cast<Structure>(target->object()) : nullptr)
            structures.push_back(structure);
    }
    ApplyBlockingPockets(structures, pockets, actionName);
}

// The undo holds on to the structures it was recorded for, so it still puts the
// pockets back on those after the selection has moved on.
void DocumentController::ApplyBlockingPockets(std::vector<std::shared_ptr<Structure>> const& structures,
                                              std::vector<std::vector<double4>> const& pockets,
                                              std::wstring const& actionName)
{
    if (structures.empty())
        return;

    std::vector<std::vector<double4>> before;
    before.reserve(structures.size());
    for (auto const& structure : structures)
        before.push_back(structure->blockingPockets());

    for (size_t i = 0; i < structures.size(); ++i)
        structures[i]->setBlockingPockets(i < pockets.size() ? pockets[i] : std::vector<double4>());

    RegisterUndo(ObjectUndoStack(), actionName, [this, structures, before, actionName]()
                 { ApplyBlockingPockets(structures, before, actionName); });
    // The pockets are drawn as spheres and are cut out of the energy grid, so the
    // grids computed without them no longer hold.
    ReloadRendererInvalidatingIsosurfaces();
    if (m_cellPane)
        m_cellPane->Reload();
}

void DocumentController::LoadBlockingPocketsFile()
{
    if (!m_host)
        return;
    m_host->OpenTextFile(L".block", [this](std::wstring const& name, std::wstring const& contents)
    {
        std::vector<double4> pockets =
            Structure::parseBlockingPockets(RKString::fromStdWString(contents));
        // A newly read file replaces the pockets of every selected structure.
        std::vector<std::vector<double4>> perStructure(TargetStructures().size(), pockets);
        SetBlockingPockets(perStructure);
        Log(L"Read " + std::to_wstring(pockets.size()) + L" blocking pockets from " + name);
    });
}

void DocumentController::ComputeBlockingPockets()
{
    std::vector<std::shared_ptr<Structure>> structures;
    std::vector<SKFrameworkSnapshot> snapshots;
    for (auto const& target : TargetStructures())
    {
        auto structure = target ? std::dynamic_pointer_cast<Structure>(target->object()) : nullptr;
        if (!structure)
            continue;
        structures.push_back(structure);
        // Cocoa: blocking-pocket probe parameters, empty pockets on the snapshot.
        SKFrameworkSnapshot snapshot = structure->frameworkSnapshot(/*applyingBlockingPockets=*/false);
        snapshot.probeParameters = structure->blockingPocketProbeParameters();
        snapshot.blockingPockets.clear();
        snapshots.push_back(std::move(snapshot));
    }
    if (structures.empty())
        return;
    try
    {
        const auto pockets = SKApolloniusPoreAnalysis::computeBlockingPockets(snapshots);
        ApplyBlockingPockets(structures, pockets, L"Compute Blocking Pockets");
        size_t count = 0;
        for (auto const& list : pockets)
            count += list.size();
        Log(L"Computed " + std::to_wstring(count) + L" blocking pockets");
    }
    catch (...)
    {
        Log(L"Blocking-pocket computation failed");
    }
}

void DocumentController::ChangeStructureType(ObjectType type)
{
    std::vector<std::tuple<std::shared_ptr<iRASPAObject>, std::shared_ptr<Object>, ObjectType>> states;
    for (auto const& frame : TargetStructures())
    {
        if (!frame || !frame->object() || frame->object()->structureType() == type)
            continue;
        try
        {
            if (auto converted = MakeObjectOfType(type, frame->object()))
                states.emplace_back(frame, converted, type);
        }
        catch (...)
        {
            Log(L"Structure-type conversion failed");
            return;
        }
    }
    if (states.empty())
        return;
    ApplyStructureTypes(states);
    Log(std::wstring(L"Structure type changed to ") + StructureTypeName(type));
}

void DocumentController::ApplyStructureTypes(
    std::vector<std::tuple<std::shared_ptr<iRASPAObject>, std::shared_ptr<Object>, ObjectType>> const& states,
    std::wstring const& actionName)
{
    std::vector<std::tuple<std::shared_ptr<iRASPAObject>, std::shared_ptr<Object>, ObjectType>> inverse;
    for (auto const& [frame, object, type] : states)
    {
        if (!frame || !object)
            continue;
        inverse.emplace_back(frame, frame->object(), frame->type());
        frame->setObject(object, type);
    }
    if (inverse.empty())
        return;

    // Cocoa's replaceStructure names this after the row it sits on.
    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, inverse, actionName]() { ApplyStructureTypes(inverse, actionName); });

    // The frame rows carry a type chip, so they are rebuilt along with the
    // renderer and the inspector.
    RefreshFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
}

void DocumentController::ApplyCellContentShift()
{
    std::vector<std::tuple<std::shared_ptr<iRASPAObject>, std::shared_ptr<Object>, ObjectType>> states;
    for (auto const& frame : TargetStructures())
    {
        auto object = frame ? frame->object() : nullptr;
        if (!object)
            continue;
        try
        {
            // Primitives carry their own overload; Cocoa's applyContentShift
            // takes everything else that is a Structure.
            std::shared_ptr<Object> shifted;
            if (auto primitive = std::dynamic_pointer_cast<Primitive>(object))
                shifted = primitive->appliedCellContentShift();
            else if (auto structure = std::dynamic_pointer_cast<Structure>(object))
                shifted = structure->appliedCellContentShift();
            if (shifted)
                states.emplace_back(frame, shifted, frame->type());
        }
        catch (...)
        {
            Log(L"Applying the cell content shift failed");
            return;
        }
    }
    if (states.empty())
    {
        Log(L"Cell content shift is not supported for this selection");
        return;
    }
    ApplyStructureTypes(states, L"Apply Content Shift");
    Log(L"Cell content shift applied");
}
