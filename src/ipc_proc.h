// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#pragma once

#ifdef __cplusplus
 #include <cstdint>
 #include <cstdio>
 #include <cstdlib>
#else
 #define _GNU_SOURCE
 #include <stdbool.h>
 #include <stdint.h>
 #include <stdio.h>
 #include <stdlib.h>
#endif

#ifdef _WIN32
 #include <winsock2.h>
 #include <windows.h>
#else
 #ifdef __cplusplus
  #include <cerrno>
  #include <cstring>
 #else
  #include <errno.h>
  #include <string.h>
 #endif
 #include <signal.h>
 #include <sys/wait.h>
#endif

typedef struct {
   #ifdef _WIN32
    PROCESS_INFORMATION process;
   #else
    pid_t pid;
   #endif
} ipc_proc_t;

static inline
ipc_proc_t* ipc_proc_start(const char* const args[])
{
   #ifdef _WIN32
   #else
    if (access(args[0], X_OK) != 0)
    {
        fprintf(stderr, "[ipc] cannot exec: %s\n", strerror(errno));
        return NULL;
    }
   #endif

    ipc_proc_t* const proc = (ipc_proc_t*)calloc(1, sizeof(ipc_proc_t));
    if (proc == NULL)
    {
        fprintf(stderr, "[ipc] ipc_proc_start failed: out of memory\n");
        return NULL;
    }

   #ifdef _WIN32
   #else
    const pid_t pid = vfork();

    switch (pid)
    {
    // child process
    case 0:
        execvp(args[0], (char* const*)args);
        fprintf(stderr, "[ipc] exec failed: %s\n", strerror(errno));
        _exit(1);
        return NULL;

    // error
    case -1:
        fprintf(stderr, "[ipc] vfork failed: %s\n", strerror(errno));
        free(proc);
        return NULL;
    }

    proc->pid = pid;
   #endif

    return proc;
}

static inline
void ipc_proc_stop(ipc_proc_t* const proc)
{
   #ifdef _WIN32
   #else
    if (proc->pid <= 0)
    {
        free(proc);
        return;
    }

    const pid_t opid = proc->pid;
    free(proc);

    for (bool should_terminate = true;;)
    {
        const pid_t ret = waitpid(opid, NULL, WNOHANG);

        switch (ret)
        {
        case -1:
            // success, child doesn't exist
            if (errno == ECHILD)
                return;

            break;

        case 0:
            if (should_terminate)
            {
                should_terminate = false;
                kill(opid, SIGTERM);
            }

            usleep(5000);
            continue;

        default:
            // success
            if (ret == opid)
                return;

            break;
        }

        fprintf(stderr, "[ipc] waitpid failed: %s\n", strerror(errno));
        return;
    }
   #endif
}

static inline
bool ipc_proc_is_running(ipc_proc_t* const proc)
{
   #ifdef _WIN32
    if (proc->process.hProcess == INVALID_HANDLE_VALUE)
        return false;

    if (WaitForSingleObject(proc->process.hProcess, 0) == WAIT_FAILED)
    {
        const PROCESS_INFORMATION oprocess = proc->process;
        proc->process = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, 0, 0 };
        CloseHandle(oprocess.hThread);
        CloseHandle(oprocess.hProcess);
        return false;
    }
   #else
    if (proc->pid <= 0)
        return false;

    const pid_t ret = waitpid(proc->pid, NULL, WNOHANG);

    if (ret == proc->pid || (ret == -1 && errno == ECHILD))
    {
        proc->pid = 0;
        return false;
    }
   #endif

    return true;
}
