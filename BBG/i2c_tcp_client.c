/**
 * @file i2c_tcp_client.c
 * @brief BeagleBone Green communication system.
 * * Handles I2C data collection from STM32 and Ethernet transmission to a TCP server
 * using a multi-process architecture and IPC pipes.
 */

#include "bbg_communication.h"

static int running = 1;
int is_parent = 0;

/**
 * @brief Main entry point. Implements Process 1 and Process 2 architecture via fork and pipe.
 */
int main(void) {
    char server_ip[32];
    int server_port;
    char i2c_bus[32];
    int i2c_addr;

    // Load external parameters 
    load_config(server_ip, &server_port, i2c_bus, &i2c_addr);

    int pipefd[2]; // pipefd[0] is read, pipefd[1] is write
    
    // When SIGINT/SIGTERM send to handle_sigint
    signal(SIGINT, handle_sigint);  // For Ctrl+C
    signal(SIGTERM, handle_sigint); // For systemctl stop
    // Ignore SIGPIPE so the program doesn't crash when server disconnects
    signal(SIGPIPE, SIG_IGN);

    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return 1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) {
        /* PROCESS 2: I2C COMMUNICATION */
        is_parent = 0;
        close(pipefd[0]); // Close read end
        int i2c_fd = init_i2c(i2c_bus, i2c_addr);
        parking_message_t msg;

        while (running) {
            if (read_i2c_message(i2c_fd, &msg) > 0) {
                // Pass data to Process 1 via PIPE
                write(pipefd[1], &msg, sizeof(parking_message_t));
            }
            else{
                if (msg.is_parking != -1){
                    log_event("Failed to read from I2C");
                }
            }
            usleep(500000); 
        }
        close(i2c_fd);
        close(pipefd[1]);
        exit(0);

    } else {
        /* PROCESS 1: TCP ETHERNET CLIENT */
        is_parent = 1;
        close(pipefd[1]); // Close write end
        int sock = -1;
        parking_message_t msg_from_pipe;

        while (running) {
            if (sock < 0) {
                sock = connect_to_server(server_ip, server_port);
                if (sock < 0) {
                    close(sock);
                    sleep(3);
                    continue;
                }
            }

            // Read from PIPE (blocks until Process 2 writes data) 
            ssize_t n = read(pipefd[0], &msg_from_pipe, sizeof(parking_message_t));
            if (n > 0) {
                if (send_to_server(sock, &msg_from_pipe) < 0) {
                    log_event("Server disconnected. Attempting to reconnect...");
                    close(sock);
                    sock = -1;
                }
            }
        }
        if (sock >= 0) close(sock);
        close(pipefd[0]);
        wait(NULL); // Cleanup child process
    }

    log_event("Program Terminated.\n");
    return 0;
}

/**
 * @brief Signal handler for graceful termination.
 * @param sig Signal number received.
 */
void handle_sigint(int sig) {
    running = 0; // Breaks the while loops in both processes
    
    if (is_parent) {
        if (sig == SIGINT) {
            log_event("Termination requested via SIGINT (Ctrl+C).");
        } else if (sig == SIGTERM) {
            log_event("Termination requested via SIGTERM (systemctl stop).");
        }
        log_event("Parent (TCP Client) shutting down...");
    }
}

/**
 * @brief Establishes a connection to the TCP Server.
 * @param ip Server IP address string.
 * @param port Server port number.
 * @return Socket file descriptor, or -1 on failure.
 */
int connect_to_server(const char *ip, int port) {
    int sock;
    struct sockaddr_in server_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        return -1;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connection failed");
        close(sock);
        return -1;
    }
    char log_msg[100];
    snprintf(log_msg, sizeof(log_msg), "Connected successfuly to server %s:%d", ip, port);
    log_event(log_msg);
    return sock;
}

/**
 * @brief Initializes the I2C bus and sets slave address.
 * @param bus Path to the I2C bus device.
 * @param addr Slave address of the STM32.
 * @return File descriptor for I2C bus.
 */
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
    char log_msg[100];
    snprintf(log_msg, sizeof(log_msg),"I2C initialized: bus=%s, addr=0x%02X", bus, addr);
    log_event(log_msg);

    return fd;
}

/**
 * @brief Reads a parking message from the STM32 via I2C.
 * @param fd I2C file descriptor.
 * @param msg Pointer to message structure to fill.
 * @return Bytes read, or -1 on failure/empty message.
 */
ssize_t read_i2c_message(int fd, parking_message_t *msg) {
    char log_msg[100];

    ssize_t bytes_read = read(fd, msg, sizeof(parking_message_t));

    if (bytes_read < 0) {
        perror("I2C read failed");
        return -1;
    }
    if (bytes_read != sizeof(parking_message_t)) {
        snprintf(log_msg, sizeof(log_msg), "Warning: incomplete or Empty I2C message (got %zd / %zu bytes)\n",
                bytes_read, sizeof(parking_message_t));
        return -1;
    }
    if(msg->is_parking == -1){
        log_event("No message. waiting 2 seconds...");
        sleep(2); // wait 3 seconds
        return -1;
    } 

    snprintf(log_msg, sizeof(log_msg),"Received I2C: ID=%s, Lat=%f, Lon=%f, Park=%d", 
            msg->vehicle_id, msg->lat, msg->lon, msg->is_parking);
    log_event(log_msg);
    
    msg->time = (uint64_t)time(NULL); 
    return bytes_read;
}

/**
 * @brief Sends a parking message to the TCP server.
 * @param sock Socket file descriptor.
 * @param msg Pointer to the message to send.
 * @return 0 on success, -1 on failure.
 */
int send_to_server(int sock, const parking_message_t *msg) {
    if (msg == NULL) return 0;
    if (send(sock, msg, sizeof(parking_message_t), 0) < 0) {
        log_event("sending to server failed");
        close(sock);
        return -1;
    }
    log_event("msg Sent to server successfully");
    return 0;
}

/**
 * @brief Loads all system parameters from an external CONFIG file. 
 * @param ip Buffer for Server IP.
 * @param port Pointer for Server Port.
 * @param bus Buffer for I2C Bus path.
 * @param addr Pointer for I2C Slave Address.
 */
void load_config(char *ip, int *port, char *bus, int *addr) {
    FILE *fp = fopen("bbg.config", "r");
    if (fp == NULL) {
        perror("Config file missing, using defaults");
        strcpy(ip, "192.168.7.1");
        *port = 8080;
        strcpy(bus, "/dev/i2c-2");
        *addr = 0x10;
        return;
    }

    // Parsing the file based on the keys
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "SERVER_IP=", 10) == 0) {
            sscanf(line, "SERVER_IP=%s", ip);
        } else if (strncmp(line, "SERVER_PORT=", 12) == 0) {
            sscanf(line, "SERVER_PORT=%d", port);
        } else if (strncmp(line, "I2C_BUS=", 8) == 0) {
            sscanf(line, "I2C_BUS=%s", bus);
        } else if (strncmp(line, "I2C_ADDR=", 9) == 0) {
            sscanf(line, "I2C_ADDR=%x", addr);
        }
    }

    fclose(fp);
    char log_msg[100];
    snprintf(log_msg, sizeof(log_msg), "Config Loaded: Server %s:%d, I2C %s (Addr: 0x%02X)", ip, *port, bus, *addr);
    log_event(log_msg);
}

/**
 * @brief Logs messages to a file with a timestamp.
 * @param message The string to log.
 */
void log_event(const char *message) {
    FILE *logfile = fopen("bbg_system.log", "a");
    if (logfile == NULL) return;

    time_t now = time(NULL);
    char *timestamp = ctime(&now);
    timestamp[strlen(timestamp) - 1] = '\0'; // Remove newline

    fprintf(logfile, "[%s] %s\n", timestamp, message);
    fclose(logfile);
}