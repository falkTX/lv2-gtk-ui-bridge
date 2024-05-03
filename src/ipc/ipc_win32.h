// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#pragma once

#ifdef _WIN32

#ifndef __cplusplus
 #define thread_local _Thread_local
#endif

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

static inline
const char* StrError(const DWORD error)
{
    wchar_t* msg;
    const DWORD msglen = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
                                        NULL, error, 0, (LPWSTR)&msg, 0, NULL);

    if (msglen < 3)
        return NULL;

    static thread_local char* _last_error = NULL;
    _last_error = (char*)realloc(_last_error, msglen + 1);

    if (_last_error != NULL && WideCharToMultiByte(CP_UTF8, 0, msg, msglen, _last_error, msglen, NULL, NULL))
    {
        LocalFree(msg);
        _last_error[msglen - 3] = '\0';
        return _last_error;
    }

    LocalFree(msg);
    return NULL;
}

#endif // _WIN32
