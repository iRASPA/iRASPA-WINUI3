#include "pch.h"
#include "DocumentController.h"
#include "StructureTypeTable.h"

#include "atomviewer.h"
#include "bondviewer.h"
#include "iraspaobject.h"
#include "primitive.h"
#include "spacegroupviewer.h"
#include "structuralpropertyviewer.h"
#include "structure.h"
#include "skspacegroup.h"

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
        properties.numberOfChannelSystems = viewer->structureNumberOfChannelSystems();
        properties.numberOfInaccessiblePockets = viewer->structureNumberOfInaccessiblePockets();
        properties.dimensionalityOfPoreSystem = viewer->structureDimensionalityOfPoreSystem();
        properties.largestCavityDiameter = viewer->structureLargestCavityDiameter();
        properties.restrictingPoreLimitingDiameter = viewer->structureRestrictingPoreLimitingDiameter();
        properties.largestCavityDiameterAlongAViablePath =
            viewer->structureLargestCavityDiameterAlongAViablePath();
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

        if (state.cell)
            object->setCell(std::make_shared<SKCell>(*state.cell));
        object->setOrientation(state.orientation);
        object->setRotationDelta(state.rotationDelta);
        object->setOrigin(state.origin);

        // The space group is only re-imposed when it actually differs, because
        // that regenerates the symmetry copies and the bonds.
        if (state.hallNumber)
        {
            if (auto spaceGroup = std::dynamic_pointer_cast<SpaceGroupViewer>(object);
                spaceGroup &&
                static_cast<int>(spaceGroup->spaceGroup().spaceGroupSetting().HallNumber()) != *state.hallNumber)
            {
                spaceGroup->setSpaceGroupHallNumber(*state.hallNumber);
                if (auto atoms = std::dynamic_pointer_cast<AtomViewer>(object))
                    atoms->expandSymmetry();
                if (auto bonds = std::dynamic_pointer_cast<BondViewer>(object))
                    bonds->computeBonds();
            }
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
                editor->setStructureNumberOfChannelSystems(p.numberOfChannelSystems);
                editor->setStructureNumberOfInaccessiblePockets(p.numberOfInaccessiblePockets);
                editor->setStructureDimensionalityOfPoreSystem(p.dimensionalityOfPoreSystem);
                editor->setStructureLargestCavityDiameter(p.largestCavityDiameter);
                editor->setStructureRestrictingPoreLimitingDiameter(p.restrictingPoreLimitingDiameter);
                editor->setStructureLargestCavityDiameterAlongAViablePath(
                    p.largestCavityDiameterAlongAViablePath);
            }
        }

        object->reComputeBoundingBox();
    }
    if (inverse.empty())
        return;

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, actionName, inverse]() { RestoreCellStates(actionName, inverse); });
    if (m_host)
        m_host->ReloadAfterCellEdit();
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
    case CellReload::None:              break;
    case CellReload::Renderer:          m_host->ReloadRendererData(); break;
    case CellReload::CameraAndRenderer: m_host->ReloadAfterCellEdit(); break;
    }
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
