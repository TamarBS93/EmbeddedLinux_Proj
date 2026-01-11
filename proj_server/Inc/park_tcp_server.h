#ifndef PARK_TCP_SERVER_H
#define PARK_TCP_SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <math.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>

#include "park_msg_protocol.h"
#include "pricing_db_handling.h"

/* Function Prototypes */
void cleanup_resources();
void handle_sigint(int sig);
void *handle_client(void *arg);
void handle_message(parking_message_t msg);

int calc_price(sqlite3 *db, parking_message_t msg);
float price_per_area(int area);
int what_area(float lat, float lon);

#endif // PARK_TCP_SERVER_H