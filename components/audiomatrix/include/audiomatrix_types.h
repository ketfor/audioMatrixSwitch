#pragma once
#ifndef __AUDIOMATRIX_TYPES_H__
#define __AUDIOMATRIX_TYPES_H__

#include <stdint.h>
#include "audiomatrix_event_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IN_PORTS CONFIG_AM_DEVICE_IN_PORTS
#define OUT_PORTS CONFIG_AM_DEVICE_OUT_PORTS

#define CLASS_DISABLE 0
#define CLASS_SWITCH 1
#define CLASS_SELECT 2

// inputs
typedef struct {
    uint8_t num;
    char name[16]; // from param
    char formatedName[16]; // by name
    char shortName[4]; // from param
    char longName[32]; // from param
} input_t;

// outputs
typedef struct {
    uint8_t class; // from param
    uint8_t num;
    char name[16]; // from param english name
    char formatedName[16]; // by name
    char shortName[4]; // from param short cyrillic name
    char longName[32]; // from param long cyrillic name
	char objectId[49]; // by device.name+object.name "sublightkitchen_do_not_disturb" 
    char uniqueId[40]; // device.identifier+object_class+object.name "0xa4c138fe6784_switch_do_not_disturb_z2mone" 
	char commandTopic[76]; // by param + "/set/out%d" "myhome/audioamatrix2/set/out1"
    uint8_t inputPort;
} output_t;

// device
typedef struct {
    input_t inputs[IN_PORTS];
    output_t outputs[OUT_PORTS];
	char identifier[16]; // from MAC [0] "0xa4c138fe6784"
    char name[32]; // from param "Audiomatrix"
    char formatedName[32]; // by name
	char manufacturer[32]; // from KConfig "Vedrel"
	char model[32]; // from KConfig "Audiomatrix switch"
	char modelId[32]; // from KConfig "Audiomatrix switch 3x4"
    char hwVersion[32]; // from KConfig
    char swVersion[32]; // from KConfig
    char configurationUrl[64]; // from param
    char stateTopic[64]; // "z2mone/bridge/state"
    char hassTopic[64]; // "/homeassistant"
} device_t;

#ifdef __cplusplus
}
#endif

#endif //__AUDIOMATRIX_TYPES_H__