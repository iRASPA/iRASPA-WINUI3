/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Qt-derived ZipReader/ZipWriter, rewritten Qt-free for iRASPA FoundationKit.
**
** $QT_BEGIN_LICENSE:LGPL$
** ...
** $QT_END_LICENSE$
**
****************************************************************************/

#define LZMA_API_STATIC

#include "zipreader.h"
#include "zipwriter.h"

#include <lzma.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if !defined(S_IFREG)
#  define S_IFREG 0100000
#endif
#if !defined(S_IFDIR)
#  define S_IFDIR 0040000
#endif
#if !defined(S_IFLNK)
#  define S_IFLNK 0120000
#endif
#if !defined(S_ISDIR)
#  define S_ISDIR(x) (((x) & 0170000) == S_IFDIR)
#endif
#if !defined(S_ISREG)
#  define S_ISREG(x) (((x) & 0170000) == S_IFREG)
#endif
#if !defined(S_ISLNK)
#  define S_ISLNK(x) (((x) & 0170000) == S_IFLNK)
#endif
#if !defined(S_IRUSR)
#  define S_IRUSR 0400
#  define S_IWUSR 0200
#  define S_IXUSR 0100
#  define S_IRGRP 0040
#  define S_IWGRP 0020
#  define S_IXGRP 0010
#  define S_IROTH 0004
#  define S_IWOTH 0002
#  define S_IXOTH 0001
#endif

namespace {

using ByteVec = std::vector<uint8_t>;

inline uint32_t readUInt(const uint8_t *data)
{
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

inline uint16_t readUShort(const uint8_t *data)
{
    return uint16_t(data[0]) | (uint16_t(data[1]) << 8);
}

inline void writeUInt(uint8_t *data, uint32_t i)
{
    data[0] = uint8_t(i & 0xff);
    data[1] = uint8_t((i >> 8) & 0xff);
    data[2] = uint8_t((i >> 16) & 0xff);
    data[3] = uint8_t((i >> 24) & 0xff);
}

inline void writeUShort(uint8_t *data, uint16_t i)
{
    data[0] = uint8_t(i & 0xff);
    data[1] = uint8_t((i >> 8) & 0xff);
}

inline void copyUInt(uint8_t *dest, const uint8_t *src)
{
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
    dest[3] = src[3];
}

inline void copyUShort(uint8_t *dest, const uint8_t *src)
{
    dest[0] = src[0];
    dest[1] = src[1];
}

void writeMSDosDate(uint8_t *dest, std::time_t t)
{
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    const uint16_t time =
        (uint16_t(tmBuf.tm_hour) << 11) |
        (uint16_t(tmBuf.tm_min) << 5) |
        (uint16_t(tmBuf.tm_sec) >> 1);
    const uint16_t date =
        (uint16_t(tmBuf.tm_year - 80) << 9) |
        (uint16_t(tmBuf.tm_mon + 1) << 5) |
        uint16_t(tmBuf.tm_mday);
    dest[0] = uint8_t(time & 0xff);
    dest[1] = uint8_t(time >> 8);
    dest[2] = uint8_t(date & 0xff);
    dest[3] = uint8_t(date >> 8);
}

int inflateRaw(uint8_t *dest, unsigned long *destLen, const uint8_t *source, unsigned long sourceLen)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(source));
    stream.avail_in = uInt(sourceLen);
    if (uLong(stream.avail_in) != sourceLen)
        return Z_BUF_ERROR;

    stream.next_out = reinterpret_cast<Bytef *>(dest);
    stream.avail_out = uInt(*destLen);
    if (uLong(stream.avail_out) != *destLen)
        return Z_BUF_ERROR;

    int err = inflateInit2(&stream, -MAX_WBITS);
    if (err != Z_OK)
        return err;

    err = inflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END)
    {
        inflateEnd(&stream);
        if (err == Z_NEED_DICT || (err == Z_BUF_ERROR && stream.avail_in == 0))
            return Z_DATA_ERROR;
        return err;
    }
    *destLen = stream.total_out;
    return inflateEnd(&stream);
}

int deflateRaw(uint8_t *dest, unsigned long *destLen, const uint8_t *source, unsigned long sourceLen)
{
    z_stream stream{};
    stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(source));
    stream.avail_in = uInt(sourceLen);
    stream.next_out = reinterpret_cast<Bytef *>(dest);
    stream.avail_out = uInt(*destLen);
    if (uLong(stream.avail_out) != *destLen)
        return Z_BUF_ERROR;

    int err = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    if (err != Z_OK)
        return err;

    err = deflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END)
    {
        deflateEnd(&stream);
        return err == Z_OK ? Z_BUF_ERROR : err;
    }
    *destLen = stream.total_out;
    return deflateEnd(&stream);
}

FILE *openFileUtf8(const std::string &path, const char *mode)
{
#if defined(_WIN32)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0)
        return nullptr;
    std::wstring wpath(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    std::wstring wmode;
    for (const char *p = mode; *p; ++p)
        wmode.push_back(static_cast<wchar_t>(*p));

    FILE *f = nullptr;
    if (_wfopen_s(&f, wpath.c_str(), wmode.c_str()) != 0)
        return nullptr;
    return f;
#else
    return std::fopen(path.c_str(), mode);
#endif
}

bool fileExistsUtf8(const std::string &path)
{
#if defined(_WIN32)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0)
        return false;
    std::wstring wpath(static_cast<size_t>(wlen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    const DWORD attrs = GetFileAttributesW(wpath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
#else
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fclose(f);
    return true;
#endif
}

std::string fromNativeSeparators(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

struct LocalFileHeader
{
    uint8_t signature[4];
    uint8_t version_needed[2];
    uint8_t general_purpose_bits[2];
    uint8_t compression_method[2];
    uint8_t last_mod_file[4];
    uint8_t crc_32[4];
    uint8_t compressed_size[4];
    uint8_t uncompressed_size[4];
    uint8_t file_name_length[2];
    uint8_t extra_field_length[2];
};

struct CentralFileHeader
{
    uint8_t signature[4];
    uint8_t version_made[2];
    uint8_t version_needed[2];
    uint8_t general_purpose_bits[2];
    uint8_t compression_method[2];
    uint8_t last_mod_file[4];
    uint8_t crc_32[4];
    uint8_t compressed_size[4];
    uint8_t uncompressed_size[4];
    uint8_t file_name_length[2];
    uint8_t extra_field_length[2];
    uint8_t file_comment_length[2];
    uint8_t disk_start[2];
    uint8_t internal_file_attributes[2];
    uint8_t external_file_attributes[4];
    uint8_t offset_local_header[4];

    LocalFileHeader toLocalHeader() const
    {
        LocalFileHeader h{};
        writeUInt(h.signature, 0x04034b50u);
        copyUShort(h.version_needed, version_needed);
        copyUShort(h.general_purpose_bits, general_purpose_bits);
        copyUShort(h.compression_method, compression_method);
        copyUInt(h.last_mod_file, last_mod_file);
        copyUInt(h.crc_32, crc_32);
        copyUInt(h.compressed_size, compressed_size);
        copyUInt(h.uncompressed_size, uncompressed_size);
        copyUShort(h.file_name_length, file_name_length);
        copyUShort(h.extra_field_length, extra_field_length);
        return h;
    }
};

struct EndOfDirectory
{
    uint8_t signature[4];
    uint8_t this_disk[2];
    uint8_t start_of_directory_disk[2];
    uint8_t num_dir_entries_this_disk[2];
    uint8_t num_dir_entries[2];
    uint8_t directory_size[4];
    uint8_t dir_start_offset[4];
    uint8_t comment_length[2];
};

struct FileHeader
{
    CentralFileHeader h{};
    ByteVec file_name;
    ByteVec extra_field;
    ByteVec file_comment;
};

class ZipFileIO
{
public:
    ZipFileIO() = default;
    ~ZipFileIO() { close(); }

    ZipFileIO(const ZipFileIO &) = delete;
    ZipFileIO &operator=(const ZipFileIO &) = delete;

    bool open(const std::string &path, const char *mode)
    {
        close();
        path_ = path;
        file_ = openFileUtf8(path, mode);
        return file_ != nullptr;
    }

    void close()
    {
        if (file_)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    bool isOpen() const { return file_ != nullptr; }

    bool seek(int64_t pos)
    {
        if (!file_)
            return false;
#if defined(_WIN32)
        return _fseeki64(file_, pos, SEEK_SET) == 0;
#else
        return fseeko(file_, static_cast<off_t>(pos), SEEK_SET) == 0;
#endif
    }

    int64_t pos() const
    {
        if (!file_)
            return -1;
#if defined(_WIN32)
        return _ftelli64(file_);
#else
        return static_cast<int64_t>(ftello(file_));
#endif
    }

    int64_t size()
    {
        if (!file_)
            return -1;
        const int64_t cur = pos();
#if defined(_WIN32)
        if (_fseeki64(file_, 0, SEEK_END) != 0)
            return -1;
        const int64_t end = _ftelli64(file_);
#else
        if (fseeko(file_, 0, SEEK_END) != 0)
            return -1;
        const int64_t end = static_cast<int64_t>(ftello(file_));
#endif
        seek(cur);
        return end;
    }

    size_t read(void *buf, size_t n)
    {
        if (!file_)
            return 0;
        return std::fread(buf, 1, n, file_);
    }

    ByteVec readBytes(size_t n)
    {
        ByteVec out(n);
        const size_t got = read(out.data(), n);
        out.resize(got);
        return out;
    }

    bool write(const void *buf, size_t n)
    {
        if (!file_)
            return false;
        return std::fwrite(buf, 1, n, file_) == n;
    }

    bool write(const ByteVec &data) { return write(data.data(), data.size()); }

    const std::string &path() const { return path_; }

private:
    FILE *file_{nullptr};
    std::string path_;
};

} // namespace

ZipReader::FileInfo::FileInfo()
    : isDir(0), isFile(0), isSymLink(0), permissions(0), crc_32(0), size(0), lastModifiedDosDate(0), d(nullptr)
{
}

ZipReader::FileInfo::~FileInfo() = default;

ZipReader::FileInfo::FileInfo(const FileInfo &other)
{
    *this = other;
}

ZipReader::FileInfo &ZipReader::FileInfo::operator=(const FileInfo &other)
{
    filePath = other.filePath;
    isDir = other.isDir;
    isFile = other.isFile;
    isSymLink = other.isSymLink;
    permissions = other.permissions;
    crc_32 = other.crc_32;
    size = other.size;
    lastModifiedDosDate = other.lastModifiedDosDate;
    d = other.d;
    return *this;
}

bool ZipReader::FileInfo::isValid() const
{
    return isDir || isFile || isSymLink;
}

class ZipPrivate
{
public:
    explicit ZipPrivate(bool ownDev)
        : ownDevice(ownDev)
    {
    }

    void fillFileInfo(int index, ZipReader::FileInfo &fileInfo) const
    {
        const FileHeader &header = fileHeaders[static_cast<size_t>(index)];
        fileInfo.filePath.assign(reinterpret_cast<const char *>(header.file_name.data()), header.file_name.size());
        const uint32_t mode = (readUInt(header.h.external_file_attributes) >> 16) & 0xFFFFu;
        fileInfo.isDir = S_ISDIR(mode) ? 1u : 0u;
        fileInfo.isFile = S_ISREG(mode) ? 1u : 0u;
        fileInfo.isSymLink = S_ISLNK(mode) ? 1u : 0u;
        fileInfo.permissions = mode & 0777u;
        fileInfo.crc_32 = readUInt(header.h.crc_32);
        fileInfo.size = readUInt(header.h.uncompressed_size);
        fileInfo.lastModifiedDosDate = readUInt(header.h.last_mod_file);
    }

    ZipFileIO device;
    bool ownDevice;
    bool dirtyFileTree{true};
    std::vector<FileHeader> fileHeaders;
    ByteVec comment;
    uint32_t start_of_directory{0};
};

class ZipReaderPrivate : public ZipPrivate
{
public:
    ZipReaderPrivate()
        : ZipPrivate(true), status(ZipReader::NoError)
    {
    }

    void scanFiles();

    ZipReader::Status status;
};

class ZipWriterPrivate : public ZipPrivate
{
public:
    ZipWriterPrivate()
        : ZipPrivate(true),
          status(ZipWriter::NoError),
          permissions(S_IRUSR | S_IWUSR),
          compressionPolicy(ZipWriter::AlwaysCompress)
    {
    }

    enum EntryType { Directory, File, Symlink };

    void addEntry(EntryType type, const std::string &fileName, const ByteVec &contents);

    ZipWriter::Status status;
    uint32_t permissions;
    ZipWriter::CompressionPolicy compressionPolicy;
};

void ZipReaderPrivate::scanFiles()
{
    if (!dirtyFileTree)
        return;

    if (!device.isOpen())
    {
        status = ZipReader::FileOpenError;
        return;
    }

    dirtyFileTree = false;
    fileHeaders.clear();

    uint8_t tmp[4];
    if (device.read(tmp, 4) != 4 || readUInt(tmp) != 0x04034b50u)
        return;

    int i = 0;
    int startDir = -1;
    EndOfDirectory eod{};
    while (startDir == -1)
    {
        const int64_t fileSize = device.size();
        const int64_t pos = fileSize - static_cast<int64_t>(sizeof(EndOfDirectory)) - i;
        if (pos < 0 || i > 65535)
            return;

        if (!device.seek(pos))
            return;
        if (device.read(&eod, sizeof(EndOfDirectory)) != sizeof(EndOfDirectory))
            return;
        if (readUInt(eod.signature) == 0x06054b50u)
            break;
        ++i;
    }

    startDir = static_cast<int>(readUInt(eod.dir_start_offset));
    const int num_dir_entries = readUShort(eod.num_dir_entries);
    const int comment_length = readUShort(eod.comment_length);
    comment = device.readBytes(static_cast<size_t>(std::min(comment_length, i)));

    if (!device.seek(startDir))
        return;

    for (i = 0; i < num_dir_entries; ++i)
    {
        FileHeader header;
        const size_t read = device.read(&header.h, sizeof(CentralFileHeader));
        if (read < sizeof(CentralFileHeader))
            break;
        if (readUInt(header.h.signature) != 0x02014b50u)
            break;

        int l = readUShort(header.h.file_name_length);
        header.file_name = device.readBytes(static_cast<size_t>(l));
        if (static_cast<int>(header.file_name.size()) != l)
            break;
        l = readUShort(header.h.extra_field_length);
        header.extra_field = device.readBytes(static_cast<size_t>(l));
        if (static_cast<int>(header.extra_field.size()) != l)
            break;
        l = readUShort(header.h.file_comment_length);
        header.file_comment = device.readBytes(static_cast<size_t>(l));
        if (static_cast<int>(header.file_comment.size()) != l)
            break;

        fileHeaders.push_back(std::move(header));
    }

    start_of_directory = static_cast<uint32_t>(startDir);
}

void ZipWriterPrivate::addEntry(EntryType type, const std::string &fileName, const ByteVec &contents)
{
    if (!device.isOpen())
    {
        status = ZipWriter::FileOpenError;
        return;
    }
    if (!device.seek(start_of_directory))
    {
        status = ZipWriter::FileWriteError;
        return;
    }

    ZipWriter::CompressionPolicy compression = compressionPolicy;
    if (compressionPolicy == ZipWriter::AutoCompress)
    {
        if (contents.size() < 64)
            compression = ZipWriter::NeverCompress;
        else
            compression = ZipWriter::AlwaysCompress;
    }

    FileHeader header;
    std::memset(&header.h, 0, sizeof(CentralFileHeader));
    writeUInt(header.h.signature, 0x02014b50u);
    writeUShort(header.h.version_needed, 0x14);
    writeUInt(header.h.uncompressed_size, static_cast<uint32_t>(contents.size()));
    writeMSDosDate(header.h.last_mod_file, std::time(nullptr));

    ByteVec data = contents;
    if (compression == ZipWriter::AlwaysCompress)
    {
        writeUShort(header.h.compression_method, 8);
        unsigned long len = static_cast<unsigned long>(contents.size());
        len += (len >> 12) + (len >> 14) + 11;
        int res;
        do
        {
            data.resize(len);
            res = deflateRaw(data.data(), &len, contents.data(), static_cast<unsigned long>(contents.size()));
            switch (res)
            {
            case Z_OK:
                data.resize(len);
                break;
            case Z_MEM_ERROR:
                data.clear();
                break;
            case Z_BUF_ERROR:
                len *= 2;
                break;
            default:
                data.clear();
                break;
            }
        } while (res == Z_BUF_ERROR);
    }

    writeUInt(header.h.compressed_size, static_cast<uint32_t>(data.size()));
    uint32_t crc = static_cast<uint32_t>(::crc32(0L, Z_NULL, 0));
    if (!contents.empty())
        crc = static_cast<uint32_t>(::crc32(crc, contents.data(), static_cast<uInt>(contents.size())));
    writeUInt(header.h.crc_32, crc);

    header.file_name.assign(fileName.begin(), fileName.end());
    if (header.file_name.size() > 0xffff)
        header.file_name.resize(0xffff);
    writeUShort(header.h.file_name_length, static_cast<uint16_t>(header.file_name.size()));

    writeUShort(header.h.version_made, 3 << 8);
    uint32_t mode = permissions;
    switch (type)
    {
    case File:
        mode |= S_IFREG;
        break;
    case Directory:
        mode |= S_IFDIR;
        break;
    case Symlink:
        mode |= S_IFLNK;
        break;
    }
    writeUInt(header.h.external_file_attributes, mode << 16);
    writeUInt(header.h.offset_local_header, start_of_directory);

    fileHeaders.push_back(header);

    const LocalFileHeader h = header.h.toLocalHeader();
    if (!device.write(&h, sizeof(LocalFileHeader)) ||
        !device.write(header.file_name) ||
        !device.write(data))
    {
        status = ZipWriter::FileWriteError;
        return;
    }
    start_of_directory = static_cast<uint32_t>(device.pos());
    dirtyFileTree = true;
}

//////////////////////////////  Reader

ZipReader::ZipReader(const std::string &archive)
    : d(new ZipReaderPrivate)
{
    if (!d->device.open(archive, "rb"))
    {
        d->status = FileOpenError;
        return;
    }
    d->status = NoError;
}

ZipReader::~ZipReader()
{
    close();
    delete d;
}

bool ZipReader::isReadable() const
{
    return d->device.isOpen();
}

bool ZipReader::exists() const
{
    return fileExistsUtf8(d->device.path());
}

std::vector<ZipReader::FileInfo> ZipReader::fileInfoList() const
{
    d->scanFiles();
    std::vector<FileInfo> files;
    files.reserve(d->fileHeaders.size());
    for (size_t i = 0; i < d->fileHeaders.size(); ++i)
    {
        FileInfo fi;
        d->fillFileInfo(static_cast<int>(i), fi);
        files.push_back(fi);
    }
    return files;
}

int ZipReader::count() const
{
    d->scanFiles();
    return static_cast<int>(d->fileHeaders.size());
}

ZipReader::FileInfo ZipReader::entryInfoAt(int index) const
{
    d->scanFiles();
    FileInfo fi;
    if (index >= 0 && index < static_cast<int>(d->fileHeaders.size()))
        d->fillFileInfo(index, fi);
    return fi;
}

RKByteArray ZipReader::fileData(const std::string &fileName) const
{
    d->scanFiles();
    size_t i = 0;
    for (; i < d->fileHeaders.size(); ++i)
    {
        const auto &name = d->fileHeaders[i].file_name;
        if (name.size() == fileName.size() &&
            std::memcmp(name.data(), fileName.data(), fileName.size()) == 0)
            break;
    }
    if (i == d->fileHeaders.size())
        return {};

    const FileHeader header = d->fileHeaders[i];

    const int compressed_size = static_cast<int>(readUInt(header.h.compressed_size));
    const int uncompressed_size = static_cast<int>(readUInt(header.h.uncompressed_size));
    const int start = static_cast<int>(readUInt(header.h.offset_local_header));

    if (!d->device.seek(start))
        return {};

    LocalFileHeader lh{};
    if (d->device.read(&lh, sizeof(LocalFileHeader)) != sizeof(LocalFileHeader))
        return {};

    const uint32_t skip = uint32_t(readUShort(lh.file_name_length)) + uint32_t(readUShort(lh.extra_field_length));
    if (!d->device.seek(d->device.pos() + skip))
        return {};

    const int compression_method = readUShort(lh.compression_method);
    ByteVec compressed = d->device.readBytes(static_cast<size_t>(compressed_size));

    if (compression_method == 0)
    {
        if (static_cast<int>(compressed.size()) > uncompressed_size)
            compressed.resize(static_cast<size_t>(uncompressed_size));
        return compressed;
    }
    if (compression_method == 8)
    {
        if (static_cast<int>(compressed.size()) > compressed_size)
            compressed.resize(static_cast<size_t>(compressed_size));

        ByteVec baunzip;
        unsigned long len = static_cast<unsigned long>(std::max(uncompressed_size, 1));
        int res;
        do
        {
            baunzip.resize(len);
            res = inflateRaw(baunzip.data(), &len, compressed.data(), static_cast<unsigned long>(compressed_size));
            switch (res)
            {
            case Z_OK:
                baunzip.resize(len);
                break;
            case Z_BUF_ERROR:
                len *= 2;
                break;
            default:
                return {};
            }
        } while (res == Z_BUF_ERROR);
        return baunzip;
    }
    return {};
}

ZipReader::Status ZipReader::status() const
{
    return d->status;
}

void ZipReader::close()
{
    d->device.close();
}

////////////////////////////// Writer

ZipWriter::ZipWriter(const std::string &fileName)
    : d(new ZipWriterPrivate)
{
    if (!d->device.open(fileName, "w+b"))
    {
        d->status = FileOpenError;
        return;
    }
    d->status = NoError;
}

ZipWriter::~ZipWriter()
{
    close();
    delete d;
}

bool ZipWriter::isWritable() const
{
    return d->device.isOpen();
}

bool ZipWriter::exists() const
{
    return fileExistsUtf8(d->device.path());
}

ZipWriter::Status ZipWriter::status() const
{
    return d->status;
}

void ZipWriter::setCompressionPolicy(CompressionPolicy policy)
{
    d->compressionPolicy = policy;
}

ZipWriter::CompressionPolicy ZipWriter::compressionPolicy() const
{
    return d->compressionPolicy;
}

void ZipWriter::setCreationPermissions(uint32_t permissions)
{
    d->permissions = permissions;
}

uint32_t ZipWriter::creationPermissions() const
{
    return d->permissions;
}

void ZipWriter::addFile(const std::string &fileName, const RKByteArray &data)
{
    d->addEntry(ZipWriterPrivate::File, fromNativeSeparators(fileName), data);
}

void ZipWriter::addDirectory(const std::string &dirName)
{
    std::string name = fromNativeSeparators(dirName);
    if (name.empty() || name.back() != '/')
        name.push_back('/');
    d->addEntry(ZipWriterPrivate::Directory, name, {});
}

void ZipWriter::addSymLink(const std::string &fileName, const std::string &destination)
{
    ByteVec dest(destination.begin(), destination.end());
    d->addEntry(ZipWriterPrivate::Symlink, fromNativeSeparators(fileName), dest);
}

void ZipWriter::close()
{
    if (!d->device.isOpen())
        return;

    if (!d->device.seek(d->start_of_directory))
    {
        d->device.close();
        return;
    }

    for (const FileHeader &header : d->fileHeaders)
    {
        if (!d->device.write(&header.h, sizeof(CentralFileHeader)) ||
            !d->device.write(header.file_name) ||
            !d->device.write(header.extra_field) ||
            !d->device.write(header.file_comment))
        {
            d->status = FileWriteError;
            d->device.close();
            return;
        }
    }

    const int64_t dir_size = d->device.pos() - static_cast<int64_t>(d->start_of_directory);
    EndOfDirectory eod{};
    std::memset(&eod, 0, sizeof(EndOfDirectory));
    writeUInt(eod.signature, 0x06054b50u);
    writeUShort(eod.num_dir_entries_this_disk, static_cast<uint16_t>(d->fileHeaders.size()));
    writeUShort(eod.num_dir_entries, static_cast<uint16_t>(d->fileHeaders.size()));
    writeUInt(eod.directory_size, static_cast<uint32_t>(dir_size));
    writeUInt(eod.dir_start_offset, d->start_of_directory);
    writeUShort(eod.comment_length, static_cast<uint16_t>(d->comment.size()));

    if (!d->device.write(&eod, sizeof(EndOfDirectory)) || !d->device.write(d->comment))
        d->status = FileWriteError;

    d->device.close();
}

/* xz codec chunk size (heap-allocated in compress/uncompress) */
#define OUT_BUF_MAX 409600

extern "C" void *lz_alloc(void *opaque, size_t nmemb, size_t size)
{
    (void)opaque;
    (void)nmemb;
    try
    {
        return new char[size];
    }
    catch (const std::bad_alloc &)
    {
        return nullptr;
    }
}

extern "C" void lz_free(void *opaque, void *ptr)
{
    (void)opaque;
    delete[] static_cast<char *>(ptr);
}

RKByteArray ZipWriter::xzCompress(const RKByteArray &data)
{
    RKByteArray arr;
    const lzma_check check = LZMA_CHECK_CRC64;
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_allocator al;
    al.alloc = lz_alloc;
    al.free = lz_free;
    strm.allocator = &al;
    // Heap buffer: OUT_BUF_MAX is ~400KB — must not live on the stack (thread-pool /
    // deep UI call chains overflow easily; was a gallery project unwrap crash).
    std::vector<uint8_t> out_buf(OUT_BUF_MAX);

    const lzma_ret ret_xz = lzma_easy_encoder(&strm, 6 | LZMA_PRESET_EXTREME, check);
    if (ret_xz != LZMA_OK)
        return {};

    strm.next_in = data.empty() ? nullptr : const_cast<uint8_t *>(data.data());
    strm.avail_in = data.size();

    lzma_ret codeRet;
    do
    {
        strm.next_out = out_buf.data();
        strm.avail_out = OUT_BUF_MAX;
        codeRet = lzma_code(&strm, LZMA_FINISH);

        const size_t out_len = OUT_BUF_MAX - strm.avail_out;
        arr.insert(arr.end(), out_buf.data(), out_buf.data() + out_len);
        out_buf[0] = 0;
    } while (strm.avail_out == 0);

    lzma_end(&strm);
    (void)codeRet;
    return arr;
}

RKByteArray ZipReader::xzUncompress(const RKByteArray &data)
{
    lzma_stream strm = LZMA_STREAM_INIT;
    const uint32_t flags = LZMA_TELL_UNSUPPORTED_CHECK | LZMA_CONCATENATED;
    const uint64_t memory_limit = UINT64_MAX;
    std::vector<uint8_t> out_buf(OUT_BUF_MAX);
    RKByteArray arr;

    const lzma_ret ret_xz = lzma_stream_decoder(&strm, memory_limit, flags);
    if (ret_xz != LZMA_OK)
        return {};

    strm.next_in = data.empty() ? nullptr : const_cast<uint8_t *>(data.data());
    strm.avail_in = data.size();

    lzma_ret codeRet;
    do
    {
        strm.next_out = out_buf.data();
        strm.avail_out = OUT_BUF_MAX;
        codeRet = lzma_code(&strm, LZMA_FINISH);

        const size_t out_len = OUT_BUF_MAX - strm.avail_out;
        arr.insert(arr.end(), out_buf.data(), out_buf.data() + out_len);
        out_buf[0] = 0;
    } while (strm.avail_out == 0);

    lzma_end(&strm);
    (void)codeRet;
    return arr;
}

