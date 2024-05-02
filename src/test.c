// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#include "ipc/ipc.h"

int main(int argc, char* argv[])
{
    if (argc == 1)
    {
        printf("starting server...\n");
        const char* const shm_name = "test2";
        const char* args[] = { argv[0], shm_name, NULL };
        ipc_server_t* const server = ipc_server_start(args, shm_name, 32);
        assert(server);
        sleep(2);
        assert(!ipc_server_is_running(server));
        ipc_server_stop(server);
        printf("server done\n");
    }
    else
    {
        printf("starting client...\n");
        ipc_client_t* const client = ipc_client_attach(argv[1], 32);
        assert(client);
        sleep(1);
        ipc_client_dettach(client);
        printf("client done\n");
    }
    return 0;
}
