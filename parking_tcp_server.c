#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sqlite3.h>
#include <math.h>

#include "park_mesage_protocol.h"

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
// #define MAX_SESSIONS 100

pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;

void create_pricing_db (void);
void *handle_client(void *arg);
void handle_message(parking_message_t msg);
int what_area(float lat, float lon);
float price_per_area(int area);
int calc_price(sqlite3 *db, parking_message_t msg);

int main()
{
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
    create_pricing_db();
    handle_message((parking_message_t){"CAR123", 23.1, 65.967, 1, time(NULL) });
    handle_message((parking_message_t){"CAR456", 4, 63.5, 1, time(NULL) });
    sleep(1);
    handle_message((parking_message_t){"CAR123", 23.1, 65.967, 0, time(NULL) });

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
    
    sqlite3_close(parking_db);
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
        calc_price(parking_db, msg);
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

int what_area(float lat, float lon)
{
    
    sqlite3 *pricing_db;
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT AREA FROM PRICING "
        "WHERE ? BETWEEN LAT_MIN AND LAT_MAX "
        "AND ? BETWEEN LON_MIN AND LON_MAX;";

    if (sqlite3_open("pricing.db", &pricing_db) != SQLITE_OK) {
        printf("Cannot open DB: %s\n", sqlite3_errmsg(pricing_db));
        return 1;
    }

    if (sqlite3_prepare_v2(pricing_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("Failed to prepare: %s\n", sqlite3_errmsg(pricing_db));
        sqlite3_close(pricing_db);
        return 0;
    }

    // bind latitude and longitude to the ? placeholders
    sqlite3_bind_double(stmt, 1, round(lat));
    sqlite3_bind_double(stmt, 2, round(lon));

    int area = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        area = sqlite3_column_int(stmt, 0);
    } else {
        printf("No matching area found.\n");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(pricing_db);
    return area;
}

float price_per_area(int area)
{
    
    sqlite3 *pricing_db;
    sqlite3_stmt *stmt;
    float price = 0.0;

    if (sqlite3_open("pricing.db", &pricing_db) != SQLITE_OK) {
        printf("Cannot open DB: %s\n", sqlite3_errmsg(pricing_db));
        return 1;
    }
    const char *sql = "SELECT PRICE_PER_MIN FROM PRICING WHERE AREA = ?;";

    if (sqlite3_prepare_v2(pricing_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("Failed to prepare statement: %s\n", sqlite3_errmsg(pricing_db));
        sqlite3_close(pricing_db);
        return 0.0f;
    }

    // bind the area value into the query
    sqlite3_bind_int(stmt, 1, area);

    // execute the query and get the result
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        price = (float)sqlite3_column_double(stmt, 0);
    } else {
        printf("No price found for area %d\n", area);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(pricing_db);

    return price;
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

