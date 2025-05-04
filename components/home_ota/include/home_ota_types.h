#pragma once
#ifndef __HOME_OTA_TYPES_H__
#define __HOME_OTA_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HOME_OTA_IDLE = 0,
    HOME_OTA_UPDATING,
    HOME_OTA_UPDATE_FAIL,
    HOME_OTA_UPDATE_BEGIN_FAIL,
    HOME_OTA_UPDATE_RELEASE_NOTFOUND,
    HOME_OTA_UPDATE_RELEASE_FILEURL_ISEMPTY,
    HOME_OTA_UPDATE_CANNOT_GET_IMG_DESCR,
    HOME_OTA_UPDATE_SAME_VERSION,
    HOME_OTA_UPDATE_NOT_COMPLITE_DATA,
    HOME_OTA_UPDATE_IMAGE_CORRUPTED

    
} home_ota_state_t;

typedef struct {
    uint64_t id;            // id
    char releaseName[32];   // name
    char releaseUrl[256];   // url
    char tagName[32];       // tag_name
    time_t published;       // published_at
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