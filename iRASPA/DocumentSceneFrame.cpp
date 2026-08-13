#include "pch.h"
#include "DocumentController.h"

#include "atomviewer.h"
#include "iraspaobject.h"
#include "skasymmetricatom.h"
#include "skatomtreecontroller.h"
#include "skatomtreenode.h"
#include "structure.h"

#include <algorithm>
#include <optional>
#include <set>

// Document-side scene, movie and frame operations (Cocoa MovieListViewController
// and FrameListViewController): every mutation records its inverse on the
// project's own undo stack, and asks the two lists to catch up afterwards.

size_t CountFrameAtoms(std::shared_ptr<iRASPAObject> const& frame)
{
    if (!frame)
        return 0;
    if (auto viewer = std::dynamic_pointer_cast<AtomViewer>(frame->object()))
    {
        if (auto controller = viewer->atomsTreeController())
            return controller->flattenedLeafNodes().size();
    }
    return 0;
}

namespace
{
    // Cocoa's add-menu titles, reused as the display name of the new movie and
    // of its object.
    wchar_t const* ObjectTypeName(ObjectType type)
    {
        switch (type)
        {
        case ObjectType::crystal: return L"Crystal";
        case ObjectType::molecularCrystal: return L"Molecular crystal";
        case ObjectType::molecule: return L"Molecule";
        case ObjectType::protein: return L"Protein";
        case ObjectType::proteinCrystal: return L"Protein crystal";
        case ObjectType::crystalEllipsoidPrimitive: return L"Crystal ellipsoid";
        case ObjectType::crystalCylinderPrimitive: return L"Crystal cylinder";
        case ObjectType::crystalPolygonalPrismPrimitive: return L"Crystal polygonal prism";
        case ObjectType::ellipsoidPrimitive: return L"Ellipsoid";
        case ObjectType::cylinderPrimitive: return L"Cylinder";
        case ObjectType::polygonalPrismPrimitive: return L"Polygonal prism";
        default: return L"Object";
        }
    }

    // Cocoa creates the object bare; the add-crystal button here has always
    // seeded a single atom so the new movie/frame renders something right away
    // (for a primitive that atom is its first instance).
    std::shared_ptr<iRASPAObject> CreateStyledObject(ObjectType type,
                                                     std::shared_ptr<DocumentData> const& document)
    {
        auto iraspa = iRASPAObject::create(type);
        if (!iraspa || !iraspa->object())
            return nullptr;
        iraspa->object()->setDisplayName(RKString::fromStdWString(ObjectTypeName(type)));
        if (auto structure = std::dynamic_pointer_cast<Structure>(iraspa->object()))
        {
            if (auto controller = structure->atomsTreeController())
            {
                controller->appendToRootnodes(
                    std::make_shared<SKAtomTreeNode>(std::make_shared<SKAsymmetricAtom>()));
                controller->setTags();
            }
            structure->expandSymmetry();
            if (document)
            {
                structure->setRepresentationStyle(Structure::RepresentationStyle::defaultStyle,
                                                  document->colorSets());
                structure->setAtomForceFieldIdentifier("Default", document->forceFieldSets());
            }
            structure->reComputeBoundingBox();
        }
        return iraspa;
    }
}

std::shared_ptr<SceneList> DocumentController::SceneListOf() const
{
    return m_project ? m_project->sceneList() : nullptr;
}

std::shared_ptr<Scene> DocumentController::SelectedScene() const
{
    auto sceneList = SceneListOf();
    return sceneList ? sceneList->selectedScene() : nullptr;
}

std::shared_ptr<Movie> DocumentController::SelectedMovie() const
{
    auto scene = SelectedScene();
    return scene ? scene->selectedMovie() : nullptr;
}

void DocumentController::SetInspectorSource(InspectorSource source)
{
    if (m_inspectorSource == source)
        return;
    m_inspectorSource = source;
    // Appearance Primitive/Ribbon visibility follows the left pane via
    // SetAppearanceSectionScope; this only switches TargetStructures.
    if (m_host)
        m_host->RefreshInspector();
}

void DocumentController::SetAppearanceSectionScope(AppearanceSectionScope scope)
{
    if (m_appearanceSectionScope == scope)
        return;
    m_appearanceSectionScope = scope;
    if (m_host)
        m_host->RefreshInspector();
}

std::vector<std::shared_ptr<iRASPAObject>> DocumentController::TargetStructures() const
{
    auto sceneList = SceneListOf();
    if (!sceneList)
        return {};

    // The frame list narrows the inspector to the frames picked in it, so an
    // edit made while it is showing reaches those frames and not the whole movie.
    if (m_inspectorSource == InspectorSource::Frames)
    {
        if (auto movie = SelectedMovie())
        {
            auto frames = movie->selectedFrames();
            if (!frames.empty())
                return frames;
        }
    }

    auto selected = sceneList->selectedMoviesIRASPAStructures();
    if (!selected.empty())
        return selected;

    return sceneList->flattenedAllIRASPAStructures();
}

std::vector<std::shared_ptr<iRASPAObject>> DocumentController::AppearanceSectionStructures() const
{
    auto sceneList = SceneListOf();
    if (!sceneList)
        return {};

    // Frame list: selected frames only.
    if (m_appearanceSectionScope == AppearanceSectionScope::Frames
        || m_inspectorSource == InspectorSource::Frames)
    {
        if (auto movie = SelectedMovie())
            return movie->selectedFrames();
        return {};
    }

    // Scene view: selected movies only — a non-primitive movie must not keep
    // Primitive Properties visible because a sibling movie in the scene is one.
    if (m_appearanceSectionScope == AppearanceSectionScope::Movies)
        return sceneList->selectedMoviesIRASPAStructures();

    // Project-tree selection: every structure in the project.
    return sceneList->flattenedAllIRASPAStructures();
}

std::shared_ptr<Object> DocumentController::FirstSelectedObject() const
{
    for (auto const& target : TargetStructures())
    {
        if (target && target->object())
            return target->object();
    }
    return nullptr;
}

void DocumentController::ForEachSelectedObject(std::function<void(Object&)> const& fn) const
{
    if (!fn)
        return;
    for (auto const& target : TargetStructures())
    {
        if (target && target->object())
            fn(*target->object());
    }
}

void DocumentController::RefreshSceneAndFrameRows()
{
    if (m_sceneList)
        m_sceneList->RefreshRows();
    RefreshFrameRows();
}

void DocumentController::RefreshFrameRows()
{
    if (m_frameList)
        m_frameList->RefreshRows();
}

void DocumentController::SetSceneDisplayName(std::shared_ptr<Scene> const& scene,
                                             std::wstring const& name)
{
    if (!scene)
        return;
    auto trimmed = RKString::fromStdWString(name).trimmed();
    if (trimmed.isEmpty())
        return;
    const std::wstring previous = scene->displayName().toStdWString();
    if (previous == trimmed.toStdWString())
        return;

    RegisterUndo(ObjectUndoStack(), L"Change Scene Name",
                 [this, scene, previous]() { SetSceneDisplayName(scene, previous); });

    scene->setDisplayName(trimmed);
    RefreshSceneAndFrameRows();
}

void DocumentController::SetMovieDisplayName(std::shared_ptr<Movie> const& movie,
                                             std::wstring const& name)
{
    if (!movie)
        return;
    auto trimmed = RKString::fromStdWString(name).trimmed();
    if (trimmed.isEmpty())
        return;
    const std::wstring previous = movie->displayName().toStdWString();
    if (previous == trimmed.toStdWString())
        return;

    RegisterUndo(ObjectUndoStack(), L"Change Movie Name",
                 [this, movie, previous]() { SetMovieDisplayName(movie, previous); });

    movie->setDisplayName(trimmed);
    RefreshSceneAndFrameRows();
}

void DocumentController::SetFrameDisplayName(std::shared_ptr<iRASPAObject> const& frame,
                                             std::wstring const& name)
{
    if (!frame || !frame->object())
        return;
    auto trimmed = RKString::fromStdWString(name).trimmed();
    if (trimmed.isEmpty())
        return;
    const std::wstring previous = frame->object()->displayName().toStdWString();
    if (previous == trimmed.toStdWString())
        return;

    RegisterUndo(ObjectUndoStack(), L"Change Frame Name",
                 [this, frame, previous]() { SetFrameDisplayName(frame, previous); });

    frame->object()->setDisplayName(trimmed);
    RefreshFrameRows();
}

void DocumentController::SetMovieVisibility(std::shared_ptr<Scene> const& scene,
                                            std::shared_ptr<Movie> const& movie, bool visible)
{
    if (!movie || movie->isVisible() == visible)
        return;
    movie->setVisibility(visible);
    if (m_host)
    {
        m_host->InvalidateSceneAmbientOcclusion(scene);
        m_host->ReloadRendererData();
    }
}

void DocumentController::ShowMovieInfo(std::shared_ptr<Movie> const& movie) const
{
    if (!movie || !m_host)
        return;
    try
    {
        size_t minAtoms = 0;
        size_t maxAtoms = 0;
        bool first = true;
        for (auto const& frame : movie->frames())
        {
            const size_t n = CountFrameAtoms(frame);
            minAtoms = first ? n : (std::min)(minAtoms, n);
            maxAtoms = first ? n : (std::max)(maxAtoms, n);
            first = false;
        }
        std::wstring info = (minAtoms == maxAtoms)
            ? L" (" + std::to_wstring(minAtoms) + L" atoms)"
            : L" (min " + std::to_wstring(minAtoms) + L" atoms, max " +
              std::to_wstring(maxAtoms) + L" atoms)";
        wchar_t const* glyph = movie->frames().size() > 1 ? L"\uE714" : L"\uE8B9";
        m_host->ShowMessage(glyph, movie->displayName().toStdWString() + info);
    }
    catch (...)
    {
    }
}

void DocumentController::ShowFrameInfo(size_t row) const
{
    if (!m_host || !m_frameList)
        return;
    try
    {
        auto frame = m_frameList->FrameAtRow(row);
        if (frame && frame->object())
        {
            m_host->ShowMessage(L"\uE8B9",
                frame->object()->displayName().toStdWString() + L" (" +
                std::to_wstring(CountFrameAtoms(frame)) + L" atoms)");
        }
    }
    catch (...)
    {
    }
}

void DocumentController::SelectSceneMovie(std::shared_ptr<Scene> const& scene,
                                          std::shared_ptr<Movie> const& movie,
                                          bool refreshRenderer)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !scene)
        return;
    sceneList->setSelectedScene(scene);
    auto chosen = movie;
    if (!chosen && !scene->movies().empty())
        chosen = scene->movies().front();
    if (chosen)
        scene->setSelectedMovie(chosen);

    // Naming one movie is a selection of one, so a selection left over in
    // another scene goes with it.
    for (auto const& other : sceneList->scenes())
    {
        if (other && other != scene)
            other->setSelectedMovies({});
    }

    size_t frameIndex = sceneList->selectedFrameIndex();
    if (chosen && frameIndex >= chosen->frames().size())
        frameIndex = 0;
    sceneList->setSelectedFrameIndex(frameIndex);
    if (chosen)
        chosen->selectedFrameIndex(frameIndex);

    RefreshFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);

    // Appearance section scope stays tied to the left pane (Project/Scene/Frame);
    // do not switch it here — project load also selects a movie and would hide
    // Primitive while the Project pane is still showing.
    if (m_host)
        m_host->RefreshInspector();

    ShowMovieInfo(chosen);
}

void DocumentController::SelectSceneMovies(
    std::vector<std::pair<std::shared_ptr<Scene>, std::shared_ptr<Movie>>> const& picks)
{
    auto sceneList = SceneListOf();
    if (!sceneList || picks.empty())
        return;

    // Cocoa keeps the primary movie put while it stays in the selection; a click
    // that lands outside it hands the role to the row that just joined.
    auto primary = picks.front();
    if (auto current = SelectedMovie())
    {
        auto held = std::find_if(picks.begin(), picks.end(),
                                 [&current](auto const& pick) { return pick.second == current; });
        if (held != picks.end())
        {
            primary = *held;
        }
        else
        {
            for (auto const& pick : picks)
            {
                auto const& already = pick.first->selectedMovies();
                if (already.find(pick.second) == already.end())
                {
                    primary = pick;
                    break;
                }
            }
        }
    }

    // The primary is named first: naming one is the single-selection case and
    // clears the scene's set, which the sets assembled below then replace.
    sceneList->setSelectedScene(primary.first);
    primary.first->setSelectedMovie(primary.second);

    for (auto const& scene : sceneList->scenes())
    {
        if (!scene)
            continue;
        std::set<std::shared_ptr<Movie>> chosen;
        for (auto const& pick : picks)
        {
            if (pick.first == scene && pick.second)
                chosen.insert(pick.second);
        }
        scene->setSelectedMovies(chosen);
    }

    size_t frameIndex = sceneList->selectedFrameIndex();
    if (frameIndex >= primary.second->frames().size())
        frameIndex = 0;
    sceneList->setSelectedFrameIndex(frameIndex);
    primary.second->selectedFrameIndex(frameIndex);

    RefreshFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);

    if (m_host)
        m_host->RefreshInspector();

    if (picks.size() == 1)
        ShowMovieInfo(primary.second);
    else
        Log(std::to_wstring(picks.size()) + L" movies selected");
}

void DocumentController::SetFrameSelection(std::weak_ptr<Movie> movieRef,
                                           size_t primaryIndex,
                                           FrameSelectionIndexSet const& selection,
                                           std::wstring const& actionName)
{
    auto movie = movieRef.lock();
    auto sceneList = SceneListOf();
    if (!movie || !sceneList)
        return;

    const size_t previousPrimary = sceneList->selectedFrameIndex();
    const FrameSelectionIndexSet previous = movie->selectedFramesIndexSet();
    if (previous == selection && previousPrimary == primaryIndex)
        return;

    auto *stack = ObjectUndoStack();
    const bool replaying = stack && (stack->isUndoing() || stack->isRedoing());
    RegisterUndo(stack, actionName,
                 [this, movieRef, previousPrimary, previous, actionName]()
                 {
                     SetFrameSelection(movieRef, previousPrimary, previous, actionName);
                 });

    // Cocoa synchronizeAllMovieFrames(to:): every movie follows the primary
    // frame index; the frame list's own movie then keeps its multi-selection.
    sceneList->setSelectedFrameIndex(primaryIndex);
    movie->setSelection(selection);

    if (replaying && m_frameList)
        m_frameList->ReloadRowSelection();

    if (m_host)
        m_host->ApplySelectionToRenderer(true);

    if (m_host)
        m_host->RefreshInspector();

    ShowFrameInfo(primaryIndex);
}

void DocumentController::AddSceneMovie(ObjectType type)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !m_document)
    {
        Log(L"Open a structure project before adding a movie");
        return;
    }

    try
    {
        auto iraspa = CreateStyledObject(type, m_document);
        if (!iraspa)
        {
            Log(L"Cannot create this object type");
            return;
        }
        auto movie = Movie::create(iraspa);
        movie->setDisplayName(RKString::fromStdWString(ObjectTypeName(type)));

        auto scene = sceneList->selectedScene();
        int row = 0;
        if (!scene)
        {
            // Cocoa addSceneNode: an empty list gets a scene to hold the movie.
            scene = std::make_shared<Scene>();
        }
        else
        {
            row = static_cast<int>(scene->movies().size());
            if (auto current = scene->selectedMovie())
            {
                if (auto idx = scene->findChildIndex(current))
                    row = static_cast<int>(*idx) + 1;
            }
        }

        InsertSceneMovie(scene, movie, row, L"Add Movies");
        Log(std::wstring(L"Added ") + ObjectTypeName(type) + L" movie");
    }
    catch (std::exception const& ex)
    {
        Log(std::wstring(L"Add movie failed: ") + winrt::to_hstring(ex.what()).c_str());
    }
    catch (...)
    {
        Log(L"Add movie failed");
    }
}

void DocumentController::InsertSceneMovie(std::shared_ptr<Scene> const& scene,
                                          std::shared_ptr<Movie> const& movie,
                                          int index,
                                          std::wstring const& actionName)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !scene || !movie)
        return;

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, movie, actionName]() { RemoveSceneMovie(movie, actionName); });

    // A scene that was dropped because it ran empty comes back with its movie.
    if (!sceneList->findChildIndex(scene))
        sceneList->insertChild(sceneList->scenes().size(), scene);

    int row = index;
    if (row < 0 || row > static_cast<int>(scene->movies().size()))
        row = static_cast<int>(scene->movies().size());
    scene->insertChild(static_cast<size_t>(row), movie);

    sceneList->setSelectedScene(scene);
    scene->setSelectedMovie(movie);
    sceneList->setSelectedFrameIndex(0);
    movie->selectedFrameIndex(0);
    RefreshSceneAndFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
}

// Cocoa removeMovieNode / deleteSelectedMovies: the scene goes too once its last
// movie is gone.
void DocumentController::RemoveSceneMovie(std::shared_ptr<Movie> const& movie,
                                          std::wstring const& actionName)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !movie)
        return;

    std::shared_ptr<Scene> owner;
    int index = 0;
    for (auto const& scene : sceneList->scenes())
    {
        if (auto idx = scene->findChildIndex(movie))
        {
            owner = scene;
            index = *idx;
            break;
        }
    }
    if (!owner)
        return;

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, owner, movie, index, actionName]()
                 {
                     InsertSceneMovie(owner, movie, index, actionName);
                 });

    owner->removeChild(static_cast<size_t>(index));

    if (owner->movies().empty())
    {
        if (auto sIdx = sceneList->findChildIndex(owner))
            sceneList->removeChild(static_cast<size_t>(*sIdx));
        if (!sceneList->scenes().empty())
        {
            sceneList->setSelectedScene(sceneList->scenes().front());
            if (auto s = sceneList->selectedScene(); s && !s->movies().empty())
                s->setSelectedMovie(s->movies().front());
        }
        else
        {
            sceneList->setSelectedScene(nullptr);
        }
    }
    else
    {
        sceneList->setSelectedScene(owner);
        owner->setSelectedMovie(owner->movies().front());
    }

    RefreshSceneAndFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
}

void DocumentController::RemoveSelectedSceneMovie()
{
    auto scene = SelectedScene();
    auto movie = scene ? scene->selectedMovie() : nullptr;
    if (!scene || !movie)
    {
        Log(L"Nothing to remove");
        return;
    }

    RemoveSceneMovie(movie, L"Delete Selection");
    Log(L"Removed movie");
}

void DocumentController::AddFrameOfMovieType()
{
    auto movie = SelectedMovie();
    if (!movie)
    {
        Log(L"Select a movie before adding a frame");
        return;
    }
    AddMovieFrame(movie->movieType());
}

void DocumentController::AddMovieFrame(ObjectType type)
{
    auto sceneList = SceneListOf();
    auto movie = SelectedMovie();
    if (!sceneList)
        return;
    if (!movie)
    {
        Log(L"Select a movie before adding a frame");
        return;
    }

    try
    {
        auto frame = CreateStyledObject(type, m_document);
        if (!frame)
        {
            Log(L"Cannot create this object type");
            return;
        }
        size_t row = sceneList->selectedFrameIndex() + 1;
        if (row > movie->frames().size())
            row = movie->frames().size();
        InsertMovieFrame(movie, frame, row, L"Add frame(s)");
        Log(std::wstring(L"Added ") + ObjectTypeName(type) + L" frame");
    }
    catch (std::exception const& ex)
    {
        Log(std::wstring(L"Add frame failed: ") + winrt::to_hstring(ex.what()).c_str());
    }
    catch (...)
    {
        Log(L"Add frame failed");
    }
}

// Cocoa addFrame(_:atIndex:) / removeFrame(_:atIndex:).
void DocumentController::InsertMovieFrame(std::shared_ptr<Movie> const& movie,
                                          std::shared_ptr<iRASPAObject> const& frame,
                                          size_t index,
                                          std::wstring const& actionName)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !movie || !frame)
        return;
    size_t row = index;
    if (row > movie->frames().size())
        row = movie->frames().size();

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, movie, row, actionName]() { RemoveMovieFrame(movie, row, actionName); });

    movie->insertChild(row, frame);
    sceneList->setSelectedFrameIndex(row);
    movie->selectedFrameIndex(row);
    RefreshFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
}

void DocumentController::RemoveMovieFrame(std::shared_ptr<Movie> const& movie,
                                          size_t index,
                                          std::wstring const& actionName)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !movie || index >= movie->frames().size())
        return;
    auto frame = movie->frames()[index];

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, movie, frame, index, actionName]()
                 {
                     InsertMovieFrame(movie, frame, index, actionName);
                 });

    movie->removeChildren(index, 1);

    size_t next = index;
    if (!movie->frames().empty() && next >= movie->frames().size())
        next = movie->frames().size() - 1;
    sceneList->setSelectedFrameIndex(next);
    movie->selectedFrameIndex(next);
    RefreshFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
}

// Cocoa deleteSelection(): the whole frame selection goes, not just the primary
// frame. A movie keeps at least one frame here.
void DocumentController::DeleteSelectedFrames()
{
    auto sceneList = SceneListOf();
    if (!sceneList)
        return;
    auto movie = SelectedMovie();
    if (!movie || movie->frames().empty())
    {
        Log(L"Nothing to remove");
        return;
    }

    FrameSelectionIndexSet indices = movie->selectedFramesIndexSet();
    if (indices.empty())
    {
        size_t index = sceneList->selectedFrameIndex();
        if (index >= movie->frames().size())
            index = movie->frames().size() - 1;
        indices.insert(index);
    }
    if (indices.size() >= movie->frames().size())
    {
        Log(L"Cannot remove the last frame");
        return;
    }

    RemoveMovieFrames(movie, indices, L"Delete selection");
    Log(indices.size() == 1 ? L"Removed frame" : L"Removed frames");
}

// Cocoa deleteSelectedFrames(_:from:newSelectedFrame:newSelection:).
void DocumentController::RemoveMovieFrames(std::shared_ptr<Movie> const& movie,
                                           FrameSelectionIndexSet const& indices,
                                           std::wstring const& actionName)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !movie || indices.empty())
        return;

    std::vector<std::pair<size_t, std::shared_ptr<iRASPAObject>>> positions;
    positions.reserve(indices.size());
    for (size_t index : indices)
    {
        if (index < movie->frames().size())
            positions.emplace_back(index, movie->frames()[index]);
    }
    if (positions.empty())
        return;

    const size_t previousPrimary = sceneList->selectedFrameIndex();
    const FrameSelectionIndexSet previousSelection = movie->selectedFramesIndexSet();
    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, movie, positions, previousSelection, previousPrimary, actionName]()
                 {
                     InsertMovieFrames(movie, positions, previousSelection, previousPrimary,
                                       actionName);
                 });

    // Bottom-up, so the recorded indices stay valid for the re-insert.
    for (auto it = positions.rbegin(); it != positions.rend(); ++it)
        movie->removeChildren(it->first, 1);

    // Cocoa picks the first surviving frame as the new selection.
    size_t next = positions.front().first;
    if (!movie->frames().empty() && next >= movie->frames().size())
        next = movie->frames().size() - 1;
    sceneList->setSelectedFrameIndex(next);
    movie->selectedFrameIndex(next);

    RefreshFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
}

// Cocoa insertSelectedFrames(_:at:newSelectedFrame:newSelection:).
void DocumentController::InsertMovieFrames(
    std::shared_ptr<Movie> const& movie,
    std::vector<std::pair<size_t, std::shared_ptr<iRASPAObject>>> const& positions,
    FrameSelectionIndexSet const& selection,
    size_t primaryIndex,
    std::wstring const& actionName)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !movie || positions.empty())
        return;

    FrameSelectionIndexSet inserted;
    // Ascending, so earlier inserts do not shift the later ones.
    std::vector<std::pair<size_t, std::shared_ptr<iRASPAObject>>> ordered = positions;
    std::sort(ordered.begin(), ordered.end(),
              [](auto const& a, auto const& b) { return a.first < b.first; });
    for (auto const& position : ordered)
    {
        if (!position.second)
            continue;
        const size_t row = (std::min)(position.first, movie->frames().size());
        movie->insertChild(row, position.second);
        inserted.insert(row);
    }
    if (inserted.empty())
        return;

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, movie, inserted, actionName]()
                 {
                     RemoveMovieFrames(movie, inserted, actionName);
                 });

    size_t primary = primaryIndex;
    if (primary >= movie->frames().size())
        primary = movie->frames().empty() ? 0 : movie->frames().size() - 1;
    sceneList->setSelectedFrameIndex(primary);
    if (selection.empty())
        movie->selectedFrameIndex(primary);
    else
        movie->setSelection(selection);

    RefreshFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
}

void DocumentController::ReorderMovieFrames(std::shared_ptr<Movie> const& movie,
                                            std::vector<std::shared_ptr<iRASPAObject>> const& order,
                                            std::wstring const& actionName)
{
    auto sceneList = SceneListOf();
    if (!sceneList || !movie)
        return;
    const std::vector<std::shared_ptr<iRASPAObject>> current = movie->frames();
    if (order.size() != current.size() || order == current)
        return;

    // The selection follows the frames themselves, not the row numbers.
    const std::vector<std::shared_ptr<iRASPAObject>> selectedFrames = movie->selectedFrames();
    const std::shared_ptr<iRASPAObject> primaryFrame =
        movie->frameAtIndex(sceneList->selectedFrameIndex());

    RegisterUndo(ObjectUndoStack(), actionName,
                 [this, movie, current, actionName]()
                 {
                     ReorderMovieFrames(movie, current, actionName);
                 });

    movie->removeChildren(0, current.size());
    for (size_t i = 0; i < order.size(); ++i)
        movie->insertChild(i, order[i]);

    auto indexOf = [&order](std::shared_ptr<iRASPAObject> const& frame) -> std::optional<size_t>
    {
        auto it = std::find(order.begin(), order.end(), frame);
        if (it == order.end())
            return std::nullopt;
        return static_cast<size_t>(it - order.begin());
    };

    FrameSelectionIndexSet selection;
    for (auto const& frame : selectedFrames)
    {
        if (auto index = indexOf(frame))
            selection.insert(*index);
    }
    size_t primary = 0;
    if (auto index = indexOf(primaryFrame))
        primary = *index;
    if (selection.empty())
        selection.insert(primary);

    sceneList->setSelectedFrameIndex(primary);
    movie->setSelection(selection);

    RefreshFrameRows();
    if (m_host)
        m_host->ApplySelectionToRenderer(true);
}
