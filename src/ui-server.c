// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#include "ui-base.h"

#include <lv2/urid/urid.h>

typedef struct {
    ipc_server_t* ipc;
    LV2UI_Write_Function write_function;
    LV2UI_Controller controller;
    LV2_URID_Map* urid_map;
    uint64_t window_id;
    bool window_ok;
} LV2UI_Bridge;

static int lv2ui_idle(LV2UI_Handle ui);

static LV2UI_Handle lv2ui_instantiate(const LV2UI_Descriptor* const descriptor,
                                      const char* const plugin_uri,
                                      const char* const bundle_path,
                                      const LV2UI_Write_Function write_function,
                                      const LV2UI_Controller controller,
                                      LV2UI_Widget* const widget,
                                      const LV2_Feature* const* const features)
{
    // TODO idle feature
    // TODO log feature

    // ----------------------------------------------------------------------------------------------------------------
    // verify host features

    void* parent = NULL;
    LV2_URID_Map* urid_map = NULL;

    for (int i=0; features[i] != NULL; ++i)
    {
        if (strcmp(features[i]->URI, LV2_UI__parent) == 0)
            parent = features[i]->data;
        else if (strcmp(features[i]->URI, LV2_URID__map) == 0)
            urid_map = features[i]->data;
    }
    if (parent == NULL)
    {
        fprintf(stderr, "ui:parent feature missing, cannot continue!\n");
        return NULL;
    }
    if (urid_map == NULL || urid_map->map == NULL)
    {
        fprintf(stderr, "urid:map feature missing, cannot continue!\n");
        return NULL;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // alloc memory for our bridge details

    LV2UI_Bridge* const bridge = malloc(sizeof(LV2UI_Bridge));
    bridge->write_function = write_function;
    bridge->controller = controller;
    bridge->urid_map = urid_map;
    bridge->window_id = 0;
    bridge->window_ok = false;

    // ----------------------------------------------------------------------------------------------------------------
    // path to bridge helper

   #ifdef _WIN32
    #define APP_EXT ".exe"
   #else
    #define APP_EXT ""
   #endif

    const char* bridge_tool;
    if (strcmp(descriptor->URI, "https://kx.studio/lv2-gtk2-ui-bridge") == 0)
    {
        bridge_tool = "lv2-gtk2-ui-bridge" APP_EXT;
    }
    else if (strcmp(descriptor->URI, "https://kx.studio/lv2-gtk3-ui-bridge") == 0)
    {
        bridge_tool = "lv2-gtk3-ui-bridge" APP_EXT;
    }
    else
    {
        fprintf(stderr, "invalid descriptor URI, cannot continue!\n");
        return NULL;
    }

    const size_t bridge_tool_len = strlen(bridge_tool);
    const size_t bundle_path_len = strlen(bundle_path);

    char* const bridge_tool_path = malloc(bundle_path_len + bridge_tool_len + 1);
    memcpy(bridge_tool_path, bundle_path, bundle_path_len);
    memcpy(bridge_tool_path + bundle_path_len, bridge_tool, bridge_tool_len + 1);

    // ----------------------------------------------------------------------------------------------------------------
    // find usable shm name

    char shm_name[24] = { 0 };
    for (int i=0; i < 9999; ++i)
    {
        snprintf(shm_name, sizeof(shm_name) - 1, "lv2-gtk-ui-bridge-%d", i + 1);
        if (ipc_shm_server_check(shm_name))
            break;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // convert parent window id into a string

    char wid[24] = { 0 };
    snprintf(wid, sizeof(wid) - 1, "%lu", (unsigned long)parent);

    // ----------------------------------------------------------------------------------------------------------------
    // unset known problematic env vars

    char* old_ld_preload = getenv("LD_PRELOAD");
    if (old_ld_preload != NULL)
    {
        old_ld_preload = strdup(old_ld_preload);
        unsetenv("LD_PRELOAD");
    }

    char* old_ld_library_path = getenv("LD_LIBRARY_PATH");
    if (old_ld_library_path != NULL)
    {
        old_ld_library_path = strdup(old_ld_library_path);
        unsetenv("LD_LIBRARY_PATH");
    }

    // ----------------------------------------------------------------------------------------------------------------
    // start IPC server

    const char* args[] = { bridge_tool_path, plugin_uri, shm_name, wid, NULL };

    bridge->ipc = ipc_server_start(args, shm_name, rbsize);

    // ----------------------------------------------------------------------------------------------------------------
    // cleanup

    if (old_ld_preload != NULL)
    {
        setenv("LD_PRELOAD", old_ld_preload, 1);
        free(old_ld_preload);
    }

    if (old_ld_library_path != NULL)
    {
        setenv("LD_LIBRARY_PATH", old_ld_library_path, 1);
        free(old_ld_library_path);
    }

    free(bridge_tool_path);

    if (bridge->ipc == NULL)
    {
        fprintf(stderr, "[lv2-gtk-ui-bridge] ipc_server_start failed\n");
        free(bridge);
        return NULL;
    }

    // ----------------------------------------------------------------------------------------------------------------
    // if we have a parent wait for first message, giving window id to host

    if (parent == NULL)
    {
        *widget = NULL;
        return bridge;
    }

    while (ipc_server_wait_secs(bridge->ipc, 1) && lv2ui_idle(bridge) == 0 && !bridge->window_ok) {}

    if (bridge->window_ok)
    {
        *widget = (LV2UI_Widget)bridge->window_id;
        return bridge;
    }

    fprintf(stderr, "[lv2-gtk-ui-bridge] ipc_server_start failed to fetch initial response\n");
    ipc_server_stop(bridge->ipc);
    free(bridge);
    return NULL;
}

static void lv2ui_cleanup(const LV2UI_Handle ui)
{
    LV2UI_Bridge* const bridge = ui;

    ipc_server_stop(bridge->ipc);
    free(bridge);
}

static void lv2ui_port_event(LV2UI_Handle ui, uint32_t port_index, uint32_t buffer_size, uint32_t format, const void* buffer)
{
    LV2UI_Bridge* const bridge = ui;

    const uint32_t msg_type = lv2ui_message_port_event;
    ipc_server_write(bridge->ipc, &msg_type, sizeof(uint32_t)) &&
    ipc_server_write(bridge->ipc, &port_index, sizeof(uint32_t)) &&
    ipc_server_write(bridge->ipc, &buffer_size, sizeof(uint32_t)) &&
    ipc_server_write(bridge->ipc, &format, sizeof(uint32_t)) &&
    ipc_server_write(bridge->ipc, buffer, buffer_size);
    ipc_server_commit(bridge->ipc);
}

static int lv2ui_idle(const LV2UI_Handle ui)
{
    LV2UI_Bridge* const bridge = ui;

    uint32_t size = 0;
    void* buffer = NULL;

    while (ipc_server_read_size(bridge->ipc) != 0)
    {
        uint32_t msg_type = lv2ui_message_null;
        uint32_t port_index, buffer_size, port_protocol;

        if (ipc_server_read(bridge->ipc, &msg_type, sizeof(uint32_t)))
        {
            if (msg_type == lv2ui_message_port_event &&
                ipc_server_read(bridge->ipc, &port_index, sizeof(uint32_t)) &&
                ipc_server_read(bridge->ipc, &buffer_size, sizeof(uint32_t)) &&
                ipc_server_read(bridge->ipc, &port_protocol, sizeof(uint32_t)))
            {
                if (buffer_size > size)
                {
                    size = buffer_size;
                    buffer = realloc(buffer, buffer_size);

                    if (buffer == NULL)
                    {
                        fprintf(stderr, "lv2ui server out of memory, abort!\n");
                        return 1;
                    }
                }

                if (ipc_server_read(bridge->ipc, buffer, buffer_size))
                {
                    if (bridge->write_function != NULL)
                        bridge->write_function(bridge->controller, port_index, buffer_size, port_protocol, buffer);

                    continue;
                }
            }
            else if (msg_type == lv2ui_message_urid_map_req &&
                     ipc_server_read(bridge->ipc, &buffer_size, sizeof(uint32_t)))
            {
                if (buffer_size > size)
                {
                    size = buffer_size;
                    buffer = realloc(buffer, buffer_size);

                    if (buffer == NULL)
                    {
                        fprintf(stderr, "lv2ui server out of memory, abort!\n");
                        return 1;
                    }
                }

                if (ipc_server_read(bridge->ipc, buffer, buffer_size))
                {
                    const uint32_t urid = bridge->urid_map->map(bridge->urid_map->handle, buffer);

                    const uint32_t msg_type = lv2ui_message_urid_map_resp;
                    ipc_server_write(bridge->ipc, &msg_type, sizeof(uint32_t)) &&
                    ipc_server_write(bridge->ipc, &urid, sizeof(uint32_t)) &&
                    ipc_server_write(bridge->ipc, &buffer_size, sizeof(uint32_t)) &&
                    ipc_server_write(bridge->ipc, buffer, buffer_size);
                    ipc_server_commit(bridge->ipc);

                    continue;
                }
            }
            else if (msg_type == lv2ui_message_window_id &&
                     ipc_server_read(bridge->ipc, &bridge->window_id, sizeof(uint64_t)))
            {
                bridge->window_ok = true;
                continue;
            }
        }

        fprintf(stderr, "lv2ui server ringbuffer data race, abort!\n");
        return 1;
    }

    return 0;
}

static const void* lv2ui_extension_data(const char* const uri)
{
    if (strcmp(uri, LV2_UI__idleInterface) == 0)
    {
        static const LV2UI_Idle_Interface idle_interface = {
            .idle = lv2ui_idle,
        };
        return &idle_interface;
    }

    return NULL;
}

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor* lv2ui_descriptor(const uint32_t index)
{
    static const LV2UI_Descriptor descriptor_gtk2 = {
        .URI = "https://kx.studio/lv2-gtk2-ui-bridge",
        .instantiate = lv2ui_instantiate,
        .cleanup = lv2ui_cleanup,
        .port_event = lv2ui_port_event,
        .extension_data = lv2ui_extension_data
    };
    static const LV2UI_Descriptor descriptor_gtk3 = {
        .URI = "https://kx.studio/lv2-gtk3-ui-bridge",
        .instantiate = lv2ui_instantiate,
        .cleanup = lv2ui_cleanup,
        .port_event = lv2ui_port_event,
        .extension_data = lv2ui_extension_data
    };

    switch (index)
    {
    case 0: return &descriptor_gtk2;
    case 1: return &descriptor_gtk3;
    default: return NULL;
    }
}
