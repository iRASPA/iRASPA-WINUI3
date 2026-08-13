/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2026 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.

    Big-endian binary archive matching Cocoa BinaryCodable / Qt QDataStream
    layouts used by .irspdoc documents.
 ********************************************************************************************************************/

#pragma once

#include "rkcolor.h"
#include "rkstring.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

class BinaryArchiveError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class BinaryArchive
{
public:
  enum class Status { Ok, ReadPastEnd, WriteError };

  BinaryArchive() = default;
  explicit BinaryArchive(std::vector<uint8_t> data);
  BinaryArchive(const uint8_t *data, std::size_t size);

  const std::vector<uint8_t> &buffer() const { return _data; }
  std::vector<uint8_t> takeBuffer() { return std::move(_data); }
  void setBuffer(std::vector<uint8_t> data);
  void clear();
  std::size_t size() const { return _data.size(); }
  std::size_t position() const { return _pos; }
  void setPosition(std::size_t pos);
  Status status() const { return _status; }
  bool atEnd() const { return _pos >= _data.size(); }

  // Primitives (big-endian), Cocoa/Qt compatible.
  BinaryArchive &operator<<(bool v);
  BinaryArchive &operator<<(int8_t v);
  BinaryArchive &operator<<(uint8_t v);
  BinaryArchive &operator<<(int16_t v);
  BinaryArchive &operator<<(uint16_t v);
  BinaryArchive &operator<<(int32_t v);
  BinaryArchive &operator<<(uint32_t v);
  BinaryArchive &operator<<(int64_t v);
  BinaryArchive &operator<<(uint64_t v);
  BinaryArchive &operator<<(float v);
  BinaryArchive &operator<<(double v);
  BinaryArchive &operator<<(const RKString &v);
  BinaryArchive &operator<<(const RKColor &v);
  BinaryArchive &operator<<(char16_t v);
  BinaryArchive &operator<<(const std::vector<uint8_t> &v);
  BinaryArchive &operator<<(const std::string &v); // as RKString wire format

  BinaryArchive &operator>>(bool &v);
  BinaryArchive &operator>>(int8_t &v);
  BinaryArchive &operator>>(uint8_t &v);
  BinaryArchive &operator>>(int16_t &v);
  BinaryArchive &operator>>(uint16_t &v);
  BinaryArchive &operator>>(int32_t &v);
  BinaryArchive &operator>>(uint32_t &v);
  BinaryArchive &operator>>(int64_t &v);
  BinaryArchive &operator>>(uint64_t &v);
  BinaryArchive &operator>>(float &v);
  BinaryArchive &operator>>(double &v);
  BinaryArchive &operator>>(RKString &v);
  BinaryArchive &operator>>(RKColor &v);
  BinaryArchive &operator>>(char16_t &v);
  BinaryArchive &operator>>(std::vector<uint8_t> &v);
  BinaryArchive &operator>>(std::string &v);

private:
  void writeBytes(const void *src, std::size_t n);
  void readBytes(void *dst, std::size_t n);
  void writeBe16(uint16_t v);
  void writeBe32(uint32_t v);
  void writeBe64(uint64_t v);
  uint16_t readBe16();
  uint32_t readBe32();
  uint64_t readBe64();

  std::vector<uint8_t> _data;
  std::size_t _pos{0};
  Status _status{Status::Ok};
};
