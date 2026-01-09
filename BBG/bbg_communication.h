#ifndef BBG_COMMUNICATION_H
#define BG_COMMUNICATION_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "park_msg_protocol.h"

void load_config(char *ip, int *port, char *bus, int *addr);
void handle_sigint(int sig);
int connect_to_server(const char *ip, int port);
int init_i2c(const char *bus, int addr);
ssize_t read_i2c_message(int fd, parking_message_t *msg);
int send_to_server(int sock, const parking_message_t *msg);

#endif // BG_COMMUNICATION_H