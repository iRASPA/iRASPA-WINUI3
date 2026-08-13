#pragma once

#include "documentdata.h"
#include "movie.h"
#include "object.h"
#include "projectstructure.h"
#include "projecttreecontroller.h"
#include "projecttreenode.h"
#include "scene.h"
#include "scenelist.h"
#include "skcell.h"
#include "volumetricdataviewer.h"
#include <rkundostack.h>

#include <optional>
#include <tuple>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

class iRASPAObject;
class SKAsymmetricBond;
class SKAtomTreeController;
class SKAtomTreeNode;
class SKBondSetController;
class Structure;

// Number of asymmetric atoms of a frame, the count Cocoa's infoPanelString
// appends as "(N atoms)".
size_t CountFrameAtoms(std::shared_ptr<iRASPAObject> const& frame);

// The atom fields that can be edited in place; Cocoa has one setter per field
// (setAtomName, setAtomElement, setAtomPositionX, ...) so every write can
// register the previous value as its undo.
enum class AtomField
{
    name,
    element,        // value is the chemical symbol
    forceFieldType,
    occupancy,
    positionX,
    positionY,
    positionZ,
    charge,
    fixedX,
    fixedY,
    fixedZ
};
using AtomFieldValue = std::variant<double, bool, std::wstring>;

// The window services the document layer calls back into. The log pane, the
// renderer, the toolbar message panel, the Edit menu labels and the inspector
// belong to the window, not to the document, so they are reached through this
// interface instead of the document layer knowing what MainWindow is.
struct DocumentHost
{
    virtual ~DocumentHost() = default;

    virtual void Log(std::wstring const& message) = 0;
    virtual void ReloadRendererData() = 0;
    // Only the selection instance buffers are re-uploaded, which is all an atom
    // selection change needs (Cocoa reloadSelectionData).
    virtual void ReloadRendererSelection() = 0;
    // Throw away the baked ambient occlusion of every frame in a scene, which is
    // what a structure appearing or disappearing invalidates: each texture was
    // baked with the others standing around it (Cocoa
    // invalidateCachedAmbientOcclusionTexture).
    virtual void InvalidateSceneAmbientOcclusion(std::shared_ptr<Scene> const& scene) = 0;
    // Rebuild whichever inspector tab is showing, so its rows follow the model.
    virtual void RefreshInspector() = 0;
    virtual void RefreshEditMenuLabels() = 0;
    virtual void ShowMessage(std::wstring const& glyph, std::wstring const& message) = 0;
    virtual void LoadProjectIntoRenderer(std::shared_ptr<ProjectStructure> const& project,
                                         std::wstring const& displayName) = 0;
    virtual void ClearRenderer() = 0;
    // A cell edit changes the bounding box, so the camera is refitted to it
    // before the renderer reloads.
    virtual void ReloadAfterCellEdit() = 0;
    // Push the scene list's selected structures at the renderer, optionally
    // rebuilding the inspector once the frame is up.
    virtual void ApplySelectionToRenderer(bool refreshInspector) = 0;
    // Continue on the UI thread after the current message, so a project switch
    // paints its "Loading (name)" message before the load blocks the thread.
    virtual void Enqueue(std::function<void()> work) = 0;
    // Serializing is document work, but the save picker needs the window handle,
    // so the window writes the file.
    virtual void SaveTextFile(std::wstring const& text, std::wstring const& extension,
                              std::wstring const& typeName,
                              std::wstring const& suggestedName) = 0;
};

// What the document layer needs from whatever is displaying the project tree.
// Implemented by ProjectView; a null presenter simply means the rows are not up
// yet, which is the case while a document is being opened.
struct ProjectListPresenter
{
    virtual ~ProjectListPresenter() = default;

    // Cocoa reloadData / reloadSelection.
    virtual void RefreshRows() = 0;
    virtual void ReloadRowSelection() = 0;
    // Expand every ancestor of the node and refresh, so a newly inserted row
    // is on screen (Cocoa's scrollRowToVisible after an insert).
    virtual void RevealNode(std::shared_ptr<ProjectTreeNode> const& node) = 0;
    // Drop a removed node from the expansion set.
    virtual void ForgetNode(std::shared_ptr<ProjectTreeNode> const& node) = 0;
    virtual void ExpandSectionRoots() = 0;
    virtual void SuppressSelectionEvents(bool suppress) = 0;
};

// What the document layer needs from the scene/movie list. Implemented by
// SceneView.
struct SceneListPresenter
{
    virtual ~SceneListPresenter() = default;
    virtual void RefreshRows() = 0;
    virtual void ReloadRowSelection() = 0;
};

// What the document layer needs from the frame list. Implemented by FrameView.
// The frame rows follow the selected movie, so anything that changes which
// movie is selected refreshes these too.
struct FrameListPresenter
{
    virtual ~FrameListPresenter() = default;
    virtual void RefreshRows() = 0;
    virtual void ReloadRowSelection() = 0;
    // The frame at a row of the list as it currently stands, for the info panel
    // message; empty once the rows are behind the model.
    virtual std::shared_ptr<iRASPAObject> FrameAtRow(size_t row) const = 0;
};

// What the document layer needs from the Cell tab of the inspector: undoing a
// cell edit has to put the restored values back on screen. Implemented by
// whatever is showing that form.
struct CellPanePresenter
{
    virtual ~CellPanePresenter() = default;
    virtual void Reload() = 0;
};

// What the document layer needs from the Atoms tab of the inspector. Cocoa
// reloads the edited row and column after a write, which the rows have to be
// told about even when the write came from an undo rather than from their own
// editor. Implemented by whatever is showing the atom list.
struct AtomsPanePresenter
{
    virtual ~AtomsPanePresenter() = default;
    virtual void RefreshField(std::shared_ptr<SKAtomTreeNode> const& node, AtomField field) = 0;
    virtual void ReloadRowSelection() = 0;
    virtual void RefreshNetCharge() = 0;
};

// What the document layer needs from the Bonds tab. Recomputing the bonds
// replaces the asymmetric bonds the rows point at, so the rows have to be
// re-pointed at the regenerated ones (Cocoa rebuilds the table instead, which
// would lose the scroll position and the selection).
struct BondsPanePresenter
{
    virtual ~BondsPanePresenter() = default;
    virtual void RebindBonds() = 0;
};

// Cocoa iRASPADocument plus the document-level half of its window controller:
// owns the document, the project currently loaded in the renderer, and the undo
// stack that project-tree operations record on. Every mutation of the project
// tree happens here. Nothing in this class touches XAML, so the views can own
// their rows and gestures and depend only on this.
class DocumentController
{
public:
    // Where a project-tree node sat, so a move can be undone by moving it back.
    struct ProjectNodePosition
    {
        std::shared_ptr<ProjectTreeNode> node;
        std::shared_ptr<ProjectTreeNode> parent;
        int index{ 0 };
    };

    void SetHost(DocumentHost* host) { m_host = host; }
    void SetProjectList(ProjectListPresenter* list) { m_projectList = list; }
    void SetSceneList(SceneListPresenter* list) { m_sceneList = list; }
    void SetFrameList(FrameListPresenter* list) { m_frameList = list; }
    void SetCellPane(CellPanePresenter* pane) { m_cellPane = pane; }
    void SetAtomsPane(AtomsPanePresenter* pane) { m_atomsPane = pane; }
    void SetBondsPane(BondsPanePresenter* pane) { m_bondsPane = pane; }

    // The views log through here rather than knowing about the window. Which
    // stack Edit > Undo shows depends on the project selection, so the list
    // asks for a menu refresh when its selection changes.
    void Log(std::wstring const& message) const;
    void RefreshEditMenuLabels() const;
    // The appearance pane's edits are not undoable and each ends in a reload,
    // so it asks for one directly rather than going through an edit method.
    void ReloadRenderer() const;
    // Atom or ribbon visibility changes what occludes what, so the baked ambient
    // occlusion of the selected scene is thrown away before the reload.
    void ReloadRendererInvalidatingAmbientOcclusion() const;
    // ... and those of them that move the bounding box refit the camera to it
    // first, as a cell edit does.
    void RefitCameraToBoundingBox() const;
    // Cocoa StructureElementDetailViewController: an element edit changes the
    // force-field types and the color set that every structure reads its atom
    // colors and radii from, so they all re-read both before the renderer
    // reloads. Not undoable, as in Cocoa.
    void ReapplyForceFieldAndColors() const;

    // Cocoa addForceFieldSet: / addColorSet:, behind the editable combos of the
    // Elements tab. A name that is already one of the document's sets selects
    // it; anything else forks the set passed in under that name, editable, and
    // appends it. The predefined sets are read-only, so this fork is the only
    // way to get a set whose types can be edited. Returns the index to show, or
    // -1 if there is nothing to select. Not undoable, as in Cocoa.
    int AddForceFieldSet(std::wstring const& name, int forkFrom);
    int AddColorSet(std::wstring const& name, int forkFrom);

    // Cocoa addForceFieldType: / removeSelectedForceFieldTypes: and
    // changeUniqueForceFieldName:. A type is added by duplicating the one at
    // row under a name not yet taken, and every color set gains an entry for
    // that name so the new type has a color; removing or renaming drops the old
    // name from the color sets once no set refers to it any more. The built-in
    // types cannot be renamed or removed. Not undoable, as in Cocoa.
    bool InsertForceFieldType(int setIndex, int row);
    bool RemoveForceFieldType(int setIndex, int row);
    bool RenameForceFieldType(int setIndex, int row, std::wstring const& name);

    std::shared_ptr<DocumentData>& Document() { return m_document; }
    std::shared_ptr<DocumentData> const& Document() const { return m_document; }
    std::shared_ptr<ProjectStructure>& Project() { return m_project; }
    // Null whenever there is no document, which callers have to expect: the
    // window comes up before one is loaded.
    std::shared_ptr<ProjectTreeController> ProjectTree() const;

    // Undo stacks, as in Cocoa: one NSUndoManager per project, plus the
    // document's own for the project tree.
    RKUndoStack& DocumentUndoStack() { return m_documentUndoStack; }
    RKUndoStack* ProjectUndoStack();
    // Where operations inside a project record, wherever the focus is.
    RKUndoStack* ObjectUndoStack();
    void RegisterUndo(RKUndoStack* stack, std::wstring name, std::function<void()> action);

    // Row classification, shared by the list view and the operations below.
    bool IsSectionRoot(std::shared_ptr<ProjectTreeNode> const& node) const;
    bool IsUnderLocalProjects(std::shared_ptr<ProjectTreeNode> const& node) const;
    bool CanEditProjectNode(std::shared_ptr<ProjectTreeNode> const& node) const;
    // Cocoa proxyProject.isEditable: the scene, frame and detail panes only edit
    // a project that came from LOCAL PROJECTS, never a gallery copy.
    bool IsSelectedProjectEditable() const;

    // Insert / remove, each registering the other as its undo.
    void AddProjectNode(std::shared_ptr<ProjectTreeNode> node,
                        std::shared_ptr<ProjectTreeNode> parent,
                        int index);
    void RemoveProjectNode(std::shared_ptr<ProjectTreeNode> const& node);
    void InsertProjectIntoDocument(std::shared_ptr<ProjectStructure> project,
                                   std::wstring const& displayName);
    void DeleteSelectedProjects();
    // Cocoa addStructureProjectContextMenu / addProjectGroupContextMenu: insert
    // inside a clicked group, after a clicked leaf, or at the top of LOCAL
    // PROJECTS when nothing local was clicked. A null node means the toolbar
    // button rather than a row context menu.
    void AddProjectNodeFromContext(bool group, std::shared_ptr<ProjectTreeNode> const& clicked);
    // The toolbar [+] and the File > New commands, which follow the selection
    // rather than a clicked row.
    void AddProjectFromToolbar(bool group);
    // Cocoa acceptDrop: resolve the target row into a parent and index, validate
    // it as validateDrop does, then move (local source) or copy (gallery or
    // cloud source) and select the result. Returns the inserted node and the
    // parent it landed under, so the view can reveal it.
    std::shared_ptr<ProjectTreeNode> ApplyProjectDrop(
        std::shared_ptr<ProjectTreeNode> const& node,
        std::shared_ptr<ProjectTreeNode> const& targetNode,
        std::shared_ptr<ProjectTreeNode>& outParent);
    // Self-inverse: records where the nodes sit now, moves them to the recorded
    // positions and registers the recording as its own inverse (Cocoa moveNode).
    void RestoreProjectNodePositions(std::vector<ProjectNodePosition> const& positions);

    // Cocoa setProjectDisplayName(_:to:): the rename registers the old name as
    // its undo, which makes undo and redo the same call with the other name.
    void SetProjectDisplayName(std::shared_ptr<ProjectTreeNode> const& node,
                              std::wstring const& name);
    // Cocoa setCurrentSelection(treeController:newValue:oldValue:): the project
    // selection belongs to the document, so picking another project is undoable.
    void SetProjectSelection(std::shared_ptr<ProjectTreeNode> const& primary,
                             std::set<std::shared_ptr<ProjectTreeNode>> const& selection,
                             std::wstring const& actionName);
    // Cocoa ProjectContextMenuComputePropertiesSelection.
    void ComputePropertiesForProjectSelection();
    // Qt ProjectTreeViewModel::insertGalleryData: graft a read database under
    // the GALLERY section root.
    void InsertGalleryData(std::shared_ptr<DocumentData> database);
    // Qt ProjectTreeViewModel::insertDatabaseCoReMOFData and friends: the shipped
    // structure databases graft under DATABASES PUBLIC instead. The label only names
    // the database in the log.
    void InsertDatabaseData(std::shared_ptr<DocumentData> database,
                            std::wstring const& label);
    // Load the selected row's project into the renderer, unless it already is.
    void ApplyPrimaryProjectIfNeeded(std::shared_ptr<ProjectTreeNode> const& primary);

    // Cocoa insert rules: group selected => first child; leaf selected =>
    // sibling below; otherwise append to LOCAL PROJECTS.
    void ResolveInsertParentAndIndex(std::shared_ptr<ProjectTreeNode>& outParent, int& outIndex) const;

    // ---- Scene / movie / frame -------------------------------------------
    // The scene list of the loaded project, or null when none is loaded.
    std::shared_ptr<SceneList> SceneListOf() const;
    std::shared_ptr<Scene> SelectedScene() const;
    std::shared_ptr<Movie> SelectedMovie() const;

    // Cocoa hands the inspector the selection of whichever left-pane list is
    // showing (MovieListViewController / FrameListViewController, each in its
    // setDetailViewController): the scene list stands for whole movies, the frame
    // list for single frames of one movie.
    enum class InspectorSource
    {
        Movies,
        Frames
    };
    void SetInspectorSource(InspectorSource source);

    // What Appearance uses to show/hide Primitive and Ribbon groups. Driven by
    // the left pane (Project / Scene / Frame), not by movie/frame selection —
    // selecting a movie while the Project pane is open must not narrow the set.
    enum class AppearanceSectionScope
    {
        Project,
        Movies,
        Frames
    };
    void SetAppearanceSectionScope(AppearanceSectionScope scope);

    // Cocoa's inspector applies an edit to the whole selection: the frames of the
    // selected movies, the selected frames while the frame list is showing, or
    // every structure of the project when nothing is selected. This is what each
    // detail pane edits through.
    std::vector<std::shared_ptr<iRASPAObject>> TargetStructures() const;
    // Structures that decide whether Appearance outline groups (Primitive, Ribbon)
    // are shown: every structure in the project (Project pane), the selected
    // movies only (Scene pane), or the selected frames (Frame pane).
    std::vector<std::shared_ptr<iRASPAObject>> AppearanceSectionStructures() const;
    // A detail pane fills its fields from the first of them and writes every
    // edit to all of them. Panes that edit a narrower interface than Object
    // (InfoEditor, ...) dynamic_cast inside their own callback.
    std::shared_ptr<Object> FirstSelectedObject() const;
    void ForEachSelectedObject(std::function<void(Object&)> const& fn) const;

    // Filling a field is the other way round: Cocoa's inspector getters yield a
    // value only while every structure that has the property agrees on it, and
    // nothing once two of them differ, which the panes show as "Multiple Values"
    // rather than one structure's value. Structures not implementing the
    // interface take no part, as Cocoa's getters compactMap over the selection.
    template <class Interface, class Read>
    auto AgreedValue(Read const& read) const
        -> std::optional<std::decay_t<decltype(read(std::shared_ptr<Interface>{}))>>
    {
        using Value = std::decay_t<decltype(read(std::shared_ptr<Interface>{}))>;
        return AgreedPartialValue<Interface>([&read](std::shared_ptr<Interface> const& typed)
                                             { return std::optional<Value>(read(typed)); });
    }

    // Some rows concern only part of the selection — the structures that have a
    // cell, say. A read yielding nothing leaves that structure out of the answer
    // instead of making it a value of its own.
    template <class Interface, class Read>
    auto AgreedPartialValue(Read const& read) const
        -> std::decay_t<decltype(read(std::shared_ptr<Interface>{}))>
    {
        std::decay_t<decltype(read(std::shared_ptr<Interface>{}))> agreed;
        bool mixed = false;
        for (auto const& target : TargetStructures())
        {
            auto typed = std::dynamic_pointer_cast<Interface>(target ? target->object() : nullptr);
            if (!typed)
                continue;
            auto value = read(typed);
            if (!value)
                continue;
            if (!agreed)
                agreed = std::move(value);
            else if (!(*agreed == *value))
                mixed = true;
        }
        if (mixed)
            return {};
        return agreed;
    }

    // The scene rows and the frame rows both follow the model; a scene change
    // always refreshes the frames, because they are the selected movie's.
    void RefreshSceneAndFrameRows();
    void RefreshFrameRows();

    // Cocoa setSceneDisplayName / setMovieDisplayName / setFrameDisplayName:
    // each registers the old name as its undo against the project's own stack,
    // so undo and redo are the same call with the other name.
    void SetSceneDisplayName(std::shared_ptr<Scene> const& scene, std::wstring const& name);
    void SetMovieDisplayName(std::shared_ptr<Movie> const& movie, std::wstring const& name);
    void SetFrameDisplayName(std::shared_ptr<iRASPAObject> const& frame, std::wstring const& name);
    // Cocoa toggleMovieVisibility, from the checkbox on a movie row. The scene comes
    // along because what a movie occludes is the rest of its scene, whose baked
    // ambient occlusion the change invalidates.
    void SetMovieVisibility(std::shared_ptr<Scene> const& scene,
                            std::shared_ptr<Movie> const& movie, bool visible);

    void SelectSceneMovie(std::shared_ptr<Scene> const& scene,
                          std::shared_ptr<Movie> const& movie,
                          bool refreshRenderer);
    // The movie list allows a selection spanning scenes, which Cocoa keeps as a
    // per-scene set plus one primary movie. The rows are handed over as they are
    // ordered in the list; which of them becomes the primary follows Cocoa: the
    // one that already was, as long as it is still in the selection.
    void SelectSceneMovies(
        std::vector<std::pair<std::shared_ptr<Scene>, std::shared_ptr<Movie>>> const& picks);
    // Cocoa setCurrentSelection: replacing the frame selection registers the
    // previous selection as its undo, and the primary frame is broadcast to the
    // other movies (synchronizeAllMovieFrames).
    void SetFrameSelection(std::weak_ptr<Movie> movieRef,
                           size_t primaryIndex,
                           FrameSelectionIndexSet const& selection,
                           std::wstring const& actionName);

    // Cocoa MovieListViewController.addCrystal / addMolecule / ...: wrap a new
    // object of the chosen type in a movie, inserted after the selected movie
    // (a scene is created first when the list is still empty).
    void AddSceneMovie(ObjectType type);
    void RemoveSelectedSceneMovie();
    // Cocoa addMovieNode / removeMovieNode: each registers the other as its undo.
    void InsertSceneMovie(std::shared_ptr<Scene> const& scene,
                          std::shared_ptr<Movie> const& movie,
                          int index,
                          std::wstring const& actionName);
    void RemoveSceneMovie(std::shared_ptr<Movie> const& movie, std::wstring const& actionName);

    void AddMovieFrame(ObjectType type);
    // The frame list's [+] with no explicit type keeps the movie's own.
    void AddFrameOfMovieType();
    void InsertMovieFrame(std::shared_ptr<Movie> const& movie,
                          std::shared_ptr<iRASPAObject> const& frame,
                          size_t index,
                          std::wstring const& actionName);
    void RemoveMovieFrame(std::shared_ptr<Movie> const& movie,
                          size_t index,
                          std::wstring const& actionName);
    // Cocoa deleteSelection / deleteSelectedFrames / insertSelectedFrames.
    void DeleteSelectedFrames();
    void RemoveMovieFrames(std::shared_ptr<Movie> const& movie,
                           FrameSelectionIndexSet const& indices,
                           std::wstring const& actionName);
    void InsertMovieFrames(std::shared_ptr<Movie> const& movie,
                           std::vector<std::pair<size_t, std::shared_ptr<iRASPAObject>>> const& positions,
                           FrameSelectionIndexSet const& selection,
                           size_t primaryIndex,
                           std::wstring const& actionName);
    // Cocoa moveFrame(fromIndex:toIndex:): the whole drop is one undo step, and
    // undoing it restores the order this call started from.
    void ReorderMovieFrames(std::shared_ptr<Movie> const& movie,
                            std::vector<std::shared_ptr<iRASPAObject>> const& order,
                            std::wstring const& actionName);

    // ---- Cell -------------------------------------------------------------
    // Cocoa StructureCellDetailViewController undo: everything a cell-view edit
    // can touch, captured per object so the inverse can put it back.
    struct CellStructuralUndoState
    {
        RKString materialType;
        ProbeMolecule probeMolecule = ProbeMolecule::helium;
        double mass = 0.0;
        double density = 0.0;
        double heliumVoidFraction = 0.0;
        double specificVolume = 0.0;
        double accessiblePoreVolume = 0.0;
        double volumetricSurfaceArea = 0.0;
        double gravimetricSurfaceArea = 0.0;
        int numberOfChannelSystems = 0;
        int numberOfInaccessiblePockets = 0;
        int dimensionalityOfPoreSystem = 0;
        double largestCavityDiameter = 0.0;
        double restrictingPoreLimitingDiameter = 0.0;
        double largestCavityDiameterAlongAViablePath = 0.0;
    };
    struct CellUndoState
    {
        std::shared_ptr<Object> object;
        std::shared_ptr<SKCell> cell;
        simd_quatd orientation{ 1.0, double3(0.0, 0.0, 0.0) };
        double rotationDelta = 5.0;
        double3 origin{ 0.0, 0.0, 0.0 };
        std::optional<int> hallNumber;
        std::optional<CellStructuralUndoState> structural;
    };
    // Every field the cell view can edit lives on the object or on its SKCell,
    // so one snapshot covers them all.
    CellUndoState SnapshotCellState(std::shared_ptr<Object> const& object) const;
    std::vector<CellUndoState> SnapshotCellStates() const;
    void RegisterCellUndo(std::wstring const& actionName, std::vector<CellUndoState> const& before);
    // Restoring is its own inverse, so undo and redo both run through here.
    void RestoreCellStates(std::wstring const& actionName, std::vector<CellUndoState> const& states);

    // Qt CellTreeWidgetChangeStructureCommand: convert the selected frames'
    // objects to another structure type (Crystal, Molecule, ...).
    void ChangeStructureType(ObjectType type);
    // Swapping the objects back is the inverse, so undo and redo run through
    // here as well.
    void ApplyStructureTypes(std::vector<std::tuple<std::shared_ptr<iRASPAObject>,
                                                    std::shared_ptr<Object>,
                                                    ObjectType>> const& states,
                             std::wstring const& actionName = L"Change Material Type");
    // Cocoa "Apply" in the transform-content box.
    void ApplyCellContentShift();

    // What an edit has to put back on screen afterwards. Changing the box or the
    // orientation moves the bounding box, so the camera is refitted to it;
    // flipping or shifting the content only redraws.
    enum class CellReload
    {
        None,
        Renderer,
        CameraAndRenderer,
    };
    // Applies one cell-view edit to every object in the selection as a single
    // undo step. The callback does the mutating (and its own recompute, where
    // Cocoa does one), and narrows to the interface it needs.
    void EditCells(std::wstring const& actionName,
                   std::function<void(Object&)> const& fn,
                   CellReload reload);
    // The same without an undo entry, for the values a slider emits while it is
    // being dragged: one entry covers the whole gesture instead.
    void EditCellsWithoutUndo(std::function<void(Object&)> const& fn, CellReload reload);
    // Cocoa setSpaceGroup: re-imposing symmetry regenerates the copies and the
    // bonds, so it does not go through EditCells.
    void SetSpaceGroupHallNumber(int hall);
    // The two recompute buttons in the structural group.
    void ComputeHeliumVoidFraction();
    void ComputeNitrogenSurfaceArea();

    // ---- Atoms ------------------------------------------------------------
    // Where an atom node sat, so a move or a delete can be undone by putting it
    // back there (Cocoa's addNode:inItem:atIndex: / removeNode:fromItem:atIndex:).
    struct AtomNodePosition
    {
        std::shared_ptr<SKAtomTreeNode> node;
        std::shared_ptr<SKAtomTreeNode> parent; // null = root node
        int index{ 0 };
    };

    // The frame the Atoms tab edits: the first of the selection that has an atom
    // tree. Null when none of them has one, which the pane shows as a hint.
    // Every atom operation takes the frame it was started on as a weak_ptr, so
    // its undo still applies to that frame after the selection has moved on.
    std::shared_ptr<iRASPAObject> AtomsFrame() const;
    // The atom tree of a frame, or null when it has none. The pane reads its
    // rows, its selection and its net charge from it; every mutation of it goes
    // through the methods below.
    std::shared_ptr<SKAtomTreeController> AtomTree(std::weak_ptr<iRASPAObject> frameRef) const;

    // Cocoa setAtomName / setAtomElement / setAtomOccupancy / setAtomPositionX /
    // ... / setAtomFixed: write one field, register the write of the previous
    // value as the undo (so the undo of an undo becomes the redo), then refresh
    // whatever that field affects. The row editors and the undo closures both
    // come through here.
    void SetAtomField(std::shared_ptr<SKAtomTreeNode> const& node, AtomField field,
                      AtomFieldValue const& value);
    // A moved atom regenerates the symmetry copies and the bonds before the
    // renderer reloads, as Cocoa does after a position edit.
    void AfterAtomGeometryEdit(std::weak_ptr<iRASPAObject> frameRef);
    // Cocoa setCurrentSelection: the atom selection belongs to the model, so
    // replacing it registers the previous selection as its undo and Ctrl+Z walks
    // back through selections too.
    void SetAtomSelection(std::weak_ptr<iRASPAObject> frameRef,
                          std::set<std::shared_ptr<SKAtomTreeNode>> const& selection,
                          std::wstring const& actionName);
    // A 3D click-pick or rubber-band drag changed the model selection; the rows
    // follow it.
    void ReloadAtomRowSelection() const;

    // Cocoa addAtom / addAtomGroup: the new node lands at the insertion point
    // the target row implies (first child of a group, below an atom, otherwise
    // appended to the root nodes), which is where a dropped row would land too.
    void AddAtomNode(std::weak_ptr<iRASPAObject> frameRef,
                     std::shared_ptr<SKAtomTreeNode> const& target,
                     bool group);
    void InsertAtomNodes(std::weak_ptr<iRASPAObject> frameRef,
                         std::vector<AtomNodePosition> const& positions,
                         std::wstring const& actionName);
    void RemoveAtomNodes(std::weak_ptr<iRASPAObject> frameRef,
                         std::vector<std::shared_ptr<SKAtomTreeNode>> const& nodes,
                         std::wstring const& actionName);
    // Self-inverse reorder: records where the nodes sit now, moves them to the
    // recorded positions and registers the recording as its own inverse (Cocoa
    // moveNodes).
    void RestoreAtomNodePositions(std::weak_ptr<iRASPAObject> frameRef,
                                  std::vector<AtomNodePosition> const& positions);
    // Cocoa acceptDrop: a drop on a group inserts as its first child, a drop on
    // an atom right below it, a drop on nothing appends at the top level. The
    // moved nodes stay selected. False when the drop was rejected, which is the
    // case for a node dropped into its own subtree.
    bool MoveAtomNodes(std::weak_ptr<iRASPAObject> frameRef,
                       std::vector<std::shared_ptr<SKAtomTreeNode>> const& nodes,
                       std::shared_ptr<SKAtomTreeNode> const& target);

    // Cocoa Selection > Invert / Copy to new Movie / Move to new Movie.
    void InvertAtomSelection(std::weak_ptr<iRASPAObject> frameRef);
    void AtomSelectionToNewMovie(std::weak_ptr<iRASPAObject> frameRef, bool move);
    // Cocoa Visibility > Match selection / Invert.
    void SetAtomVisibilityFromSelection(std::weak_ptr<iRASPAObject> frameRef, bool matchSelection);

    // Cocoa's four whole-structure commands, each of which hands back a
    // replacement structure the frame's object is swapped for.
    enum class AtomStructureOperation
    {
        FlattenHierarchy,
        SuperCell,
        RemoveSymmetry,
        WrapAtomsToCell,
    };
    void RunAtomStructureOperation(std::weak_ptr<iRASPAObject> frameRef,
                                   AtomStructureOperation operation);
    // Cocoa FindAndImposeSymmetry / FindPrimitive / FindNiggli: run the
    // spglib-style search on the atom symmetry data and rebuild the structure
    // from the result.
    enum class AtomSymmetrySearch
    {
        Impose,
        Primitive,
        Niggli,
    };
    void FindAtomSymmetry(std::weak_ptr<iRASPAObject> frameRef, AtomSymmetrySearch search);

    // Cocoa Export As > PDB / mmCIF / CIF / XYZ / VASP POSCAR, written with the
    // SymmetryKit writers.
    enum class AtomExportFormat
    {
        PDB,
        mmCIF,
        CIF,
        XYZ,
        POSCAR,
    };
    void ExportAtoms(std::weak_ptr<iRASPAObject> frameRef, AtomExportFormat format) const;

    // ---- Bonds ------------------------------------------------------------
    // The frame the Bonds tab edits: the first of the selection that has a bond
    // set. Null when none of them has one, which the pane shows as a hint. As
    // with the atom operations, every bond operation takes the frame it was
    // started on, so it still lands there after the selection has moved on.
    std::shared_ptr<iRASPAObject> BondsFrame() const;
    std::shared_ptr<SKBondSetController> BondSet(std::weak_ptr<iRASPAObject> frameRef) const;

    // The length the row shows: measured across the periodic image the bond was
    // drawn through, so it matches what the 3D view draws.
    double BondLength(std::weak_ptr<iRASPAObject> frameRef,
                      std::shared_ptr<SKAsymmetricBond> const& bond) const;
    // Cocoa changedBondLength: move both atoms along the bond axis (the
    // periodic-boundary case included), then regenerate the symmetry copies and
    // the bond topology. That replaces the asymmetric bonds, so the pane is
    // asked to re-point its rows rather than being rebuilt.
    void SetBondLength(std::weak_ptr<iRASPAObject> frameRef,
                       std::shared_ptr<SKAsymmetricBond> const& bond, double length);
    // The footer's Recompute Bonds button (Cocoa's context-menu action).
    void RecomputeBonds(std::weak_ptr<iRASPAObject> frameRef);
    // Cocoa tableViewSelectionDidChange: the bond selection belongs to the bond
    // set, and the 3D view draws it.
    void SetBondSelection(std::weak_ptr<iRASPAObject> frameRef,
                          std::set<int64_t> const& rows) const;

private:
    // Shared by InsertGalleryData and InsertDatabaseData: moves the read document's
    // top-level nodes under the given section root.
    void GraftDatabase(std::shared_ptr<DocumentData> database,
                       std::shared_ptr<ProjectTreeNode> const& section,
                       std::wstring const& label);

    // The set the Elements pane shows, provided both it and the row may be
    // edited: the built-in sets are read-only.
    ForceFieldSet* EditableForceFieldSet(int setIndex, int row);

    struct AtomExport
    {
        std::wstring text;
        std::wstring extension;
        std::wstring typeName;
        std::wstring suggestedName;
    };
    std::optional<AtomExport> WriteAtoms(std::weak_ptr<iRASPAObject> frameRef,
                                        AtomExportFormat format) const;

    // Shared tail of Cocoa's setStructureState: swap the frame's object for the
    // structure an operation produced, restyle it and reload everything. Putting
    // the previous object back is the inverse.
    void ApplyReplacedAtomStructure(std::weak_ptr<iRASPAObject> frameRef,
                                    std::shared_ptr<Structure> const& newStructure,
                                    std::wstring const& actionName);

    // Cocoa Movie.infoPanelString / iRASPAObject.infoPanelString: "name (N
    // atoms)", or the min/max range across a multi-frame movie.
    void ShowMovieInfo(std::shared_ptr<Movie> const& movie) const;
    void ShowFrameInfo(size_t row) const;

    // The presenter is set right after construction, but the document layer runs
    // before the rows are up (while a document is opening) and during teardown.
    void RefreshRows() const;

    DocumentHost* m_host{ nullptr };
    ProjectListPresenter* m_projectList{ nullptr };
    SceneListPresenter* m_sceneList{ nullptr };
    FrameListPresenter* m_frameList{ nullptr };
    CellPanePresenter* m_cellPane{ nullptr };
    AtomsPanePresenter* m_atomsPane{ nullptr };
    BondsPanePresenter* m_bondsPane{ nullptr };

    void ReloadAfterCellEdit(CellReload reload);

    // Which left-pane list the inspector reads its selection from; the window
    // sets it as the panes are switched.
    InspectorSource m_inspectorSource{ InspectorSource::Movies };
    AppearanceSectionScope m_appearanceSectionScope{ AppearanceSectionScope::Project };

    std::shared_ptr<DocumentData> m_document;
    std::shared_ptr<ProjectStructure> m_project;
    // Project-tree operations undo here (Cocoa's document undo manager);
    // everything inside a project uses that project's own stack.
    RKUndoStack m_documentUndoStack;
};
