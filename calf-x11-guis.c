#include "calf-x11-guis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BRIDGE_UI_URI "http://calf.sourceforge.net/plugins/gui/x11-gui"

typedef struct {
    SharedData* shared;
} CalfBridge;

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

    // make sure calf lv2 ui binary exists before continuing
    if (access("/usr/lib/lv2/calf.lv2/calflv2gui.so", R_OK) != F_OK)
    {
        fprintf(stderr, "ui:parent feature missing, cannot continue!\n");
        return NULL;
    }

    CalfBridge* const bridge = malloc(sizeof(CalfBridge));

    jack_ringbuffer_init_from_shm(&bridge->shared->rb, bridge->shared->data);

    // TODO init shm
    // TODO start process

    return bridge;
}

static void lv2ui_cleanup(LV2UI_Handle ui)
{
    CalfBridge* const bridge = ui;

    free(bridge);
}

static void lv2ui_port_event(LV2UI_Handle ui, uint32_t portIndex, uint32_t bufferSize, uint32_t format, const void* buffer)
{
    CalfBridge* const bridge = ui;

    jack_ringbuffer_write(&bridge->shared->rb, (const char*)&portIndex, sizeof(portIndex));
    jack_ringbuffer_write(&bridge->shared->rb, (const char*)&bufferSize, sizeof(bufferSize));
    jack_ringbuffer_write(&bridge->shared->rb, (const char*)&format, sizeof(format));
    jack_ringbuffer_write(&bridge->shared->rb, buffer, bufferSize);
    sem_post(&bridge->shared->sem);
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
