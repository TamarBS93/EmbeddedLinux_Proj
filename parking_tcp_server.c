#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sqlite3.h>

#include "park_mesage_protocol.h"

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
// #define MAX_SESSIONS 100

pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;

void create_pricing_db (void);
void *handle_client(void *arg);
void handle_message(parking_message_t msg);

int main()
{
    const char *sql = NULL;

    // creates the Parking  DB file if it doesn't exist
    sqlite3 *parking_db;
    char *err_msg = 0;

    if (sqlite3_open("parking.db", &parking_db)) { 
        printf("Can't open database: %s\n", sqlite3_errmsg(parking_db));
        return 1;
    } else {
        printf("Opened database successfully.\n");
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
        "OVERALL_TIME INTEGER,"
        "PRICE REAL);";

    if (sqlite3_exec(parking_db, sql, 0, 0, &err_msg) != SQLITE_OK) {
        printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("Table created successfully\n");
    }
    sqlite3_close(parking_db);

    create_pricing_db();
    // handle_message((parking_message_t){"CAR123", 23.1, 65.967, 1, time(NULL) });
    // handle_message((parking_message_t){"CAR456", 4, 65.5, 1, time(NULL) });
    // sleep(3);
    // handle_message((parking_message_t){"CAR123", 23.1, 65.967, 0, time(NULL) });

    // Creating a socket: 
    int server_fd, new_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("TCP server listening on port %d...\n", SERVER_PORT);

    while (1)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (new_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        pthread_t thread_id;
        int *client_sock = malloc(sizeof(int));
        *client_sock = new_socket;
        pthread_create(&thread_id, NULL, handle_client, client_sock);
        pthread_detach(thread_id); // no need to join
    }

    close(server_fd);
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
    if (sqlite3_open("parking.db", &parking_db)) { 
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(parking_db));
        return;
    }

    sqlite3_stmt *stmt = NULL;
    const char *sql = NULL;

    if (msg.is_parking) // INSERT new parking start
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
        sql = "UPDATE PARKING SET END_TIME = ? WHERE VEHICLE_ID = ? AND END_TIME IS NULL;";

        if (sqlite3_prepare_v2(parking_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        {
            fprintf(stderr, "Failed to prepare update: %s\n", sqlite3_errmsg(parking_db));
            sqlite3_close(parking_db);
            return;
        }

        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)msg.time);
        sqlite3_bind_text(stmt, 2, msg.vehicle_id, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            fprintf(stderr, "Update failed: %s\n", sqlite3_errmsg(parking_db));

        }

        sqlite3_finalize(stmt);
    }

    sqlite3_close(parking_db);
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
    printf("Opened database successfully.\n");

    // Create table
    strcpy(sql,
        "CREATE TABLE IF NOT EXISTS PRICING("
        "AREA INTEGER PRIMARY KEY AUTOINCREMENT,"
        "LAT_MIN REAL,"
        "LAT_MAX REAL,"
        "LON_MIN REAL,"
        "LON_MAX REAL,"
        "PRICE REAL);");

    if (sqlite3_exec(pricing_db, sql, 0, 0, &err_msg) != SQLITE_OK) {
        printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    } else {
        printf("Table created successfully\n");
    }

    // Fill table
    for (int i = 0; i < 50; i += 5)
    {
        for (int j = 0; j < 50; j += 5)
        {
            snprintf(sql, sizeof(sql),
                "INSERT INTO PRICING (LAT_MIN, LAT_MAX, LON_MIN, LON_MAX, PRICE) "
                "VALUES (%f, %f, %f, %f, %f);",
                (float)i, (float)(i+5), (float)j, (float)(j+5), (float)(i+j));

            if (sqlite3_exec(pricing_db, sql, 0, 0, &err_msg) != SQLITE_OK) 
            { 
                printf("SQL error: %s\n", err_msg);
                sqlite3_free(err_msg); 
            }
        }
    }

    sqlite3_close(pricing_db);
}

