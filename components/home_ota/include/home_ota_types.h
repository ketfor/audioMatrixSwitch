#pragma once
#ifndef __HOME_OTA_TYPES_H__
#define __HOME_OTA_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UPDATE_IDLE = 0,
    UPDATE_OTA_UPDATING,
    UPDATE_OTA_SUCCESSFULLY,
    UPDATE_RELEASE_NOTFOUND,
    UPDATE_OTA_FAIL,
    UPDATE_OTA_BEGIN_FAIL,
    UPDATE_OTA_FILEURL_ISEMPTY,
    UPDATE_OTA_CANNOT_GET_IMG_DESCR,
    UPDATE_OTA_SAME_VERSION,
    UPDATE_OTA_NOT_COMPLITE_DATA,
    UPDATE_OTA_IMAGE_CORRUPTED,
    UPDATE_WWW_UPDATING,
    UPDATE_WWW_SUCCESSFULLY,
    UPDATE_WWW_FAIL,
    UPDATE_WWW_FILEURL_ISEMPTY,
    UPDATE_WWW_NOT_COMPLITE_DATA,
    UPDATE_WWW_UPDATE_NOT_COMPLITE_DATA,
} home_ota_state_t;

typedef struct {
    uint64_t id;            // id
    char releaseName[32];   // name
    char releaseUrl[256];   // url
    char tagName[32];       // tag_name
    time_t published;       // published_at
    char fileOtaUrl[256];      // assets/browser_download_url
    char fileOtaName[64];      // assets/name   
    char fileWwwUrl[256];      // assets/browser_download_url
    char fileWwwName[64];      // assets/name 
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