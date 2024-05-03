// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#pragma once

#ifdef __cplusplus
 #define IPC_STRUCT_INIT {}
 #include <cstddef>
 #include <cstdint>
 #include <cstdio>
#else
 #define IPC_STRUCT_INIT { 0 }
 #define _GNU_SOURCE
 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
 #include <stdio.h>
#endif

#ifdef _WIN32
 #include "ipc_win32.h"
#else
 #ifdef __cplusplus
  #include <cerrno>
  #include <cstring>
 #else
  #include <errno.h>
  #include <string.h>
 #endif
 #include <fcntl.h>
 #include <unistd.h>
 #include <sys/mman.h>
#endif

#define IPC_SHM_NAME_SIZE 32

typedef struct {
    uint8_t* ptr;
   #ifdef _WIN32
    HANDLE handle;
   #else
    int fd;
    char name[IPC_SHM_NAME_SIZE];
    uint32_t size;
   #endif
} ipc_shm_server_t;

typedef struct {
    uint8_t* ptr;
   #ifdef _WIN32
    HANDLE handle;
   #else
    int fd;
    uint32_t size;
   #endif
} ipc_shm_client_t;

static inline
void __ipc_shm_name(char shmname[IPC_SHM_NAME_SIZE], const char* const name)
{
   #ifdef _WIN32
    snprintf(shmname, IPC_SHM_NAME_SIZE - 1, "Local\\%s", name);
   #else
    snprintf(shmname, IPC_SHM_NAME_SIZE - 1, "/%s", name);
   #endif
    shmname[IPC_SHM_NAME_SIZE - 1] = '\0';
}

static inline
bool ipc_shm_server_check(const char* const name)
{
    char shmname[IPC_SHM_NAME_SIZE];
    __ipc_shm_name(shmname, name);

   #ifdef _WIN32
    const HANDLE handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmname);
    if (handle == NULL)
        return true;

    CloseHandle(handle);
   #else
    const int fd = shm_open(shmname, O_RDONLY, 0);

    if (fd < 0)
        return true;

    close(fd);
   #endif

    return false;
}

static inline
bool ipc_shm_server_create(ipc_shm_server_t* const shm, const char* const name, const uint32_t size, const bool memlock)
{
    char shmname[IPC_SHM_NAME_SIZE] = IPC_STRUCT_INIT;
    __ipc_shm_name(shmname, name);

   #ifdef _WIN32
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(sa), .lpSecurityDescriptor = NULL, .bInheritHandle = TRUE };
    shm->handle = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE|SEC_COMMIT, 0, (DWORD)size, shmname);
    if (shm->handle == NULL)
    {
        fprintf(stderr, "[ipc] CreateFileMapping failed: %s\n", StrError(GetLastError()));
        return false;
    }

    shm->ptr = (uint8_t*)MapViewOfFile(shm->handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (shm->ptr == NULL)
    {
        fprintf(stderr, "[ipc] MapViewOfFile failed: %s\n", StrError(GetLastError()));
        CloseHandle(shm->handle);
        return false;
    }

    if (memlock)
        VirtualLock(shm->ptr, size);
   #else
    shm->fd = shm_open(shmname, O_CREAT|O_EXCL|O_RDWR, 0666);
    if (shm->fd < 0)
    {
        fprintf(stderr, "[ipc] shm_open failed: %s\n", strerror(errno));
        return false;
    }

    if (ftruncate(shm->fd, (off_t)size) != 0)
    {
        fprintf(stderr, "[ipc] ftruncate failed: %s\n", strerror(errno));
        close(shm->fd);
        shm_unlink(shmname);
        return false;
    }

   #ifdef MAP_LOCKED
    if (memlock)
    {
        shm->ptr = (uint8_t*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED, shm->fd, 0);

        if (shm->ptr == NULL || shm->ptr == MAP_FAILED)
            shm->ptr = (uint8_t*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, shm->fd, 0);
    }
    else
   #endif
    {
        shm->ptr = (uint8_t*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, shm->fd, 0);
    }

    if (shm->ptr == NULL || shm->ptr == MAP_FAILED)
    {
        fprintf(stderr, "[ipc] mmap failed: %s\n", strerror(errno));
        close(shm->fd);
        shm_unlink(shmname);
        return false;
    }

   #ifndef MAP_LOCKED
    if (memlock)
        mlock(shm->ptr, size);
   #endif

    shm->size = size;
    memcpy(shm->name, shmname, IPC_SHM_NAME_SIZE);
   #endif

    return true;
}

static inline
void ipc_shm_server_destroy(ipc_shm_server_t* const shm)
{
   #ifdef _WIN32
    UnmapViewOfFile(shm->ptr);
    CloseHandle(shm->handle);
   #else
    munmap(shm->ptr, shm->size);
    close(shm->fd);
    shm_unlink(shm->name);
   #endif
}

static inline
bool ipc_shm_client_attach(ipc_shm_client_t* const shm, const char* const name, const uint32_t size, const bool memlock)
{
    char shmname[IPC_SHM_NAME_SIZE] = IPC_STRUCT_INIT;
    __ipc_shm_name(shmname, name);

   #ifdef _WIN32
    shm->handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmname);
    if (shm->handle == NULL)
    {
        fprintf(stderr, "[ipc] OpenFileMapping failed: %s\n", StrError(GetLastError()));
        return false;
    }

    shm->ptr = (uint8_t*)MapViewOfFile(shm->handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (shm->ptr == NULL)
    {
        fprintf(stderr, "[ipc] MapViewOfFile failed: %s\n", StrError(GetLastError()));
        CloseHandle(shm->handle);
        return false;
    }

    if (memlock)
        VirtualLock(shm->ptr, size);
   #else
    shm->fd = shm_open(shmname, O_RDWR, 0);
    if (shm->fd < 0)
    {
        fprintf(stderr, "[ipc] shm_open failed: %s\n", strerror(errno));
        return false;
    }

   #ifdef MAP_LOCKED
    if (memlock)
    {
        shm->ptr = (uint8_t*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED, shm->fd, 0);

        if (shm->ptr == NULL || shm->ptr == MAP_FAILED)
            shm->ptr = (uint8_t*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, shm->fd, 0);
    }
    else
   #endif
    {
        shm->ptr = (uint8_t*)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, shm->fd, 0);
    }

    if (shm->ptr == NULL || shm->ptr == MAP_FAILED)
    {
        fprintf(stderr, "[ipc] mmap failed: %s\n", strerror(errno));
        close(shm->fd);
        return false;
    }

   #ifndef MAP_LOCKED
    if (memlock)
        mlock(shm->ptr, size);
   #endif

    shm->size = size;
   #endif

    return true;
}

static inline
void ipc_shm_client_dettach(ipc_shm_client_t* const shm)
{
   #ifdef _WIN32
    UnmapViewOfFile(shm->ptr);
    CloseHandle(shm->handle);
   #else
    munmap(shm->ptr, shm->size);
    close(shm->fd);
   #endif
}
