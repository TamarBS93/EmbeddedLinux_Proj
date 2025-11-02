#ifndef PRICING_DB_H
#define PRICING_DB_H

#include <stdint.h>    // for fixed-size integer types if needed
#include <pthread.h>

#pragma pack(1)  // Disable padding
typedef struct {
    int area_id;
    float lat_min, lat_max;
    float lon_min, lon_max;
    float price_per_min;
} pricing_entry_t;
#pragma pack()  // Restore default packing

#pragma pack(1)  // Disable padding
typedef struct {
    pthread_mutex_t shm_mutex;          // shared between processes
    int num_pricing_areas;
    pricing_entry_t table[100];
} shm_pricing_block_t;
#pragma pack()  // Restore default packing

#endif // PRICING_DB_H