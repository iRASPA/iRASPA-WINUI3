#include "pch.h"
#include "DocumentController.h"

#include "atomviewer.h"
#include "bondviewer.h"
#include "iraspaobject.h"
#include "structure.h"
#include "skasymmetricatom.h"
#include "skasymmetricbond.h"
#include "skbondsetcontroller.h"

// The document-level half of the Bonds tab (Cocoa
// StructureBondDetailViewController). As in Cocoa, a bond edit is not undoable:
// the row writes straight through to the model, and the operations that follow
// from it -- regenerating the symmetry copies and the bond topology after a
// length change, recomputing the whole bond set -- live here. Nothing here
// touches XAML.

namespace
{
    std::shared_ptr<BondViewer> BondViewerOf(std::shared_ptr<iRASPAObject> const& frame)
    {
        return frame ? std::dynamic_pointer_cast<BondViewer>(frame->object()) : nullptr;
    }
}

std::shared_ptr<iRASPAObject> DocumentController::BondsFrame() const
{
    for (auto const& iraspa : TargetStructures())
    {
        if (auto viewer = BondViewerOf(iraspa); viewer && viewer->bondSetController())
            return iraspa;
    }
    return nullptr;
}

std::shared_ptr<SKBondSetController> DocumentController::BondSet(
    std::weak_ptr<iRASPAObject> frameRef) const
{
    auto viewer = BondViewerOf(frameRef.lock());
    return viewer ? viewer->bondSetController() : nullptr;
}

double DocumentController::BondLength(std::weak_ptr<iRASPAObject> frameRef,
                                      std::shared_ptr<SKAsymmetricBond> const& bond) const
{
    if (!bond || bond->copies().empty())
        return 0.0;
    auto viewer = BondViewerOf(frameRef.lock());
    return viewer ? viewer->bondLength(bond->copies().front()) : 0.0;
}

void DocumentController::SetBondLength(std::weak_ptr<iRASPAObject> frameRef,
                                       std::shared_ptr<SKAsymmetricBond> const& bond,
                                       double length)
{
    auto frame = frameRef.lock();
    if (!frame || !bond || bond->copies().empty())
        return;
    auto structure = std::dynamic_pointer_cast<Structure>(frame->object());
    if (!structure)
        return;
    auto atom1 = bond->atom1();
    auto atom2 = bond->atom2();
    if (!atom1 || !atom2)
        return;

    // Cocoa changedBondLength: both atoms move along the bond axis, and
    // computeChangedBondLength is what knows which periodic image the bond was
    // drawn through.
    const std::pair<double3, double3> moved =
        structure->computeChangedBondLength(bond->copies().front(), length);
    atom1->setPosition(moved.first);
    atom2->setPosition(moved.second);

    if (auto atoms = std::dynamic_pointer_cast<AtomViewer>(frame->object()))
        atoms->expandSymmetry();
    if (auto bonds = BondViewerOf(frame))
        bonds->computeBonds();

    // computeBonds built a new set of asymmetric bonds, so the rows are now
    // holding the previous ones.
    if (m_bondsPane)
        m_bondsPane->RebindBonds();
    ReloadRenderer();
}

void DocumentController::RecomputeBonds(std::weak_ptr<iRASPAObject> frameRef)
{
    auto viewer = BondViewerOf(frameRef.lock());
    if (!viewer)
        return;
    viewer->computeBonds();
    ReloadRenderer();
    // The bond set is a different length now, so the rows are rebuilt rather
    // than re-pointed.
    if (m_host)
        m_host->RefreshInspector();
}

void DocumentController::SetBondSelection(std::weak_ptr<iRASPAObject> frameRef,
                                          std::set<int64_t> const& rows) const
{
    auto controller = BondSet(frameRef);
    if (!controller)
        return;
    controller->setSelectionIndexSet(BondSelectionIndexSet(rows.begin(), rows.end()));
    if (m_host)
        m_host->ReloadRendererSelection();
}
