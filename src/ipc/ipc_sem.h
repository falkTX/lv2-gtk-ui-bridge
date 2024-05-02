// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#pragma once

#ifdef __cplusplus
 #include <cstddef>
 #include <cstdint>
 #include <cstdio>
#else
 #define _GNU_SOURCE
 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
 #include <stdio.h>
#endif

#ifndef _WIN32
 #ifdef __cplusplus
  #include <cerrno>
 #else
  #include <errno.h>
 #endif
#endif

#if defined(__APPLE__)
 #ifdef __cplusplus
  extern "C" {
 #endif
   int __ulock_wait(uint32_t operation, void* addr, uint64_t value, uint32_t timeout_us);
   int __ulock_wake(uint32_t operation, void* addr, uint64_t value);
 #ifdef __cplusplus
  }
 #endif
#elif defined(__linux__) && 0
 #include <syscall.h>
 #include <unistd.h>
 #include <linux/futex.h>
 #include <sys/time.h>
#elif defined(_WIN32)
 #include <winsock2.h>
 #include <windows.h>
#else
 #include <semaphore.h>
#endif

#if defined(__APPLE__) || defined(__linux__) && 0
typedef int32_t ipc_sem_t;
#elif defined(_WIN32)
typedef HANDLE ipc_sem_t;
#else
typedef sem_t ipc_sem_t;
#endif

static inline
bool ipc_sem_create(ipc_sem_t* const sem)
{
   #if defined(__APPLE__) || defined(__linux__) && 0
    // nothing to do
    return true;
   #elif defined(_WIN32)
    const SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    return (*sem = CreateSemaphoreA(&sa, 0, MAX_LONG, NULL)) != NULL;
   #else
    if (sem_init(sem, 1, 0) == 0)
        return true;
    fprintf(stderr, "[ipc] sem_init failed: %s\n", strerror(errno));
    return false;
   #endif
}

static inline
void ipc_sem_destroy(ipc_sem_t* const sem)
{
   #if defined(__APPLE__) || defined(__linux__) && 0
    // nothing to do
   #elif defined(_WIN32)
    CloseHandle(*sem);
   #else
    sem_destroy(sem);
   #endif
}

static inline
void ipc_sem_wake(ipc_sem_t* const sem)
{
   #if defined(__APPLE__)
    if (! __sync_bool_compare_and_swap(sem, 0, 1))
        __ulock_wake(0x1000003, sem, 0);
   #elif defined(__linux__) && 0
    if (! __sync_bool_compare_and_swap(sem, 0, 1))
        syscall(SYS_futex, sem, FUTEX_WAKE, 1, NULL, NULL, 0);
   #elif defined(_WIN32)
    ReleaseSemaphore(*sem, 1, NULL);
   #else
    sem_post(sem);
   #endif
}

static inline
bool ipc_sem_wait_secs(ipc_sem_t* const sem, const uint32_t secs)
{
   #if defined(__APPLE__)
    for (;;)
    {
        if (__sync_bool_compare_and_swap(sem, 1, 0))
            return true;
        if (__ulock_wait(0x3, sem, 0, secs * 1000000) != 0)
            if (errno != EAGAIN && errno != EINTR)
                return false;
    }
   #elif defined(__linux__) && 0
    const struct timespec timeout = { secs, 0 };
    for (;;)
    {
        if (__sync_bool_compare_and_swap(sem, 1, 0))
            return true;
        if (syscall(SYS_futex, sem, FUTEX_WAIT, 0, &timeout, NULL, 0) != 0)
            if (errno != EAGAIN && errno != EINTR)
                return false;
    }
   #elif defined(_WIN32)
    return WaitForSingleObject(sem, secs * 1000) == WAIT_OBJECT_0;
   #else
    // TODO
    return sem_wait(sem) == 0;
   #endif
}
