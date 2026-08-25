#include "pch.h"
#include "DocumentController.h"

#include "atomviewer.h"
#include "bondviewer.h"
#include "iraspaobject.h"
#include "spacegroupviewer.h"
#include "structure.h"
#include "skasymmetricatom.h"
#include "skatomtreecontroller.h"
#include "skatomtreenode.h"
#include "skcifwriter.h"
#include "skelement.h"
#include "skmmcifwriter.h"
#include "skpdbwriter.h"
#include "skposcarwriter.h"
#include "skspacegroup.h"
#include "skxyzwriter.h"
#include "protein.h"
#include "proteincrystal.h"

#include <algorithm>
#include <cmath>

// The document-level half of the Atoms tab (Cocoa
// StructureAtomDetailViewController plus the atom half of its undo manager):
// the per-field setters, the node insert/remove/move operations and the
// whole-structure commands of the row context menu. Nothing here touches XAML.

namespace
{
    std::shared_ptr<AtomViewer> AtomViewerOf(std::shared_ptr<iRASPAObject> const& frame)
    {
        return frame ? std::dynamic_pointer_cast<AtomViewer>(frame->object()) : nullptr;
    }

    std::shared_ptr<SKAtomTreeController> AtomTreeOf(std::shared_ptr<iRASPAObject> const& frame)
    {
        auto viewer = AtomViewerOf(frame);
        return viewer ? viewer->atomsTreeController() : nullptr;
    }

    std::wstring ElementSymbolOf(std::shared_ptr<SKAsymmetricAtom> const& atom)
    {
        if (!atom)
            return {};
        const auto z = static_cast<size_t>(atom->elementIdentifier());
        if (z < PredefinedElements::predefinedElements.size())
            return PredefinedElements::predefinedElements[z]._chemicalSymbol.toStdWString();
        return L"?";
    }

    std::optional<int> AtomicNumberOf(std::wstring const& text)
    {
        RKString symbol = RKString::fromStdWString(text).trimmed();
        auto it = PredefinedElements::atomicNumberData.find(symbol);
        if (it != PredefinedElements::atomicNumberData.end())
            return it->second;
        for (size_t z = 0; z < PredefinedElements::predefinedElements.size(); ++z)
        {
            if (PredefinedElements::predefinedElements[z]._chemicalSymbol.toLower() == symbol.toLower())
                return static_cast<int>(z);
        }
        return std::nullopt;
    }

    // Where a node sits right now, as the undo of a move or a delete needs it.
    DocumentController::AtomNodePosition PositionOf(
        std::shared_ptr<SKAtomTreeNode> const& node,
        std::shared_ptr<SKAtomTreeController> const& tree)
    {
        DocumentController::AtomNodePosition position;
        position.node = node;
        auto parent = node ? node->parent() : nullptr;
        position.parent = (parent == tree->hiddenRootNode()) ? nullptr : parent;
        if (parent)
        {
            if (auto index = parent->findChildIndex(node))
                position.index = static_cast<int>(*index);
        }
        return position;
    }

    // A node whose ancestor is in the same set goes (and comes back) with it.
    bool CoveredByAncestor(std::shared_ptr<SKAtomTreeNode> const& node,
                           std::vector<std::shared_ptr<SKAtomTreeNode>> const& nodes)
    {
        return std::any_of(nodes.begin(), nodes.end(),
                           [&node](std::shared_ptr<SKAtomTreeNode> const& other)
                           {
                               return other && other != node && node->isDescendantOfNode(other);
                           });
    }
}

std::shared_ptr<iRASPAObject> DocumentController::AtomsFrame() const
{
    for (auto const& iraspa : TargetStructures())
    {
        if (AtomViewerOf(iraspa))
            return iraspa;
    }
    return nullptr;
}

std::shared_ptr<SKAtomTreeController> DocumentController::AtomTree(
    std::weak_ptr<iRASPAObject> frameRef) const
{
    return AtomTreeOf(frameRef.lock());
}

void DocumentController::SetAtomField(std::shared_ptr<SKAtomTreeNode> const& node,
                                      AtomField field, AtomFieldValue const& value)
{
    if (!node)
        return;
    auto atom = node->representedObject();
    if (!atom)
        return;

    // Gallery and cloud projects are read-only (Cocoa gates the field editors on
    // proxyProject.isEditable): refuse the write and put the stored value back
    // into the row.
    if (!IsSelectedProjectEditable())
    {
        if (m_atomsPane)
            m_atomsPane->RefreshField(node, field);
        return;
    }

    auto number = [&value](double fallback)
    {
        auto const* v = std::get_if<double>(&value);
        return v ? *v : fallback;
    };
    auto flag = [&value](bool fallback)
    {
        auto const* v = std::get_if<bool>(&value);
        return v ? *v : fallback;
    };
    auto text = [&value]() -> std::wstring
    {
        auto const* v = std::get_if<std::wstring>(&value);
        return v ? *v : std::wstring{};
    };

    auto* stack = ObjectUndoStack();
    auto registerInverse = [this, stack, node](wchar_t const* name, AtomField f,
                                               AtomFieldValue const& previous)
    {
        RegisterUndo(stack, name, [this, node, f, previous]() { SetAtomField(node, f, previous); });
    };

    const bool3 fixed = atom->isFixed();

    switch (field)
    {
    case AtomField::name:
    {
        registerInverse(L"Change Atom Name", field, atom->displayName().toStdWString());
        RKString newName = RKString::fromStdWString(text());
        node->setDisplayName(newName);
        atom->setDisplayName(newName);
        break;
    }
    case AtomField::element:
    {
        auto z = AtomicNumberOf(text());
        if (!z || *z < 0 || *z >= static_cast<int>(PredefinedElements::predefinedElements.size()))
            return;
        // The port renames the atom and its force-field type along with the
        // element, so all three writes have to undo as one step.
        if (stack)
            stack->beginUndoGrouping(L"Change Atom Element");
        registerInverse(L"Change Atom Element", AtomField::name,
                        atom->displayName().toStdWString());
        registerInverse(L"Change Atom Element", AtomField::forceFieldType,
                        atom->uniqueForceFieldName().toStdWString());
        registerInverse(L"Change Atom Element", field, ElementSymbolOf(atom));
        auto const& el = PredefinedElements::predefinedElements[static_cast<size_t>(*z)];
        atom->setElementIdentifier(*z);
        atom->setDisplayName(el._chemicalSymbol);
        atom->setUniqueForceFieldName(el._chemicalSymbol);
        node->setDisplayName(el._chemicalSymbol);
        if (stack)
            stack->endUndoGrouping();
        break;
    }
    case AtomField::forceFieldType:
        registerInverse(L"Change Atom Force Field Type", field,
                        atom->uniqueForceFieldName().toStdWString());
        atom->setUniqueForceFieldName(RKString::fromStdWString(text()));
        break;
    case AtomField::occupancy:
        registerInverse(L"Change Atom Occupancy", field, atom->occupancy());
        atom->setOccupancy(std::clamp(number(atom->occupancy()), 0.0, 1.0));
        break;
    case AtomField::positionX:
        registerInverse(L"Change Atom X-Position", field, atom->position().x);
        atom->setPositionX(number(atom->position().x));
        break;
    case AtomField::positionY:
        registerInverse(L"Change Atom Y-Position", field, atom->position().y);
        atom->setPositionY(number(atom->position().y));
        break;
    case AtomField::positionZ:
        registerInverse(L"Change Atom Z-position", field, atom->position().z);
        atom->setPositionZ(number(atom->position().z));
        break;
    case AtomField::charge:
        registerInverse(L"Change Atom Charge", field, atom->charge());
        atom->setCharge(number(atom->charge()));
        break;
    case AtomField::fixedX:
    case AtomField::fixedY:
    case AtomField::fixedZ:
    {
        const bool previous = (field == AtomField::fixedX) ? fixed.x
                            : (field == AtomField::fixedY) ? fixed.y : fixed.z;
        registerInverse(L"Change Fix Atom", field, previous);
        bool3 updated = fixed;
        bool& channel = (field == AtomField::fixedX) ? updated.x
                      : (field == AtomField::fixedY) ? updated.y : updated.z;
        channel = flag(previous);
        atom->setIsFixed(updated);
        break;
    }
    }

    if (m_atomsPane)
        m_atomsPane->RefreshField(node, field);

    switch (field)
    {
    case AtomField::element:
    case AtomField::occupancy:
    case AtomField::positionX:
    case AtomField::positionY:
    case AtomField::positionZ:
        AfterAtomGeometryEdit(AtomsFrame());
        break;
    case AtomField::charge:
        if (m_atomsPane)
            m_atomsPane->RefreshNetCharge();
        ReloadRenderer();
        break;
    default:
        break;
    }
}

void DocumentController::AfterAtomGeometryEdit(std::weak_ptr<iRASPAObject> frameRef)
{
    if (auto frame = frameRef.lock())
    {
        auto object = frame->object();
        if (auto atoms = std::dynamic_pointer_cast<AtomViewer>(object))
            atoms->expandSymmetry();
        if (auto bonds = std::dynamic_pointer_cast<BondViewer>(object))
            bonds->computeBonds();
    }
    ReloadRenderer();
    if (m_atomsPane)
        m_atomsPane->RefreshNetCharge();
}

void DocumentController::SetAtomSelection(std::weak_ptr<iRASPAObject> frameRef,
                                          std::set<std::shared_ptr<SKAtomTreeNode>> const& selection,
                                          std::wstring const& actionName)
{
    auto tree = AtomTreeOf(frameRef.lock());
    if (!tree)
        return;

    auto previous = tree->selectedTreeNodes();
    if (previous == selection)
        return;

    auto* stack = ObjectUndoStack();
    const bool replaying = stack && (stack->isUndoing() || stack->isRedoing());
    RegisterUndo(stack, actionName,
                 [this, frameRef, previous, actionName]()
                 {
                     SetAtomSelection(frameRef, previous, actionName);
                 });

    tree->clearSelection();
    for (auto const& node : selection)
    {
        if (node)
            tree->insertSelectionIndexPath(node->indexPath());
    }

    // On the way in the rows are the ones that changed and already show the new
    // selection; while replaying an undo the model is ahead of them.
    if (replaying)
        ReloadAtomRowSelection();

    // Light path: only the selection instance buffers are re-uploaded.
    if (m_host)
        m_host->ReloadRendererSelection();
}

void DocumentController::ReloadAtomRowSelection() const
{
    if (m_atomsPane)
        m_atomsPane->ReloadRowSelection();
}

void DocumentController::AddAtomNode(std::weak_ptr<iRASPAObject> frameRef,
                                     std::shared_ptr<SKAtomTreeNode> const& target,
                                     bool group)
{
    auto tree = AtomTreeOf(frameRef.lock());
    if (!tree)
        return;

    std::shared_ptr<SKAtomTreeNode> parent = tree->hiddenRootNode();
    int index = static_cast<int>(parent->childCount());
    if (target)
    {
        if (target->isGroup())
        {
            parent = target;
            index = 0;
        }
        else if (auto targetParent = target->parent())
        {
            parent = targetParent;
            auto found = targetParent->findChildIndex(target);
            index = found ? static_cast<int>(*found) + 1
                          : static_cast<int>(targetParent->childCount());
        }
    }

    auto atom = std::make_shared<SKAsymmetricAtom>(RKString(group ? "New group" : "C"), 6);
    auto node = std::make_shared<SKAtomTreeNode>(atom);
    node->setIsGroup(group);

    AtomNodePosition position;
    position.node = node;
    position.parent = (parent == tree->hiddenRootNode()) ? nullptr : parent;
    position.index = index;
    InsertAtomNodes(frameRef, { position }, group ? L"Add Atom-Group" : L"Adding New Atom");
    Log(group ? L"Atom group added" : L"Atom added");
}

// Cocoa addNode(_:inItem:atIndex:inStructure:): insert atom nodes at fixed
// positions and register their removal as the undo.
void DocumentController::InsertAtomNodes(std::weak_ptr<iRASPAObject> frameRef,
                                         std::vector<AtomNodePosition> const& positions,
                                         std::wstring const& actionName)
{
    auto frame = frameRef.lock();
    auto viewer = AtomViewerOf(frame);
    auto tree = viewer ? viewer->atomsTreeController() : nullptr;
    if (!tree || positions.empty())
        return;

    std::vector<std::shared_ptr<SKAtomTreeNode>> inserted;
    inserted.reserve(positions.size());

    // Ascending index order, so earlier inserts do not shift later ones.
    std::vector<AtomNodePosition> ordered = positions;
    std::sort(ordered.begin(), ordered.end(),
              [](AtomNodePosition const& a, AtomNodePosition const& b) { return a.index < b.index; });

    for (auto const& position : ordered)
    {
        if (!position.node)
            continue;
        auto parent = position.parent ? position.parent : tree->hiddenRootNode();
        int index = position.index;
        if (index < 0 || index > static_cast<int>(parent->childCount()))
            index = static_cast<int>(parent->childCount());
        tree->insertNodeInParent(position.node, parent, index);
        inserted.push_back(position.node);
    }
    tree->setTags();

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, frameRef, inserted, actionName]()
                 {
                     RemoveAtomNodes(frameRef, inserted, actionName);
                 });

    viewer->expandSymmetry();
    if (auto bonds = std::dynamic_pointer_cast<BondViewer>(frame->object()))
        bonds->computeBonds();
    ReloadRenderer();
    // Rebuild whichever inspector tab is showing, so the atom rows follow.
    if (m_host)
        m_host->RefreshInspector();
}

// Cocoa removeNode(_:fromItem:atIndex:inStructure:).
void DocumentController::RemoveAtomNodes(std::weak_ptr<iRASPAObject> frameRef,
                                         std::vector<std::shared_ptr<SKAtomTreeNode>> const& nodes,
                                         std::wstring const& actionName)
{
    auto frame = frameRef.lock();
    auto viewer = AtomViewerOf(frame);
    auto tree = viewer ? viewer->atomsTreeController() : nullptr;
    if (!tree || nodes.empty())
        return;

    // Record where each node sits, then remove bottom-up so the recorded indices
    // stay valid for the re-insert.
    std::vector<AtomNodePosition> positions;
    positions.reserve(nodes.size());
    for (auto const& node : nodes)
    {
        if (!node || CoveredByAncestor(node, nodes))
            continue;
        positions.push_back(PositionOf(node, tree));
    }
    if (positions.empty())
        return;

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, frameRef, positions, actionName]()
                 {
                     InsertAtomNodes(frameRef, positions, actionName);
                 });

    std::vector<AtomNodePosition> descending = positions;
    std::sort(descending.begin(), descending.end(),
              [](AtomNodePosition const& a, AtomNodePosition const& b) { return a.index > b.index; });
    for (auto const& position : descending)
        tree->removeNode(position.node);

    tree->clearSelection();
    tree->setTags();
    viewer->expandSymmetry();
    if (auto bonds = std::dynamic_pointer_cast<BondViewer>(frame->object()))
        bonds->computeBonds();
    ReloadRenderer();
    if (m_host)
        m_host->RefreshInspector();
}

// Cocoa moveNodes(_:inStructure:) with its reverse-move registration.
void DocumentController::RestoreAtomNodePositions(std::weak_ptr<iRASPAObject> frameRef,
                                                  std::vector<AtomNodePosition> const& positions)
{
    auto frame = frameRef.lock();
    auto viewer = AtomViewerOf(frame);
    auto tree = viewer ? viewer->atomsTreeController() : nullptr;
    if (!tree || positions.empty())
        return;

    std::vector<AtomNodePosition> current;
    current.reserve(positions.size());
    for (auto const& target : positions)
    {
        if (target.node)
            current.push_back(PositionOf(target.node, tree));
    }

    RegisterUndo(ObjectUndoStack(), L"Reorder Atoms",
                 [this, frameRef, current]() { RestoreAtomNodePositions(frameRef, current); });

    for (auto const& target : positions)
    {
        if (!target.node)
            continue;
        auto parent = target.parent ? target.parent : tree->hiddenRootNode();
        tree->removeNode(target.node);
        int index = target.index;
        if (index < 0 || index > static_cast<int>(parent->childCount()))
            index = static_cast<int>(parent->childCount());
        tree->insertNodeInParent(target.node, parent, index);
    }
    tree->setTags();

    viewer->expandSymmetry();
    if (auto bonds = std::dynamic_pointer_cast<BondViewer>(frame->object()))
        bonds->computeBonds();
    ReloadRenderer();
    if (m_host)
        m_host->RefreshInspector();
}

bool DocumentController::MoveAtomNodes(std::weak_ptr<iRASPAObject> frameRef,
                                       std::vector<std::shared_ptr<SKAtomTreeNode>> const& nodes,
                                       std::shared_ptr<SKAtomTreeNode> const& target)
{
    auto frame = frameRef.lock();
    auto tree = AtomTreeOf(frame);
    if (!tree || nodes.empty())
        return false;

    // Cocoa validateDrop: a parent can not be dragged into its own descendant
    // (or onto itself).
    if (target)
    {
        for (auto const& node : nodes)
        {
            if (node && (target == node || target->isDescendantOfNode(node)))
                return false;
        }
    }

    std::shared_ptr<SKAtomTreeNode> newParent = tree->hiddenRootNode();
    int insertIndex = static_cast<int>(newParent->childCount());
    if (target)
    {
        if (target->isGroup())
        {
            newParent = target;
            insertIndex = 0;
        }
        else if (auto parent = target->parent())
        {
            newParent = parent;
            auto found = parent->findChildIndex(target);
            insertIndex = found ? static_cast<int>(*found) + 1
                                : static_cast<int>(parent->childCount());
        }
    }

    try
    {
        // Cocoa moveNodes registers the reverse moves as the undo.
        std::vector<AtomNodePosition> origins;
        origins.reserve(nodes.size());
        for (auto const& node : nodes)
        {
            if (node)
                origins.push_back(PositionOf(node, tree));
        }
        RegisterUndo(ObjectUndoStack(), L"Reorder Atoms",
                     [this, frameRef, origins]() { RestoreAtomNodePositions(frameRef, origins); });

        for (auto const& node : nodes)
        {
            if (!node)
                continue;
            // Moving within the same parent: account for the removal when the
            // destination lies past the old position (Cocoa acceptDrop).
            if (node->parent() == newParent)
            {
                if (auto oldIndex = newParent->findChildIndex(node))
                {
                    if (insertIndex > static_cast<int>(*oldIndex))
                        --insertIndex;
                }
            }
            tree->removeNode(node);
            if (insertIndex > static_cast<int>(newParent->childCount()))
                insertIndex = static_cast<int>(newParent->childCount());
            tree->insertNodeInParent(node, newParent, insertIndex);
            ++insertIndex;
        }
        tree->setTags();

        // Keep the moved nodes selected, as Cocoa keeps the dragged rows
        // selected after the move.
        tree->clearSelection();
        for (auto const& node : nodes)
        {
            if (node)
                tree->insertSelectionIndexPath(node->indexPath());
        }
    }
    catch (...)
    {
        Log(L"Atom drag & drop failed");
        return false;
    }

    ReloadRenderer();
    if (m_host)
        m_host->RefreshInspector();
    return true;
}

void DocumentController::InvertAtomSelection(std::weak_ptr<iRASPAObject> frameRef)
{
    auto tree = AtomTreeOf(frameRef.lock());
    if (!tree)
        return;

    auto selected = tree->selectedTreeNodes();
    std::set<std::shared_ptr<SKAtomTreeNode>> inverted;
    for (auto const& node : tree->flattenedLeafNodes())
    {
        if (node && selected.count(node) == 0)
            inverted.insert(node);
    }
    SetAtomSelection(frameRef, inverted, L"Invert Selection");
    ReloadAtomRowSelection();
}

// Cocoa Selection > Copy/Move to new Movie: clone the frame's settings (with an
// empty atom list), copy the selected atoms into it, and append it to the
// current scene as a new movie.
void DocumentController::AtomSelectionToNewMovie(std::weak_ptr<iRASPAObject> frameRef, bool move)
{
    auto frame = frameRef.lock();
    auto viewer = AtomViewerOf(frame);
    auto tree = viewer ? viewer->atomsTreeController() : nullptr;
    if (!tree)
        return;
    auto selectedNodes = tree->selectedTreeNodes();
    if (selectedNodes.empty())
    {
        Log(L"No atoms selected");
        return;
    }

    auto movie = frame->parent().lock();
    auto scene = movie ? movie->parent().lock() : nullptr;
    if (!scene)
    {
        Log(L"No scene to add the new movie to");
        return;
    }

    try
    {
        int hallNumber = 1;
        if (auto spaceGroup = std::dynamic_pointer_cast<SpaceGroupViewer>(frame->object()))
            hallNumber = spaceGroup->spaceGroup().spaceGroupSetting().HallNumber();

        auto newFrame = frame->shallowClone();
        auto newViewer = newFrame ? std::dynamic_pointer_cast<AtomViewer>(newFrame->object())
                                  : nullptr;
        if (!newViewer || !newViewer->atomsTreeController())
        {
            Log(L"Copy to new movie failed");
            return;
        }
        if (auto spaceGroup = std::dynamic_pointer_cast<SpaceGroupViewer>(newFrame->object()))
            spaceGroup->setSpaceGroupHallNumber(hallNumber);

        for (auto const& node : selectedNodes)
        {
            if (!node || !node->representedObject())
                continue;
            auto newAtom = std::make_shared<SKAsymmetricAtom>(*node->representedObject());
            newViewer->atomsTreeController()->appendToRootnodes(
                std::make_shared<SKAtomTreeNode>(newAtom));
        }
        newViewer->expandSymmetry();
        newViewer->atomsTreeController()->setTags();
        if (auto newStructure = std::dynamic_pointer_cast<Structure>(newFrame->object()))
        {
            newStructure->reComputeBoundingBox();
            newStructure->computeBonds();
            if (auto bonds = newStructure->bondSetController())
                bonds->setTags();
        }

        auto newMovie = Movie::create(newFrame);
        scene->insertChild(static_cast<int>(scene->movies().size()), newMovie);

        if (move)
        {
            std::vector<std::shared_ptr<SKAtomTreeNode>> nodes(selectedNodes.begin(),
                                                               selectedNodes.end());
            for (auto const& node : nodes)
            {
                if (!CoveredByAncestor(node, nodes))
                    tree->removeNode(node);
            }
            tree->clearSelection();
            tree->setTags();
            viewer->expandSymmetry();
            if (auto bonds = std::dynamic_pointer_cast<BondViewer>(frame->object()))
                bonds->computeBonds();
        }

        RefreshSceneAndFrameRows();
        if (m_host)
            m_host->ApplySelectionToRenderer(true);
        Log(move ? L"Selection moved to new movie" : L"Selection copied to new movie");
    }
    catch (...)
    {
        Log(L"Selection to new movie failed");
    }
}

void DocumentController::SetAtomVisibilityFromSelection(std::weak_ptr<iRASPAObject> frameRef,
                                                        bool matchSelection)
{
    auto tree = AtomTreeOf(frameRef.lock());
    if (!tree)
        return;

    auto nodes = tree->flattenedLeafNodes();
    if (matchSelection)
    {
        auto selected = tree->selectedTreeNodes();
        for (auto const& node : nodes)
        {
            if (node && node->representedObject())
                node->representedObject()->setVisibility(selected.count(node) > 0);
        }
    }
    else
    {
        for (auto const& node : nodes)
        {
            if (node && node->representedObject())
                node->representedObject()->toggleVisibility();
        }
    }
    ReloadRendererInvalidatingAmbientOcclusion();
    // Rebuild so the vis checkboxes reflect the new state.
    if (m_host)
        m_host->RefreshInspector();
}

// Whether the operation applies is the structure's own answer: the four builders are declared on
// Structure and hand back nothing for a type they do not fit, which ApplyReplacedAtomStructure
// reports. Which of them the atom context menu offers is settled separately, the way Cocoa's
// validateMenuItem does it.
void DocumentController::RunAtomStructureOperation(std::weak_ptr<iRASPAObject> frameRef,
                                                   AtomStructureOperation operation)
{
    auto frame = frameRef.lock();
    auto structure = frame ? std::dynamic_pointer_cast<Structure>(frame->object()) : nullptr;
    if (!structure)
        return;
    try
    {
        switch (operation)
        {
        case AtomStructureOperation::FlattenHierarchy:
            ApplyReplacedAtomStructure(frameRef, structure->flattenHierarchy(), L"Flatten Hierarchy");
            break;
        case AtomStructureOperation::SuperCell:
            ApplyReplacedAtomStructure(frameRef, structure->superCell(), L"Make Super-Cell");
            break;
        case AtomStructureOperation::RemoveSymmetry:
            ApplyReplacedAtomStructure(frameRef, structure->removeSymmetry(), L"Remove Symmetry");
            break;
        case AtomStructureOperation::WrapAtomsToCell:
            ApplyReplacedAtomStructure(frameRef, structure->wrapAtomsToCell(), L"Wrap Atoms to Cell");
            break;
        }
    }
    catch (...)
    {
        Log(L"Structure operation failed");
    }
}

void DocumentController::FindAtomSymmetry(std::weak_ptr<iRASPAObject> frameRef,
                                          AtomSymmetrySearch search)
{
    auto frame = frameRef.lock();
    auto structure = frame ? std::dynamic_pointer_cast<Structure>(frame->object()) : nullptr;
    if (!structure || !structure->cell())
    {
        Log(L"Symmetry search needs a structure with a cell");
        return;
    }

    try
    {
        const std::vector<std::tuple<double3, int, double>> symmetryData =
            structure->atomSymmetryData();
        const double3x3 unitCell = structure->cell()->unitCell();
        const double precision = structure->cell()->precision();

        std::optional<double3x3> newUnitCell;
        std::vector<std::tuple<double3, int, double>> newAtoms;
        std::optional<int> hallNumber;

        switch (search)
        {
        case AtomSymmetrySearch::Impose:
            if (auto found = SKSpaceGroup::findSpaceGroup(unitCell, symmetryData, true, precision))
            {
                newUnitCell = found->cell.unitCell();
                newAtoms = found->asymmetricAtoms;
                hallNumber = found->HallNumber;
            }
            break;
        case AtomSymmetrySearch::Primitive:
            if (auto found = SKSpaceGroup::SKFindPrimitive(unitCell, symmetryData, true, precision))
            {
                newUnitCell = found->cell.unitCell();
                newAtoms = found->atoms;
                hallNumber = 1;
            }
            break;
        case AtomSymmetrySearch::Niggli:
            if (auto found = SKSpaceGroup::findNiggliCell(unitCell, symmetryData, false, precision))
            {
                newUnitCell = found->cell.unitCell();
                newAtoms = found->atoms;
                hallNumber = found->HallNumber;
            }
            break;
        }

        if (!newUnitCell)
        {
            Log(L"No symmetry found");
            return;
        }

        auto newObject = frame->object()->shallowClone();
        auto newStructure = std::dynamic_pointer_cast<Structure>(newObject);
        if (!newStructure)
        {
            Log(L"Symmetry search failed");
            return;
        }
        newStructure->setAtomSymmetryData(*newUnitCell, newAtoms);
        if (hallNumber)
        {
            if (auto spaceGroup = std::dynamic_pointer_cast<SpaceGroupViewer>(newObject))
                spaceGroup->setSpaceGroupHallNumber(*hallNumber);
        }
        // Cocoa action names for the three symmetry searches.
        ApplyReplacedAtomStructure(frameRef, newStructure,
                                   search == AtomSymmetrySearch::Impose
                                       ? L"Find and Impose Symmetry"
                                       : (search == AtomSymmetrySearch::Primitive ? L"Find Primitive"
                                                                                  : L"Find Niggli"));
    }
    catch (...)
    {
        Log(L"Symmetry search failed");
    }
}

void DocumentController::ApplyReplacedAtomStructure(std::weak_ptr<iRASPAObject> frameRef,
                                                    std::shared_ptr<Structure> const& newStructure,
                                                    std::wstring const& actionName)
{
    auto frame = frameRef.lock();
    if (!frame)
        return;
    if (!newStructure)
    {
        Log(L"Operation not supported for this structure type");
        return;
    }
    // Cocoa setStructureState keeps the previous cell/space-group/atoms/bonds for
    // the undo; here the whole object is swapped, so putting the previous object
    // back is the inverse.
    auto previous = std::dynamic_pointer_cast<Structure>(frame->object());
    try
    {
        newStructure->reComputeBoundingBox();
        if (auto tree = newStructure->atomsTreeController())
            tree->setTags();
        if (m_document)
        {
            newStructure->setRepresentationColorSchemeIdentifier(
                newStructure->atomColorSchemeIdentifier(), m_document->colorSets());
            newStructure->updateForceField(m_document->forceFieldSets());
        }
        newStructure->expandSymmetry();
        if (auto tree = newStructure->atomsTreeController())
            tree->setTags();
        newStructure->computeBonds();
        if (auto bonds = newStructure->bondSetController())
            bonds->setTags();
        // Flattening and re-tiling both rebuild the atom tree, and the ribbon of a protein is swept
        // along a backbone read off that tree; left alone it would keep drawing the old atoms.
        if (auto ribbon = std::dynamic_pointer_cast<ProteinRibbonStructureEditor>(newStructure))
            ribbon->rebuildBackbone();
        frame->setObject(newStructure);
    }
    catch (...)
    {
        Log(L"Structure operation failed");
        return;
    }
    // Registered only now: a build that threw leaves the frame untouched, and an undo entry for it
    // would put back the object that is already there.
    if (previous)
    {
        RegisterUndo(ObjectUndoStack(), actionName,
                     [this, frameRef, previous, actionName]()
                     {
                         ApplyReplacedAtomStructure(frameRef, previous, actionName);
                     });
    }
    // Cocoa setStructureState resets the camera to frame the (possibly very different) new bounding
    // box — a super-cell is far larger than the original cell, for instance.
    if (m_project)
    {
        if (auto cam = m_project->camera())
            cam->resetForNewBoundingBox(m_project->renderBoundingBox());
    }
    // Reloads the renderer and rebuilds the current inspector tab.
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
    Log(L"Structure updated");
}

void DocumentController::ExportAtoms(std::weak_ptr<iRASPAObject> frameRef,
                                     AtomExportFormat format) const
{
    auto written = WriteAtoms(frameRef, format);
    if (!written || !m_host)
        return;
    m_host->SaveTextFile(written->text, written->extension, written->typeName,
                         written->suggestedName);
}

std::optional<DocumentController::AtomExport> DocumentController::WriteAtoms(
    std::weak_ptr<iRASPAObject> frameRef, AtomExportFormat format) const
{
    auto frame = frameRef.lock();
    auto structure = frame ? std::dynamic_pointer_cast<Structure>(frame->object()) : nullptr;
    if (!structure)
    {
        Log(L"Export needs a structure");
        return std::nullopt;
    }

    try
    {
        AtomExport result;
        RKString displayName = structure->displayName();
        result.suggestedName = displayName.toStdWString();

        SKSpaceGroup spaceGroup = SKSpaceGroup(1);
        if (auto viewer = std::dynamic_pointer_cast<SpaceGroupViewer>(frame->object()))
            spaceGroup = viewer->spaceGroup();

        RKString out;
        if (format == AtomExportFormat::PDB || format == AtomExportFormat::mmCIF ||
            format == AtomExportFormat::XYZ)
        {
            // PDB / mmCIF / XYZ use Cartesian positions.
            auto atoms = structure->asymmetricAtomsCopiedAndTransformedToCartesianPositions();
            auto cellData = structure->cellForCartesianPositions();
            std::shared_ptr<SKCell> cell = cellData ? cellData->first : nullptr;
            double3 origin = cellData ? cellData->second : double3(0.0, 0.0, 0.0);
            if (format == AtomExportFormat::PDB)
            {
                out = SKPDBWriter(displayName, spaceGroup, cell, origin, atoms).string();
                result.extension = L".pdb";
                result.typeName = L"PDB";
            }
            else if (format == AtomExportFormat::mmCIF)
            {
                const bool withProteinInfo =
                    std::dynamic_pointer_cast<Protein>(frame->object()) != nullptr ||
                    std::dynamic_pointer_cast<ProteinCrystal>(frame->object()) != nullptr;
                out = SKmmCIFWriter(displayName, spaceGroup, cell, origin, atoms,
                                    false, false, withProteinInfo).string();
                result.extension = L".mmcif";
                result.typeName = L"mmCIF";
            }
            else
            {
                out = SKXYZWriter(displayName, spaceGroup, cell, origin, atoms).string();
                result.extension = L".xyz";
                result.typeName = L"XYZ";
            }
        }
        else
        {
            // CIF / POSCAR use fractional positions.
            auto atoms = structure->asymmetricAtomsCopiedAndTransformedToFractionalPositions();
            auto cellData = structure->cellForFractionalPositions();
            if (!cellData)
            {
                Log(L"Export failed: no cell for fractional positions");
                return std::nullopt;
            }
            if (format == AtomExportFormat::CIF)
            {
                out = SKCIFWriter(displayName, spaceGroup, cellData->first,
                                  cellData->second, atoms).string();
                result.extension = L".cif";
                result.typeName = L"CIF";
            }
            else
            {
                out = SKPOSCARWriter(displayName, spaceGroup, cellData->first,
                                     cellData->second, atoms).string();
                result.extension = L".poscar";
                result.typeName = L"VASP POSCAR";
            }
        }
        result.text = out.toStdWString();
        return result;
    }
    catch (...)
    {
        Log(L"Export failed");
        return std::nullopt;
    }
}
