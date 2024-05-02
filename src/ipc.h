// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#pragma once

#include "ipc_proc.h"
#include "ipc_ring.h"
#include "ipc_sem.h"
#include "ipc_shm.h"

typedef struct {
    ipc_sem_t sem_server;
    ipc_sem_t sem_client;
    uint8_t rbdata[];
} ipc_shared_data_t;

typedef struct {
    ipc_shm_server_t shm;
    ipc_ring_t* ring_send;
    ipc_ring_t* ring_recv;
    ipc_proc_t* proc;
} ipc_server_t;

typedef struct {
    ipc_shm_client_t shm;
    ipc_ring_t* ring_recv;
    ipc_ring_t* ring_send;
} ipc_client_t;

// --------------------------------------------------------------------------------------------------------------------

/*
 */
static inline
bool ipc_server_check(const char* name);

/*
 */
static inline
ipc_server_t* ipc_server_start(const char* args[], const char* name, uint32_t rbsize);

/*
 */
static inline
void ipc_server_stop(ipc_server_t* server);

/*
 */
static inline
bool ipc_server_is_running(ipc_server_t* server);

/*
 */
static inline
bool ipc_server_read(ipc_server_t* server, void* dst, uint32_t size);

/*
 */
static inline
bool ipc_server_write(ipc_server_t* server, const void* src, uint32_t size);

/*
 */
static inline
bool ipc_server_commit(ipc_server_t* server);

/*
 */
static inline
void ipc_server_wake(ipc_server_t* server);

/*
 */
static inline
bool ipc_server_wait_secs(ipc_server_t* server, uint32_t secs);

// --------------------------------------------------------------------------------------------------------------------

/*
 */
static inline
ipc_client_t* ipc_client_attach(const char* name, uint32_t rbsize);

/*
 */
static inline
void ipc_client_dettach(ipc_client_t* client);

/*
 */
static inline
bool ipc_client_read(ipc_client_t* client, void* dst, uint32_t size);

/*
 */
static inline
bool ipc_client_write(ipc_client_t* client, const void* src, uint32_t size);

/*
 */
static inline
bool ipc_client_commit(ipc_client_t* client);

/*
 */
static inline
void ipc_client_wake(ipc_client_t* client);

/*
 */
static inline
bool ipc_client_wait_secs(ipc_client_t* client, uint32_t secs);

// --------------------------------------------------------------------------------------------------------------------

static inline
bool ipc_server_check(const char* const name)
{
    return ipc_shm_server_check(name);
}

static inline
ipc_server_t* ipc_server_start(const char* args[], const char* const name, const uint32_t rbsize)
{
    ipc_server_t* const server = (ipc_server_t*)calloc(1, sizeof(ipc_server_t));
    if (server == NULL)
    {
        fprintf(stderr, "[ipc] ipc_process_start failed: out of memory\n");
        return NULL;
    }

    const uint32_t shared_data_size = sizeof(ipc_shared_data_t) + (sizeof(ipc_ring_t) + rbsize) * 2;

    if (! ipc_shm_server_create(&server->shm, name, shared_data_size, false))
    {
        fprintf(stderr, "[ipc] ipc_process_start failed: could not create shared memory segment\n");
        free(server);
        return NULL;
    }

    ipc_shared_data_t* const shared_data = (ipc_shared_data_t*)server->shm.ptr;
    memset(shared_data, 0, shared_data_size);

    server->ring_send = (ipc_ring_t*)shared_data->rbdata;
    ipc_ring_init(server->ring_send, rbsize);

    server->ring_recv = (ipc_ring_t*)(shared_data->rbdata + sizeof(ipc_ring_t) + rbsize);
    ipc_ring_init(server->ring_recv, rbsize);

    if (! ipc_sem_create(&shared_data->sem_server))
    {
        fprintf(stderr, "[ipc] ipc_sem_create failed\n");
        ipc_shm_server_destroy(&server->shm);
        free(server);
        return NULL;
    }

    if (! ipc_sem_create(&shared_data->sem_client))
    {
        fprintf(stderr, "[ipc] ipc_sem_create failed\n");
        ipc_sem_destroy(&shared_data->sem_server);
        ipc_shm_server_destroy(&server->shm);
        free(server);
        return NULL;
    }

    server->proc = ipc_proc_start(args);
    if (server->proc == NULL)
    {
        ipc_sem_destroy(&shared_data->sem_server);
        ipc_sem_destroy(&shared_data->sem_client);
        ipc_shm_server_destroy(&server->shm);
        free(server);
        return NULL;
    }

    return server;
}

static inline
void ipc_server_stop(ipc_server_t* const server)
{
    ipc_shared_data_t* const shared_data = (ipc_shared_data_t*)server->shm.ptr;

    ipc_proc_stop(server->proc);
    ipc_sem_destroy(&shared_data->sem_server);
    ipc_sem_destroy(&shared_data->sem_client);
    ipc_shm_server_destroy(&server->shm);
    free(server);
}

static inline
bool ipc_server_is_running(ipc_server_t* const server)
{
    return ipc_proc_is_running(server->proc);
}

static inline
bool ipc_server_read(ipc_server_t* const server, void* const dst, const uint32_t size)
{
    return ipc_ring_read(server->ring_recv, dst, size);
}

static inline
bool ipc_server_write(ipc_server_t* const server, const void* const src, const uint32_t size)
{
    return ipc_ring_write(server->ring_send, src, size);
}

static inline
bool ipc_server_commit(ipc_server_t* const server)
{
    return ipc_ring_commit(server->ring_send);
}

static inline
void ipc_server_wake(ipc_server_t* const server)
{
    ipc_shared_data_t* const shared_data = (ipc_shared_data_t*)server->shm.ptr;
    return ipc_sem_wake(&shared_data->sem_server);
}

static inline
bool ipc_server_wait_secs(ipc_server_t* const server, const uint32_t secs)
{
    ipc_shared_data_t* const shared_data = (ipc_shared_data_t*)server->shm.ptr;
    return ipc_sem_wait_secs(&shared_data->sem_client, secs);
}

// --------------------------------------------------------------------------------------------------------------------

static inline
ipc_client_t* ipc_client_attach(const char* const name, const uint32_t rbsize)
{
    ipc_client_t* const client = (ipc_client_t*)calloc(1, sizeof(ipc_client_t));

    if (client == NULL)
    {
        fprintf(stderr, "[ipc] ipc_client_attach failed: out of memory\n");
        return NULL;
    }

    const uint32_t shared_data_size = sizeof(ipc_shared_data_t) + (sizeof(ipc_ring_t) + rbsize) * 2;

    if (! ipc_shm_client_attach(&client->shm, name, shared_data_size, false))
    {
        fprintf(stderr, "[ipc] ipc_client_attach failed: could not attach shared memory segment\n");
        free(client);
        return NULL;
    }

    ipc_shared_data_t* const shared_data = (ipc_shared_data_t*)client->shm.ptr;
    client->ring_send = (ipc_ring_t*)shared_data->rbdata;
    client->ring_recv = (ipc_ring_t*)(shared_data->rbdata + sizeof(ipc_ring_t) + rbsize);

    return client;
}

static inline
void ipc_client_dettach(ipc_client_t* const client)
{
    ipc_shm_client_dettach(&client->shm);
    free(client);
}

static inline
bool ipc_client_read_size(const ipc_client_t* const client)
{
    return ipc_ring_read_size(client->ring_recv);
}

static inline
bool ipc_client_read(ipc_client_t* const client, void* const dst, const uint32_t size)
{
    return ipc_ring_read(client->ring_recv, dst, size);
}

static inline
bool ipc_client_write(ipc_client_t* const client, const void* const src, const uint32_t size)
{
    return ipc_ring_write(client->ring_send, src, size);
}

static inline
bool ipc_client_commit(ipc_client_t* const client)
{
    return ipc_ring_commit(client->ring_send);
}

static inline
void ipc_client_wake(ipc_client_t* const client)
{
    ipc_shared_data_t* const shared_data = (ipc_shared_data_t*)client->shm.ptr;
    return ipc_sem_wake(&shared_data->sem_client);
}

static inline
bool ipc_client_wait_secs(ipc_client_t* const client, const uint32_t secs)
{
    ipc_shared_data_t* const shared_data = (ipc_shared_data_t*)client->shm.ptr;
    return ipc_sem_wait_secs(&shared_data->sem_server, secs);
}

// --------------------------------------------------------------------------------------------------------------------
