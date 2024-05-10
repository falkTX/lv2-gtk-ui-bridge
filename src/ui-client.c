// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#define IPC_LOG_NAME "ipc-client"
#include "ui-base.h"

#include <dlfcn.h>
// NOTE cannot use C11 threads as it very poorly supported
#include <pthread.h>

#include <gtk/gtk.h>
#ifdef UI_GTK3
#include <gtk/gtkx.h>
#endif
#include <lilv/lilv.h>
#include <X11/Xlib.h>

typedef struct {
    char* bundlepath;
    void* lib;
    const LV2UI_Descriptor* desc;
} LV2UI_Object;

typedef struct {
    char** uris;
    const char* waiting_uri;
    uint32_t max_urid;
} LV2UI_URIs;

typedef struct {
    ipc_client_t* ipc;
    LV2UI_Object* uiobj;
    LV2UI_Handle uihandle;
    LV2UI_URIs uiuris;
} LV2UI_Bridge;

static LV2UI_Object* lv2ui_object_load(const char* const uri)
{
    LilvWorld* const world = lilv_world_new();
    lilv_world_load_all(world);

    const LilvPlugins* const plugins = lilv_world_get_all_plugins(world);
    LilvNode* const urinode = lilv_new_uri(world, uri);
    if (plugins == NULL || urinode == NULL)
        goto error;

    const LilvPlugin* const plugin = lilv_plugins_get_by_uri(plugins, urinode);
    lilv_node_free(urinode);
    if (plugin == NULL)
        goto error;

    LilvUIs* const uis = lilv_plugin_get_uis(plugin);
    if (uis == NULL)
        goto error;

   #ifdef UI_GTK3
    LilvNode* const gtkuinode = lilv_new_uri(world, LV2_UI__Gtk3UI);
   #else
    LilvNode* const gtkuinode = lilv_new_uri(world, LV2_UI__GtkUI);
   #endif
    if (gtkuinode == NULL)
        goto error;

    char* bundlepath;
    void* uilib = NULL;
    const LV2UI_Descriptor* uidesc = NULL;

    LILV_FOREACH(uis, i, uis)
    {
        const LilvUI* const ui = lilv_uis_get(uis, i);

        if (! lilv_ui_is_a(ui, gtkuinode))
            continue;

        const LilvNode* const bundlenode = lilv_ui_get_binary_uri(ui);
        char* const binarypath = lilv_file_uri_parse(lilv_node_as_uri(bundlenode), NULL);

        uilib = dlopen(binarypath, RTLD_NOW);

        if (uilib == NULL)
        {
            fprintf(stderr, "could not load UI binary: %s\n", dlerror());
            lilv_free(binarypath);
            continue;
        }

        const LV2UI_DescriptorFunction lv2uifn = dlsym(uilib, "lv2ui_descriptor");
        if (lv2uifn != NULL)
        {
            for (uint32_t j=0;; ++j)
            {
                uidesc = lv2uifn(j);
                if (uidesc == NULL)
                {
                    fprintf(stderr, "lv2ui did not return valid UI, cannot continue!\n");
                    break;
                }
                if (uidesc->instantiate == NULL || uidesc->cleanup == NULL)
                {
                    uidesc = NULL;
                    fprintf(stderr, "lv2ui is invalid, cannot continue!\n");
                    break;
                }

                if (strcmp(uidesc->URI, lilv_node_as_uri(lilv_ui_get_uri(ui))) == 0)
                    break;

                fprintf(stderr, "skip lv2 ui %s\n", uidesc->URI);
            }
        }
        else
        {
            fprintf(stderr, "lv2ui_descriptor symbol missing, cannot continue!\n");
        }

        if (uidesc != NULL)
        {
            bundlepath = binarypath;
            char* const sep = strrchr(bundlepath, '/');
            if (sep != NULL)
                sep[1] = '\0';
            break;
        }

        lilv_free(binarypath);

        if (uilib != NULL)
        {
            dlclose(uilib);
            uilib = NULL;
        }
    }

    if (uilib == NULL || uidesc == NULL)
        goto error;

    LV2UI_Object* const uiobj = malloc(sizeof(LV2UI_Object));
    uiobj->bundlepath = bundlepath;
    uiobj->lib = uilib;
    uiobj->desc = uidesc;

    fprintf(stderr, "bundlepath is '%s'\n", bundlepath);

    return uiobj;

error:
    lilv_world_free(world);
    return NULL;
}

static void lv2ui_object_unload(LV2UI_Object* const uiobj)
{
    lilv_free(uiobj->bundlepath);

    if (uiobj->lib != NULL)
        dlclose(uiobj->lib);

    free(uiobj);
}

static void lv2ui_uris_add(LV2UI_URIs* const uiuris, const uint32_t urid, const char* const uri)
{
    if (urid >= uiuris->max_urid)
    {
        uiuris->uris = realloc(uiuris->uris, sizeof(char*) * (urid + 1));

        for (uint32_t i = uiuris->max_urid; i < urid + 1; ++i)
            uiuris->uris[i] = NULL;

        uiuris->max_urid = urid + 1;
    }

    assert(uiuris->uris[urid] == NULL);
    uiuris->uris[urid] = strdup(uri);
}

static void lv2ui_uris_cleanup(LV2UI_URIs* const uiuris)
{
    for (uint32_t i = 0; i < uiuris->max_urid; ++i)
        free(uiuris->uris[i]);
}

static void lv2ui_write_function(LV2UI_Controller controller,
                                 uint32_t port_index,
                                 uint32_t buffer_size,
                                 uint32_t format,
                                 const void* buffer)
{
    LV2UI_Bridge* const bridge = controller;

    const uint32_t msg_type = lv2ui_message_port_event;
    ipc_client_write(bridge->ipc, &msg_type, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, &port_index, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, &buffer_size, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, &format, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, buffer, buffer_size);
    ipc_client_commit(bridge->ipc);
}

static int lv2ui_idle(void* const ptr)
{
    LV2UI_Bridge* const bridge = ptr;

    uint32_t size = 0;
    void* buffer = NULL;

    while (ipc_client_read_size(bridge->ipc) != 0)
    {
        uint32_t msg_type = lv2ui_message_null;
        if (ipc_client_read(bridge->ipc, &msg_type, sizeof(uint32_t)))
        {
            uint32_t port_index, buffer_size, format;
            switch (msg_type)
            {
            case lv2ui_message_port_event:
                if (ipc_client_read(bridge->ipc, &port_index, sizeof(uint32_t)) &&
                    ipc_client_read(bridge->ipc, &buffer_size, sizeof(uint32_t)) &&
                    ipc_client_read(bridge->ipc, &format, sizeof(uint32_t)))
                {
                    if (buffer_size > size)
                    {
                        size = buffer_size;
                        buffer = realloc(buffer, buffer_size);

                        if (buffer == NULL)
                        {
                            fprintf(stderr, "lv2ui client out of memory, abort!\n");
                            abort();
                        }
                    }

                    if (ipc_client_read(bridge->ipc, buffer, buffer_size))
                    {
                        if (bridge->uiobj->desc->port_event != NULL)
                            bridge->uiobj->desc->port_event(bridge->uihandle, port_index, buffer_size, format, buffer);

                        continue;
                    }
                }
                break;
            case lv2ui_message_urid_map_resp:
                if (ipc_client_read(bridge->ipc, &port_index, sizeof(uint32_t)) &&
                    ipc_client_read(bridge->ipc, &buffer_size, sizeof(uint32_t)))
                {
                    if (buffer_size > size)
                    {
                        size = buffer_size;
                        buffer = realloc(buffer, buffer_size);

                        if (buffer == NULL)
                        {
                            fprintf(stderr, "lv2ui client out of memory, abort!\n");
                            abort();
                        }
                    }

                    if (ipc_client_read(bridge->ipc, buffer, buffer_size))
                    {
                        lv2ui_uris_add(&bridge->uiuris, port_index, buffer);

                        if (bridge->uiuris.waiting_uri != NULL && strcmp(bridge->uiuris.waiting_uri, buffer) == 0)
                            bridge->uiuris.waiting_uri = NULL;

                        continue;
                    }
                }
                break;
            }
        }

        fprintf(stderr, "lv2ui client ringbuffer data race, abort!\n");
        abort();
    }

    free(buffer);

    return 0;
}

static LV2_URID lv2ui_uri_map(const LV2_URID_Map_Handle handle, const char* const uri)
{
    LV2UI_Bridge* const bridge = handle;

    for (uint32_t i = 0; i < bridge->uiuris.max_urid; ++i)
    {
        if (bridge->uiuris.uris[i] != NULL && strcmp(bridge->uiuris.uris[i], uri) == 0)
            return i;
    }

    bridge->uiuris.waiting_uri = uri;

    const uint32_t msg_type = lv2ui_message_urid_map_req;
    const uint32_t buffer_size = strlen(uri) + 1;
    ipc_client_write(bridge->ipc, &msg_type, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, &buffer_size, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, uri, buffer_size);
    ipc_client_commit(bridge->ipc);

    while (ipc_client_wait_secs(bridge->ipc, 1) && lv2ui_idle(bridge) == 0 && bridge->uiuris.waiting_uri != NULL) {}

    for (uint32_t i = 0; i < bridge->uiuris.max_urid; ++i)
    {
        if (bridge->uiuris.uris[i] != NULL && strcmp(bridge->uiuris.uris[i], uri) == 0)
            return i;
    }

    fprintf(stderr, "lv2ui client uri map failed\n");
    return 0;
}

static void* lv2ui_thread_run(void* const ptr)
{
    LV2UI_Bridge* const bridge = ptr;

    while (bridge->ipc != NULL)
    {
        if (ipc_client_wait_secs(bridge->ipc, 1))
            g_main_context_invoke(NULL, lv2ui_idle, bridge);
    }

    return NULL;
}

static void signal_handler(const int sig)
{
    gtk_main_quit();

    // unused
    (void)sig;
}

int main(int argc, char* argv[])
{
    if (! gtk_init_check(&argc, &argv))
    {
        fprintf(stderr, "could not init gtk, cannot continue!\n");
        return 1;
    }

    if (argc != 2 && argc != 4)
    {
        fprintf(stderr, "usage: %s <lv2-uri> [shm-access-key] [x11-ui-parent]\n", argv[0]);
        return 1;
    }

    struct sigaction sig = { 0 };
    sig.sa_handler = signal_handler;
    sig.sa_flags = SA_RESTART;
    sigemptyset(&sig.sa_mask);
    sigaction(SIGTERM, &sig, NULL);

    const char* const uri = argv[1];
    const char* const shm = argc == 4 ? argv[2] : NULL;
    const char* const wid = argc == 4 ? argv[3] : NULL;

    LV2UI_Bridge bridge = { 0 };

    bridge.uiobj = lv2ui_object_load(uri);
    if (bridge.uiobj == NULL)
    {
        fprintf(stderr, "lv2ui failed to load UI details, cannot continue!\n");
        return 1;
    }

    if (shm != NULL)
    {
        bridge.ipc = ipc_client_attach(shm, rbsize);
        if (bridge.ipc == NULL)
            goto fail;

        assert(bridge.ipc->ring_send->size != 0);
        assert(bridge.ipc->ring_recv->size != 0);
    }

    // FIXME hexa create shm
    const long long winId = wid != NULL ? atoll(wid) : 0;

    // create plug window
    GtkWidget* const window =
       #ifndef __APPLE__
        winId != 0 ? gtk_plug_new(winId) :
       #endif
        gtk_window_new(GTK_WINDOW_TOPLEVEL);

    if (window == NULL)
    {
        fprintf(stderr, "gtk window creation fail, cannot continue!\n");
        return 1;
    }

    LV2UI_Widget widget = NULL;
    LV2_URID_Map urid_map = { .handle = &bridge, .map = lv2ui_uri_map };
    const LV2_Feature feature_urid_map = { .URI = LV2_URID__map, .data = &urid_map };
    const LV2_Feature* features[] = {
        &feature_urid_map,
        NULL
    };
    bridge.uihandle = bridge.uiobj->desc->instantiate(bridge.uiobj->desc,
                                                      uri,
                                                      bridge.uiobj->bundlepath,
                                                      lv2ui_write_function,
                                                      &bridge,
                                                      &widget,
                                                      features);

    if (bridge.uihandle == NULL)
    {
        fprintf(stderr, "lv2ui failed to initialize, cannot continue!\n");
        goto fail;
    }

    if (widget == NULL)
    {
        fprintf(stderr, "lv2ui failed to provide a gtk2 widget, cannot continue!\n");
        bridge.uiobj->desc->cleanup(bridge.uihandle);
        goto fail;
    }

    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(widget));

    // handle any pending events before showing window
    lv2ui_idle(&bridge);

   #ifndef __APPLE__
    if (winId != 0)
    {
        gtk_widget_show_all(window);

        const Window win = gtk_plug_get_id(GTK_PLUG(window));

        Display* const display = XOpenDisplay(NULL);
        if (display != NULL)
        {
            XMapWindow(display, win);
            XFlush(display);
            XCloseDisplay(display);
        }

        // pass child window id to server side
        if (bridge.ipc != NULL)
        {
            const uint32_t msg_type = lv2ui_message_window_id;
            const uint64_t window_id = win;
            ipc_client_write(bridge.ipc, &msg_type, sizeof(uint32_t)) &&
            ipc_client_write(bridge.ipc, &window_id, sizeof(uint64_t));
            ipc_client_commit(bridge.ipc);
        }
    }
    else
   #endif
    if (shm == NULL)
    {
        g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
        gtk_widget_show_all(window);
    }

    pthread_t thread = { 0 };
    if (bridge.ipc != NULL)
    {
        pthread_create(&thread, NULL, lv2ui_thread_run, &bridge);
    }

    fprintf(stderr, "gtk ready '%s' %lld\n", shm, winId);

    gtk_main();

    if (bridge.ipc != NULL)
    {
        ipc_client_t* const ipc = bridge.ipc;
        bridge.ipc = NULL;
        pthread_join(thread, NULL);
        ipc_client_dettach(ipc);
    }

    bridge.uiobj->desc->cleanup(bridge.uihandle);

fail:
    lv2ui_uris_cleanup(&bridge.uiuris);
    lv2ui_object_unload(bridge.uiobj);
    return 0;
}
