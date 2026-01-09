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

#include "park_msg_protocol.h"

#define I2C_BUS       "/dev/i2c-2"   
#define I2C_ADDR      0x10          // match STM32 STM32 slave 7-bit address
#define SERVER_IP     "192.168.7.1"
#define SERVER_PORT   8080
#define MSG_BUF_SIZE  128
#define POLL_INTERVAL_SEC  1

static int running = 1;

void handle_sigint(int sig);
int connect_to_server(const char *ip, int port);
int init_i2c(const char *bus, int addr);
ssize_t read_i2c_message(int fd, parking_message_t *msg);
int send_to_server(int sock, const parking_message_t *msg);

int main(void) {
    signal(SIGINT, handle_sigint);

    // verify size of parking_message_t
    // printf("Server sizeof(msg) = %zu\n", sizeof(parking_message_t));
    
    int i2c_fd = init_i2c(I2C_BUS, I2C_ADDR);
    if (i2c_fd < 0) {
        fprintf(stderr, "Failed to init I2C, exiting.\n");
        return 1;
    }

    int sock = -1;
    parking_message_t msg;

    while (running) {
        if (sock < 0) {
            sock = connect_to_server(SERVER_IP, SERVER_PORT);
            if (sock < 0) {
                printf("Retrying server connection in 3 sec...\n");
                sleep(3);
                continue;
            }
        }

        ssize_t len = read_i2c_message(i2c_fd, &msg);

        if (len > 0 ) {
           
            if (send_to_server(sock, &msg) < 0) {
                printf("Server send failed, will reconnect.\n");
                close(sock);
                sock = -1;
            }
            usleep(100000); // 100ms
        } else {
            // printf("No valid message received.\n");
            usleep(500000); // 500ms
            continue;
        }
    }

    if (sock >= 0) close(sock);
    
    close(i2c_fd);
    printf("Terminated.\n");
    return 0;
}

/*--------------------------------------------
 * Handling signals
 *-------------------------------------------*/
void handle_sigint(int sig) {
    (void)sig;
    running = 0;
    printf("\nExiting gracefully...\n");
}

/*--------------------------------------------
 * Connect to TCP server
 *-------------------------------------------*/
int connect_to_server(const char *ip, int port) {
    int sock;
    struct sockaddr_in server_addr;

    // Create TCP socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        return -1;
    }
    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        close(sock);
        return -1;
    }
    // Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connection failed");
        close(sock);
        return -1;
    }

    printf("Connected to server %s: %d\n", ip, port);
    return sock;
}

/*--------------------------------------------
 * Initialize I2C device
 *-------------------------------------------*/
int init_i2c(const char *bus, int addr) {
    int fd = open(bus, O_RDWR);
    if (fd < 0) {
        perror("open I2C bus failed");
        exit(EXIT_FAILURE);
    }

    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("ioctl I2C_SLAVE failed");
        close(fd);
        exit(EXIT_FAILURE);
    }

    printf("I2C initialized: bus=%s, addr=0x%02X\n", bus, addr);
    return fd;
}

/*--------------------------------------------
 * Read message from STM32
 *-------------------------------------------*/
ssize_t read_i2c_message(int fd, parking_message_t *msg) {
    
    ssize_t bytes_read = read(fd, msg, sizeof(parking_message_t));

    if (bytes_read < 0) {
        perror("I2C read failed");
        return -1;
    }

    if (bytes_read != sizeof(parking_message_t)) {
        fprintf(stderr, "Warning: incomplete or Empty I2C message (got %zd / %zu bytes)\n",
                bytes_read, sizeof(parking_message_t));
        return -1;
    }
    if(msg->is_parking == -1){
        printf("No message. waiting...\n");
        return -1;
    } 
    
    // sscanf((char*)buf, "%s,%f,%f,%d,%llu", 
    //    msg->vehicle_id, msg->lat, msg->lon, msg->is_parking, msg->time);

    // Optional: debug printout
    printf("Received I2C message:\n");
    printf("  Vehicle ID : %s\n", msg->vehicle_id);
    printf("  Latitude   : %f\n", msg->lat);
    printf("  Longitude  : %f\n", msg->lon);
    printf("  Parking    : %d\n", msg->is_parking);

    time_t real_now = time(NULL);
    msg->time = real_now; // override time with server time
    printf("  Timestamp  : %llu\n", msg->time);

    return bytes_read;
}

/*--------------------------------------------
 * Send message to server
 *-------------------------------------------*/
int send_to_server(int sock, const parking_message_t *msg) {
    int len = sizeof(parking_message_t);
    if (msg == NULL) return 0;
    ssize_t sent = send(sock, msg, len, 0);
    if (sent < 0) {
        perror("sending to server failed");
        return -1;
    }
    printf("sent message successfully\n");

    return 0;
}