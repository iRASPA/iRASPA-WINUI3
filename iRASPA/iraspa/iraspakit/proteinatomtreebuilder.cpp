/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Ported from iRASPAKit ProteinAtomTreeBuilder.swift (MIT License, 2014-2022).
 ********************************************************************************************************************/

#include "proteinatomtreebuilder.h"

#include <algorithm>
#include <map>
#include <set>

#include "proteinbackbone.h"
#include "proteinribbonsecondarystructure.h"
#include "proteinribbonsegmentsupport.h"

namespace
{
  // The residue name is deliberately not part of the key: two records of the same residue may spell
  // it differently, and the chain, sequence number and insertion code already identify a residue.
  struct ResidueKey
  {
    char chainIdentifier = ' ';
    int64_t residueSequenceNumber = 0;
    char codeForInsertionOfResidues = ' ';

    bool operator<(const ResidueKey &other) const
    {
      if (chainIdentifier != other.chainIdentifier) return chainIdentifier < other.chainIdentifier;
      if (residueSequenceNumber != other.residueSequenceNumber) return residueSequenceNumber < other.residueSequenceNumber;
      return codeForInsertionOfResidues < other.codeForInsertionOfResidues;
    }

    bool operator==(const ResidueKey &other) const
    {
      return chainIdentifier == other.chainIdentifier
          && residueSequenceNumber == other.residueSequenceNumber
          && codeForInsertionOfResidues == other.codeForInsertionOfResidues;
    }
  };

  struct ResidueBucket
  {
    RKString residueName;
    std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms;
  };

  struct SecondaryStructureSegment
  {
    ProteinRibbonSecondaryStructure structureType = ProteinRibbonSecondaryStructure::coil;
    char chainIdentifier = ' ';
    std::vector<ResidueKey> residueKeys;
  };

  // Group placeholders are fresh objects on every build and start visible, so a rebuild would
  // quietly bring back the ribbon of a chain, a segment or a residue the user had hidden. They are
  // matched across the rebuild by the path of names that leads to them, which is what the shape
  // comparison below already treats as a group's identity. Only the hidden ones are recorded, so a
  // group the rebuild introduces keeps the visible default. The atoms need none of this: they are
  // the same objects on both sides of the rebuild and carry their own visibility with them.
  void collectHiddenGroupPaths(const std::vector<std::shared_ptr<SKAtomTreeNode>> &nodes,
                               const RKString &prefix,
                               std::set<RKString> &hiddenPaths)
  {
    for (const std::shared_ptr<SKAtomTreeNode> &node : nodes)
    {
      if (!node || !node->isGroup()) continue;
      const RKString path = prefix + RKString("/") + node->displayName();
      if (node->representedObject() && !node->representedObject()->isVisible())
      {
        hiddenPaths.insert(path);
      }
      collectHiddenGroupPaths(node->childNodes(), path, hiddenPaths);
    }
  }

  void applyHiddenGroupPaths(const std::vector<std::shared_ptr<SKAtomTreeNode>> &nodes,
                             const RKString &prefix,
                             const std::set<RKString> &hiddenPaths)
  {
    for (const std::shared_ptr<SKAtomTreeNode> &node : nodes)
    {
      if (!node || !node->isGroup()) continue;
      const RKString path = prefix + RKString("/") + node->displayName();
      if (node->representedObject() && hiddenPaths.count(path) > 0)
      {
        node->representedObject()->setVisibility(false);
      }
      applyHiddenGroupPaths(node->childNodes(), path, hiddenPaths);
    }
  }

  void replaceRootNodes(SKAtomTreeController &controller, const std::vector<std::shared_ptr<SKAtomTreeNode>> &nodes)
  {
    const std::vector<std::shared_ptr<SKAtomTreeNode>> roots = controller.rootNodes();
    for (const std::shared_ptr<SKAtomTreeNode> &root : roots)
    {
      controller.removeNode(root);
    }
    for (const std::shared_ptr<SKAtomTreeNode> &node : nodes)
    {
      controller.appendToRootnodes(node);
    }
  }

  bool hasResidueChild(const std::shared_ptr<SKAtomTreeNode> &node)
  {
    for (const std::shared_ptr<SKAtomTreeNode> &child : node->childNodes())
    {
      if (ProteinRibbonSegmentSupport::isResidueGroupNode(child)) return true;
    }
    return false;
  }

  // A tree already carrying chain, segment or HETATM groups with residue groups under them is a
  // candidate for being left alone; whether it really is current is decided against the tree the
  // builder would put in its place.
  bool hasChainOrderedSegmentHierarchy(const std::vector<std::shared_ptr<SKAtomTreeNode>> &rootNodes)
  {
    for (const std::shared_ptr<SKAtomTreeNode> &rootNode : rootNodes)
    {
      if (ProteinRibbonSegmentSupport::isChainGroupNode(rootNode))
      {
        for (const std::shared_ptr<SKAtomTreeNode> &childNode : rootNode->childNodes())
        {
          if (hasResidueChild(childNode)) return true;
        }
      }
      else if (hasResidueChild(rootNode))
      {
        return true;
      }
    }
    return false;
  }

  // Same groups of the same kind with the same names holding the same atoms in the same order.
  // Group placeholders are fresh objects on every build, so they are compared by what they say of
  // themselves, while atoms are the very same objects.
  bool haveSameShape(const std::vector<std::shared_ptr<SKAtomTreeNode>> &left,
                     const std::vector<std::shared_ptr<SKAtomTreeNode>> &right)
  {
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index)
    {
      const std::shared_ptr<SKAtomTreeNode> &leftNode = left[index];
      const std::shared_ptr<SKAtomTreeNode> &rightNode = right[index];
      if (!leftNode || !rightNode) return false;
      if (leftNode->groupKind() != rightNode->groupKind()) return false;
      if (leftNode->isGroup())
      {
        if (!(leftNode->displayName() == rightNode->displayName())) return false;
        if (!haveSameShape(leftNode->childNodes(), rightNode->childNodes())) return false;
      }
      else if (leftNode->representedObject() != rightNode->representedObject())
      {
        return false;
      }
    }
    return true;
  }

  ResidueKey residueKeyForAtom(const std::shared_ptr<SKAsymmetricAtom> &atom)
  {
    return {static_cast<char>(atom->chainIdentifier()),
            atom->residueSequenceNumber(),
            static_cast<char>(atom->codeForInsertionOfResidues())};
  }

  ResidueKey residueKeyForResidue(char chainIdentifier, const ProteinBackboneResidue &residue)
  {
    return {chainIdentifier, residue.residueSequenceNumber, residue.codeForInsertionOfResidues};
  }

  bool hasResidueIdentity(const std::shared_ptr<SKAsymmetricAtom> &atom)
  {
    return !atom->residueName().trimmed().isEmpty() || atom->residueSequenceNumber() != 0;
  }

  bool atomSortOrder(const std::shared_ptr<SKAsymmetricAtom> &lhs, const std::shared_ptr<SKAsymmetricAtom> &rhs)
  {
    const std::string leftName = lhs->displayName().toUpper().utf8();
    const std::string rightName = rhs->displayName().toUpper().utf8();
    if (leftName != rightName) return leftName < rightName;
    return lhs->elementIdentifier() < rhs->elementIdentifier();
  }

  RKString trimmedResidueName(const RKString &residueName)
  {
    const RKString trimmed = residueName.trimmed();
    return trimmed.isEmpty() ? RKString("RES") : trimmed;
  }

  RKString residueNumberLabel(const ResidueKey &key)
  {
    RKString label = RKString::number(key.residueSequenceNumber);
    if (key.codeForInsertionOfResidues != ' ') label += key.codeForInsertionOfResidues;
    return label;
  }

  RKString chainDisplayName(char chainIdentifier)
  {
    if (chainIdentifier != ' ') return RKString("Chain ") + RKString(chainIdentifier);
    return RKString("Chain");
  }

  // A group node still needs a represented object for the tree to hold it, so it gets a placeholder
  // atom of no element, no size and no colour. It has no symmetry copies, which is what keeps it out
  // of the renderer and out of the atom count. The kind is what the tree is read by afterwards; the
  // name is the label and nothing more.
  std::shared_ptr<SKAtomTreeNode> makeGroupNode(const RKString &displayName, SKAtomTreeGroupKind groupKind)
  {
    std::shared_ptr<SKAsymmetricAtom> containerAtom = std::make_shared<SKAsymmetricAtom>(displayName, 0);
    containerAtom->setDisplayName(displayName);
    containerAtom->setColor(RKColor(0.0, 0.0, 0.0));
    containerAtom->setDrawRadius(0.0);
    std::shared_ptr<SKAtomTreeNode> node = std::make_shared<SKAtomTreeNode>(containerAtom);
    node->setDisplayName(displayName);
    node->setGroupKind(groupKind);
    return node;
  }

  RKString residueDisplayName(const ResidueKey &key, const ResidueBucket &bucket)
  {
    return trimmedResidueName(bucket.residueName) + RKString(" ") + residueNumberLabel(key);
  }

  std::map<ResidueKey, ProteinRibbonSecondaryStructure> assignSecondaryStructure(
    const ProteinBackbone &backbone,
    ProteinRibbonSecondaryStructureMethod secondaryStructureMethod)
  {
    std::map<ResidueKey, ProteinRibbonSecondaryStructure> assignmentByResidue;
    for (const ProteinBackboneChain &chain : backbone.chains)
    {
      // The assigners only speak about residues that have an alpha carbon, so the assignment lines
      // up with that subset rather than with every residue of the chain.
      std::vector<ProteinBackboneResidue> residuesWithAlphaCarbon;
      for (const ProteinBackboneResidue &residue : chain.residues)
      {
        if (residue.alphaCarbon) residuesWithAlphaCarbon.push_back(residue);
      }
      const std::vector<ProteinRibbonSecondaryStructure> assignment =
        ProteinRibbonSecondaryStructureAssigner::assign(chain, double3(0.0, 0.0, 0.0), secondaryStructureMethod);
      for (size_t index = 0; index < residuesWithAlphaCarbon.size() && index < assignment.size(); ++index)
      {
        assignmentByResidue[residueKeyForResidue(chain.chainIdentifier, residuesWithAlphaCarbon[index])] = assignment[index];
      }
    }
    return assignmentByResidue;
  }

  RKString segmentDisplayName(const SecondaryStructureSegment &segment,
                              const std::map<ResidueKey, ResidueBucket> &residuesByKey)
  {
    RKString typeLabel;
    switch (segment.structureType)
    {
    case ProteinRibbonSecondaryStructure::helix: typeLabel = RKString("Alpha-helix"); break;
    case ProteinRibbonSecondaryStructure::sheet: typeLabel = RKString("Beta-sheet"); break;
    case ProteinRibbonSecondaryStructure::coil: typeLabel = RKString("Coil"); break;
    }
    if (segment.residueKeys.empty()) return typeLabel;

    const ResidueKey &firstKey = segment.residueKeys.front();
    const ResidueKey &lastKey = segment.residueKeys.back();
    const RKString firstName = trimmedResidueName(residuesByKey.at(firstKey).residueName);
    if (firstKey == lastKey)
    {
      return typeLabel + RKString(" (") + firstName + RKString(" ") + residueNumberLabel(firstKey) + RKString(")");
    }
    const RKString lastName = trimmedResidueName(residuesByKey.at(lastKey).residueName);
    // En dash between the two ends of the range, as Cocoa labels them.
    return typeLabel + RKString(" (") + firstName + RKString(" ") + residueNumberLabel(firstKey) +
           RKString("\xE2\x80\x93") + lastName + RKString(" ") + residueNumberLabel(lastKey) + RKString(")");
  }

  // PDB marks every HETATM as solvent. Polymer MODRES written as HETATM (SET for aminoserine, and
  // the like) still carry the peptide backbone and stay with the chain segments; the rest — waters,
  // ions, ligands — go under HETATM.
  bool residueIsHetatmListing(const ResidueBucket &bucket)
  {
    if (bucket.atoms.empty()) return false;
    for (const std::shared_ptr<SKAsymmetricAtom> &atom : bucket.atoms)
    {
      if (!atom->solvent()) return false;
    }
    bool hasNitrogen = false;
    bool hasAlphaCarbon = false;
    bool hasCarbonyl = false;
    for (const std::shared_ptr<SKAsymmetricAtom> &atom : bucket.atoms)
    {
      const RKString atomName = atom->displayName().toUpper();
      if (atomName == "N") hasNitrogen = true;
      else if (atomName == "CA") hasAlphaCarbon = true;
      else if (atomName == "C") hasCarbonyl = true;
    }
    if (hasNitrogen && hasAlphaCarbon && hasCarbonyl) return false;
    return true;
  }

  std::shared_ptr<SKAtomTreeNode> makeResidueNode(const ResidueKey &key, const ResidueBucket &bucket)
  {
    const std::shared_ptr<SKAtomTreeNode> residueNode =
      makeGroupNode(residueDisplayName(key, bucket), SKAtomTreeGroupKind::residue);
    std::vector<std::shared_ptr<SKAsymmetricAtom>> sortedAtoms = bucket.atoms;
    std::sort(sortedAtoms.begin(), sortedAtoms.end(), atomSortOrder);
    for (const std::shared_ptr<SKAsymmetricAtom> &atom : sortedAtoms)
    {
      std::make_shared<SKAtomTreeNode>(atom)->appendToParent(residueNode);
    }
    return residueNode;
  }

  std::shared_ptr<SKAtomTreeNode> makeHetatmGroupNode(const std::vector<ResidueKey> &keys,
                                                      const std::map<ResidueKey, ResidueBucket> &residuesByKey)
  {
    const std::shared_ptr<SKAtomTreeNode> hetatmNode =
      makeGroupNode(RKString("HETATM"), SKAtomTreeGroupKind::hetatm);
    for (const ResidueKey &key : keys)
    {
      const auto bucket = residuesByKey.find(key);
      if (bucket == residuesByKey.end()) continue;
      makeResidueNode(key, bucket->second)->appendToParent(hetatmNode);
    }
    return hetatmNode;
  }

  std::shared_ptr<SKAtomTreeNode> makeSegmentNode(const SecondaryStructureSegment &segment,
                                                  const std::map<ResidueKey, ResidueBucket> &residuesByKey)
  {
    const std::shared_ptr<SKAtomTreeNode> segmentNode =
      makeGroupNode(segmentDisplayName(segment, residuesByKey), SKAtomTreeGroupKind::secondaryStructureSegment);
    for (const ResidueKey &key : segment.residueKeys)
    {
      const auto bucket = residuesByKey.find(key);
      if (bucket == residuesByKey.end()) continue;
      makeResidueNode(key, bucket->second)->appendToParent(segmentNode);
    }
    return segmentNode;
  }

  std::map<char, std::vector<SecondaryStructureSegment>> buildSegmentsByChain(
    const ProteinBackbone &backbone,
    const std::map<ResidueKey, ResidueBucket> &residuesByKey,
    const std::map<ResidueKey, ProteinRibbonSecondaryStructure> &secondaryStructureByResidue)
  {
    std::map<char, std::vector<SecondaryStructureSegment>> segmentsByChain;
    std::set<ResidueKey> assignedKeys;

    for (const ProteinBackboneChain &chain : backbone.chains)
    {
      std::vector<ProteinBackboneResidue> residuesWithAlphaCarbon;
      for (const ProteinBackboneResidue &residue : chain.residues)
      {
        if (residue.alphaCarbon) residuesWithAlphaCarbon.push_back(residue);
      }

      std::vector<ProteinRibbonSecondaryStructure> assignment;
      assignment.reserve(residuesWithAlphaCarbon.size());
      for (const ProteinBackboneResidue &residue : residuesWithAlphaCarbon)
      {
        const auto entry = secondaryStructureByResidue.find(residueKeyForResidue(chain.chainIdentifier, residue));
        assignment.push_back(entry != secondaryStructureByResidue.end() ? entry->second
                                                                       : ProteinRibbonSecondaryStructure::coil);
      }

      // The same runs the ribbon mesh is swept from, so a segment group matches a segment draw range.
      const std::vector<ProteinRibbonResidueSegment> runs =
        ProteinRibbonSegmentSupport::residueSegments(assignment, chain.chainIdentifier);
      std::vector<SecondaryStructureSegment> chainSegments;
      for (const ProteinRibbonResidueSegment &run : runs)
      {
        std::vector<ResidueKey> keys;
        for (int index = run.firstResidueIndex;
             index <= run.lastResidueIndex && index < static_cast<int>(residuesWithAlphaCarbon.size());
             ++index)
        {
          const ResidueKey key = residueKeyForResidue(chain.chainIdentifier, residuesWithAlphaCarbon[index]);
          if (residuesByKey.find(key) == residuesByKey.end()) continue;
          keys.push_back(key);
        }
        if (keys.empty()) continue;
        chainSegments.push_back({run.structureType, run.chainIdentifier, keys});
        assignedKeys.insert(keys.begin(), keys.end());
      }
      if (!chainSegments.empty()) segmentsByChain[chain.chainIdentifier] = chainSegments;
    }

    // Residues the ribbon never sweeps still belong in the tree, but only the polymer ones: HETATM
    // waters and ligands are listed under a separate HETATM group per chain instead of as lone coils.
    std::vector<ResidueKey> unassignedKeys;
    for (const auto &entry : residuesByKey)
    {
      if (!assignedKeys.count(entry.first)) unassignedKeys.push_back(entry.first);
    }
    std::sort(unassignedKeys.begin(), unassignedKeys.end());
    for (const ResidueKey &key : unassignedKeys)
    {
      const auto entry = secondaryStructureByResidue.find(key);
      const ProteinRibbonSecondaryStructure structureType =
        entry != secondaryStructureByResidue.end() ? entry->second : ProteinRibbonSecondaryStructure::coil;
      segmentsByChain[key.chainIdentifier].push_back({structureType, key.chainIdentifier, {key}});
    }
    return segmentsByChain;
  }
}

bool ProteinAtomTreeBuilder::applyHierarchyIfNeeded(SKAtomTreeController &controller,
                                                    ProteinRibbonSecondaryStructureMethod secondaryStructureMethod)
{
  // Only the leaves are real atoms. Reading the group placeholders back in would re-parent them as
  // atoms of their own and grow the tree on every call.
  std::vector<std::shared_ptr<SKAsymmetricAtom>> atoms;
  for (const std::shared_ptr<SKAtomTreeNode> &node : controller.flattenedLeafNodes())
  {
    if (const std::shared_ptr<SKAsymmetricAtom> atom = node->representedObject()) atoms.push_back(atom);
  }
  if (atoms.empty()) return false;

  const ProteinBackbone backbone = ProteinBackbone::build(atoms);
  if (backbone.chains.empty()) return false;

  const std::vector<std::shared_ptr<SKAtomTreeNode>> rebuilt = build(atoms, secondaryStructureMethod);

  // Whether the tree is already current is decided against the tree that would replace it, not
  // against a count derived from the backbone: the builder also groups the residues the ribbon never
  // sweeps, waters and lone fragments among them, and no count taken from the backbone knows about
  // those.
  if (hasChainOrderedSegmentHierarchy(controller.rootNodes())
      && haveSameShape(controller.rootNodes(), rebuilt))
  {
    return false;
  }

  std::set<RKString> hiddenGroupPaths;
  collectHiddenGroupPaths(controller.rootNodes(), RKString(""), hiddenGroupPaths);

  replaceRootNodes(controller, rebuilt);
  if (!hiddenGroupPaths.empty())
  {
    applyHiddenGroupPaths(controller.rootNodes(), RKString(""), hiddenGroupPaths);
  }
  // The tags name atoms by their position in the flattened tree, and that order has just changed.
  controller.setTags();
  return true;
}

std::vector<std::shared_ptr<SKAtomTreeNode>> ProteinAtomTreeBuilder::build(
  const std::vector<std::shared_ptr<SKAsymmetricAtom>> &atoms,
  ProteinRibbonSecondaryStructureMethod secondaryStructureMethod)
{
  const ProteinBackbone backbone = ProteinBackbone::build(atoms);
  const std::map<ResidueKey, ProteinRibbonSecondaryStructure> secondaryStructureByResidue =
    assignSecondaryStructure(backbone, secondaryStructureMethod);

  std::map<ResidueKey, ResidueBucket> residuesByKey;
  std::vector<std::shared_ptr<SKAsymmetricAtom>> orphanAtoms;
  for (const std::shared_ptr<SKAsymmetricAtom> &atom : atoms)
  {
    if (!hasResidueIdentity(atom))
    {
      orphanAtoms.push_back(atom);
      continue;
    }
    const ResidueKey key = residueKeyForAtom(atom);
    const auto bucket = residuesByKey.find(key);
    if (bucket == residuesByKey.end())
    {
      residuesByKey[key] = {atom->residueName(), {atom}};
      continue;
    }
    bucket->second.atoms.push_back(atom);
  }

  std::map<ResidueKey, ResidueBucket> polymerResiduesByKey;
  std::map<char, std::vector<ResidueKey>> hetatmKeysByChain;
  for (const auto &entry : residuesByKey)
  {
    if (residueIsHetatmListing(entry.second))
    {
      hetatmKeysByChain[entry.first.chainIdentifier].push_back(entry.first);
    }
    else
    {
      polymerResiduesByKey[entry.first] = entry.second;
    }
  }
  for (auto &entry : hetatmKeysByChain)
  {
    std::sort(entry.second.begin(), entry.second.end());
  }

  const std::map<char, std::vector<SecondaryStructureSegment>> segmentsByChain =
    buildSegmentsByChain(backbone, polymerResiduesByKey, secondaryStructureByResidue);

  std::vector<char> chainOrder;
  std::set<char> seenChains;
  for (const ProteinBackboneChain &chain : backbone.chains)
  {
    chainOrder.push_back(chain.chainIdentifier);
    seenChains.insert(chain.chainIdentifier);
  }
  for (const auto &entry : hetatmKeysByChain)
  {
    if (seenChains.insert(entry.first).second) chainOrder.push_back(entry.first);
  }

  // One chain needs no chain level: the segments (and HETATM) are the top of the tree.
  const bool useChainLevel = chainOrder.size() > 1;

  std::vector<std::shared_ptr<SKAtomTreeNode>> rootNodes;
  for (const char chainIdentifier : chainOrder)
  {
    const auto segments = segmentsByChain.find(chainIdentifier);
    const auto hetatmKeys = hetatmKeysByChain.find(chainIdentifier);
    const bool hasSegments = segments != segmentsByChain.end() && !segments->second.empty();
    const bool hasHetatm = hetatmKeys != hetatmKeysByChain.end() && !hetatmKeys->second.empty();
    if (!hasSegments && !hasHetatm) continue;

    std::shared_ptr<SKAtomTreeNode> chainNode;
    if (useChainLevel)
    {
      chainNode = makeGroupNode(chainDisplayName(chainIdentifier), SKAtomTreeGroupKind::chain);
      rootNodes.push_back(chainNode);
    }

    if (hasSegments)
    {
      for (const SecondaryStructureSegment &segment : segments->second)
      {
        const std::shared_ptr<SKAtomTreeNode> segmentNode = makeSegmentNode(segment, polymerResiduesByKey);
        if (chainNode) segmentNode->appendToParent(chainNode);
        else rootNodes.push_back(segmentNode);
      }
    }

    if (hasHetatm)
    {
      const std::shared_ptr<SKAtomTreeNode> hetatmNode =
        makeHetatmGroupNode(hetatmKeys->second, residuesByKey);
      if (chainNode) hetatmNode->appendToParent(chainNode);
      else rootNodes.push_back(hetatmNode);
    }
  }

  if (!orphanAtoms.empty())
  {
    const std::shared_ptr<SKAtomTreeNode> otherNode = makeGroupNode(RKString("Other"), SKAtomTreeGroupKind::other);
    std::vector<std::shared_ptr<SKAsymmetricAtom>> sortedOrphans = orphanAtoms;
    std::sort(sortedOrphans.begin(), sortedOrphans.end(), atomSortOrder);
    for (const std::shared_ptr<SKAsymmetricAtom> &atom : sortedOrphans)
    {
      std::make_shared<SKAtomTreeNode>(atom)->appendToParent(otherNode);
    }
    rootNodes.push_back(otherNode);
  }

  return rootNodes;
}
