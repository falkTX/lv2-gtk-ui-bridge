// Copyright 2024 Filipe Coelho <falktx@falktx.com>
// SPDX-License-Identifier: ISC

#include "ipc/ipc.h"
#include <lv2/ui/ui.h>

const uint32_t rbsize = 0x7fff;

typedef enum {
    lv2ui_message_null,
    lv2ui_message_port_event,
    lv2ui_message_touch_event,
    lv2ui_message_window_id,
} LV2UI_Bridge_Message_Type;
