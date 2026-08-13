/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://www.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef ZipReader_H
#define ZipReader_H

#include <cstdint>
#include <string>
#include <vector>

using RKByteArray = std::vector<uint8_t>;

class ZipReaderPrivate;

class ZipReader
{
public:
    explicit ZipReader(const std::string &fileName);
    ~ZipReader();

    ZipReader(const ZipReader &) = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    bool isReadable() const;
    bool exists() const;

    struct FileInfo
    {
        FileInfo();
        FileInfo(const FileInfo &other);
        ~FileInfo();
        FileInfo &operator=(const FileInfo &other);
        bool isValid() const;

        std::string filePath;
        uint32_t isDir : 1;
        uint32_t isFile : 1;
        uint32_t isSymLink : 1;
        uint32_t permissions; // Unix mode bits (owner/group/other rwx)
        uint32_t crc_32;
        int64_t size;
        uint32_t lastModifiedDosDate;
        void *d;
    };

    std::vector<FileInfo> fileInfoList() const;
    int count() const;

    FileInfo entryInfoAt(int index) const;
    RKByteArray fileData(const std::string &fileName) const;

    enum Status {
        NoError,
        FileReadError,
        FileOpenError,
        FilePermissionsError,
        FileError
    };

    Status status() const;

    void close();

    static RKByteArray xzUncompress(const RKByteArray &data);

private:
    ZipReaderPrivate *d;
};

#endif // ZipReader_H
