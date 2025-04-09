#pragma once
#ifndef __HOME_WEB_SERVER_TYPES_H__
#define __HOME_WEB_SERVER_TYPES_H__

#include "esp_vfs.h"
#include "home_web_server_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (4096)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

//

#ifdef __cplusplus
}
#endif

#endif //__HOME_WEB_SERVER_TYPES_H__