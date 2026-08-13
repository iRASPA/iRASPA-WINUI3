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

void iRASPAProject::readData(ZipReader& reader)
{
  const RKByteArray bytes = reader.fileData(
      std::string("nl.darkwing.iRASPA_Project_") + _fileNameUUID.toStdString());
  _data = bytes;
}

void iRASPAProject::unwrapIfNeeded(LogReporting *logReporter)
{
  if(_lazyStatus == LazyStatus::lazy)
  {
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
