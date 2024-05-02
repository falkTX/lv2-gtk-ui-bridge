#include "src/ipc.h"

#include <linux/limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <lv2/ui/ui.h>

#define BRIDGE_UI_URI "http://calf.sourceforge.net/plugins/gui/x11-gui"

typedef struct {
    ipc_server_t* ipc;
    ipc_proc_t* proc;
    ipc_ring_t* ring_send;
    ipc_ring_t* ring_recv;
} LV2UI_Bridge;

static void lv2ui_cleanup(LV2UI_Handle ui);

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

    LV2UI_Bridge* const process = (LV2UI_Bridge*)calloc(1, sizeof(LV2UI_Bridge));
    if (process == NULL)
    {
        fprintf(stderr, "[lv2-gtk-ui-bridge] out of memory\n");
        return NULL;
    }

    const uint32_t rbsize = 0x7fff;
    const uint32_t shared_data_size = (sizeof(ipc_ring_t) + rbsize) * 2;

    process->ipc = ipc_server_create(shm, shared_data_size, false);
    if (process->ipc == NULL)
    {
        fprintf(stderr, "[lv2-gtk-ui-bridge] ipc_shm_server_create failed\n");
        free(process);
        return NULL;
    }

    memset(process->ipc->data->data, 0, shared_data_size);

    process->ring_send = (ipc_ring_t*)process->ipc->data->data;
    ipc_ring_init(process->ring_send, rbsize);

    process->ring_recv = (ipc_ring_t*)(process->ipc->data->data + sizeof(ipc_ring_t) + rbsize);
    ipc_ring_init(process->ring_recv, rbsize);

    process->proc = ipc_proc_start(args);
    if (process->proc == NULL)
    {
        fprintf(stderr, "[lv2-gtk-ui-bridge] ipc_server_create failed\n");
        ipc_server_destroy(process->ipc);
        free(process);
        return NULL;
    }

    while (ipc_proc_is_running(process->proc))
    {
        // if (ipc_server_wait_secs(process->ipc, 2))
        {
            fprintf(stderr, "[lv2-gtk-ui-bridge] setup complete '%s' %s\n", shm, wid);
            return process;
        }
    }

    fprintf(stderr, "[lv2-gtk-ui-bridge] failed to setup IPC\n");
    lv2ui_cleanup(process);
    return NULL;
}

static void lv2ui_cleanup(LV2UI_Handle ui)
{
    LV2UI_Bridge* const process = ui;

    ipc_proc_stop(process->proc);
    ipc_server_destroy(process->ipc);
    free(process);
}

static void lv2ui_port_event(LV2UI_Handle ui, uint32_t portIndex, uint32_t bufferSize, uint32_t format, const void* buffer)
{
    return;
    LV2UI_Bridge* const process = ui;

    ipc_ring_write(process->ring_send, &portIndex, sizeof(portIndex)) &&
    ipc_ring_write(process->ring_send, &bufferSize, sizeof(bufferSize)) &&
    ipc_ring_write(process->ring_send, &format, sizeof(format)) &&
    ipc_ring_write(process->ring_send, buffer, bufferSize);

    if (ipc_ring_commit(process->ring_send))
        ipc_server_wake(process->ipc);
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
        BRIDGE_UI_URI,
        lv2ui_instantiate,
        lv2ui_cleanup,
        lv2ui_port_event,
        lv2ui_extension_data
    };

    return index == 0 ? &descriptor : NULL;
}
