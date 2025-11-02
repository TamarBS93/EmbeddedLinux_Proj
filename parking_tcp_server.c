#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <math.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "park_msg_protocol.h"
#include "pricing_db_handling.h"

pthread_mutex_t parking_db_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t pricing_db_mutex = PTHREAD_MUTEX_INITIALIZER;

void create_pricing_db (void);
void *handle_client(void *arg);
void handle_message(parking_message_t msg);
int what_area(float lat, float lon);
float price_per_area(int area);
int calc_price(sqlite3 *db, parking_message_t msg);

int server_port;
char server_ip[32];
size_t shm_size;
char pricing_db_path[16];
pricing_entry_t *pricing_table = NULL;
int num_pricing_areas = 0;


void read_configurations(){
    FILE *cfg = fopen("server.config", "r");
    if (cfg) {
        fscanf(cfg, "SERVER_PORT %d\n", &server_port);
        fscanf(cfg, "SERVER_IP %s\n", server_ip);
        server_ip[strcspn(server_ip, "\r\n")] = '\0';
        fscanf(cfg, "\nSHM_SIZE %zu\n", &shm_size);
        fscanf(cfg, "PRICING_DB %s\n", pricing_db_path);
        fclose(cfg);
    } else {
        perror("Could not open config file");
        exit(1);
    }
}


int main()
{
    read_configurations();

    const char *sql = NULL;

    // creates the Parking  DB file if it doesn't exist
    sqlite3 *parking_db;
    char *err_msg = 0;

    if (sqlite3_open("parking.db", &parking_db)) 
    { 
        printf("Can't open database: %s\n", sqlite3_errmsg(parking_db));
        return 1;
    }
    sql =
        "CREATE TABLE IF NOT EXISTS PARKING("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
        "VEHICLE_ID TEXT,"
        "LAT REAL,"
        "LON REAL,"
        "AREA INTEGER,"
        "START_TIME INTEGER,"
        "END_TIME INTEGER,"
        "PRICE REAL);";

    if (sqlite3_exec(parking_db, sql, 0, 0, &err_msg) != SQLITE_OK) {
        printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("PARKING Table created successfully\n");
    }

    // Shared memory for pricing table ------------------
    key_t key = ftok(pricing_db_path, 65);   // generate a unique key
    if (key == -1) { perror("ftok"); exit(1); }

    int shmid = shmget(key, sizeof(shm_pricing_block_t), 0666 | IPC_CREAT);
    if (shmid == -1) { perror("shmget"); exit(1); }

    // Load pricing table into shared memory
    shm_pricing_block_t *shm_ptr = shmat(shmid, NULL, 0);
    if (shm_ptr == (void *) -1) { perror("shmat"); exit(1); }

    pthread_mutex_lock(&shm_ptr->shm_mutex);
    pricing_table = shm_ptr->table;
    num_pricing_areas = shm_ptr->num_pricing_areas;
    pthread_mutex_unlock(&shm_ptr->shm_mutex);
    // ------------------------------------------------------

    // For testing purposes, simulate some parking messages
    handle_message((parking_message_t){"CAR123", 23.1, 65.967, 1, time(NULL) });
    handle_message((parking_message_t){"CAR456", 4, 63.5, 1, time(NULL) });
    sleep(1);
    handle_message((parking_message_t){"CAR123", 23.1, 65.967, 0, time(NULL) });

    // // Creating a socket: 
    // int server_fd, new_socket;
    // struct sockaddr_in server_addr, client_addr;
    // socklen_t addr_len = sizeof(client_addr);

    // if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    // {
    //     perror("Socket creation failed");
    //     exit(EXIT_FAILURE);
    // }

    // server_addr.sin_family = AF_INET;
    // // server_addr.sin_addr.s_addr = SERVER_IP;
    // server_addr.sin_port = htons(server_port);

    // if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0)
    // {
    //     perror("Invalid address / Address not supported");
    //     exit(EXIT_FAILURE);
    // }

    // if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    // {
    //     perror("Bind failed");
    //     close(server_fd);
    //     exit(EXIT_FAILURE);
    // }

    // if (listen(server_fd, 5) < 0)
    // {
    //     perror("Listen failed");
    //     close(server_fd);
    //     exit(EXIT_FAILURE);
    // }

    // printf("TCP server listening on port %d...\n", server_port);

    // while (1)
    // {
    //     new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    //     if (new_socket < 0)
    //     {
    //         perror("Accept failed");
    //         continue;
    //     }

    //     pthread_t thread_id;
    //     int *client_sock = malloc(sizeof(int));
    //     *client_sock = new_socket;
    //     pthread_create(&thread_id, NULL, handle_client, client_sock);
    //     pthread_detach(thread_id); // no need to join
    // }

    // close(server_fd);
    
    sqlite3_close(parking_db);
    shmdt(shm_ptr); // detach shm
    // shmctl(shmid, IPC_RMID, NULL); // remove shared memory (optional)

    return 0;
}

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

void handle_message(parking_message_t msg)
{
    sqlite3 *parking_db;

    if (pthread_mutex_lock(&parking_db_mutex) != 0) {
        printf("Failed to lock mutex\n");
    }

    if (sqlite3_open("parking.db", &parking_db)) { 
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(parking_db));
        return;
    }

    sqlite3_stmt *stmt = NULL;
    const char *sql = NULL;

    if (msg.is_parking == START) // INSERT new parking start
    {
        sql = "INSERT INTO PARKING (VEHICLE_ID, LAT, LON, START_TIME) VALUES (?, ?, ?, ?);";

        if (sqlite3_prepare_v2(parking_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        {
            fprintf(stderr, "Failed to prepare insert: %s\n", sqlite3_errmsg(parking_db));
            sqlite3_close(parking_db);
            return;
        }

        sqlite3_bind_text(stmt, 1, msg.vehicle_id, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 2, msg.lat);
        sqlite3_bind_double(stmt, 3, msg.lon);
        sqlite3_bind_int64(stmt, 4, (sqlite3_int64)msg.time);

        if (sqlite3_step(stmt) != SQLITE_DONE){
            fprintf(stderr, "Insert failed: %s\n", sqlite3_errmsg(parking_db));
        }

        sqlite3_finalize(stmt);
    }
    else // UPDATE end of parking
    {
        calc_price(parking_db, msg);
    }

    sqlite3_close(parking_db);
    pthread_mutex_unlock(&parking_db_mutex);
}

int what_area(float lat, float lon)
{
    for (int i = 0; i < num_pricing_areas; i++) {
        if (lat >= pricing_table[i].lat_min && lat <= pricing_table[i].lat_max &&
            lon >= pricing_table[i].lon_min && lon <= pricing_table[i].lon_max) {
            return pricing_table[i].area_id;
        }
    }
    printf("No matching area found for lat %.2f lon %.2f\n", lat, lon);
    return -1;  
}

float price_per_area(int area)
{
    for (int i = 0; i < num_pricing_areas; i++) {
        if (pricing_table[i].area_id == area) {
            return pricing_table[i].price_per_min;
        }
    }
    printf("No price found for area %d\n", area);
    return 0.0f;
}

int calc_price(sqlite3 *db, parking_message_t msg)
{
    sqlite3_stmt *stmt;
    time_t start_time, end_of_parking= msg.time;
    float total_price = 0.0f;

    // Get START_TIME from PARKING table
    const char *select_sql =
        "SELECT START_TIME FROM PARKING "
        "WHERE VEHICLE_ID = ? AND END_TIME IS NULL;";

    if (sqlite3_prepare_v2(db, select_sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("Failed to prepare SELECT: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, msg.vehicle_id, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        start_time = sqlite3_column_int64(stmt, 0);
    } else {
        printf("No matching record found for vehicle %s\n", msg.vehicle_id);
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);

    // Calculate total price
    int area = what_area(msg.lat, msg.lon);
    double duration_minutes = difftime(end_of_parking, start_time) / 60.0;
    total_price = (float)(duration_minutes * price_per_area(area));

    // Update PRICE column
    const char *update_sql =
        "UPDATE PARKING "
        "SET AREA = ?, END_TIME = ?, PRICE = ROUND(?,2) "
        "WHERE VEHICLE_ID = ? AND END_TIME IS NULL;";

    if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("Failed to prepare UPDATE: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, area);
    sqlite3_bind_int(stmt, 2, end_of_parking);
    sqlite3_bind_double(stmt, 3, total_price);
    sqlite3_bind_text(stmt, 4, msg.vehicle_id, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        printf("Failed to execute UPDATE: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    printf("Updated price for vehicle %s (area %d): %.2f\n", msg.vehicle_id, area, total_price);
    return 0;
}

void create_pricing_db(void)
{
    sqlite3 *pricing_db;
    char *err_msg = 0;
    char sql[512];

    if (sqlite3_open("pricing.db", &pricing_db)) { 
        printf("Can't open database: %s\n", sqlite3_errmsg(pricing_db));
        return;
    } 
    // Create table
    strcpy(sql,
        "CREATE TABLE IF NOT EXISTS PRICING("
        "AREA INTEGER PRIMARY KEY AUTOINCREMENT,"
        "LAT_MIN REAL,"
        "LAT_MAX REAL,"
        "LON_MIN REAL,"
        "LON_MAX REAL,"
        "PRICE_PER_MIN REAL);");

    if (sqlite3_exec(pricing_db, sql, 0, 0, &err_msg) != SQLITE_OK) {
        printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("PRICING Table created successfully\n");
    }

    // Fill table
    for (int i = 0; i < 100; i += 20)
    {
        for (int j = 0; j < 100; j += 20)
        {
            snprintf(sql, sizeof(sql),
                "INSERT INTO PRICING (LAT_MIN, LAT_MAX, LON_MIN, LON_MAX, PRICE_PER_MIN) "
                "VALUES (%f, %f, %f, %f, %f);",
                (float)i, (float)(i+20), (float)j, (float)(j+20), (float)(i+j)*0.5); // Area segment and (random) pricing

            if (sqlite3_exec(pricing_db, sql, 0, 0, &err_msg) != SQLITE_OK) 
            { 
                printf("SQL error: %s\n", err_msg);
                sqlite3_free(err_msg); 
            }
        }
    }

    sqlite3_close(pricing_db);
}