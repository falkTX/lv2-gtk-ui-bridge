#pragma once

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#else
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#endif

#ifdef _WIN32
#else
#include <errno.h>
#include <semaphore.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

static constexpr const uint32_t kShmNameSize = 32;

typedef struct {
   #ifdef _WIN32
    HANDLE sem1;
    HANDLE sem2;
   #else
    int32_t sem1;
    int32_t sem2;
   #endif
    uint8_t data[];
} SharedData;

typedef struct {
   #ifdef _WIN32
    HANDLE shm;
   #else
    int shmfd;
    char name[kShmNameSize];
    uint32_t size;
   #endif
    SharedData* data;
} SharedDataServer;

typedef struct {
   #ifdef _WIN32
    HANDLE shm;
   #else
    int shmfd;
    uint32_t size;
   #endif
    SharedData* data;
} SharedDataClient;

static inline
void ipc_shm_name(char shmname[kShmNameSize], const char name[24])
{
   #ifdef _WIN32
    snprintf(shmname, sizeof(shmname) - 1, "Local\\", name);
   #else
    snprintf(shmname, sizeof(shmname) - 1, "/%s", name);
   #endif
}

static inline
bool ipc_server_check(const char name[24])
{
    char shmname[kShmNameSize];
    ipc_shm_name(shmname, name);

   #ifdef _WIN32
    const HANDLE handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmname);
    if (handle == NULL)
        return true;

    CloseHandle(handle);
   #else
    const int shmfd = shm_open(shmname, O_RDONLY, 0);

    if (shmfd < 0)
        return true;

    close(shmfd);
   #endif

    return false;
}

static inline
SharedDataServer* ipc_server_create(const char name[24], const uint32_t dataSize, const bool memlock)
{
    const uint32_t fullDataSize = sizeof(SharedData) + dataSize;

    SharedDataServer* const server = (SharedDataServer*)calloc(1, sizeof(SharedDataServer));
    if (server == NULL)
    {
        return NULL;
    }

    char shmname[kShmNameSize] = {};
    ipc_shm_name(shmname, name);

   #ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    const HANDLE sem1 = CreateSemaphoreA(&sa, 0, 1, NULL);
    const HANDLE sem2 = CreateSemaphoreA(&sa, 0, 1, NULL);
    if (sem1 == NULL || sem2 == NULL)
        goto fail;

    server->shm = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE|SEC_COMMIT, 0, (DWORD)fullDataSize, shmname);
    if (shm == NULL)
        goto fail;

    server->data = (SharedData*)MapViewOfFile(shm, FILE_MAP_ALL_ACCESS, 0, 0, fullDataSize);
    if (server->data == NULL)
        goto fail;

    if (memlock)
        VirtualLock(server->data, fullDataSize);
   #else
    server->shmfd = shm_open(shmname, O_CREAT|O_EXCL|O_RDWR, 0666);
    if (server->shmfd < 0)
    {
        fprintf(stderr, "[ipc] shm_open failed: %s\n", strerror(errno));
        goto fail;
    }

    if (ftruncate(server->shmfd, (off_t)fullDataSize) != 0)
    {
        fprintf(stderr, "[ipc] ftruncate failed: %s\n", strerror(errno));
        goto fail;
    }

    if (memlock)
    {
       #ifdef MAP_LOCKED
        server->data = (SharedData*)mmap(NULL, fullDataSize, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED, server->shmfd, 0);
        if (server->data == NULL || server->data == MAP_FAILED)
       #endif
        {
            server->data = (SharedData*)mmap(NULL, fullDataSize, PROT_READ|PROT_WRITE, MAP_SHARED, server->shmfd, 0);

            if (server->data == NULL || server->data == MAP_FAILED)
            {
                fprintf(stderr, "[ipc] mmap failed: %s\n", strerror(errno));
                goto fail;
            }

           #ifndef MAP_LOCKED
            mlock(data, fullDataSize);
           #endif
        }
    }
    else
    {
        server->data = mmap(NULL, fullDataSize, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);

        if (server->data == NULL || server->data == MAP_FAILED)
        {
            fprintf(stderr, "[ipc] mmap failed: %s\n", strerror(errno));
            goto fail;
        }
    }
   #endif

    memset(server->data, 0, fullDataSize);

   #ifdef _WIN32
    server->shm = shm;
    server->data->sem1 = sem1;
    server->data->sem2 = sem2;
   #else
    server->size = fullDataSize;
    memcpy(server->name, shmname, kShmNameSize);
   #endif

    return server;

fail:
   #ifdef _WIN32
    if (sem1 != NULL)
        CloseHandle(sem1);

    if (sem2 != NULL)
        CloseHandle(sem2);

    if (server->data != NULL)
        UnmapViewOfFile(server->data);

    if (server->shm != NULL)
        CloseHandle(server->shm);
   #else
    if (server->data != NULL && server->data != MAP_FAILED)
        munmap(server->data, fullDataSize);

    if (server->shmfd > 0)
    {
        close(server->shmfd);
        shm_unlink(shmname);
    }
   #endif

    free(server);
    return NULL;
}

static inline
void ipc_server_destroy(SharedDataServer* const server)
{
   #ifdef _WIN32
    CloseHandle(server->data->sem1);
    CloseHandle(server->data->sem2);
    UnmapViewOfFile(server->data);
    CloseHandle(server->shm);
   #else
    munmap(server->data, server->size);
    close(server->shmfd);
    shm_unlink(server->name);
   #endif

    free(server);
}

static inline
SharedDataClient* ipc_client_create(const char name[24], const uint32_t dataSize, const bool memlock)
{
    const uint32_t fullDataSize = sizeof(SharedData) + dataSize;

    SharedDataClient* const client = (SharedDataClient*)calloc(1, sizeof(SharedDataClient));
    if (server == NULL)
    {
        return NULL;
    }

    char shmname[kShmNameSize];
    ipc_shm_name(shmname, name);

   #ifdef _WIN32
    client->shm = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmname);
    if (client->shm == NULL)
    {
        fprintf(stderr, "[ipc] OpenFileMapping failed\n");
        goto fail;
    }

    client->data = (SharedData*)MapViewOfFile(client->shm, FILE_MAP_ALL_ACCESS, 0, 0, fullDataSize);
    if (client->data == NULL)
    {
        fprintf(stderr, "[ipc] MapViewOfFile failed\n");
        goto fail;
    }

    if (memlock)
        VirtualLock(client->data, fullDataSize);
   #else
    client->shmfd = shm_open(shmname, O_RDWR, 0);
    if (client->shmfd < 0)
    {
        fprintf(stderr, "[ipc] shm_open failed: %s\n", strerror(errno));
        goto fail;
    }

    if (memlock)
    {
       #ifdef MAP_LOCKED
        client->data = (SharedData*)mmap(NULL, fullDataSize, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED, server->shmfd, 0);
        if (client->data == NULL || client->data == MAP_FAILED)
       #endif
        {
            client->data = (SharedData*)mmap(NULL, fullDataSize, PROT_READ|PROT_WRITE, MAP_SHARED, server->shmfd, 0);

            if (client->data == NULL || client->data == MAP_FAILED)
            {
                fprintf(stderr, "[ipc] mmap failed: %s\n", strerror(errno));
                goto fail;
            }

           #ifndef MAP_LOCKED
            mlock(data, fullDataSize);
           #endif
        }
    }
    else
    {
        client->data = mmap(NULL, fullDataSize, PROT_READ|PROT_WRITE, MAP_SHARED, shmfd, 0);

        if (client->data == NULL || client->data == MAP_FAILED)
        {
            fprintf(stderr, "[ipc] mmap failed: %s\n", strerror(errno));
            goto fail;
        }
    }
   #endif

   #ifdef _WIN32
    client->shm = shm;
   #else
    client->size = fullDataSize;
    memcpy(client->name, shmname, kShmNameSize);
   #endif

    return client;

fail:
   #ifdef _WIN32
    if (client->data != NULL)
        UnmapViewOfFile(client->data);

    if (client->shm != NULL)
        CloseHandle(client->shm);
   #else
    if (client->data != NULL && client->data != MAP_FAILED)
        munmap(client->data, fullDataSize);

    if (client->shmfd > 0)
        close(client->shmfd);
   #endif

    free(client);
    return NULL;
}

static inline
void ipc_client_destroy(SharedDataClient* const client)
{
   #ifdef _WIN32
    UnmapViewOfFile(client->data);
    CloseHandle(client->shm);
   #else
    munmap(client->data, client->size);
    close(client->shmfd);
   #endif

    free(client);
}
