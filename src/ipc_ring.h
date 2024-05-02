// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#pragma once

#ifdef __cplusplus
 #include <cassert>
 #include <cstdint>
 #include <cstring>
#else
 #define _GNU_SOURCE
 #include <assert.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>
#endif

typedef enum {
    ipc_ring_flag_invalidate_commit = 0x1,
    ipc_ring_flag_error_reading = 0x2,
    ipc_ring_flag_error_writing = 0x4,
} ipc_ring_flag_t;

typedef struct {
  uint32_t size, head, tail, wrtn, flags;
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
    const uint32_t wrap = ring->head >= ring->tail ? 0 : ring->size;
    return wrap + ring->head - ring->tail;
}

static inline
uint32_t ipc_ring_write_size(const ipc_ring_t* ring)
{
    const uint32_t wrap = ring->tail > ring->wrtn ? 0 : ring->size;
    return wrap + ring->tail - ring->wrtn - 1;
}

static inline
bool ipc_ring_read(ipc_ring_t* ring, void* dst, uint32_t size)
{
    assert(dst != NULL);
    assert(size != 0);
    assert(size < ring->size);

    // empty
    if (ring->head == ring->tail)
        return false;

    uint8_t* const dstbuffer = (uint8_t*)dst;

    const uint32_t head = ring->head;
    const uint32_t tail = ring->tail;
    const uint32_t wrap = head > tail ? 0 : ring->size;

    if (size > wrap + head - tail)
    {
        if ((ring->flags & ipc_ring_flag_error_reading) == 0)
        {
            ring->flags |= ipc_ring_flag_error_reading;
            fprintf(stderr, "[ipc] ipc_ring_read failed: not enough space\n");
        }
        return false;
    }

    uint32_t readto = tail + size;

    if (readto > ring->size)
    {
        readto -= ring->size;

        if (size == 1)
        {
            memcpy(dstbuffer, ring->buffer + tail, 1);
        }
        else
        {
            const uint32_t firstpart = ring->size - tail;
            memcpy(dstbuffer, ring->buffer + tail, firstpart);
            memcpy(dstbuffer + firstpart, ring->buffer, readto);
        }
    }
    else
    {
        memcpy(dstbuffer, ring->buffer + tail, size);

        if (readto == ring->size)
            readto = 0;
    }

    ring->tail = readto;
    ring->flags &= ~ipc_ring_flag_error_reading;
    return true;
}

static inline
bool ipc_ring_write(ipc_ring_t* ring, const void* src, uint32_t size)
{
    assert(src != NULL);
    assert(size != 0);
    assert(size < ring->size);

    uint8_t* const srcbuffer = (uint8_t*)src;

    const uint32_t tail = ring->tail;
    const uint32_t wrtn = ring->wrtn;
    const uint32_t wrap = tail > wrtn ? 0 : ring->size;

    if (size >= wrap + tail - wrtn)
    {
        if ((ring->flags & ipc_ring_flag_error_writing) == 0)
        {
            ring->flags |= ipc_ring_flag_error_writing;
            fprintf(stderr, "[ipc] ipc_ring_write failed: not enough space\n");
        }
        ring->flags |= ipc_ring_flag_invalidate_commit;
        return false;
    }

    uint32_t writeto = wrtn + size;

    if (writeto > ring->size)
    {
        writeto -= ring->size;

        if (size == 1)
        {
            memcpy(ring->buffer, srcbuffer, 1);
        }
        else
        {
            const uint32_t firstpart = ring->size - wrtn;
            memcpy(ring->buffer + wrtn, srcbuffer, firstpart);
            memcpy(ring->buffer, srcbuffer + firstpart, writeto);
        }
    }
    else
    {
        memcpy(ring->buffer + wrtn, srcbuffer, size);

        if (writeto == ring->size)
            writeto = 0;
    }

    ring->wrtn = writeto;
    return true;
}

static inline
bool ipc_ring_commit(ipc_ring_t* ring)
{
    if (ring->flags & ipc_ring_flag_invalidate_commit)
    {
        ring->wrtn = ring->head;
        ring->flags &= ~ipc_ring_flag_invalidate_commit;
        return false;
    }

    assert(ring->head != ring->wrtn);

    ring->head = ring->wrtn;
    return true;
}
