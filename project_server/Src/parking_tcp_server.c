/**
for me-
For passing between wsl and windows-
In windows shell:
sudo netsh interface portproxy add v4tov4 listenport=8080 listenaddress=192.168.7.1 connectport=8080 connectaddress=$wsl_ip

Server listens on 0.0.0.0
Client sends to 192.168.7.1
*/

/**
 * @file parking_tcp_server.c
 * @brief TCP Server for Parking System with centralized logging.
 * * This server handles incoming vehicle data, manages a SQLite database, 
 * and utilizes shared memory for real-time pricing calculations. All events
 * are recorded in server_system.log.
 */

#include "general_funcs.h"
#include "pricing_db_handling.h"
#include "park_tcp_server.h"

/* Global Variables */
int server_port;
char server_ip[32];
char pricing_db_path[16];
shm_pricing_block_t *shm_ptr = NULL;

pthread_mutex_t parking_db_mutex = PTHREAD_MUTEX_INITIALIZER;

int running = 1;
sqlite3 *parking_db = NULL;
int server_fd = -1;
int shmid = -1;
pid_t child_pid; // Global variable to store child process ID

/**
 * @brief Signal handler for graceful termination via SIGINT or SIGTERM.
 * @param sig Signal number received.
 */
void handle_sigint(int sig) {
    running = 0;
    char log_msg[64];
    snprintf(log_msg, sizeof(log_msg), "Shutdown signal (%d) received.", sig);
    log_event(log_msg);

    if (child_pid > 0) { 
        // The parent proccess (knows the child's ID as 'child_pid') kills the child process
        snprintf(log_msg, sizeof(log_msg), "Parent: Killing child process %d", child_pid);
        log_event(log_msg);
        kill(child_pid, SIGTERM); 
    }
    cleanup_resources();
    _exit(0); 
}

/**
 * @brief Main entry point for the TCP Server.
 */
int main() {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    
    //read configurations
    read_configurations(&server_port, server_ip, pricing_db_path);
    // Fork the process
    child_pid = fork();

    if (child_pid < 0) {
        perror("Fork failed");
        exit(1);
    }
    if (child_pid == 0) {
        // --- CHILD PROCESS ---
        // This process handles the Pricing and DB watching
        pricing_handle_process();
    } else {
        // --- PARENT PROCESS ---
        // This process handles the TCP Server
        log_event("Integrated Server Starting...");
        
        char log_msg[130];

        // Initialize Database
        if (sqlite3_open("DBs/parking.db", &parking_db)) { 
            snprintf(log_msg, sizeof(log_msg), "DB Open Error: %s", sqlite3_errmsg(parking_db));
            log_event(log_msg);
            return 1;
        }

        const char *sql = "CREATE TABLE IF NOT EXISTS PARKING("
                        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
                        "VEHICLE_ID TEXT,"
                        "LAT REAL,"
                        "LON REAL,"
                        "AREA INTEGER,"
                        "START_TIME INTEGER,"
                        "END_TIME INTEGER,"
                        "PRICE REAL);";

        char *err_msg = 0;
        if (sqlite3_exec(parking_db, sql, 0, 0, &err_msg) != SQLITE_OK) {
            log_event("SQL Table Creation Error for Parking DB");
            sqlite3_free(err_msg);
        } else {
            log_event("Parking Database table verified/created successfully.");
        }

        // Shared Memory Initialization
        key_t key = ftok(pricing_db_path, 65);
        if (key == -1) { log_event("FTOK Failure"); exit(1); }

        shmid = shmget(key, sizeof(shm_pricing_block_t), 0666 | IPC_CREAT);
        if (shmid < 0) { log_event("SHMGET Failure"); exit(1); }

        shm_ptr = (shm_pricing_block_t *)shmat(shmid, NULL, 0);
        if (shm_ptr == (void *)-1) { log_event("SHMAT Failure"); exit(1); }

        while (shm_ptr->ready == 0) {
            log_event("Waiting for Pricing Process to initialize SHM...");
            sleep(1);
        }
        log_event("SHM attached successfully by TCP Server.");

        // Network Initialization
        struct sockaddr_in server_addr, client_addr;
        socklen_t addr_len = sizeof(client_addr);

        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            log_event("Socket Creation Failed");
            exit(EXIT_FAILURE);
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(server_port);
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

        if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            log_event("Bind Failed: check if port is in use");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        listen(server_fd, 5);
        snprintf(log_msg, sizeof(log_msg), "TCP Server listening on %s:%d", server_ip, server_port);
        log_event(log_msg);

        while (running) {
            int new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (new_socket < 0) continue;

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            snprintf(log_msg, sizeof(log_msg), "New connection from %s", client_ip);
            log_event(log_msg);

            pthread_t thread_id;
            int *client_sock = malloc(sizeof(int));
            *client_sock = new_socket;
            
            pthread_create(&thread_id, NULL, handle_client, client_sock);
            pthread_detach(thread_id); 
        }

        cleanup_resources();
    }

    return 0;
}

/**
 * @brief Thread routine to handle communication with a connected client.
 * * This function is executed in a detached thread for every new connection.
 * It reads incoming parking messages from the socket and passes them to the 
 * message handler until the client disconnects or an error occurs.
 * * @param arg A void pointer to the integer client socket file descriptor (allocated on heap).
 * @return void* Always returns NULL upon thread termination.
 */
void *handle_client(void *arg)
{
    int client_socket = *(int *)arg;
    free(arg);

    parking_message_t msg;
    int bytes_read;
    
    while ((bytes_read = recv(client_socket, &msg, sizeof(parking_message_t), 0)) > 0)
    {
        handle_message(msg);
    }

    close(client_socket);
    return NULL;
}

/**
 * @brief Processes the parking message and updates the database.
 * @param msg The received parking data structure.
 */
void handle_message(parking_message_t msg) {
    char log_msg[130];
    snprintf(log_msg, sizeof(log_msg), "Msg Received: ID=%s, State=%d", msg.vehicle_id, msg.is_parking);
    log_event(log_msg);

    pthread_mutex_lock(&parking_db_mutex);

    if (msg.is_parking == PARK_START) {
        sqlite3_stmt *stmt;
        const char *sql = "INSERT INTO PARKING (VEHICLE_ID, LAT, LON, START_TIME) VALUES (?, ?, ?, ?);";
        
        if (sqlite3_prepare_v2(parking_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, msg.vehicle_id, -1, SQLITE_STATIC);
            sqlite3_bind_double(stmt, 2, msg.lat);
            sqlite3_bind_double(stmt, 3, msg.lon);
            sqlite3_bind_int64(stmt, 4, (sqlite3_int64)msg.time);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
        log_event("Parking START recorded in DB.");
    } 
    else if (msg.is_parking == PARK_END) {
        calc_price(parking_db, msg);
    }

    pthread_mutex_unlock(&parking_db_mutex);
}

/**
 * @brief Determines the parking area ID based on coordinates stored in SHM.
 * @param lat Latitude of the vehicle.
 * @param lon Longitude of the vehicle.
 * @return int Area ID or -1 if not found.
 */
int what_area(float lat, float lon) {
    int area_id = -1;
    pthread_mutex_lock(&shm_ptr->shm_mutex);
    for (int i = 0; i < shm_ptr->num_pricing_areas; i++) {
        if (lat >= shm_ptr->table[i].lat_min && lat <= shm_ptr->table[i].lat_max &&
            lon >= shm_ptr->table[i].lon_min && lon <= shm_ptr->table[i].lon_max) {
            area_id = shm_ptr->table[i].area_id;
            break;
        }
    }
    pthread_mutex_unlock(&shm_ptr->shm_mutex);
    return area_id;
}

/**
 * @brief Retrieves the price per minute for a specific area from SHM.
 * @param area The Area ID.
 * @return float Price per minute.
 */
float price_per_area(int area) {
    float price = 0.0f;
    pthread_mutex_lock(&shm_ptr->shm_mutex);
    for (int i = 0; i < shm_ptr->num_pricing_areas; i++) {
        if (shm_ptr->table[i].area_id == area) {
            price = shm_ptr->table[i].price_per_min;
            break;
        }
    }
    pthread_mutex_unlock(&shm_ptr->shm_mutex);
    return price;
}

/**
 * @brief Finalizes parking session and calculates total fee.
 * @param db Pointer to the active SQLite database.
 * @param msg The exit message from the vehicle.
 * @return int 0 on success.
 */
int calc_price(sqlite3 *db, parking_message_t msg) {
    sqlite3_stmt *stmt;
    time_t start_time;
    
    const char *sel = "SELECT START_TIME FROM PARKING WHERE VEHICLE_ID = ? AND END_TIME IS NULL;";
    sqlite3_prepare_v2(db, sel, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, msg.vehicle_id, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        start_time = sqlite3_column_int64(stmt, 0);
    } else {
        log_event("Error: No open parking record found for calc_price.");
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);

    int area = what_area(msg.lat, msg.lon);
    float rate = price_per_area(area);
    double duration = difftime(msg.time, start_time) / 60.0;
    float total = (float)(duration * rate);

    const char *upd = "UPDATE PARKING SET AREA=?, END_TIME=?, PRICE=ROUND(?,2) "
                      "WHERE VEHICLE_ID=? AND END_TIME IS NULL;";
    sqlite3_prepare_v2(db, upd, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, area);
    sqlite3_bind_int(stmt, 2, msg.time);
    sqlite3_bind_double(stmt, 3, total);
    sqlite3_bind_text(stmt, 4, msg.vehicle_id, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    char log_msg[130];
    snprintf(log_msg, sizeof(log_msg), "Parking END updated in DB. Billing %s | Total: %.2f for Area: %d", msg.vehicle_id, total, area);
    log_event(log_msg);
    return 0;
}

/**
 * @brief Cleans up IPC and Network resources.
 */
void cleanup_resources() {
    if (parking_db) sqlite3_close(parking_db);
    if (shm_ptr && shm_ptr != (void*)-1) shmdt(shm_ptr);
    if (shmid != -1) shmctl(shmid, IPC_RMID, NULL);
    if (server_fd > 0) close(server_fd);
    log_event("Resources released. Server down.");
}