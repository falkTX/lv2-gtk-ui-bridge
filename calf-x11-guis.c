#include "src/ipc.h"

#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lv2/ui/ui.h>

static LV2UI_Handle lv2ui_instantiate(const LV2UI_Descriptor*,
                                      const char* const uri,
                                      const char* const bundlePath,
                                      const LV2UI_Write_Function writeFunction,
                                      const LV2UI_Controller controller,
                                      LV2UI_Widget* const widget,
                                      const LV2_Feature* const* const features)
{
    // TODO idle feature
    // TODO log feature

    // verify host features
    void* parent = NULL;
    for (int i=0; features[i] != NULL; ++i)
    {
        if (strcmp(features[i]->URI, LV2_UI__parent) == 0)
        {
            parent = features[i]->data;
            break;
        }
    }
    if (parent == NULL)
    {
        fprintf(stderr, "ui:parent feature missing, cannot continue!\n");
        return NULL;
    }

    char runtool[PATH_MAX];
    snprintf(runtool, sizeof(runtool) - 1, "%scalf-x11-run", bundlePath);

    char shm[24] = { 0 };
    for (int i=0; i < 9999; ++i)
    {
        snprintf(shm, sizeof(shm) - 1, "lv2-gtk-ui-bridge-%d", i + 1);
        if (ipc_shm_server_check(shm))
            break;
    }

    char wid[24] = { 0 };
    snprintf(wid, sizeof(wid) - 1, "%lu", (unsigned long)parent);

    const char* args[] = { runtool, uri, shm, wid, NULL };

    const uint32_t rbsize = 0x7fff;

    ipc_server_t* const server = ipc_server_start(args, shm, rbsize);
    if (server == NULL)
    {
        fprintf(stderr, "[lv2-gtk-ui-bridge] ipc_server_create failed\n");
        return NULL;
    }

    while (ipc_server_is_running(server))
    {
        // if (ipc_server_wait_for_client(server, 2))
        {
            fprintf(stderr, "[lv2-gtk-ui-bridge] setup complete '%s' %s\n", shm, wid);
            return server;
        }
    }

    fprintf(stderr, "[lv2-gtk-ui-bridge] failed to setup IPC\n");
    ipc_server_stop(server);
    return NULL;
}

static void lv2ui_cleanup(LV2UI_Handle ui)
{
    ipc_server_t* const server = ui;

    ipc_server_stop(server);
}

static void lv2ui_port_event(LV2UI_Handle ui, uint32_t portIndex, uint32_t bufferSize, uint32_t format, const void* buffer)
{
    ipc_server_t* const server = ui;

    ipc_server_write(server, &portIndex, sizeof(portIndex)) &&
    ipc_server_write(server, &bufferSize, sizeof(bufferSize)) &&
    ipc_server_write(server, &format, sizeof(format)) &&
    ipc_server_write(server, buffer, bufferSize);

    if (ipc_server_commit(server))
        ipc_server_wake(server);
}

// TODO idle extension

static const void* lv2ui_extension_data(const char* uri)
{
    // TODO idle extension
    return NULL;
}

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index)
{
    static const LV2UI_Descriptor descriptor = {
        "http://calf.sourceforge.net/plugins/gui/x11-gui",
        lv2ui_instantiate,
        lv2ui_cleanup,
        lv2ui_port_event,
        lv2ui_extension_data
    };

    return index == 0 ? &descriptor : NULL;
}
