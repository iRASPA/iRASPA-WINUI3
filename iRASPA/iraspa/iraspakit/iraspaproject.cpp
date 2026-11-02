#include "zipreader.h"
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

#include "iraspaproject.h"
#include "rkstring.h"
#include "project.h"
#include "structuralpropertyviewer.h"
#include <iostream>
#include <lzma.h>

iRASPAProject::iRASPAProject(): _projectType(ProjectType::none),
  _fileNameUUID(generateUuidString()),
  _project(std::make_shared<Project>()),
  _nodeType(NodeType::leaf),
  _storageType(StorageType::local),
  _lazyStatus(LazyStatus::loaded),
  _undoStack()
{

}

iRASPAProject::iRASPAProject(std::shared_ptr<Project> project):
  _projectType(ProjectType::generic),
  _fileNameUUID(generateUuidString()),
  _project(project),
  _nodeType(NodeType::leaf),
  _storageType(StorageType::local),
  _lazyStatus(LazyStatus::loaded),
  _undoStack()
{

}

iRASPAProject::iRASPAProject(std::shared_ptr<ProjectStructure> project):
  _projectType(ProjectType::material),
  _fileNameUUID(generateUuidString()),
  _project(project),
  _nodeType(NodeType::leaf),
  _storageType(StorageType::local),
  _lazyStatus(LazyStatus::loaded),
  _undoStack()
{
  syncCachedPropertiesFromProject();
}

iRASPAProject::iRASPAProject(std::shared_ptr<ProjectGroup> project):
  _projectType(ProjectType::group),
  _fileNameUUID(generateUuidString()),
  _project(project),
  _nodeType(NodeType::group),
  _storageType(StorageType::local),
  _lazyStatus(LazyStatus::loaded),
  _undoStack()
{

}

void iRASPAProject::regenerateFileNameUUID()
{
  _fileNameUUID = generateUuidString();
}

void iRASPAProject::readData(ZipReader& reader)
{
  const RKByteArray bytes = reader.fileData(
      std::string("nl.darkwing.iRASPA_Project_") + _fileNameUUID.toStdString());
  _data = bytes;
}

void iRASPAProject::ensureDataLoaded()
{
  if(_data.empty() && !_archiveFileName.empty())
  {
    ZipReader reader(_archiveFileName);
    if(reader.exists() && reader.isReadable())
    {
      readData(reader);
    }
    reader.close();
  }
}

void iRASPAProject::unwrapIfNeeded(LogReporting *logReporter)
{
  if(_lazyStatus == LazyStatus::lazy)
  {
    ensureDataLoaded();
    const RKByteArray compressed(_data.begin(), _data.end());
    RKByteArray uncompressedData = ZipReader::xzUncompress(compressed);
    BinaryArchive stream(std::move(uncompressedData));

    try
    {
      switch(_projectType)
      {
      case ProjectType::none:
        break;
      case ProjectType::generic:
        stream >> _project;
        break;
      case ProjectType::group:
      {
        std::shared_ptr<ProjectGroup> groupNode = std::make_shared<ProjectGroup>();
        stream >> groupNode;
        _project = groupNode;
        stream >> _project;
        break;
      }
      case ProjectType::material:
      {
        std::shared_ptr<ProjectStructure> projectNode = std::make_shared<ProjectStructure>();
        stream >> projectNode;
        _project = projectNode;
        stream >> _project;
        syncCachedPropertiesFromProject();
        break;
      }
      case ProjectType::VASP:
        break;
      case ProjectType::RASPA:
        break;
      case ProjectType::GROMACS:
        break;
      case ProjectType::CP2K:
        break;
      case ProjectType::OPENMM:
        break;
      }

      _lazyStatus = LazyStatus::loaded;
    }
    catch (InvalidArchiveVersionException ex)
    {
      if(logReporter)
      {
        logReporter->logMessage(LogReporting::ErrorLevel::error,
            RKString("Error: %1 in file %2 function %3")
                .arg(RKString::fromStdString(ex.message()))
                .arg(RKString(ex.get_file()))
                .arg(RKString(ex.get_func())));
      }
      else
      {
        std::cout << "Error: " << ex.message() << std::endl;
        std::cout << ex.what() << ex.get_file() << std::endl;
        std::cout << "Function: " << ex.get_func() << std::endl;
      }
    }
    catch(InconsistentArchiveException ex)
    {
      if(logReporter)
      {
        logReporter->logMessage(LogReporting::ErrorLevel::error,
            RKString("Error: %1 in file %2 function %3")
                .arg(RKString::fromStdString(ex.message()))
                .arg(RKString(ex.get_file()))
                .arg(RKString(ex.get_func())));
      }
      else
      {
        std::cout << "Error: " << ex.message() << std::endl;
        std::cout << ex.what() << ex.get_file() << std::endl;
        std::cout << "Function: " << ex.get_func() << std::endl;
      }
    }
    catch(std::exception e)
    {
      if(logReporter)
      {
        logReporter->logMessage(LogReporting::ErrorLevel::error, "Error: " + RKString(e.what()));
      }
      else
      {
        std::cout << "Error: " << e.what() << std::endl;
      }
    }
  }
}

void iRASPAProject::syncCachedPropertiesFromProject()
{
  if(std::shared_ptr<ProjectStructure> projectStructure = std::dynamic_pointer_cast<ProjectStructure>(_project))
  {
    if(std::shared_ptr<SceneList> sceneList = projectStructure->sceneList())
    {
      for(const std::shared_ptr<Scene> &scene : sceneList->scenes())
      {
        if(!scene) continue;
        for(const std::shared_ptr<Movie> &movie : scene->movies())
        {
          if(!movie) continue;
          for(const std::shared_ptr<iRASPAObject> &frame : movie->frames())
          {
            if(!frame) continue;
            if(std::shared_ptr<StructuralPropertyViewer> viewer = std::dynamic_pointer_cast<StructuralPropertyViewer>(frame->object()))
            {
              _materialType = viewer->structureMaterialType();
              _mass = viewer->structureMass();
              _density = viewer->structureDensity();
              _heliumVoidFraction = viewer->structureHeliumVoidFraction();
              _specificVolume = viewer->structureSpecificVolume();
              _accessiblePoreVolume = viewer->structureAccessiblePoreVolume();
              _volumetricSurfaceArea = viewer->structureVolumetricNitrogenSurfaceArea();
              _gravimetricSurfaceArea = viewer->structureGravimetricNitrogenSurfaceArea();
              _numberOfChannelSystems = viewer->structureNumberOfChannelSystems();
              _numberOfInaccesiblePockets = viewer->structureNumberOfInaccessiblePockets();
              _dimensionalityPoreSystem = viewer->structureDimensionalityOfPoreSystem();
              _largestOverallCavityDiameter = viewer->structureLargestCavityDiameter();
              _restrictingPoreDiameter = viewer->structureRestrictingPoreLimitingDiameter();
              _largestDiameterAlongViablePath = viewer->structureLargestCavityDiameterAlongAViablePath();
              _cachedPropertiesLoaded = true;
              return;
            }
          }
        }
      }
    }
  }
}

void iRASPAProject::ensureCachedPropertiesLoaded()
{
  if(_cachedPropertiesLoaded)
  {
    return;
  }
  if(_projectType != ProjectType::material || _nodeType != NodeType::leaf)
  {
    _cachedPropertiesLoaded = true;
    return;
  }
  if(_lazyStatus == LazyStatus::loaded)
  {
    syncCachedPropertiesFromProject();
    _cachedPropertiesLoaded = true;
    return;
  }
  ensureDataLoaded();
  if(_data.empty())
  {
    _cachedPropertiesLoaded = true;
    return;
  }

  try
  {
    RKByteArray uncompressed = ZipReader::xzUncompress(_data);
    const uint8_t* buf = uncompressed.data();
    const size_t n = uncompressed.size();

    if(n >= 300)
    {
      // Scan for the first Structure's adsorption surface front/back material block (148 bytes).
      for(size_t pos = 0; pos + 260 < n; ++pos)
      {
        if(buf[pos + 9] != 1 || buf[pos + 20] != 1 || buf[pos + 31] != 1) continue;
        if(buf[pos + 83] != 1 || buf[pos + 94] != 1 || buf[pos + 105] != 1) continue;
        if(buf[pos] > 1 || buf[pos + 74] > 1) continue;
        if(buf[pos + 18] != 0 || buf[pos + 19] != 0 ||
           buf[pos + 29] != 0 || buf[pos + 30] != 0 ||
           buf[pos + 40] != 0 || buf[pos + 41] != 0) continue;
        if(buf[pos + 92] != 0 || buf[pos + 93] != 0 ||
           buf[pos + 103] != 0 || buf[pos + 104] != 0 ||
           buf[pos + 114] != 0 || buf[pos + 115] != 0) continue;

        try
        {
          const size_t propStart = pos + 148;
          if(buf[propStart + 8] != 0 || buf[propStart + 9] != 0 || buf[propStart + 10] != 0 || (buf[propStart + 11] & 1) != 0) continue;
          const size_t sliceLen = std::min<size_t>(n - propStart, 4096);
          RKByteArray slice(buf + propStart, buf + propStart + sliceLen);
          BinaryArchive stream(std::move(slice));

          int64_t structureType = -1;
          stream >> structureType;
          if(structureType < 0 || structureType > 20) continue;

          RKString matType;
          stream >> matType;
          std::string matStd = matType.toStdString();
          if(matStd.size() > 128) continue;
          bool validPrintable = true;
          for(unsigned char ch : matStd)
          {
            if(ch < 0x20 || ch > 0x7E) { validPrintable = false; break; }
          }
          if(!validPrintable) continue;

          double sMass = 0.0, sDensity = 0.0, sHelium = 0.0, sSpecVol = 0.0, sAccVol = 0.0, sVSA = 0.0, sGSA = 0.0;
          int64_t sChan = 0, sPock = 0, sDim = 0;
          double sLCD = 0.0, sPLD = 0.0, sDf = 0.0;

          stream >> sMass;
          stream >> sDensity;
          stream >> sHelium;
          stream >> sSpecVol;
          stream >> sAccVol;
          stream >> sVSA;
          stream >> sGSA;
          stream >> sChan;
          stream >> sPock;
          stream >> sDim;
          stream >> sLCD;
          stream >> sPLD;
          stream >> sDf;

          if(sChan < 0 || sChan > 10000 || sPock < 0 || sPock > 100000 || sDim < 0 || sDim > 3) continue;

          // Verify post-property creation fields (either v9+ directly, or pre-v9 after 10 author strings + date)
          auto checkCreationFields = [](BinaryArchive& s) -> bool {
            RKString cTemp, cPress;
            int64_t cTempScale = -1, cPressScale = -1, cMethod = -1, cRelax = -1;
            s >> cTemp >> cTempScale;
            if(cTempScale < 0 || cTempScale > 2) return false;
            s >> cPress >> cPressScale >> cMethod >> cRelax;
            return (cPressScale >= 0 && cPressScale <= 3) &&
                   (cMethod >= 0 && cMethod <= 10) &&
                   (cRelax >= 0 && cRelax <= 10);
          };

          const size_t afterPropsPos = stream.position();
          bool verified = false;
          try
          {
            verified = checkCreationFields(stream);
          }
          catch(...)
          {
            verified = false;
          }
          if(!verified)
          {
            try
            {
              stream.setPosition(afterPropsPos);
              for(int a = 0; a < 10; ++a)
              {
                RKString authorField;
                stream >> authorField;
              }
              uint16_t day = 0, month = 0;
              uint32_t year = 0;
              stream >> day >> month >> year;
              if(day <= 31 && month <= 12 && year <= 3000)
              {
                verified = checkCreationFields(stream);
              }
            }
            catch(...)
            {
              verified = false;
            }
          }
          if(!verified) continue;

          _materialType = matType;
          _mass = sMass;
          _density = sDensity;
          _heliumVoidFraction = sHelium;
          _specificVolume = sSpecVol;
          _accessiblePoreVolume = sAccVol;
          _volumetricSurfaceArea = sVSA;
          _gravimetricSurfaceArea = sGSA;
          _numberOfChannelSystems = sChan;
          _numberOfInaccesiblePockets = sPock;
          _dimensionalityPoreSystem = sDim;
          _largestOverallCavityDiameter = sLCD;
          _restrictingPoreDiameter = sPLD;
          _largestDiameterAlongViablePath = sDf;
          _cachedPropertiesLoaded = true;
          return;
        }
        catch(...)
        {
          continue;
        }
      }
    }
  }
  catch(...)
  {
  }

  _cachedPropertiesLoaded = true;
}

void iRASPAProject::saveData(ZipWriter& writer)
{
  const std::string entryName =
      std::string("nl.darkwing.iRASPA_Project_") + _fileNameUUID.toStdString();

  // Either branch below hands the zip an xz stream: compressed here, or carried through
  // untouched for a project still lazy. Deflating that a second time on the way into the
  // archive makes the entry very slightly larger and costs about a second per 40MB, so
  // the entry is stored instead. Both methods read back the same, and the Qt and Cocoa
  // versions store these entries too.
  const ZipWriter::CompressionPolicy previousPolicy = writer.compressionPolicy();
  writer.setCompressionPolicy(ZipWriter::NeverCompress);

  if(_lazyStatus == LazyStatus::lazy)
  {
    ensureDataLoaded();
    // lazy projects: write compressed data directly (stored in _data)
    writer.addFile(entryName, RKByteArray(_data.begin(), _data.end()));
  }
  else
  {
    BinaryArchive stream;

    switch(_projectType)
    {
    case ProjectType::none:
      break;
    case ProjectType::generic:
      stream << _project;
      break;
    case ProjectType::group:
      stream << std::dynamic_pointer_cast<ProjectGroup>(_project);
      stream << _project;
      break;
    case ProjectType::material:
      stream << std::dynamic_pointer_cast<ProjectStructure>(_project);
      stream << _project;
      break;
    case ProjectType::VASP:
      break;
    case ProjectType::RASPA:
      break;
    case ProjectType::GROMACS:
      break;
    case ProjectType::CP2K:
      break;
    case ProjectType::OPENMM:
      break;
    }

    RKByteArray compressedData = ZipWriter::xzCompress(stream.buffer());
    writer.addFile(entryName, compressedData);
  }

  writer.setCompressionPolicy(previousPolicy);
}

BinaryArchive &operator<<(BinaryArchive & stream, const std::shared_ptr<iRASPAProject>& node)
{
  stream << node->_versionNumber;
  stream << static_cast<typename std::underlying_type<iRASPAProject::ProjectType>::type>(node->_projectType);
  stream << node->_fileNameUUID;
  stream << static_cast<typename std::underlying_type<iRASPAProject::NodeType>::type>(node->_nodeType);
  stream << static_cast<typename std::underlying_type<iRASPAProject::StorageType>::type>(node->_storageType);
  stream << static_cast<typename std::underlying_type<iRASPAProject::LazyStatus>::type>(iRASPAProject::LazyStatus::lazy);

  if(node->_storageType == iRASPAProject::StorageType::publicCloud &&
     node->_projectType == iRASPAProject::ProjectType::material)
  {
    stream << node->_volumetricSurfaceArea;
    stream << node->_gravimetricSurfaceArea;
    stream << node->_heliumVoidFraction;
    stream << node->_largestOverallCavityDiameter;
    stream << node->_restrictingPoreDiameter;
    stream << node->_largestDiameterAlongViablePath;
    stream << node->_density;
    stream << node->_mass;
    stream << node->_specificVolume;
    stream << node->_accessiblePoreVolume;
    stream << node->_numberOfChannelSystems;
    stream << node->_numberOfInaccesiblePockets;
    stream << node->_dimensionalityPoreSystem;
    stream << node->_materialType;
  }

  return stream;
}

BinaryArchive &operator>>(BinaryArchive & stream, std::shared_ptr<iRASPAProject>& node)
{
  int64_t versionNumber;
  stream >> versionNumber;
  if(versionNumber > node->_versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "iRASPAProject");
  }
  int64_t projectType;
  stream >> projectType;
  node->_projectType = iRASPAProject::ProjectType(projectType);
  stream >> node->_fileNameUUID;

  int64_t nodeType;
  stream >> nodeType;
  node->_nodeType = iRASPAProject::NodeType(nodeType);
  int64_t storageType;
  stream >> storageType;
  node->_storageType = iRASPAProject::StorageType(storageType);
  int64_t lazyStatus;
  stream >> lazyStatus;
  node->_lazyStatus = iRASPAProject::LazyStatus(lazyStatus);

  if(node->_storageType == iRASPAProject::StorageType::publicCloud &&
     node->_projectType == iRASPAProject::ProjectType::material)
  {
    stream >> node->_volumetricSurfaceArea;
    stream >> node->_gravimetricSurfaceArea;
    stream >> node->_heliumVoidFraction;
    stream >> node->_largestOverallCavityDiameter;
    stream >> node->_restrictingPoreDiameter;
    stream >> node->_largestDiameterAlongViablePath;
    stream >> node->_density;
    stream >> node->_mass;
    stream >> node->_specificVolume;
    stream >> node->_accessiblePoreVolume;
    stream >> node->_numberOfChannelSystems;
    stream >> node->_numberOfInaccesiblePockets;
    stream >> node->_dimensionalityPoreSystem;
    stream >> node->_materialType;
    node->_cachedPropertiesLoaded = true;
  }

  return stream;
}

BinaryArchive &operator<<=(BinaryArchive & stream, const std::shared_ptr<iRASPAProject>& node)
{
  node->unwrapIfNeeded(nullptr);

  stream << node->_versionNumber;
  stream << static_cast<typename std::underlying_type<iRASPAProject::ProjectType>::type>(node->_projectType);
  stream << node->_fileNameUUID;
  stream << static_cast<typename std::underlying_type<iRASPAProject::NodeType>::type>(node->_nodeType);
  stream << static_cast<typename std::underlying_type<iRASPAProject::StorageType>::type>(node->_storageType);
  stream << static_cast<typename std::underlying_type<iRASPAProject::LazyStatus>::type>(iRASPAProject::LazyStatus::loaded);

  switch(node->_projectType)
  {
  case iRASPAProject::ProjectType::none:
    break;
  case iRASPAProject::ProjectType::generic:
    stream << node->_project;
    break;
  case iRASPAProject::ProjectType::group:
    stream << std::dynamic_pointer_cast<ProjectGroup>(node->_project);
    stream << node->_project;
    break;
  case iRASPAProject::ProjectType::material:
    stream << std::dynamic_pointer_cast<ProjectStructure>(node->_project);
    stream << node->_project;
    break;
  case iRASPAProject::ProjectType::VASP:
    break;
  case iRASPAProject::ProjectType::RASPA:
    break;
  case iRASPAProject::ProjectType::GROMACS:
    break;
  case iRASPAProject::ProjectType::CP2K:
    break;
  case iRASPAProject::ProjectType::OPENMM:
    break;
  }

  return stream;
}

BinaryArchive &operator>>=(BinaryArchive & stream, std::shared_ptr<iRASPAProject>& node)
{
  int64_t versionNumber;
  stream >> versionNumber;
  if(versionNumber > node->_versionNumber)
  {
    throw InvalidArchiveVersionException(__FILE__, __LINE__, "iRASPAProject");
  }
  int64_t projectType;
  stream >> projectType;
  node->_projectType = iRASPAProject::ProjectType(projectType);
  stream >> node->_fileNameUUID;

  int64_t nodeType;
  stream >> nodeType;
  node->_nodeType = iRASPAProject::NodeType(nodeType);
  int64_t storageType;
  stream >> storageType;
  node->_storageType = iRASPAProject::StorageType(storageType);
  int64_t lazyStatus;
  stream >> lazyStatus;
  node->_lazyStatus = iRASPAProject::LazyStatus(lazyStatus);

  try
  {
  switch(node->_projectType)
  {
  case iRASPAProject::ProjectType::none:
    break;
  case iRASPAProject::ProjectType::generic:
    stream >> node->_project;
    break;
  case iRASPAProject::ProjectType::group:
  {
    std::shared_ptr<ProjectGroup> groupNode = std::make_shared<ProjectGroup>();
    stream >> groupNode;
    node->_project = groupNode;
    stream >> node->_project;
    break;
  }
  case iRASPAProject::ProjectType::material:
  {
    std::shared_ptr<ProjectStructure> projectNode = std::make_shared<ProjectStructure>();
    stream >> projectNode;
    node->_project = projectNode;
    stream >> node->_project;
    node->syncCachedPropertiesFromProject();
    break;
  }
  case iRASPAProject::ProjectType::VASP:
    break;
  case iRASPAProject::ProjectType::RASPA:
    break;
  case iRASPAProject::ProjectType::GROMACS:
    break;
  case iRASPAProject::ProjectType::CP2K:
    break;
  case iRASPAProject::ProjectType::OPENMM:
    break;
  }
    node->_lazyStatus = iRASPAProject::LazyStatus::loaded;
  }
  catch (InvalidArchiveVersionException ex)
  {
    std::cout << "Error: " << ex.message() << std::endl;
    std::cout << ex.what() << ex.get_file() << std::endl;
    std::cout << "Function: " << ex.get_func() << std::endl;
  }
  catch(InconsistentArchiveException ex)
  {
    std::cout << "Error: " << ex.message() << std::endl;
    std::cout << ex.what() << ex.get_file() << std::endl;
    std::cout << "Function: " << ex.get_func() << std::endl;
  }
  catch(std::exception e)
  {
    std::cout << "Error: " << e.what() << std::endl;
  }

  std::cerr << "correctly read!";

  return stream;
}
