#pragma once
#ifndef __HOME_OTA_TYPES_H__
#define __HOME_OTA_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HOME_OTA_IDLE = 0,
    HOME_OTA_UPDATING,
} home_ota_state_t;

typedef struct {
    char releaseName[32];   // name
    char releaseUrl[256];   // url
    char tagName[32];       // tag_name
    char published[21];     // published_at
    char fileUrl[256];      // assets/browser_download_url
    char fileName[64];      // assets/name   
} release_t;

typedef struct {
    release_t releases[5];
    uint8_t countReleases;
    time_t lastCheck;
    char currentRelease[32];
    home_ota_state_t otaState;
} releaseInfo_t;

#ifdef __cplusplus
}
#endif

#endif // __HOME_OTA_TYPES_H__