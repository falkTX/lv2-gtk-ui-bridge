#include "src/ipc.h"
#include "src/ipc_ring.h"

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
} ipc_process2_t;

static LV2UI_Handle lv2ui_instantiate(const LV2UI_Descriptor*,
                                      const char* const uri,
                                      const char* const bundlePath,
                                      const LV2UI_Write_Function writeFunction,
                                      const LV2UI_Controller controller,
                                      LV2UI_Widget* const widget,
                                      const LV2_Feature* const* const features)
{
    // verify valid URI
    if (uri == NULL || strcmp(uri, BRIDGE_UI_URI) != 0)
    {
        fprintf(stderr, "Invalid plugin URI\n");
        return NULL;
    }

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
    snprintf(runtool, sizeof(runtool) - 1, "%slv2-gtk-ui-bridge", bundlePath);

    char shm[24] = { 0 };
    for (int i=0; i < 9999; ++i)
    {
        snprintf(shm, sizeof(shm) - 1, "lv2-gtk-ui-bridge-%d", i + 1);
        if (ipc_server_check(shm))
            break;
    }

    char wid[24] = { 0 };
    snprintf(wid, sizeof(wid) - 1, "%lx", (unsigned long)parent);

    const char* args[] = { runtool, uri, shm, wid, NULL };

    ipc_process2_t* const process = (ipc_process2_t*)calloc(1, sizeof(ipc_process2_t));
    if (process == NULL)
    {
        fprintf(stderr, "[ipc] ipc_process_start failed: out of memory\n");
        return NULL;
    }

    const uint32_t rbsize = 0x7fff;
    const uint32_t shared_data_size = sizeof(ipc_shared_data_t) + (sizeof(ipc_ring_t) + rbsize) * 2;

    process->ipc = ipc_server_create(shm, shared_data_size, false);
    if (process->ipc == NULL)
    {
        fprintf(stderr, "[ipc] ipc_server_create failed\n");
        free(process);
        return NULL;
    }

    memset(process->ipc->data, 0, shared_data_size);

    process->ring_send = (ipc_ring_t*)process->ipc->data;
    ipc_ring_init(process->ring_send, rbsize);

    process->ring_recv = (ipc_ring_t*)(process->ipc->data + sizeof(ipc_ring_t) + rbsize);
    ipc_ring_init(process->ring_recv, rbsize);

    process->proc = ipc_proc_start(args);
    if (process->proc == NULL)
    {
        fprintf(stderr, "[ipc] ipc_server_create failed\n");
        ipc_server_destroy(process->ipc);
        free(process);
        return NULL;
    }

    return process;
}

static void lv2ui_cleanup(LV2UI_Handle ui)
{
    ipc_process2_t* const process = ui;

    ipc_proc_stop(process->proc);
    ipc_server_destroy(process->ipc);
    free(process);
}

static void lv2ui_port_event(LV2UI_Handle ui, uint32_t portIndex, uint32_t bufferSize, uint32_t format, const void* buffer)
{
    ipc_process2_t* const process = ui;

    ipc_ring_write(process->ring_send, &portIndex, sizeof(portIndex));
    ipc_ring_write(process->ring_send, &bufferSize, sizeof(bufferSize));
    ipc_ring_write(process->ring_send, &format, sizeof(format));
    ipc_ring_write(process->ring_send, buffer, bufferSize);
    ipc_ring_commit(process->ring_send);
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
