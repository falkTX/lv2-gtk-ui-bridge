// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#include "ipc.h"

#include <assert.h>

int main()
{
    const char* args[] = { "/usr/bin/echo", "hahahaha", NULL };
    ipc_server_t* const server = ipc_server_start(args, "tst1", 32);
    assert(server);
    sleep(1);
    assert(!ipc_server_is_running(server));
    ipc_server_stop(server);

    // const char* const name = "test1";
    // assert(ipc_server_check(name));
    // ipc_server_t* server = ipc_server_create(name, 32, true);
    // assert(server);
    // assert(!ipc_server_check(name));
    // ipc_client_t* client = ipc_client_attach(name, 32, true);
    // ipc_client_dettach(client);
    // ipc_server_destroy(server);
    printf("done\n");
    return 0;
}
