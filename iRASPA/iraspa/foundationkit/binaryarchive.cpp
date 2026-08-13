/********************************************************************************************************************
    iRASPA: GPU-accelated visualisation software for materials scientists
    Copyright (c) 2016-2026 David Dubbeldam, Sofia Calero, Thijs J.H. Vlugt.
 ********************************************************************************************************************/

#include "binaryarchive.h"

#include <cstring>

namespace
{
  constexpr uint32_t kNullLength = 0xFFFFFFFFu;

#if !defined(__cpp_lib_byteswap)
  inline uint16_t swap16(uint16_t v)
  {
    return static_cast<uint16_t>((v << 8) | (v >> 8));
  }
  inline uint32_t swap32(uint32_t v)
  {
    return (v << 24) | ((v << 8) & 0x00FF0000u) | ((v >> 8) & 0x0000FF00u) | (v >> 24);
  }
  inline uint64_t swap64(uint64_t v)
  {
    return (static_cast<uint64_t>(swap32(static_cast<uint32_t>(v))) << 32) |
           swap32(static_cast<uint32_t>(v >> 32));
  }
#else
  using std::byteswap;
  inline uint16_t swap16(uint16_t v) { return byteswap(v); }
  inline uint32_t swap32(uint32_t v) { return byteswap(v); }
  inline uint64_t swap64(uint64_t v) { return byteswap(v); }
#endif

  inline bool hostIsLittleEndian()
  {
    const uint16_t x = 1;
    return *reinterpret_cast<const uint8_t *>(&x) == 1;
  }

  inline uint16_t toBe16(uint16_t v) { return hostIsLittleEndian() ? swap16(v) : v; }
  inline uint32_t toBe32(uint32_t v) { return hostIsLittleEndian() ? swap32(v) : v; }
  inline uint64_t toBe64(uint64_t v) { return hostIsLittleEndian() ? swap64(v) : v; }
  inline uint16_t fromBe16(uint16_t v) { return toBe16(v); }
  inline uint32_t fromBe32(uint32_t v) { return toBe32(v); }
  inline uint64_t fromBe64(uint64_t v) { return toBe64(v); }

  // UTF-8 -> UTF-16 code units (no surrogate pairs for BMP-only paths; handle surrogates).
  void utf8ToUtf16(const std::string &utf8, std::vector<uint16_t> &out)
  {
    out.clear();
    const auto *p = reinterpret_cast<const unsigned char *>(utf8.data());
    const auto *end = p + utf8.size();
    while (p < end)
    {
      uint32_t cp = 0;
      if (*p < 0x80)
      {
        cp = *p++;
      }
      else if ((*p & 0xE0) == 0xC0 && p + 1 < end)
      {
        cp = (*p++ & 0x1F) << 6;
        cp |= (*p++ & 0x3F);
      }
      else if ((*p & 0xF0) == 0xE0 && p + 2 < end)
      {
        cp = (*p++ & 0x0F) << 12;
        cp |= (*p++ & 0x3F) << 6;
        cp |= (*p++ & 0x3F);
      }
      else if ((*p & 0xF8) == 0xF0 && p + 3 < end)
      {
        cp = (*p++ & 0x07) << 18;
        cp |= (*p++ & 0x3F) << 12;
        cp |= (*p++ & 0x3F) << 6;
        cp |= (*p++ & 0x3F);
      }
      else
      {
        ++p;
        cp = 0xFFFD;
      }

      if (cp <= 0xFFFF)
      {
        out.push_back(static_cast<uint16_t>(cp));
      }
      else
      {
        cp -= 0x10000;
        out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
      }
    }
  }

  std::string utf16BeBytesToUtf8(const uint8_t *bytes, std::size_t byteCount)
  {
    std::string out;
    out.reserve(byteCount / 2);
    for (std::size_t i = 0; i + 1 < byteCount;)
    {
      uint16_t w1 = (static_cast<uint16_t>(bytes[i]) << 8) | bytes[i + 1];
      i += 2;
      uint32_t cp = w1;
      if (w1 >= 0xD800 && w1 <= 0xDBFF && i + 1 < byteCount)
      {
        uint16_t w2 = (static_cast<uint16_t>(bytes[i]) << 8) | bytes[i + 1];
        if (w2 >= 0xDC00 && w2 <= 0xDFFF)
        {
          i += 2;
          cp = 0x10000 + (((w1 - 0xD800) << 10) | (w2 - 0xDC00));
        }
      }

      if (cp < 0x80)
      {
        out.push_back(static_cast<char>(cp));
      }
      else if (cp < 0x800)
      {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
      else if (cp < 0x10000)
      {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
      else
      {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    }
    return out;
  }
}

BinaryArchive::BinaryArchive(std::vector<uint8_t> data)
    : _data(std::move(data))
{
}

BinaryArchive::BinaryArchive(const uint8_t *data, std::size_t size)
    : _data(data, data + size)
{
}

void BinaryArchive::setBuffer(std::vector<uint8_t> data)
{
  _data = std::move(data);
  _pos = 0;
  _status = Status::Ok;
}

void BinaryArchive::clear()
{
  _data.clear();
  _pos = 0;
  _status = Status::Ok;
}

void BinaryArchive::setPosition(std::size_t pos)
{
  _pos = pos;
}

void BinaryArchive::writeBytes(const void *src, std::size_t n)
{
  const auto *p = static_cast<const uint8_t *>(src);
  _data.insert(_data.end(), p, p + n);
}

void BinaryArchive::readBytes(void *dst, std::size_t n)
{
  if (_pos + n > _data.size())
  {
    _status = Status::ReadPastEnd;
    throw BinaryArchiveError("BinaryArchive: premature end of data");
  }
  std::memcpy(dst, _data.data() + _pos, n);
  _pos += n;
}

void BinaryArchive::writeBe16(uint16_t v)
{
  v = toBe16(v);
  writeBytes(&v, 2);
}

void BinaryArchive::writeBe32(uint32_t v)
{
  v = toBe32(v);
  writeBytes(&v, 4);
}

void BinaryArchive::writeBe64(uint64_t v)
{
  v = toBe64(v);
  writeBytes(&v, 8);
}

uint16_t BinaryArchive::readBe16()
{
  uint16_t v = 0;
  readBytes(&v, 2);
  return fromBe16(v);
}

uint32_t BinaryArchive::readBe32()
{
  uint32_t v = 0;
  readBytes(&v, 4);
  return fromBe32(v);
}

uint64_t BinaryArchive::readBe64()
{
  uint64_t v = 0;
  readBytes(&v, 8);
  return fromBe64(v);
}

BinaryArchive &BinaryArchive::operator<<(bool v)
{
  return *this << static_cast<uint8_t>(v ? 1 : 0);
}

BinaryArchive &BinaryArchive::operator<<(int8_t v)
{
  writeBytes(&v, 1);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(uint8_t v)
{
  writeBytes(&v, 1);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(int16_t v)
{
  writeBe16(static_cast<uint16_t>(v));
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(uint16_t v)
{
  writeBe16(v);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(int32_t v)
{
  writeBe32(static_cast<uint32_t>(v));
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(uint32_t v)
{
  writeBe32(v);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(int64_t v)
{
  writeBe64(static_cast<uint64_t>(v));
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(uint64_t v)
{
  writeBe64(v);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(float v)
{
  uint32_t bits = 0;
  static_assert(sizeof(float) == 4, "float must be 32-bit");
  std::memcpy(&bits, &v, 4);
  writeBe32(bits);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(double v)
{
  uint64_t bits = 0;
  static_assert(sizeof(double) == 8, "double must be 64-bit");
  std::memcpy(&bits, &v, 8);
  writeBe64(bits);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(const RKString &v)
{
  return *this << v.utf8();
}

BinaryArchive &BinaryArchive::operator<<(const std::string &v)
{
  if (v.empty())
  {
    writeBe32(kNullLength);
    return *this;
  }
  std::vector<uint16_t> utf16;
  utf8ToUtf16(v, utf16);
  writeBe32(static_cast<uint32_t>(utf16.size() * 2));
  for (uint16_t w : utf16)
    writeBe16(w);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(const RKColor &v)
{
  *this << static_cast<int8_t>(1);
  *this << static_cast<uint16_t>(v.alphaF() * 65535.0 + 0.5);
  *this << static_cast<uint16_t>(v.redF() * 65535.0 + 0.5);
  *this << static_cast<uint16_t>(v.greenF() * 65535.0 + 0.5);
  *this << static_cast<uint16_t>(v.blueF() * 65535.0 + 0.5);
  *this << static_cast<uint16_t>(0);
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(char16_t v)
{
  writeBe16(static_cast<uint16_t>(v));
  return *this;
}

BinaryArchive &BinaryArchive::operator<<(const std::vector<uint8_t> &v)
{
  if (v.empty())
  {
    writeBe32(kNullLength);
    return *this;
  }
  writeBe32(static_cast<uint32_t>(v.size()));
  writeBytes(v.data(), v.size());
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(bool &v)
{
  uint8_t b = 0;
  *this >> b;
  if (b != 0 && b != 1)
    throw BinaryArchiveError("BinaryArchive: bool out of range");
  v = (b != 0);
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(int8_t &v)
{
  readBytes(&v, 1);
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(uint8_t &v)
{
  readBytes(&v, 1);
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(int16_t &v)
{
  v = static_cast<int16_t>(readBe16());
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(uint16_t &v)
{
  v = readBe16();
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(int32_t &v)
{
  v = static_cast<int32_t>(readBe32());
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(uint32_t &v)
{
  v = readBe32();
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(int64_t &v)
{
  v = static_cast<int64_t>(readBe64());
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(uint64_t &v)
{
  v = readBe64();
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(float &v)
{
  uint32_t bits = readBe32();
  std::memcpy(&v, &bits, 4);
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(double &v)
{
  uint64_t bits = readBe64();
  std::memcpy(&v, &bits, 8);
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(RKString &v)
{
  std::string s;
  *this >> s;
  v = RKString(std::move(s));
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(std::string &v)
{
  const uint32_t count = readBe32();
  if (count == kNullLength)
  {
    v.clear();
    return *this;
  }
  std::vector<uint8_t> bytes(count);
  if (count)
    readBytes(bytes.data(), count);
  v = utf16BeBytesToUtf8(bytes.data(), bytes.size());
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(RKColor &v)
{
  int8_t spec = 0;
  uint16_t a = 0, r = 0, g = 0, b = 0, pad = 0;
  *this >> spec >> a >> r >> g >> b >> pad;
  v = RKColor(r / 65535.0, g / 65535.0, b / 65535.0, a / 65535.0);
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(char16_t &v)
{
  v = static_cast<char16_t>(readBe16());
  return *this;
}

BinaryArchive &BinaryArchive::operator>>(std::vector<uint8_t> &v)
{
  const uint32_t count = readBe32();
  v.clear();
  if (count == kNullLength)
    return *this;
  v.resize(count);
  if (count)
    readBytes(v.data(), count);
  return *this;
}

