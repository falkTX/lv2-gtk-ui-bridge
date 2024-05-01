#include "calf-x11-guis.h"

#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtk/gtkplug.h>
#include <lilv/lilv.h>
#include <X11/Xlib.h>

typedef struct {
    SharedData* shared;
    char* bundlepath;
    void* lib;
    const LV2UI_Descriptor* desc;
    LV2UI_Handle handle;
} CalfBridge;

static CalfBridge* bridge_init(const char* const uri)
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

    CalfBridge* const bridge = malloc(sizeof(CalfBridge));
    bridge->bundlepath = lilv_file_uri_parse(lilv_node_as_uri(bundlenode), NULL);
    bridge->lib = uilib;
    bridge->desc = uidesc;
    return bridge;

error:
    lilv_world_free(world);
    return NULL;
}

static void bridge_close(CalfBridge* const bridge)
{
    lilv_free(bridge->bundlepath);

    if (bridge->desc != NULL && bridge->handle != NULL)
        bridge->desc->cleanup(bridge->handle);

    if (bridge->lib != NULL)
        dlclose(bridge->lib);

    free(bridge);
}

static void lv2ui_write_function(LV2UI_Controller controller,
                                 uint32_t portIndex,
                                 uint32_t bufferSize,
                                 uint32_t format,
                                 const void* buffer)
{
    CalfBridge* const bridge = controller;

    jack_ringbuffer_write(&bridge->shared->rb, (const char*)&portIndex, sizeof(portIndex));
    jack_ringbuffer_write(&bridge->shared->rb, (const char*)&bufferSize, sizeof(bufferSize));
    jack_ringbuffer_write(&bridge->shared->rb, (const char*)&format, sizeof(format));
    jack_ringbuffer_write(&bridge->shared->rb, buffer, bufferSize);
    sem_post(&bridge->shared->sem);
}

int main(int argc, char* argv[])
{
    if (! gtk_init_check(&argc, &argv))
    {
        fprintf(stderr, "could not init gtk2, cannot continue!\n");
        return 1;
    }

    if (argc != 2 || argc != 4)
    {
        fprintf(stderr, "usage: %s <lv2-uri> [shm-access-key] [x11-ui-parent]\n", argv[0]);
        return 1;
    }

    const char* const uri = argv[1];
    const char* const shm = argc > 2 ? argv[2] : NULL;
    const char* const wid = argc > 3 ? argv[3] : NULL;

    CalfBridge* const bridge = bridge_init(uri);
    if (bridge == NULL)
        return 1;

    // TODO create shm
    const int winId = 0;

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
    bridge->handle = bridge->desc->instantiate(bridge.desc,
                                               uri,
                                               bundlepath,
                                               lv2ui_write_function,
                                               &widget,
                                               &widget,
                                               features);
    lilv_free(bundlepath);

    if (bridge->handle == NULL)
    {
        fprintf(stderr, "lv2ui failed to initialize, cannot continue!\n");
        goto fail;
    }
    if (widget == NULL)
    {
        fprintf(stderr, "lv2ui failed to provide a gtk2 widget, cannot continue!\n");
        goto fail;
    }

    gtk_container_add(GTK_CONTAINER(window), widget);
    gtk_widget_show_all(window);

    if (winId != 0)
    {
        Display* const display = XOpenDisplay(NULL);

        if (display != NULL)
        {
            XMapWindow(display, gtk_plug_get_id(GTK_PLUG(window)));
            XFlush(display);
            XCloseDisplay(display);
        }
    }
    else
    {
    }

    gtk_main();
    return 0;

fail:
    bridge_close(bridge);
    return 1;
}
