/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include <vector>
#include <mathkit.h>
#include "skgeometricsurface.h"

// The zeo++ pore properties computed from the Apollonius diagram: the pore diameters Di, Df and Dif,
// and the channel/pocket analysis for a probe.
//
// Ported from Cocoa StructureKit SKApolloniusPoreAnalysis / raspa3 structurekit/diagrams/apollonius.
// Both analyses run on one pore network read off the additively weighted diagram. Building the
// diagram costs far more than either analysis, which is why they are done together here.
//
// The diagram is the exact answer where a radical Voronoi network is an approximation of it. Its
// vertices are the true maxima of the clearance, so Di is the deepest of them rather than the best
// a local ascent could find; its arcs carry the true narrowest point of each passage, so Df is the
// real bottleneck of the percolating path.

/// Largest included / free / included-along-free-path sphere diameters (Di, Df, Dif).
///
///   Di  : diameter of the largest sphere that fits anywhere in the pore space.
///   Df  : diameter of the largest sphere that can travel through the network along a
///         path that percolates through the periodic boundary.
///   Dif : diameter of the largest sphere that can be inscribed anywhere along that same
///         percolating (free-sphere) path.
///
/// A second independent percolating channel (another lattice direction, or a disconnected
/// channel system) gets its own Df and Dif. They stay zero when the network has only one
/// channel. Di is a property of the whole void space, not of a channel.
struct SKPoreDiameters
{
  double includedSphereDiameter = 0.0;
  double freeSphereDiameter = 0.0;
  double includedAlongFreePathDiameter = 0.0;
  double freeSphereDiameter2 = 0.0;
  double includedAlongFreePathDiameter2 = 0.0;

  SKPoreDiameters() = default;
  SKPoreDiameters(double includedSphereDiameterValue, double freeSphereDiameterValue,
                  double includedAlongFreePathDiameterValue, double freeSphereDiameter2Value,
                  double includedAlongFreePathDiameter2Value)
      : includedSphereDiameter(includedSphereDiameterValue), freeSphereDiameter(freeSphereDiameterValue),
        includedAlongFreePathDiameter(includedAlongFreePathDiameterValue),
        freeSphereDiameter2(freeSphereDiameter2Value),
        includedAlongFreePathDiameter2(includedAlongFreePathDiameter2Value)
  {
  }
};

/// Identification of channels and pockets of a pore network for a given probe radius, and their
/// dimensionality. After pruning nodes and edges that are too narrow for the probe, each connected
/// component is examined for periodic self-connection: if a walk reaches a node it has already
/// visited but at a different periodic image, the component percolates and is a channel; otherwise
/// it is an isolated pocket. The dimensionality (1/2/3) is the rank of the lattice-translation
/// vectors along which the component connects to itself.
struct SKChannelAnalysis
{
  int numberOfChannels = 0;
  int numberOfPockets = 0;
  /// The dimensionality of the pore system as a whole: the widest-running of its channels.
  int dimensionality = 0;

  SKChannelAnalysis() = default;
  SKChannelAnalysis(int numberOfChannelsValue, int numberOfPocketsValue, int dimensionalityValue)
      : numberOfChannels(numberOfChannelsValue), numberOfPockets(numberOfPocketsValue),
        dimensionality(dimensionalityValue)
  {
  }
};

/// The first three crystallographic window sizes in the IZA sense: smallest and largest
/// opposite-oxygen free diameters of the rings that open onto percolating channels, in Å.
/// That is the measurement the zeolite atlas quotes for every framework type (8-, 10-, 12-,
/// 14-rings, …), using the atom radii of the snapshot (atlas oxygen is 1.35 Å). Empty pairs
/// stay zero when fewer than three distinct apertures are present. `ringAtomCounts` is the
/// number of T-atoms (equivalently oxygens) that form each ring.
struct SKPoreWindowSizes
{
  std::vector<double2> sizes;
  std::vector<int> ringAtomCounts;

  SKPoreWindowSizes()
      : sizes{double2(), double2(), double2()}, ringAtomCounts{0, 0, 0}
  {
  }

  SKPoreWindowSizes(std::vector<double2> sizesValue, std::vector<int> ringAtomCountsValue);
};

/// The Apollonius-diagram pore properties of one framework: Di, Df, Dif, the channel/pocket
/// split for the snapshot's probe, and the first three channel window sizes.
struct SKApolloniusPoreAnalysis
{
  SKPoreDiameters diameters{};
  SKChannelAnalysis channels{};
  SKPoreWindowSizes windows{};

  SKApolloniusPoreAnalysis() = default;
  SKApolloniusPoreAnalysis(const SKPoreDiameters &diametersValue, const SKChannelAnalysis &channelsValue,
                           const SKPoreWindowSizes &windowsValue)
      : diameters(diametersValue), channels(channelsValue), windows(windowsValue)
  {
  }

  /// Atom radius for the diagram: half the Lennard-Jones sigma, as in raspa3.
  static double atomRadius(double sigma);

  /// Probe radius used to prune the network into channels and pockets: half the probe sigma.
  static double probeRadius(double sigma);

  static SKApolloniusPoreAnalysis compute(const SKFrameworkSnapshot &snapshot);

  static std::vector<SKApolloniusPoreAnalysis> compute(const std::vector<SKFrameworkSnapshot> &structures);

  static std::vector<SKChannelAnalysis> computeChannels(const std::vector<SKFrameworkSnapshot> &structures);

  static std::vector<SKPoreDiameters> computeDiameters(const std::vector<SKFrameworkSnapshot> &structures);

  static std::vector<SKPoreWindowSizes> computeWindows(const std::vector<SKFrameworkSnapshot> &structures);

  /// One blocking sphere per isolated pocket: the largest inscribed sphere of that pocket, as a
  /// fractional position and a radius in Å. Channels are left open.
  static std::vector<double4> computeBlockingPockets(const SKFrameworkSnapshot &snapshot);

  static std::vector<std::vector<double4>>
  computeBlockingPockets(const std::vector<SKFrameworkSnapshot> &structures);
};

/// Rank (0..3) of a set of integer lattice vectors, i.e. the dimensionality they span.
int SKLatticeVectorRank(const std::vector<int3> &vectors);
