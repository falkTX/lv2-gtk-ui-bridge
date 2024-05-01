// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#pragma once

#include "ipc_proc.h"
#include "ipc_ring.h"
#include "ipc_sem.h"
#include "ipc_shm.h"

typedef struct {
    ipc_sem_t sem1;
    ipc_sem_t sem2;
    uint8_t data[];
} ipc_shared_data_t;

typedef struct {
    ipc_shm_server_t shm;
    ipc_shared_data_t* data;
    ipc_ring_t* ring1;
    ipc_ring_t* ring2;
    ipc_proc_t* proc;
} ipc_process_t;

typedef struct {
    ipc_shm_server_t shm;
    ipc_shared_data_t* data;
} ipc_server_t;

typedef struct {
    ipc_shm_client_t shm;
    ipc_shared_data_t* data;
} ipc_client_t;

/*
 */
static inline
bool ipc_server_check(const char* const name)
{
    return ipc_shm_server_check(name);
}

/*
 */
static inline
ipc_server_t* ipc_server_create(const char* name, uint32_t size, bool memlock);

/*
 */
static inline
void ipc_server_destroy(ipc_server_t* server);

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
ipc_process_t* ipc_process_start(const char* args[], const char* name, uint32_t rbsize);

/*
 */
static inline
void ipc_process_stop(ipc_process_t* process);

/*
 */
static inline
bool ipc_server_read(ipc_process_t* process, void* dst, uint32_t size);

/*
 */
static inline
bool ipc_server_write(ipc_process_t* process, const void* src, uint32_t size);

/*
 */
static inline
bool ipc_server_commit(ipc_process_t* process);

// --------------------------------------------------------------------------------------------------------------------

/*
 */
static inline
ipc_client_t* ipc_client_attach(const char* const name, const uint32_t size, const bool memlock);

/*
 */
static inline
void ipc_client_dettach(ipc_client_t* const client);

/*
 */
static inline
void ipc_client_wake(ipc_client_t* const client);

/*
 */
static inline
bool ipc_client_wait_secs(ipc_client_t* const client, const uint32_t secs);

// --------------------------------------------------------------------------------------------------------------------

static inline
ipc_server_t* ipc_server_create(const char* const name, const uint32_t size, const bool memlock)
{
    ipc_server_t* const server = (ipc_server_t*)calloc(1, sizeof(ipc_server_t));
    if (server == NULL)
    {
        fprintf(stderr, "[ipc] ipc_server_create failed: out of memory\n");
        return NULL;
    }

    const uint32_t shared_data_size = sizeof(ipc_shared_data_t) + size;

    if (! ipc_shm_server_create(&server->shm, name, shared_data_size, memlock))
    {
        fprintf(stderr, "[ipc] ipc_server_create failed: could not create shared memory segment\n");
        free(server);
        return NULL;
    }

    server->data = (ipc_shared_data_t*)server->shm.ptr;
    memset(server->data, 0, shared_data_size);

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
void ipc_server_destroy(ipc_server_t* const server)
{
    ipc_sem_destroy(&server->data->sem1);
    ipc_sem_destroy(&server->data->sem2);
    ipc_shm_server_destroy(&server->shm);
    free(server);
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

// --------------------------------------------------------------------------------------------------------------------

static inline
ipc_process_t* ipc_process_start(const char* args[], const char* name, uint32_t rbsize)
{
    ipc_process_t* const process = (ipc_process_t*)calloc(1, sizeof(ipc_process_t));
    if (process == NULL)
    {
        fprintf(stderr, "[ipc] ipc_process_start failed: out of memory\n");
        return NULL;
    }

    const uint32_t shared_data_size = sizeof(ipc_shared_data_t) + (sizeof(ipc_ring_t) + rbsize) * 2;

    if (! ipc_shm_server_create(&process->shm, name, shared_data_size, false))
    {
        fprintf(stderr, "[ipc] ipc_process_start failed: could not create shared memory segment\n");
        free(process);
        return NULL;
    }

    process->data = (ipc_shared_data_t*)process->shm.ptr;
    memset(process->data, 0, shared_data_size);

    process->ring1 = (ipc_ring_t*)process->data->data;
    ipc_ring_init(process->ring1, rbsize);

    process->ring2 = (ipc_ring_t*)(process->data->data + sizeof(ipc_ring_t) + rbsize);
    ipc_ring_init(process->ring2, rbsize);

    if (! ipc_sem_create(&process->data->sem1))
    {
        fprintf(stderr, "[ipc] ipc_sem_create failed\n");
        ipc_shm_server_destroy(&process->shm);
        free(process);
        return NULL;
    }

    if (! ipc_sem_create(&process->data->sem2))
    {
        fprintf(stderr, "[ipc] ipc_sem_create failed\n");
        ipc_sem_destroy(&process->data->sem1);
        ipc_shm_server_destroy(&process->shm);
        free(process);
        return NULL;
    }

    process->proc = ipc_proc_start(args);
    if (process->proc == NULL)
    {
        ipc_sem_destroy(&process->data->sem1);
        ipc_sem_destroy(&process->data->sem2);
        ipc_shm_server_destroy(&process->shm);
        free(process);
        return NULL;
    }

    return process;
}

static inline
void ipc_process_stop(ipc_process_t* const process)
{
    ipc_proc_stop(process->proc);
    ipc_sem_destroy(&process->data->sem1);
    ipc_sem_destroy(&process->data->sem2);
    ipc_shm_server_destroy(&process->shm);
    free(process);
}

/*
 */
static inline
bool ipc_server_read(ipc_process_t* process, void* dst, uint32_t size)
{
    return ipc_ring_read(process->ring1, dst, size);
}

/*
 */
static inline
bool ipc_server_write(ipc_process_t* process, const void* src, uint32_t size)
{
    return ipc_ring_write(process->ring1, src, size);
}

/*
 */
static inline
bool ipc_server_commit(ipc_process_t* process)
{
    return ipc_ring_commit(process->ring1);
}

// --------------------------------------------------------------------------------------------------------------------

static inline
ipc_client_t* ipc_client_attach(const char* const name, const uint32_t size, const bool memlock)
{
    ipc_client_t* const client = (ipc_client_t*)calloc(1, sizeof(ipc_client_t));

    if (client == NULL)
    {
        fprintf(stderr, "[ipc] ipc_client_attach failed: out of memory\n");
        return NULL;
    }

    const uint32_t shared_data_size = sizeof(ipc_shared_data_t) + size;

    if (! ipc_shm_client_attach(&client->shm, name, shared_data_size, memlock))
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

// --------------------------------------------------------------------------------------------------------------------
