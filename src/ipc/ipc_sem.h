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
#elif defined(__linux__)
 #include <syscall.h>
 #include <unistd.h>
 #include <linux/futex.h>
 #include <sys/time.h>
#elif defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef NOKERNEL
  #define NOKERNEL
 #endif
 #ifndef NOSERVICE
  #define NOSERVICE
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <winsock2.h>
 #include <windows.h>
#else
 #ifdef __cplusplus
  #include <ctime>
 #else
  #include <time.h>
 #endif
 #include <semaphore.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
typedef int32_t ipc_sem_t;
#elif defined(_WIN32)
typedef HANDLE ipc_sem_t;
#else
typedef sem_t ipc_sem_t;
#endif

static inline
bool ipc_sem_create(ipc_sem_t* const sem)
{
   #if defined(__APPLE__) || defined(__linux__)
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
   #if defined(__APPLE__) || defined(__linux__)
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
    if (__sync_bool_compare_and_swap(sem, 0, 1))
        __ulock_wake(0x1000003, sem, 0);
   #elif defined(__linux__)
    if (__sync_bool_compare_and_swap(sem, 0, 1))
        syscall(__NR_futex, sem, FUTEX_WAKE, 1, NULL, NULL, 0);
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
   #elif defined(__linux__)
    const struct timespec timeout = { secs, 0 };
    for (;;)
    {
        if (__sync_bool_compare_and_swap(sem, 1, 0))
            return true;
        if (syscall(__NR_futex, sem, FUTEX_WAIT, 0, &timeout, NULL, 0) != 0)
            if (errno != EAGAIN && errno != EINTR)
                return false;
    }
   #elif defined(_WIN32)
    return WaitForSingleObject(sem, secs * 1000) == WAIT_OBJECT_0;
   #else
    struct timespec timeout;
    if (clock_gettime(CLOCK_REALTIME, &timeout) != 0)
        return false;

    timeout.tv_sec += secs;

    for (int r;;)
    {
        r = sem_timedwait(sem, &timeout);

        if (r < 0)
            r = errno;

        if (r == EINTR)
            continue;

        return r == 0;
    }
   #endif
}
