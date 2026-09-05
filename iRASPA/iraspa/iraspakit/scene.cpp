/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2021 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
    D.Dubbeldam@uva.nl            https://www.uva.nl/en/profile/d/u/d.dubbeldam/d.dubbeldam.html
    S.Calero@tue.nl               https://www.tue.nl/en/research/researchers/sofia-calero/
    t.j.h.vlugt@tudelft.nl        http://homepage.tudelft.nl/v9k6y

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ********************************************************************************************************************/

#include "scene.h"
#include "rkstring.h"
#include "iraspaobject.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include "skposcarparser.h"
#include "skpdbparser.h"
#include "proteinribbonmixin.h"
#include "skcifparser.h"
#include "skxyzparser.h"
#include "skmaterialtype.h"
#include "structuralpropertyviewer.h"

namespace {

std::string toLowerAscii(std::string s)
{
  for (char &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string extensionLower(const std::filesystem::path &path)
{
  std::string ext = path.extension().string();
  if (!ext.empty() && ext[0] == '.')
    ext.erase(ext.begin());
  return toLowerAscii(ext);
}

// An imported protein opens as a ribbon, and its mesh has to be built here because nothing else
// asks for it before the renderer reads the vertices. Drawing atoms and bonds stays switched on:
// what the first look owes to the ribbon is settled per atom, so any one of them can be brought
// back over it, and its bonds return with it. The atom look is licorice and the ribbon is Default —
// the Fancy lighting is left for the user to ask for.
void applyImportedProteinRibbonDefaults(const std::shared_ptr<Object> &object,
                                        const SKColorSets &colorSets)
{
  ProteinRibbonStructureEditor *ribbonEditor = dynamic_cast<ProteinRibbonStructureEditor*>(object.get());
  if (!ribbonEditor) { return; }

  Structure *structure = dynamic_cast<Structure*>(object.get());

  // The solvent of a protein crystal is a protein crystal like any other and can edit a ribbon, but
  // it has no backbone to sweep one along. Trading its atoms for a ribbon would leave it showing
  // nothing at all, so it keeps them.
  if (structure && structure->containsOnlySolventAtoms()) { return; }

  if (structure)
  {
    structure->setRepresentationStyle(Structure::RepresentationStyle::licorice, colorSets);
    // Thinner than the stock licorice 0.25: with a ribbon drawn, thick sticks bury the backbone.
    structure->setBondScaleFactor(0.1);
  }

  object->setDrawUnitCell(false);
  ribbonEditor->setDrawRibbon(true);

  int atomCount = 0;
  int residueCount = 0;
  std::shared_ptr<SKAtomTreeController> atomTree;
  if (AtomViewer *atomViewer = dynamic_cast<AtomViewer*>(object.get()))
  {
    atomTree = atomViewer->atomsTreeController();
    atomCount = static_cast<int>(atomTree->flattenedLeafNodes().size());
  }
  if (ProteinRibbonMixin *ribbonMixin = dynamic_cast<ProteinRibbonMixin*>(object.get()))
  {
    residueCount = ribbonMixin->backbone().alphaCarbonResidueCount();
    if (atomTree) { ribbonMixin->hideAtomsBehindRibbon(*atomTree); }
  }
  if (residueCount <= 0) { residueCount = atomCount; }

  setRibbonMeshParameters(*ribbonEditor, ProteinRibbonMeshParameters::forImportedStructure(atomCount, residueCount));
  applyRibbonRepresentationStyle(*ribbonEditor, ProteinRibbonRepresentationStyle::defaultStyle);
  ribbonEditor->rebuildBackbone();
}

std::string filenameUpper(const std::filesystem::path &path)
{
  std::string name = path.filename().string();
  for (char &c : name)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return name;
}

} // namespace

Scene::Scene()
{

}

Scene::Scene(RKString displayName): _displayName(displayName)
{
}

// private constructor, parent of movie needs to be set
Scene::Scene(std::shared_ptr<Movie> movie): _movies{movie}
{
}

std::shared_ptr<Scene> Scene::create(std::shared_ptr<Movie> movie)
{
  // std::make_shared can not call private constructor
  std::shared_ptr<Scene> scene =  std::shared_ptr<Scene>( new Scene( std::forward<Scene>(movie)));
  movie->setParent(scene);
  return scene;
}

std::shared_ptr<Movie> Scene::selectedMovie()
{
  if(_selectedMovie)
    return _selectedMovie;
  return nullptr;
}

/*
void Scene::setSelectedFrameIndices(int frameIndex)
{
  for(std::shared_ptr<Movie> movie : _movies)
  {
    movie->setSelectedFrameIndex(frameIndex);
  }
}*/

// Cocoa keeps the primary movie and the selection as two separate properties,
// and the movie list writes both. Naming a primary here is the single-selection
// case, so the selection becomes that one movie rather than growing by one.
void Scene::setSelectedMovie(std::shared_ptr<Movie> movie)
{
  _selectedMovie = movie;
  _selectedMovies.clear();
  if(_selectedMovie)
  {
    _selectedMovies.insert(_selectedMovie);
  }
}

void Scene::setSelectedMovies(std::set<std::shared_ptr<Movie>> movies)
{
  _selectedMovies = movies;
}

Scene::Scene(const std::filesystem::path &path, const SKColorSets& colorSets, ForceFieldSets& forcefieldSets, bool proteinOnlyAsymmetricUnit, bool asMolecule, bool separatePolymerChains) noexcept(false)
{
  const std::string fileName = filenameUpper(path);
  const std::string suffix = extensionLower(path);

  std::shared_ptr<SKParser> parser;

  if((fileName == "POSCAR" && suffix.empty()) ||
     (fileName == "CONTCAR" && suffix.empty()) ||
      suffix == "poscar")
  {
    parser = std::make_shared<SKPOSCARParser>(path, proteinOnlyAsymmetricUnit, asMolecule, CharacterSet::whitespaceAndNewlineCharacterSet());
  }
  else if(fileName == "CHGCAR" && suffix.empty())
  {
    parser = std::make_shared<SKCHGCARParser>(path, proteinOnlyAsymmetricUnit, asMolecule, CharacterSet::whitespaceAndNewlineCharacterSet());
  }
  else if(fileName == "ELFCAR" && suffix.empty())
  {
    parser = std::make_shared<SKELFCARParser>(path, proteinOnlyAsymmetricUnit, asMolecule, CharacterSet::whitespaceAndNewlineCharacterSet());
  }
  else if(fileName == "LOCPOT" && suffix.empty())
  {
    parser = std::make_shared<SKLOCPOTParser>(path, proteinOnlyAsymmetricUnit, asMolecule, CharacterSet::whitespaceAndNewlineCharacterSet());
  }
  else if (suffix == "cif" || suffix == "mmcif")
  {
    parser = std::make_shared<SKCIFParser>(path, proteinOnlyAsymmetricUnit, asMolecule,
                                          CharacterSet::whitespaceAndNewlineCharacterSet(),
                                          true, separatePolymerChains);
  }
  else if (suffix == "pdb")
  {
    parser = std::make_shared<SKPDBParser>(path, proteinOnlyAsymmetricUnit, asMolecule,
                                          CharacterSet::whitespaceAndNewlineCharacterSet(),
                                          separatePolymerChains);
  }
  else if (suffix == "xyz")
  {
    parser = std::make_shared<SKXYZParser>(path, proteinOnlyAsymmetricUnit, asMolecule, CharacterSet::whitespaceAndNewlineCharacterSet());
  }
  else if (suffix == "cube")
  {
    parser = std::make_shared<SKGaussianCubeParser>(path, proteinOnlyAsymmetricUnit, asMolecule, CharacterSet::whitespaceAndNewlineCharacterSet());
  }
  else
  {
    throw std::runtime_error("Unsupported structure file format: " + path.string());
  }

  parser->startParsing();

  std::vector<std::vector<std::shared_ptr<SKStructure>>> movies = parser->movies();

  for (const std::vector<std::shared_ptr<SKStructure>> &movieFrames : movies)
  {
    std::vector<std::shared_ptr<iRASPAObject>> iraspastructures{};
    for (std::shared_ptr<SKStructure> frame : movieFrames)
    {
      std::shared_ptr<iRASPAObject> iraspastructure;

      switch(frame->kind)
      {
      case SKStructure::Kind::crystal:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<Crystal>(frame));
        break;
      case SKStructure::Kind::molecularCrystal:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<MolecularCrystal>(frame));
        break;
      case SKStructure::Kind::molecule:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<Molecule>(frame));
        break;
      case SKStructure::Kind::protein:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<Protein>(frame));
        break;
      case SKStructure::Kind::proteinCrystal:
      // Cocoa builds a protein crystal for the solvent too, and lets its atoms say which of the two
      // it is, so there is one class for both.
      case SKStructure::Kind::proteinCrystalSolvent:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<ProteinCrystal>(frame));
        break;
      case SKStructure::Kind::RASPADensityVolume:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<RASPADensityVolume>(frame));
        break;
      case SKStructure::Kind::VTKDensityVolume:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<VTKDensityVolume>(frame));
        break;
      case SKStructure::Kind::VASPDensityVolume:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<VASPDensityVolume>(frame));
        break;
      case SKStructure::Kind::GaussianCubeVolume:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<GaussianCubeVolume>(frame));
        break;
      case SKStructure::Kind::dna:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<Protein>(frame));
        break;
      case SKStructure::Kind::dnaCrystal:
        iraspastructure = std::make_shared<iRASPAObject>(std::make_shared<ProteinCrystal>(frame));
        break;
      default:
        throw std::runtime_error("Unknown structure format");
      }

      if(std::shared_ptr<Structure> structure = std::dynamic_pointer_cast<Structure>(iraspastructure->object()))
      {
        if (frame->materialType == SKMaterialType::unspecified)
          frame->applyInferredMaterialType();
        const RKString materialName = SKMaterialTypeAPI::displayName(frame->materialType);
        if (auto editor = std::dynamic_pointer_cast<StructuralPropertyEditor>(structure))
          editor->setStructureMaterialType(materialName);
        else
          structure->setImportedStructureMaterialType(materialName);

        structure->setRepresentationStyle(Structure::RepresentationStyle::defaultStyle, colorSets);

        // Cocoa: suggested force field from inferred material (Aluminosilicate for zeolites).
        structure->setAtomForceFieldIdentifier(ForceFieldSets::suggestedDisplayName(materialName),
                                              forcefieldSets);

        structure->computeBonds();

        structure->reComputeBoundingBox();
        structure->recomputeDensityProperties();
      }

      applyImportedProteinRibbonDefaults(iraspastructure->object(), colorSets);

      iraspastructure->object()->reComputeBoundingBox();
      iraspastructures.push_back(iraspastructure);
    }
    // A file names its movies after itself, but the solvent is not another copy of the file: it is
    // the water the crystal was soaked in, and Cocoa calls it so.
    const bool solventMovie = !movieFrames.empty() &&
      std::all_of(movieFrames.begin(), movieFrames.end(),
                  [](const std::shared_ptr<SKStructure> &movieFrame)
                  { return movieFrame->kind == SKStructure::Kind::proteinCrystalSolvent; });
    const RKString baseName = path.stem().string().empty() ? RKString("Scene") : RKString(path.stem().string());
    std::shared_ptr<Movie> movie = Movie::create(solventMovie ? RKString("SOLVENT") : baseName, iraspastructures);
    if(solventMovie)
    {
      // Thousands of waters drawn over the protein hide what was opened to be looked at. The solvent
      // is read and kept, it just starts out switched off, the way Cocoa imports it.
      movie->setVisibility(false);
    }
    _movies.push_back(movie);
  }
}

std::optional<int> Scene::selectMovieIndex()
{
  std::vector<std::shared_ptr<Movie>>::const_iterator itr = std::find(_movies.begin(), _movies.end(), selectedMovie());
  if (itr != _movies.end())
  {
    int row = itr-_movies.begin();
    return row;
  }

  return std::nullopt;
}

std::optional<int> Scene::findChildIndex(std::shared_ptr<Movie> movie)
{
  std::vector<std::shared_ptr<Movie>>::const_iterator itr = std::find(_movies.begin(), _movies.end(), movie);
  if (itr != _movies.end())
  {
    int row = itr-_movies.begin();
    return row;
  }

  return std::nullopt;
}

bool Scene::removeChild(size_t row)
{
  if (row < 0 || row >= _movies.size())
     return false;

  _movies.erase(_movies.begin() + row);
  return true;
}

bool Scene::removeChildren(size_t position, size_t count)
{
  if (position < 0 || position + count > _movies.size())
    return false;
  std::vector<std::shared_ptr<Movie>>::iterator start = _movies.begin() + position;
  std::vector<std::shared_ptr<Movie>>::iterator end = _movies.begin() + position + count;
  _movies.erase(start, end);

  return true;
}

bool Scene::insertChild(size_t row, std::shared_ptr<Movie> child)
{
  if (row < 0 || row > _movies.size())
    return false;

  _movies.insert(_movies.begin() + row, child);
  child->setParent(this->shared_from_this());
  return true;
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::vector<std::shared_ptr<Movie>>& val)
{
  stream << static_cast<int32_t>(val.size());
  for(const std::shared_ptr<Movie>& singleVal : val)
    stream << singleVal;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::vector<std::shared_ptr<Movie>>& val)
{
  int32_t vecSize;
  val.clear();
  stream >> vecSize;
  val.reserve(vecSize);

  while(vecSize--)
  {
    std::shared_ptr<Movie> tempVal = std::make_shared<Movie>();
    stream >> tempVal;
    val.push_back(tempVal);
  }
  return stream;
}

BinaryArchive &operator<<(BinaryArchive &stream, const std::shared_ptr<Scene> &scene)
{
  stream << scene->_versionNumber;
  stream << scene->_displayName;
  stream << scene->_movies;
  return stream;
}

BinaryArchive &operator>>(BinaryArchive &stream, std::shared_ptr<Scene> &scene)
{
  int64_t versionNumber;
  stream >> versionNumber;

  if(versionNumber > scene->_versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "Scene");
  }
  stream >> scene->_displayName;
  stream >> scene->_movies;

  for(std::shared_ptr<Movie> movie : scene->_movies)
  {
    movie->setParent(scene);
  }

  return stream;
}
