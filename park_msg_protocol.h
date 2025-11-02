#ifndef PARK_MESSAGE_H
#define PARK_MESSAGE_H

#include <stdint.h>    // for fixed-size integer types if needed
#include <time.h>      // for time_t

typedef enum {
    END = 0,
    START = 1,
} Parking;

#pragma pack(1)  // Disable padding
typedef struct parking_message_t {
    char vehicle_id[8];
    double lat;
    double lon;
    Parking is_parking;   // 1 = START (parking), 0 = END (moved)
    time_t time;         // timestamp of this message
} parking_message_t;
#pragma pack()  // Restore default packing

#endif // PARK_MESSAGE_H