#include "src/ipc.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include <gtk/gtk.h>
#include <gtk/gtkplug.h>
#include <lilv/lilv.h>
#include <lv2/ui/ui.h>
#include <X11/Xlib.h>
#include <sys/types.h>

typedef struct {
    char* bundlepath;
    void* lib;
    const LV2UI_Descriptor* desc;
} LV2UI_Object;

typedef struct {
    ipc_client_t* ipc;
    LV2UI_Object* uiobj;
    LV2UI_Handle uihandle;
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

    LilvNode* const gtk2uinode = lilv_new_uri(world, "http://lv2plug.in/ns/extensions/ui#GtkUI");
    if (gtk2uinode == NULL)
        goto error;

    void* uilib = NULL;
    const LV2UI_Descriptor* uidesc = NULL;

    LILV_FOREACH(uis, i, uis)
    {
        const LilvUI* const ui = lilv_uis_get(uis, i);

        if (! lilv_ui_is_a(ui, gtk2uinode))
            continue;

        const LilvNode* const bundlenode = lilv_ui_get_binary_uri(ui);
        char* const binarypath = lilv_file_uri_parse(lilv_node_as_uri(bundlenode), NULL);

        uilib = dlopen(binarypath, RTLD_NOW);
        lilv_free(binarypath);

        if (uilib == NULL)
        {
            fprintf(stderr, "could not load UI binary: %s\n", dlerror());
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
            break;

        if (uilib != NULL)
        {
            dlclose(uilib);
            uilib = NULL;
        }
    }

    if (uilib == NULL || uidesc == NULL)
        goto error;

    const LilvNode* const bundlenode = lilv_plugin_get_bundle_uri(plugin);

    LV2UI_Object* const uiobj = malloc(sizeof(LV2UI_Object));
    uiobj->bundlepath = lilv_file_uri_parse(lilv_node_as_uri(bundlenode), NULL);
    uiobj->lib = uilib;
    uiobj->desc = uidesc;

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

static void lv2ui_write_function(LV2UI_Controller controller,
                                 uint32_t port_index,
                                 uint32_t buffer_size,
                                 uint32_t format,
                                 const void* buffer)
{
    LV2UI_Bridge* const bridge = controller;

    ipc_client_write(bridge->ipc, &port_index, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, &buffer_size, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, &format, sizeof(uint32_t)) &&
    ipc_client_write(bridge->ipc, buffer, buffer_size);

    if (ipc_client_commit(bridge->ipc))
        ipc_client_wake(bridge->ipc);
}

static int lv2ui_idle(void* const ptr)
{
    LV2UI_Bridge* const bridge = ptr;

    uint32_t size = 0;
    void* buffer = NULL;

    while (ipc_client_read_size(bridge->ipc) != 0)
    {
        uint32_t port_index, buffer_size, format;
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
                    fprintf(stderr, "lv2ui out of memory, abort!\n");
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

        fprintf(stderr, "lv2ui ringbuffer data race, abort!\n");
        abort();
    }

    free(buffer);

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

    const char* const uri = argv[1];
    const char* const shm = argc > 2 ? argv[2] : NULL;
    const char* const wid = argc > 3 ? argv[3] : NULL;

    LV2UI_Bridge bridge = { 0 };

    bridge.uiobj = lv2ui_object_load(uri);
    if (bridge.uiobj == NULL)
    {
        fprintf(stderr, "lv2ui failed to load UI details, cannot continue!\n");
        return 1;
    }

    if (shm != NULL)
    {
        const uint32_t rbsize = 0x7fff;
    
        bridge.ipc = ipc_client_attach(shm, rbsize);
        if (bridge.ipc == NULL)
            goto fail;
    }

    // FIXME hexa create shm
    const long long winId = wid != NULL ? atoll(wid) : 0;

    // create plug window
    GtkWidget* const window = winId != 0
                            ? gtk_plug_new(winId)
                            : gtk_window_new(GTK_WINDOW_TOPLEVEL);

    if (window == NULL)
    {
        fprintf(stderr, "gtk window creation fail, cannot continue!\n");
        return 1;
    }

    LV2UI_Widget widget = NULL;
    const LV2_Feature* features[] = { NULL };
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

    if (winId != 0)
    {
        gtk_widget_show_all(window);

        Display* const display = XOpenDisplay(NULL);
        if (display != NULL)
        {
            const Window win = gtk_plug_get_id(GTK_PLUG(window));
            // XResizeWindow(display, win, 1200, 600);
            XMapWindow(display, win);
            XFlush(display);
            XCloseDisplay(display);
        }
    }
    // NOTE: in case of shm but no wid, we dont show the UI
    // can happen in case of host using LV2 idle + show interface without parent feature
    else if (shm == NULL)
    {
        g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);
        gtk_widget_show_all(window);
    }

    pthread_t thread = { 0 };
    if (bridge.ipc != NULL)
    {
        ipc_client_wake(bridge.ipc);
        pthread_create(&thread, NULL, lv2ui_thread_run, &bridge);
    }

    fprintf(stderr, "gtk ready '%s' %lld\n", shm, winId);

    gtk_main();

    if (bridge.ipc != NULL)
    {
        ipc_client_t* const ipc = bridge.ipc;
        bridge.ipc = NULL;
        // TODO ipc_client_signal(); ??
        pthread_join(thread, NULL);
        ipc_client_dettach(ipc);
    }

    bridge.uiobj->desc->cleanup(bridge.uihandle);

fail:
    lv2ui_object_unload(bridge.uiobj);
    return 0;
}
