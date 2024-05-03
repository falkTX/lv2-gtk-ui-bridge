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
 #include "ipc_win32.h"
#else
 #ifdef __cplusplus
  #include <cerrno>
  #include <cstring>
 #else
  #include <errno.h>
  #include <string.h>
 #endif
 #include <signal.h>
 #include <unistd.h>
 #include <sys/wait.h>
#endif

typedef struct {
   #ifdef _WIN32
    PROCESS_INFORMATION pinfo;
   #else
    pid_t pid;
   #endif
} ipc_proc_t;

static inline
ipc_proc_t* ipc_proc_start(const char* const args[])
{
    /*
   #ifdef _WIN32
   #else
    if (access(args[0], X_OK) != 0)
    {
        fprintf(stderr, "[ipc] cannot exec: %s\n", strerror(errno));
        return NULL;
    }
   #endif
   */

    ipc_proc_t* const proc = (ipc_proc_t*)calloc(1, sizeof(ipc_proc_t));
    if (proc == NULL)
    {
        fprintf(stderr, "[ipc] ipc_proc_start failed: out of memory\n");
        return NULL;
    }

   #ifdef _WIN32
    size_t cmdlen = 1;
    for (int i = 0; args[i] != NULL; ++i)
    {
        cmdlen += strlen(args[i]) + 1;

        if (strchr(args[i], ' ') != NULL)
            cmdlen += 2;
    }

    wchar_t* const cmd = malloc(sizeof(wchar_t) * cmdlen);
    if (cmd == NULL)
    {
        fprintf(stderr, "[ipc] ipc_proc_start failed: out of memory\n");
        free(proc);
        return NULL;
    }

    wchar_t* cmdptr = cmd;
    for (int i = 0; args[i] != NULL; ++i)
    {
        const bool quoted = args[i][0] != '"' && strchr(args[i], ' ') != NULL;

        if (i != 0)
            *cmdptr++ = L' ';

        if (quoted)
            *cmdptr++ = L'"';

        const DWORD wrtn = MultiByteToWideChar(CP_UTF8, 0, args[i], -1, cmdptr, cmdlen - (cmdptr - cmd) / sizeof(wchar_t));
        if (wrtn <= 0)
        {
            fprintf(stderr, "[ipc] ipc_proc_start failed: %s\n", StrError(GetLastError()));
            free(cmd);
            free(proc);
            return NULL;
        }

        cmdptr += wrtn - 1;

        if (quoted)
            *cmdptr++ = L'"';
    }

    *cmdptr = 0;

    fprintf(stderr, "[ipc] ipc_proc_start trying to launch '%ls'\n", cmd);

    STARTUPINFOW si = { .cb = sizeof(si) };
    if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, /*CREATE_NO_WINDOW*/ 0, NULL, NULL, &si, &proc->pinfo) == FALSE)
    {
        fprintf(stderr, "[ipc] ipc_proc_start failed: %s\n", StrError(GetLastError()));
        free(cmd);
        free(proc);
        return NULL;
    }

    free(cmd);
    return proc;
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
    bool should_terminate = true;

   #ifdef _WIN32
    if (proc->pinfo.hProcess == INVALID_HANDLE_VALUE)
    {
        free(proc);
        return;
    }

    const PROCESS_INFORMATION opinfo = proc->pinfo;
    proc->pinfo = (PROCESS_INFORMATION){
        .hProcess = INVALID_HANDLE_VALUE,
        .hThread = INVALID_HANDLE_VALUE,
        .dwProcessId = 0,
        .dwThreadId = 0,
    };
    free(proc);

    for (DWORD exit_code;;)
    {
        if (GetExitCodeProcess(opinfo.hProcess, &exit_code) == FALSE ||
            exit_code != STILL_ACTIVE ||
            WaitForSingleObject(opinfo.hProcess, 0) != WAIT_TIMEOUT)
        {
            CloseHandle(opinfo.hThread);
            CloseHandle(opinfo.hProcess);
            return;
        }

        if (should_terminate)
        {
            should_terminate = false;
            TerminateProcess(opinfo.hProcess, ERROR_BROKEN_PIPE);
        }

        Sleep(5);
        continue;
    }
   #else
    if (proc->pid <= 0)
    {
        free(proc);
        return;
    }

    const pid_t opid = proc->pid;
    free(proc);

    for (;;)
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
    if (proc->pinfo.hProcess == INVALID_HANDLE_VALUE)
        return false;

    DWORD exit_code;
    if (GetExitCodeProcess(proc->pinfo.hProcess, &exit_code) == FALSE ||
        exit_code != STILL_ACTIVE ||
        WaitForSingleObject(proc->pinfo.hProcess, 0) != WAIT_TIMEOUT)
    {
        const PROCESS_INFORMATION opinfo = proc->pinfo;
        proc->pinfo = (PROCESS_INFORMATION){
            .hProcess = INVALID_HANDLE_VALUE,
            .hThread = INVALID_HANDLE_VALUE,
            .dwProcessId = 0,
            .dwThreadId = 0,
        };
        CloseHandle(opinfo.hThread);
        CloseHandle(opinfo.hProcess);
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
