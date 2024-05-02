// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#pragma once

#ifdef __cplusplus
 #include <cstdint>
 #include <cstring>
#else
 #define _GNU_SOURCE
 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>
#endif

typedef struct {
  uint32_t size;
  uint32_t write_head;
  uint32_t read_head;
  uint8_t buffer[];
} ipc_ring_t;

static inline
void ipc_ring_init(ipc_ring_t* ring, uint32_t size)
{
    memset(ring, 0, sizeof(ipc_ring_t) + size);
    ring->size = size;
}

static inline
uint32_t ipc_ring_read_size(const ipc_ring_t* ring)
{
    return 0;
}

static inline
bool ipc_ring_read(ipc_ring_t* ring, void* dst, uint32_t size)
{
    return false;
}

static inline
bool ipc_ring_write(ipc_ring_t* ring, const void* src, uint32_t size)
{
    return false;
}

static inline
bool ipc_ring_commit(ipc_ring_t* ring)
{
    return false;
}
