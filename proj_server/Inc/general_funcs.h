#ifndef GENERAL_FUNCTIONS_H
#define GENERAL_FUNCTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>    // for time functions
#include <pthread.h> // for mutexes

#include "pricing_db_handling.h"
/* Declare variables as extern so all .c files can see them */
extern int server_port; 
extern char server_ip[32];
extern char pricing_db_path[16];
extern shm_pricing_block_t *shm_ptr;

/* Function Prototypes */
void log_event(const char *message);
void read_configurations(int *server_port, char *server_ip, char *pricing_db_path);

#endif // GENERAL_FUNCTIONS_H