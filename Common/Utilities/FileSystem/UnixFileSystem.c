/*
 * Copyright (c) 2017-2026 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../Utilities/Interfaces/IFileSystem.h"
#include "../../Utilities/Interfaces/ILog.h"

extern TFResourceDirectoryInfo gResourceDirectories[TF_RD_COUNT];

#if defined(__APPLE__)
#include <sys/param.h>
#endif

struct UnixFileStream
{
    ssize_t size;
    void*   mapping;
    int     descriptor;
};

#define USD(name, fs) struct UnixFileStream* name = (struct UnixFileStream*)(fs)->mUser.data

static const char* getFileName(const struct UnixFileStream* stream, char* buffer, size_t bufferSize)
{
#if defined(__APPLE__)
    char tmpBuffer[MAXPATHLEN];
    if (fcntl(stream->descriptor, F_GETPATH, tmpBuffer) != -1)
    {
        bufferSize = bufferSize > sizeof(tmpBuffer) ? sizeof(tmpBuffer) : bufferSize;
        memcpy(buffer, tmpBuffer, bufferSize);
        buffer[bufferSize - 1] = 0;
        return buffer;
    }
#else
    char fdpath[32];
    snprintf(fdpath, sizeof fdpath, "/proc/self/fd/%i", stream->descriptor);

    ssize_t len = readlink(fdpath, buffer, bufferSize - 1);
    if (len >= 0)
    {
        buffer[len] = 0;
        return buffer;
    }
#endif

    LOGF(eERROR, "Failed to get file name for %i descriptor: %s", stream->descriptor, strerror(errno));
    return "unknown file name";
}

static bool ioUnixFsOpen(TFIFileSystem* io, const TFResourceDirectory rd, const char* fileName, TFFileMode mode, TFFileStream* fs)
{
    MTRACY_ZONE("File / Open");
    MTRACY_TEXT(fileName);
    MTRACY_VALUE(mode);
    __FS_NO_ERR;
    memset(fs, 0, sizeof *fs);

#if !defined(ANDROID)
    char filePath[TF_FS_MAX_PATH] = { 0 };
    strcat(filePath, gResourceDirectories[rd].mPath);
    strcat(filePath, fileName);
#else
    const char* filePath = fileName;
#endif

    int oflags = 0;

    // 666
    mode_t omode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IROTH;

    if (mode & TF_FM_WRITE)
    {
        oflags |= O_CREAT;

        if (mode & TF_FM_APPEND)
        {
            oflags |= O_APPEND;
        }
        else
        {
            oflags |= O_TRUNC;
        }

        if (mode & TF_FM_READ)
        {
            oflags |= O_RDWR;
        }
        else
        {
            oflags |= O_WRONLY;
        }
    }
    else
    {
        oflags |= O_RDONLY;
    }

    int fd = open(filePath, oflags, omode);
    // Might fail to open the file for read+write if file doesn't exist
    if (fd < 0)
    {
        __FS_SET_ERR(translateErrno(errno));
        return false;
    }

    USD(stream, fs);

    stream->size = -1;
    stream->descriptor = fd;

    struct stat finfo;
    if (fstat(stream->descriptor, &finfo) == 0)
    {
        stream->size = finfo.st_size;
    }

    fs->mMode = mode;
    fs->pIO = io;

    if ((mode & TF_FM_READ) && (mode & TF_FM_APPEND) && !(mode & TF_FM_WRITE))
    {
        if (!io->Seek(fs, TF_SBO_END_OF_FILE, 0))
        {
            io->Close(fs);
            return false;
        }
    }

    return true;
}

static bool ioUnixFsMemoryMap(TFFileStream* fs, size_t* outSize, void const** outData)
{
    MTRACY_ZONE("File / MemoryMap");
    __FS_NO_ERR;
    *outSize = 0;
    *outData = NULL;

    if (fs->mMode & TF_FM_WRITE)
    {
        __FS_SET_ERR(TF_FS_NOT_PERMITTED_ERR);
        return false;
    }

    USD(stream, fs);

    // if not mapped already
    if (!stream->mapping)
    {
        if (stream->size < 0)
        {
            __FS_SET_ERR(TF_FS_INVALID_STATE_ERR);
            return false;
        }
        if (stream->size == 0)
            return true;

        void* mem = mmap(NULL, (size_t)stream->size, PROT_READ, MAP_PRIVATE, stream->descriptor, 0);

        if (mem == MAP_FAILED)
        {
            __FS_SET_ERR(translateErrno(errno));
            return false;
        }

        stream->mapping = mem;
    }

    *outSize = (size_t)stream->size;
    *outData = stream->mapping;
    return true;
}

static bool ioUnixFsClose(TFFileStream* fs)
{
    MTRACY_ZONE("File / Close");
    __FS_NO_ERR;
    USD(stream, fs);

    if (stream->mapping)
    {
        if (munmap(stream->mapping, (size_t)stream->size))
        {
            __FS_SET_ERR(translateErrno(errno));
        }
        else
        {
            stream->mapping = NULL;
        }
    }

    bool success = stream->descriptor < 0 || close(stream->descriptor) == 0;
    if (!success)
    {
        __FS_SET_ERR(translateErrno(errno));
    }
    stream->descriptor = -1;
    return success;
}

static size_t ioUnixFsRead(TFFileStream* fs, void* dst, size_t size)
{
    MTRACY_ZONE("File / Read");
    MTRACY_VALUE(size);
    __FS_NO_ERR;
    USD(stream, fs);
    ssize_t res = read(stream->descriptor, dst, size);
    if (res >= 0)
        return (size_t)res;

    __FS_SET_ERR(translateErrno(errno));
    return 0;
}

static ssize_t ioUnixFsGetPosition(TFFileStream* fs)
{
    __FS_NO_ERR;
    USD(stream, fs);

    off_t res = lseek(stream->descriptor, 0, SEEK_CUR);
    if (res >= 0)
        return res;

    __FS_SET_ERR(translateErrno(errno));
    return false;
}

static size_t ioUnixFsWrite(TFFileStream* fs, const void* src, size_t size)
{
    MTRACY_ZONE("File / Write");
    MTRACY_VALUE(size);
    __FS_NO_ERR;
    USD(stream, fs);
    ssize_t res = write(stream->descriptor, src, size);
    if (res >= 0)
        return (size_t)res;

    __FS_SET_ERR(translateErrno(errno));
    return 0;
}

static bool ioUnixFsSeek(TFFileStream* fs, TFSeekBaseOffset baseOffset, ssize_t offset)
{
    MTRACY_ZONE("File / Seek");
    MTRACY_VALUE(offset);
    __FS_NO_ERR;
    USD(stream, fs);

    int whence = SEEK_SET;
    switch (baseOffset)
    {
    case TF_SBO_START_OF_FILE:
        whence = SEEK_SET;
        break;
    case TF_SBO_CURRENT_POSITION:
        whence = SEEK_CUR;
        break;
    case TF_SBO_END_OF_FILE:
        whence = SEEK_END;
        break;
    }

    off_t res = lseek(stream->descriptor, offset, whence);
    if (res >= 0)
        return true;

    __FS_SET_ERR(translateErrno(errno));
    return false;
}

static bool ioUnixFsFlush(TFFileStream* fs)
{
    MTRACY_ZONE("File / Flush");
    __FS_NO_ERR;
    if (!(fs->mMode & TF_FM_WRITE))
        return true;

    USD(stream, fs);

#if defined(_POSIX_SYNCHRONIZED_IO) && _POSIX_SYNCHRONIZED_IO > 0
    // datasync is a bit faster, because it can skip flush of modified metadata,
    // e.g. file access time
    if (!fdatasync(stream->descriptor))
        return true;
#else
    if (!fsync(stream->descriptor))
        return true;
#endif

    __FS_SET_ERR(translateErrno(errno));
    return false;
}

static bool unixFsUpdateSize(struct UnixFileStream* stream)
{
    __FS_NO_ERR;

    off_t offset = lseek(stream->descriptor, 0, SEEK_CUR);
    if (offset < 0)
    {
        __FS_SET_ERR(translateErrno(errno));
        return false;
    }
    off_t size = lseek(stream->descriptor, 0, SEEK_END);
    if (size < 0)
    {
        __FS_SET_ERR(translateErrno(errno));
        return false;
    }

    if (offset == size)
    {
        stream->size = size;
        return true;
    }

    off_t offset2 = lseek(stream->descriptor, offset, SEEK_SET);
    if (offset2 < 0 || offset2 != offset)
    {
        char buffer[1024];
        LOGF(eWARNING, "File position is broken and so file is closed '%s': %s", getFileName(stream, buffer, sizeof buffer),
             strerror(errno));
        __FS_SET_ERR(translateErrno(errno));
        close(stream->descriptor);
        stream->descriptor = -1;
    }
    stream->size = size;
    return true;
}

static ssize_t ioUnixFsGetSize(TFFileStream* fs)
{
    USD(stream, fs);
    if ((fs->mMode & TF_FM_WRITE) && !unixFsUpdateSize(stream))
        return -1;
    return stream->size;
}

static void* ioUnixGetSystemHandle(TFFileStream* fs)
{
    USD(stream, fs);
    return (void*)(ssize_t)stream->descriptor;
}

static bool ioUnixFsIsAtEnd(TFFileStream* fs) { return ioUnixFsGetPosition(fs) >= ioUnixFsGetSize(fs); }

TFIFileSystem gUnixSystemFileIO = { ioUnixFsOpen,          ioUnixFsClose, ioUnixFsRead,    ioUnixFsWrite, ioUnixFsSeek, ioUnixFsGetPosition,
                                    ioUnixFsGetSize,       ioUnixFsFlush, ioUnixFsIsAtEnd, NULL,          NULL,         ioUnixFsMemoryMap,
                                    ioUnixGetSystemHandle, NULL };

#if !defined(ANDROID)
TFIFileSystem* pSystemFileIO = &gUnixSystemFileIO;
#endif
