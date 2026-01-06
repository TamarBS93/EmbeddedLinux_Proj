#ifndef PARK_MESSAGE_H
#define PARK_MESSAGE_H

#include <stdint.h>    // for fixed-size integer types if needed
#include <time.h>      // for time_t

typedef enum {
    PARK_END = 0,
    PARK_START = 1,
} Parking;

#pragma pack(1)  // Disable padding
typedef struct __attribute__((packed)) {
    char vehicle_id[11];
    float lat;
    float lon;
    int8_t is_parking;
    uint64_t time;
} parking_message_t;
#pragma pack()  // Restore default packing

#endif // PARK_MESSAGE_H
