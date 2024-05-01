#pragma once

#include "ipc_sem.h"
#include "ipc_shm.h"

#ifdef __cplusplus
 #include <cstdlib>
#else
 #include <stdlib.h>
#endif

typedef struct {
    ipc_sem_t sem1;
    ipc_sem_t sem2;
    uint8_t data[];
} ipc_shared_data_t;

typedef struct {
    ipc_shm_server_t shm;
    ipc_shared_data_t* data;
} ipc_server_t;

typedef struct {
    ipc_shm_client_t shm;
    ipc_shared_data_t* data;
} ipc_client_t;

static inline
bool ipc_server_check(const char name[24])
{
    return ipc_shm_server_check(name);
}

static inline
ipc_server_t* ipc_server_create(const char name[24], const uint32_t dataSize, const bool memlock)
{
    ipc_server_t* const server = (ipc_server_t*)calloc(1, sizeof(ipc_server_t));
    if (server == NULL)
    {
        fprintf(stderr, "[ipc] ipc_server_create failed: out of memory\n");
        return NULL;
    }

    const uint32_t fullDataSize = sizeof(ipc_shared_data_t) + dataSize;

    if (! ipc_shm_server_create(&server->shm, name, fullDataSize, memlock))
    {
        fprintf(stderr, "[ipc] ipc_server_create failed: could not create shared memory segment\n");
        free(server);
        return NULL;
    }

    server->data = (ipc_shared_data_t*)server->shm.ptr;
    memset(server->data, 0, fullDataSize);

    if (! ipc_sem_create(&server->data->sem1))
    {
        fprintf(stderr, "[ipc] ipc_sem_create failed\n");
        ipc_shm_server_destroy(&server->shm);
        free(server);
        return NULL;
    }

    if (! ipc_sem_create(&server->data->sem2))
    {
        fprintf(stderr, "[ipc] ipc_sem_create failed\n");
        ipc_sem_destroy(&server->data->sem1);
        ipc_shm_server_destroy(&server->shm);
        free(server);
        return NULL;
    }

    return server;
}

static inline
void ipc_server_wake(ipc_server_t* const server)
{
    return ipc_sem_wake(&server->data->sem1);
}

static inline
bool ipc_server_wait_secs(ipc_server_t* const server, const uint32_t secs)
{
    return ipc_sem_wait_secs(&server->data->sem2, secs);
}

static inline
void ipc_server_destroy(ipc_server_t* const server)
{
    ipc_sem_destroy(&server->data->sem1);
    ipc_sem_destroy(&server->data->sem2);
    ipc_shm_server_destroy(&server->shm);
    free(server);
}

static inline
ipc_client_t* ipc_client_attach(const char name[24], const uint32_t dataSize, const bool memlock)
{
    ipc_client_t* const client = (ipc_client_t*)calloc(1, sizeof(ipc_client_t));

    if (client == NULL)
    {
        fprintf(stderr, "[ipc] ipc_client_attach failed: out of memory\n");
        return NULL;
    }

    if (! ipc_shm_client_attach(&client->shm, name, dataSize, memlock))
    {
        fprintf(stderr, "[ipc] ipc_client_attach failed: could not attach shared memory segment\n");
        free(client);
        return NULL;
    }

    client->data = (ipc_shared_data_t*)client->shm.ptr;

    return client;
}

static inline
void ipc_client_dettach(ipc_client_t* const client)
{
    ipc_shm_client_dettach(&client->shm);
    free(client);
}

static inline
void ipc_client_wake(ipc_client_t* const client)
{
    return ipc_sem_wake(&client->data->sem2);
}

static inline
bool ipc_client_wait_secs(ipc_client_t* const client, const uint32_t secs)
{
    return ipc_sem_wait_secs(&client->data->sem1, secs);
}
