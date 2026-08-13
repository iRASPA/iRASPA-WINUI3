/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#pragma once

#include "rkstring.h"
#include <filesystem>
#include <vector>
#include <tuple>
#include <unordered_set>
#include <optional>
#include <ostream>
#include <sstream>
#include <fstream>
#include <simulationkit.h>
#include <symmetrykit.h>
#include "movie.h"
#include "iraspakitprotocols.h"

class SceneList;

class Scene: public std::enable_shared_from_this<Scene>, public DisplayableProtocol
{
public:
  Scene();
  Scene(RKString displayName);
  static std::shared_ptr<Scene> create(std::shared_ptr<Movie> movie);
  Scene(const std::filesystem::path &path, const SKColorSets& colorSets, ForceFieldSets &forcefieldSets, bool proteinOnlyAsymmetricUnit, bool asMolecule, bool separatePolymerChains = false) noexcept(false);
  const std::vector<std::shared_ptr<Movie>> &movies() const {return _movies;}
  std::optional<int> findChildIndex(std::shared_ptr<Movie> movie);
  bool removeChild(size_t row);
  bool removeChildren(size_t position, size_t count);
  bool insertChild(size_t row, std::shared_ptr<Movie> child);
  void setSelectedMovie(std::shared_ptr<Movie> movie);
  void setSelectedMovies(std::set<std::shared_ptr<Movie>> movies);
  std::set<std::shared_ptr<Movie>>& selectedMovies() {return _selectedMovies;}
  std::shared_ptr<Movie> selectedMovie();
  RKString displayName() const override final {return _displayName;}
  void setDisplayName(RKString name) override final {_displayName = name;}
  std::optional<int> selectMovieIndex();
  void setParent(std::weak_ptr<SceneList> parent) {_parent = parent;}
  std::weak_ptr<SceneList> parent() {return _parent;}
private:
  Scene(std::shared_ptr<Movie> movie);
  int64_t _versionNumber{1};
  RKString _displayName = RKString("Scene");
  std::weak_ptr<SceneList> _parent{};
  std::vector<std::shared_ptr<Movie>> _movies{};
  std::shared_ptr<Movie> _selectedMovie{nullptr};
  std::set<std::shared_ptr<Movie>> _selectedMovies;

  friend BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<std::shared_ptr<Movie>>& val);
  friend BinaryArchive &operator>>(BinaryArchive & stream, std::vector<std::shared_ptr<Movie>>& val);

  friend BinaryArchive &operator<<(BinaryArchive &, const std::shared_ptr<Scene> &);
  friend BinaryArchive &operator>>(BinaryArchive &, std::shared_ptr<Scene> &);
};
