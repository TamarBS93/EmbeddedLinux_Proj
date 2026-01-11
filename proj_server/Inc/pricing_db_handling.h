#ifndef PRICING_DB_H
#define PRICING_DB_H

#include <stdint.h>    // for fixed-size integer types if needed
#include <stdio.h>      
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>     // for mutexes
#include <sqlite3.h>     // for SQLite database handling
#include <sys/ipc.h>     // for IPC keys
#include <sys/shm.h>     // for shared memory
#include <sys/inotify.h> // for inotify
#include <limits.h>      // for NAME_MAX
#include <sys/stat.h>    // for stat()

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
    pthread_mutex_t shm_mutex;          // SHM shared between processes
    pthread_mutex_t log_mutex;          // logging file shared between processes
    int num_pricing_areas;
    volatile int ready;          // 0 = not ready, 1 = ready
    pricing_entry_t table[100];
} shm_pricing_block_t;
#pragma pack()  // Restore default packing

/* Declare variables as extern so all .c files can see them */
extern int server_port;
extern char server_ip[32];
extern char pricing_db_path[16];
extern shm_pricing_block_t *shm_ptr;

/* Declare Functions */
int pricing_handle_process();
sqlite3* create_pricing_db(void);
int load_pricing_into_shm(sqlite3 *db, pricing_entry_t *table);
void watch_db_file(const char *path, sqlite3 *db);

#endif // PRICING_DB_H