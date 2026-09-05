#include "pch.h"
#include "DocumentController.h"
#include "ProjectNodeUtil.h"

#include "iraspaobject.h"
#include "iraspaproject.h"
#include "projectgroup.h"
#include "scenelist.h"
#include "structuralpropertyviewer.h"
#include "structure.h"

#include <algorithm>
#include <cwctype>

// Document-side project operations, ported from the NSUndoManager use in
// iRASPA-COCOA: every model mutation registers the closure that reverses it, and
// the reversing closure registers its own inverse, which becomes the redo entry.

void DocumentController::Log(std::wstring const& message) const
{
    if (m_host)
        m_host->Log(message);
}

void DocumentController::RefreshEditMenuLabels() const
{
    if (m_host)
        m_host->RefreshEditMenuLabels();
}

void DocumentController::ReloadRenderer() const
{
    if (m_host)
        m_host->ReloadRendererData();
}

void DocumentController::ReloadRendererInvalidatingAmbientOcclusion() const
{
    if (!m_host)
        return;
    if (auto scene = SelectedScene())
        m_host->InvalidateSceneAmbientOcclusion(scene);
    m_host->ReloadRendererData();
}

void DocumentController::ReloadRendererInvalidatingIsosurfaces() const
{
    if (!m_host)
        return;
    if (auto scene = SelectedScene())
        m_host->InvalidateSceneIsosurfaces(scene);
    m_host->ReloadRendererData();
}

void DocumentController::RefitCameraToBoundingBox() const
{
    if (m_host)
        m_host->ReloadAfterCellEdit();
}

void DocumentController::ReapplyForceFieldAndColors() const
{
    if (!m_document || !m_project || !m_project->sceneList())
        return;
    try
    {
        for (auto const& sceneRow : m_project->sceneList()->allIRASPAStructures())
        {
            for (auto const& iraspa : sceneRow)
            {
                if (!iraspa)
                    continue;
                if (auto structure = std::dynamic_pointer_cast<Structure>(iraspa->object()))
                {
                    structure->setRepresentationColorSchemeIdentifier(
                        structure->atomColorSchemeIdentifier(), m_document->colorSets());
                    structure->updateForceField(m_document->forceFieldSets());
                }
            }
        }
        ReloadRenderer();
    }
    catch (...)
    {
        Log(L"Elements apply error");
    }
}

namespace
{
// Trim whitespace; keep the typed casing so multi-word names like "VMD CPK"
// still match built-in sets (Cocoa forceFieldSetIndex / colorSetIndex).
std::wstring TrimName(std::wstring text)
{
    const size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
        return {};
    return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

bool NamesEqualIgnoreCase(std::wstring const& a, std::wstring const& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (std::towlower(a[i]) != std::towlower(b[i]))
            return false;
    }
    return true;
}
}

int DocumentController::AddForceFieldSet(std::wstring const& name, int forkFrom)
{
    if (!m_document)
        return -1;
    const std::wstring wanted = TrimName(name);
    if (wanted.empty())
        return -1;

    ForceFieldSets& sets = m_document->forceFieldSets();
    std::vector<ForceFieldSet>& list = sets.forceFieldSets();
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (NamesEqualIgnoreCase(list[i].displayName().toStdWString(), wanted))
            return static_cast<int>(i);
    }
    if (forkFrom < 0 || forkFrom >= static_cast<int>(list.size()))
        return -1;

    sets.append(ForceFieldSet(RKString::fromStdWString(wanted),
                              list[static_cast<size_t>(forkFrom)], true));
    Log(L"Added force field set " + wanted);
    return static_cast<int>(sets.forceFieldSets().size()) - 1;
}

int DocumentController::AddColorSet(std::wstring const& name, int forkFrom)
{
    if (!m_document)
        return -1;
    const std::wstring wanted = TrimName(name);
    if (wanted.empty())
        return -1;

    SKColorSets& sets = m_document->colorSets();
    std::vector<SKColorSet>& list = sets.colorSets();
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (NamesEqualIgnoreCase(list[i].displayName().toStdWString(), wanted))
            return static_cast<int>(i);
    }
    if (forkFrom < 0 || forkFrom >= static_cast<int>(list.size()))
        return -1;

    sets.append(SKColorSet(RKString::fromStdWString(wanted),
                           list[static_cast<size_t>(forkFrom)], true));
    Log(L"Added color set " + wanted);
    return static_cast<int>(sets.colorSets().size()) - 1;
}

// The force-field set of the Elements pane, provided it may be edited at all:
// the built-in sets are read-only, so the type operations below refuse them as
// Cocoa's validateMenuItem does.
ForceFieldSet* DocumentController::EditableForceFieldSet(int setIndex, int row)
{
    if (!m_document)
        return nullptr;
    std::vector<ForceFieldSet>& list = m_document->forceFieldSets().forceFieldSets();
    if (setIndex < 0 || setIndex >= static_cast<int>(list.size()))
        return nullptr;
    ForceFieldSet& set = list[static_cast<size_t>(setIndex)];
    if (!set.editable())
        return nullptr;
    if (row < 0 || row >= static_cast<int>(set.atomTypeList().size()))
        return nullptr;
    return &set;
}

bool DocumentController::InsertForceFieldType(int setIndex, int row)
{
    ForceFieldSet* set = EditableForceFieldSet(setIndex, row);
    if (!set)
        return false;

    ForceFieldType copy = set->atomTypeList()[static_cast<size_t>(row)];
    const RKString name = set->uniqueName(static_cast<int>(copy.atomicNumber()));
    copy.setForceFieldStringIdentifier(name);
    set->insert(row + 1, copy);
    m_document->colorSets().insert(name, static_cast<int>(copy.atomicNumber()));

    Log(L"Added force field type " + name.toStdWString());
    return true;
}

bool DocumentController::RemoveForceFieldType(int setIndex, int row)
{
    ForceFieldSet* set = EditableForceFieldSet(setIndex, row);
    if (!set)
        return false;

    const RKString name =
        set->atomTypeList()[static_cast<size_t>(row)].forceFieldStringIdentifier();
    if (ForceFieldSet::isDefaultForceFieldType(name))
    {
        Log(L"A default force field type cannot be removed");
        return false;
    }

    set->remove(static_cast<size_t>(row));
    // Its color is only stale once no set names the type any more, as the same
    // name may live on in a set that was forked before it was removed here.
    if (!m_document->forceFieldSets().contains(name))
        m_document->colorSets().remove(name);

    Log(L"Removed force field type " + name.toStdWString());
    return true;
}

bool DocumentController::RenameForceFieldType(int setIndex, int row,
                                              std::wstring const& name)
{
    ForceFieldSet* set = EditableForceFieldSet(setIndex, row);
    if (!set)
        return false;

    ForceFieldType& type = set->atomTypeList()[static_cast<size_t>(row)];
    const RKString oldName = type.forceFieldStringIdentifier();
    if (ForceFieldSet::isDefaultForceFieldType(oldName))
        return false;

    const RKString wanted = RKString::fromStdWString(name).trimmed();
    if (wanted.isEmpty() || wanted == oldName)
        return false;
    // A name has to stay unique within the set, and may not shadow a built-in
    // type: the structures resolve their colors and radii by it.
    if ((*set)[wanted] != nullptr || ForceFieldSet::isDefaultForceFieldType(wanted))
    {
        Log(L"Force field type " + wanted.toStdWString() + L" is already taken");
        return false;
    }

    m_document->colorSets().insert(wanted, static_cast<int>(type.atomicNumber()));
    type.setForceFieldStringIdentifier(wanted);
    if (!m_document->forceFieldSets().contains(oldName))
        m_document->colorSets().remove(oldName);

    Log(L"Renamed force field type " + oldName.toStdWString() + L" to " +
        wanted.toStdWString());
    return true;
}

void DocumentController::RefreshRows() const
{
    if (m_projectList)
        m_projectList->RefreshRows();
}

std::shared_ptr<ProjectTreeController> DocumentController::ProjectTree() const
{
    return m_document ? m_document->projectTreeController() : nullptr;
}

// Cocoa: ProjectNode owns a lazily created UndoManager, reachable through the
// iRASPAProject proxy of the selected project-tree row.
RKUndoStack* DocumentController::ProjectUndoStack()
{
    auto controller = ProjectTree();
    if (!controller)
        return nullptr;
    auto node = controller->selectedTreeNode();
    if (!node)
        return nullptr;
    auto iraspa = node->representedObject();
    if (!iraspa)
        return nullptr;
    return &iraspa->undoManager();
}

// Cocoa hands project.undoManager to every operation inside a project,
// independent of where the focus is.
RKUndoStack* DocumentController::ObjectUndoStack()
{
    if (auto stack = ProjectUndoStack())
        return stack;
    return &m_documentUndoStack;
}

void DocumentController::RegisterUndo(RKUndoStack* stack, std::wstring name, std::function<void()> action)
{
    if (!stack || !action)
        return;
    stack->registerUndo(std::move(name), std::move(action));
    if (m_host)
        m_host->RefreshEditMenuLabels();
}

bool DocumentController::IsSectionRoot(std::shared_ptr<ProjectTreeNode> const& node) const
{
    if (!node)
        return false;
    auto controller = ProjectTree();
    if (!controller)
        return false;
    for (auto const& root : controller->rootNodes())
    {
        if (root == node)
            return true;
    }
    return false;
}

bool DocumentController::IsUnderLocalProjects(std::shared_ptr<ProjectTreeNode> const& node) const
{
    if (!node)
        return false;
    auto controller = ProjectTree();
    if (!controller)
        return false;
    auto local = controller->localProjects();
    if (!local)
        return false;
    if (node == local)
        return true;
    return node->isDescendantOfNode(local.get());
}

bool DocumentController::CanEditProjectNode(std::shared_ptr<ProjectTreeNode> const& node) const
{
    if (!node || !node->isEditable())
        return false;
    auto controller = ProjectTree();
    if (!controller || !controller->localProjects())
        return false;
    if (node == controller->localProjects())
        return false;
    return IsUnderLocalProjects(node);
}

// Cocoa proxyProject.isEditable: gallery and cloud projects are read-only, so
// the scene and frame lists refuse to rename anything inside them.
bool DocumentController::IsSelectedProjectEditable() const
{
    auto controller = ProjectTree();
    if (!controller)
        return false;
    auto node = controller->selectedTreeNode();
    return node && node->isEditable();
}

void DocumentController::ResolveInsertParentAndIndex(std::shared_ptr<ProjectTreeNode>& outParent,
                                                     int& outIndex) const
{
    outParent = nullptr;
    outIndex = 0;
    auto controller = ProjectTree();
    if (!controller)
        return;

    auto local = controller->localProjects();
    outParent = local;
    outIndex = local ? static_cast<int>(local->childCount()) : 0;

    auto primary = controller->selectedTreeNode();
    if (!primary || !IsUnderLocalProjects(primary))
        return;

    if (NodeIsGroup(primary) && primary->isDropEnabled())
    {
        outParent = primary;
        outIndex = 0;
        return;
    }

    auto parent = primary->parent();
    if (!parent || !IsUnderLocalProjects(parent))
        return;

    auto idx = parent->findChildIndex(primary);
    outParent = parent;
    outIndex = idx ? static_cast<int>(*idx) + 1 : static_cast<int>(parent->childCount());
}

// Qt ProjectTreeViewModel::insertGalleryData and insertDatabaseCoReMOFData — graft a read
// database under one of the section roots via in-place rows.
void DocumentController::GraftDatabase(std::shared_ptr<DocumentData> database,
                                       std::shared_ptr<ProjectTreeNode> const& section,
                                       std::wstring const& label)
{
    if (!database || !m_document)
        return;

    auto controller = ProjectTree();
    auto sourceLocal = database->projectTreeController()
        ? database->projectTreeController()->localProjects()
        : nullptr;
    if (!controller || !section || !sourceLocal)
        return;

    // Copy then clear so the temporary DocumentData does not retain ownership.
    auto children = sourceLocal->childNodes();
    sourceLocal->childNodes().clear();

    int inserted = 0;
    for (auto const& child : children)
    {
        if (!child)
            continue;
        const int index = static_cast<int>(section->childCount());
        controller->insertNodeInParent(child, section, index);
        ++inserted;
    }

    if (m_projectList)
        m_projectList->ExpandSectionRoots();

    Log(label + L" loaded (" + std::to_wstring(inserted) + L" items)");
}

void DocumentController::InsertGalleryData(std::shared_ptr<DocumentData> database)
{
    auto controller = ProjectTree();
    GraftDatabase(database, controller ? controller->galleryProjects() : nullptr, L"Gallery");
}

// The shipped structure databases land under DATABASES PUBLIC, not the gallery, which is
// where Qt puts them too.
void DocumentController::InsertDatabaseData(std::shared_ptr<DocumentData> database,
                                            std::wstring const& label)
{
    auto controller = ProjectTree();
    GraftDatabase(database, controller ? controller->icloudProjects() : nullptr, label);
}

// Cocoa addNode(_:inItem:atIndex:): model insert + flat-row refresh + selection
// reload.
void DocumentController::AddProjectNode(std::shared_ptr<ProjectTreeNode> node,
                                        std::shared_ptr<ProjectTreeNode> parent,
                                        int index)
{
    if (!node)
        return;
    auto controller = ProjectTree();
    if (!controller)
        return;
    if (!parent)
        parent = controller->localProjects();
    if (!parent)
        return;
    if (index < 0 || index > static_cast<int>(parent->childCount()))
        index = static_cast<int>(parent->childCount());

    // Cocoa addNode registers removeNode as its undo (and vice versa), so the
    // redo entry falls out of the removal registering the insert again. The
    // insert and the selection it moves undo as one step.
    auto& stack = m_documentUndoStack;
    stack.beginUndoGrouping(L"Add Projects");

    controller->insertNodeInParent(node, parent, index);
    RegisterUndo(&stack, L"Add Projects", [this, node]() { RemoveProjectNode(node); });

    // Cocoa's newly inserted row becomes the selection, and that is what loads
    // the project, so the new project is the one being edited.
    try
    {
        SetProjectSelection(node, { node }, L"Add Projects");
    }
    catch (...)
    {
        Log(L"Project selection path error after insert");
    }

    stack.endUndoGrouping();

    // Reveal the new row: expand every ancestor, then refresh the rows (the
    // rebuild reloads the selection too).
    if (m_projectList)
        m_projectList->RevealNode(parent);
}

// Cocoa removeNode(_:fromItem:atIndex:): the counterpart of AddProjectNode.
void DocumentController::RemoveProjectNode(std::shared_ptr<ProjectTreeNode> const& node)
{
    if (!node)
        return;
    auto parent = node->parent();
    int index = 0;
    if (parent)
    {
        if (auto idx = parent->findChildIndex(node))
            index = static_cast<int>(*idx);
    }

    RegisterUndo(&m_documentUndoStack, L"Remove Projects",
                 [this, node, parent, index]() { AddProjectNode(node, parent, index); });

    // A removed project must not stay loaded in the renderer.
    if (auto iraspa = node->representedObject())
    {
        if (auto structure = std::dynamic_pointer_cast<ProjectStructure>(iraspa->project());
            structure && structure == m_project)
        {
            if (m_host)
                m_host->ClearRenderer();
        }
    }

    if (m_projectList)
    {
        m_projectList->ForgetNode(node);
        m_projectList->SuppressSelectionEvents(true);
    }
    node->removeFromParent();
    if (m_projectList)
        m_projectList->SuppressSelectionEvents(false);
    if (auto controller = ProjectTree())
        controller->clearSelection();
    RefreshRows();
}

void DocumentController::InsertProjectIntoDocument(std::shared_ptr<ProjectStructure> project,
                                                   std::wstring const& displayName)
{
    if (!project)
        return;
    if (!m_document)
        m_document = std::make_shared<DocumentData>();

    auto iraspa = std::make_shared<iRASPAProject>(project);
    auto node = std::make_shared<ProjectTreeNode>(
        RKString::fromStdWString(displayName), iraspa, true, false);

    std::shared_ptr<ProjectTreeNode> parent;
    int index = 0;
    ResolveInsertParentAndIndex(parent, index);
    AddProjectNode(node, parent, index);
}

// Cocoa removeNode(_:fromItem:atIndex:): row remove + model remove.
void DocumentController::DeleteSelectedProjects()
{
    auto controller = ProjectTree();
    if (!controller)
        return;

    auto editable = controller->selectionEditableNodesAndIndexPaths();
    std::vector<std::shared_ptr<ProjectTreeNode>> toRemove;
    for (auto const& pair : editable.second)
    {
        if (pair.first && CanEditProjectNode(pair.first))
            toRemove.push_back(pair.first);
    }
    if (editable.first.first && CanEditProjectNode(editable.first.first))
    {
        bool found = false;
        for (auto const& n : toRemove)
        {
            if (n == editable.first.first)
            {
                found = true;
                break;
            }
        }
        if (!found)
            toRemove.push_back(editable.first.first);
    }

    if (toRemove.empty())
    {
        Log(L"Nothing deletable in selection");
        return;
    }

    // Bottom-up so sibling indices stay valid while removing.
    std::sort(toRemove.begin(), toRemove.end(),
              [](auto const& a, auto const& b)
              {
                  return a->indexPath() > b->indexPath();
              });

    // Cocoa wraps the whole multi-row delete in one undo group.
    m_documentUndoStack.beginUndoGrouping(L"Remove Projects");
    for (auto const& node : toRemove)
        RemoveProjectNode(node);
    m_documentUndoStack.endUndoGrouping();
    if (m_host)
        m_host->RefreshEditMenuLabels();

    controller->clearSelection();
    RefreshRows();
    Log(L"Deleted " + std::to_wstring(toRemove.size()) + L" project(s)");
}

void DocumentController::AddProjectNodeFromContext(bool group,
                                                   std::shared_ptr<ProjectTreeNode> const& clicked)
{
    auto controller = ProjectTree();
    if (!controller)
        return;

    std::shared_ptr<ProjectTreeNode> parent = controller->localProjects();
    int index = 0;
    if (clicked && IsUnderLocalProjects(clicked))
    {
        if (NodeIsGroup(clicked))
        {
            parent = clicked;
            index = 0;
        }
        else if (auto par = clicked->parent())
        {
            parent = par;
            auto idx = par->findChildIndex(clicked);
            index = idx ? static_cast<int>(*idx) + 1 : static_cast<int>(par->childCount());
        }
    }

    std::shared_ptr<ProjectTreeNode> node;
    if (group)
    {
        auto g = std::make_shared<ProjectGroup>();
        node = std::make_shared<ProjectTreeNode>(RKString("New Group project"),
                                                 std::make_shared<iRASPAProject>(g), true, true);
    }
    else
    {
        auto structure = std::make_shared<ProjectStructure>();
        node = std::make_shared<ProjectTreeNode>(RKString("New Structure project"),
                                                 std::make_shared<iRASPAProject>(structure), true, false);
    }
    AddProjectNode(node, parent, index);
    Log(group ? L"Group project added" : L"Structure project added");
}

// The toolbar [+], which follows the selection rather than a clicked row, and
// names its rows as the File > New commands do.
void DocumentController::AddProjectFromToolbar(bool group)
{
    std::shared_ptr<ProjectTreeNode> node;
    if (group)
    {
        auto g = std::make_shared<ProjectGroup>();
        node = std::make_shared<ProjectTreeNode>(RKString("New Group"),
                                                 std::make_shared<iRASPAProject>(g), true, true);
    }
    else
    {
        auto structure = std::make_shared<ProjectStructure>();
        node = std::make_shared<ProjectTreeNode>(RKString("New Project"),
                                                 std::make_shared<iRASPAProject>(structure), true, false);
    }

    std::shared_ptr<ProjectTreeNode> parent;
    int index = 0;
    ResolveInsertParentAndIndex(parent, index);
    AddProjectNode(node, parent, index);
    Log(group ? L"Project group added" : L"Structure project added");
}

// Cocoa ProjectContextMenuComputePropertiesSelection: for every editable
// selected project, recompute density properties, the helium void fraction and
// the nitrogen surface area, then restyle and refresh.
void DocumentController::ComputePropertiesForProjectSelection()
{
    auto controller = ProjectTree();
    if (!controller)
        return;

    auto nodes = controller->selectedTreeNodes();
    if (auto primary = controller->selectedTreeNode())
        nodes.insert(primary);

    int computed = 0;
    bool touchedActive = false;
    for (auto const& node : nodes)
    {
        if (!node || !CanEditProjectNode(node))
            continue;
        auto iraspa = node->representedObject();
        if (!iraspa || iraspa->isGroup())
            continue;

        std::shared_ptr<ProjectStructure> project;
        try
        {
            iraspa->unwrapIfNeeded(nullptr);
            project = std::dynamic_pointer_cast<ProjectStructure>(iraspa->project());
        }
        catch (...)
        {
            project = nullptr;
        }
        if (!project || !project->sceneList())
        {
            Log(L"Project " + node->displayName().toStdWString() + L" not loaded");
            continue;
        }

        for (auto const& movieRow : project->sceneList()->allIRASPAStructures())
        {
            for (auto const& obj : movieRow)
            {
                if (!obj || !obj->object())
                    continue;
                auto ed = std::dynamic_pointer_cast<StructuralPropertyEditor>(obj->object());
                if (!ed)
                    continue;
                try
                {
                    ed->recomputeDensityProperties();
                    ed->setStructureHeliumVoidFraction(ed->computeVoidFractionAccelerated());
                    if (auto structure = std::dynamic_pointer_cast<Structure>(obj->object()))
                    {
                        structure->setStructureNitrogenSurfaceArea(
                            ed->computeNitrogenSurfaceAreaAccelerated());
                        structure->setRepresentationStyle(Structure::RepresentationStyle::fancy,
                                                          m_document->colorSets());
                    }
                    ed->recomputeDensityProperties();
                    ++computed;
                }
                catch (...)
                {
                    Log(L"Property computation failed for " +
                        node->displayName().toStdWString());
                }
            }
        }
        if (project == m_project)
            touchedActive = true;
    }

    Log(L"Computed properties for " + std::to_wstring(computed) + L" structure(s)");
    if (computed == 0)
        return;

    // Cocoa reloads the detail tab; the fancy style also changes the render.
    if (touchedActive && m_host)
    {
        m_host->ReloadRendererData();
        try
        {
            m_host->RefreshInspector();
        }
        catch (...)
        {
        }
    }
}

void DocumentController::ApplyPrimaryProjectIfNeeded(std::shared_ptr<ProjectTreeNode> const& primary)
{
    if (!m_host)
        return;

    // Nothing selected, or a group row: there is no structure to show.
    if (!primary)
    {
        m_host->ClearRenderer();
        return;
    }
    auto iraspa = primary->representedObject();
    if (!iraspa || iraspa->isGroup())
    {
        m_host->ClearRenderer();
        return;
    }

    std::shared_ptr<ProjectStructure> structure;
    try
    {
        iraspa->unwrapIfNeeded(nullptr);
        structure = std::dynamic_pointer_cast<ProjectStructure>(iraspa->project());
    }
    catch (...)
    {
        Log(L"Project unwrap error");
        return;
    }
    if (!structure)
    {
        m_host->ClearRenderer();
        return;
    }
    if (structure == m_project)
        return;

    const std::wstring name = primary->displayName().toStdWString();
    // Cocoa: the toolbar message panel shows "Loading (name)" while the project
    // switch is in flight.
    m_host->ShowMessage(L"\uE7C3", L"Loading (" + name + L")");
    m_host->Enqueue([this, structure, name]()
    {
        try
        {
            if (structure == m_project)
                return;
            m_host->LoadProjectIntoRenderer(structure, name);
        }
        catch (...)
        {
            Log(L"Project select error");
        }
    });
}

// The tree view is only re-synced while replaying an undo or redo; on the way in
// the view is the one that changed.
void DocumentController::SetProjectSelection(std::shared_ptr<ProjectTreeNode> const& primary,
                                             std::set<std::shared_ptr<ProjectTreeNode>> const& selection,
                                             std::wstring const& actionName)
{
    auto controller = ProjectTree();
    if (!controller)
        return;

    auto previousPrimary = controller->selectedTreeNode();
    auto previousSelection = controller->selectedTreeNodes();
    if (previousPrimary == primary && previousSelection == selection)
        return;

    auto& stack = m_documentUndoStack;
    const bool replaying = stack.isUndoing() || stack.isRedoing();
    RegisterUndo(&stack, actionName,
                 [this, previousPrimary, previousSelection, actionName]()
                 {
                     SetProjectSelection(previousPrimary, previousSelection, actionName);
                 });

    controller->clearSelection();
    if (primary)
        controller->setSelectionIndexPath(primary->indexPath());
    for (auto const& node : selection)
    {
        if (node)
            controller->insertSelectionIndexPath(node->indexPath());
    }

    if (replaying && m_projectList)
        m_projectList->ReloadRowSelection();
    // Also when the selection becomes empty: the renderer follows the selection,
    // so undoing back to "nothing selected" empties the render view too.
    if (primary != previousPrimary)
        ApplyPrimaryProjectIfNeeded(primary);
    if (m_host)
        m_host->RefreshEditMenuLabels();
}

void DocumentController::SetProjectDisplayName(std::shared_ptr<ProjectTreeNode> const& node,
                                               std::wstring const& name)
{
    if (!node)
        return;
    // An empty name would hide the row (and its children) from the flat rebuild,
    // so a blank edit is simply discarded.
    auto trimmed = RKString::fromStdWString(name).trimmed();
    if (trimmed.isEmpty())
        return;
    const std::wstring previous = node->displayName().toStdWString();
    if (previous == trimmed.toStdWString())
        return;

    RegisterUndo(&m_documentUndoStack, L"Change Project Name",
                 [this, node, previous]() { SetProjectDisplayName(node, previous); });

    node->setDisplayName(trimmed);
    RefreshRows();
}

std::shared_ptr<ProjectTreeNode> DocumentController::ApplyProjectDrop(
    std::shared_ptr<ProjectTreeNode> const& node,
    std::shared_ptr<ProjectTreeNode> const& targetNode,
    std::shared_ptr<ProjectTreeNode>& outParent)
{
    outParent = nullptr;
    if (!node)
        return nullptr;

    auto controller = ProjectTree();
    auto local = controller ? controller->localProjects() : nullptr;
    if (!controller || !local)
        return nullptr;

    // Drop on a group inserts as its first child, drop on a leaf inserts below
    // it, anywhere else appends to Local Projects.
    std::shared_ptr<ProjectTreeNode> newParent = local;
    int newIndex = static_cast<int>(local->childCount());
    if (targetNode && targetNode != node)
    {
        if (NodeIsGroup(targetNode) && targetNode->isDropEnabled())
        {
            newParent = targetNode;
            newIndex = 0;
        }
        else if (auto parent = targetNode->parent())
        {
            auto idx = parent->findChildIndex(targetNode);
            newParent = parent;
            newIndex = idx ? static_cast<int>(*idx) + 1
                           : static_cast<int>(parent->childCount());
        }
    }

    // Validation, per Cocoa validateDrop:
    if (!IsUnderLocalProjects(newParent))
        return nullptr;                           // only under LOCAL PROJECTS
    if (newParent->isDescendantOfNode(node.get()) || newParent == node)
        return nullptr;                           // no drop into own subtree
    if (!newParent->isDropEnabled() && newParent != local)
        return nullptr;

    auto oldParent = node->parent();
    const bool fromLocal = IsUnderLocalProjects(node) && CanEditProjectNode(node);

    std::shared_ptr<ProjectTreeNode> inserted;
    if (fromLocal)
    {
        // No-op drop back onto the same spot.
        if (newParent == oldParent && oldParent)
        {
            auto modelIdx = oldParent->findChildIndex(node);
            if (modelIdx)
            {
                int cur = static_cast<int>(*modelIdx);
                if (cur == newIndex || cur + 1 == newIndex)
                    return nullptr;
                // Removing first shifts later siblings left by one.
                if (newParent == oldParent && cur < newIndex)
                    --newIndex;
            }
        }
        // Cocoa moveNode: the undo is the move back to where it came from.
        ProjectNodePosition origin;
        origin.node = node;
        origin.parent = oldParent;
        if (oldParent)
        {
            if (auto idx = oldParent->findChildIndex(node))
                origin.index = static_cast<int>(*idx);
        }
        RegisterUndo(&m_documentUndoStack, L"Reorder Projects",
                     [this, origin]() { RestoreProjectNodePositions({ origin }); });

        node->removeFromParent();
        controller->insertNodeInParent(node, newParent, newIndex);
        inserted = node;
    }
    else
    {
        // Gallery/cloud source: Cocoa copies into local.
        auto copy = node->shallowClone();
        if (!copy)
            return nullptr;
        copy->setIsEditable(true);
        copy->setIsDropEnabled(NodeIsGroup(copy));
        controller->insertNodeInParent(copy, newParent, newIndex);
        inserted = copy;
        RegisterUndo(&m_documentUndoStack, L"Add Projects",
                     [this, copy]() { RemoveProjectNode(copy); });
    }

    // Select the moved/copied node.
    controller->clearSelection();
    try
    {
        controller->setSelectionIndexPath(inserted->indexPath());
        controller->insertSelectionIndexPath(inserted->indexPath());
    }
    catch (...)
    {
    }

    outParent = newParent;
    return inserted;
}

// Cocoa moveNode(_:toItem:childIndex:): moving a row registers the move back as
// its undo, so this one function serves as both directions.
void DocumentController::RestoreProjectNodePositions(std::vector<ProjectNodePosition> const& positions)
{
    if (positions.empty())
        return;
    auto controller = ProjectTree();
    if (!controller)
        return;

    std::vector<ProjectNodePosition> current;
    current.reserve(positions.size());
    for (auto const& target : positions)
    {
        if (!target.node)
            continue;
        ProjectNodePosition now;
        now.node = target.node;
        now.parent = target.node->parent();
        now.index = 0;
        if (now.parent)
        {
            if (auto idx = now.parent->findChildIndex(target.node))
                now.index = static_cast<int>(*idx);
        }
        current.push_back(now);
    }

    RegisterUndo(&m_documentUndoStack, L"Reorder Projects",
                 [this, current]() { RestoreProjectNodePositions(current); });

    for (auto const& target : positions)
    {
        if (!target.node || !target.parent)
            continue;
        target.node->removeFromParent();
        int index = target.index;
        if (index < 0 || index > static_cast<int>(target.parent->childCount()))
            index = static_cast<int>(target.parent->childCount());
        controller->insertNodeInParent(target.node, target.parent, index);
    }

    RefreshRows();
}
