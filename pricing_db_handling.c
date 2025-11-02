#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h> // for mutexes
#include <sqlite3.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "park_msg_protocol.h"
#include "pricing_db_handling.h"

shm_pricing_block_t *shm_ptr;

sqlite3* create_pricing_db(void);
int load_pricing_into_shm(sqlite3 *db, pricing_entry_t *table);

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

    sqlite3 *pricing_db;
    
    pricing_db = create_pricing_db();
    if (pricing_db == NULL) {
        perror("Could create pricing database");
        exit(1);
    }

    // Setup shared memory for pricing table
    key_t key = ftok(pricing_db_path, 65);   // generate a unique key
    if (key == -1) { perror("ftok"); exit(1); }

    int shmid = shmget(key, sizeof(shm_pricing_block_t), 0666 | IPC_CREAT);
    if (shmid == -1) { perror("shmget"); exit(1); }

    // Load pricing table into shared memory
    shm_ptr = shmat(shmid, NULL, 0);
    if (shm_ptr == (void *) -1) { perror("shmat"); exit(1); }

    // Protection for shared memory mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // allow use between processes
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    // initialize mutex
    pthread_mutex_init(&shm_ptr->shm_mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    // fill table safely
    pthread_mutex_lock(&shm_ptr->shm_mutex);
    shm_ptr->num_pricing_areas = load_pricing_into_shm(pricing_db, shm_ptr->table);
    pthread_mutex_unlock(&shm_ptr->shm_mutex);

    sqlite3_close(pricing_db);

    shmdt(shm_ptr); // detach
    // shmctl(shmid, IPC_RMID, NULL); // remove shared memory (optional)

    return 0;
}

sqlite3* create_pricing_db(void)
{
    char *err_msg = 0;
    char sql[512];
    sqlite3 *pricing_db;

    if (sqlite3_open(pricing_db_path, &pricing_db)) { 
        printf("Can't open database: %s\n", sqlite3_errmsg(pricing_db));
        return NULL;
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

    return pricing_db;
}

int load_pricing_into_shm(sqlite3 *db, pricing_entry_t *table) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT AREA, LAT_MIN, LAT_MAX, LON_MIN, LON_MAX, PRICE_PER_MIN FROM PRICING;";
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        table[i].area_id = sqlite3_column_int(stmt, 0);
        table[i].lat_min = sqlite3_column_double(stmt, 1);
        table[i].lat_max = sqlite3_column_double(stmt, 2);
        table[i].lon_min = sqlite3_column_double(stmt, 3);
        table[i].lon_max = sqlite3_column_double(stmt, 4);
        table[i].price_per_min = sqlite3_column_double(stmt, 5);
        i++;
    }
    sqlite3_finalize(stmt);
    return i;
}