/**
 * @file pricing_db_handling.c
 * @brief Logic for managing the pricing database and shared memory synchronization.
 * * This module handles the creation of the pricing SQLite database, loading
 * data into shared memory, and using inotify to watch for database changes
 * to ensure real-time updates across processes.
 */

#include "pricing_db_handling.h"
#include "general_funcs.h"

/* Global counter for the number of pricing areas currently loaded. */
int num_pricing_areas = 0;

/**
 * @brief Main logic for the pricing manager process.
 * * This function initializes the pricing database, sets up the shared memory 
 * segment, initializes process-shared mutexes, and enters an infinite loop 
 * watching the database file for manual updates.
 * @return int Returns 0 on successful execution (though typically exits via process termination).
 */
int pricing_handle_process()
{
    // Create or open pricing database
    sqlite3 *pricing_db;
    pricing_db = create_pricing_db();
    if (!pricing_db) {
        exit(1);
    }
    while (access(pricing_db_path, F_OK) == -1) {
        log_event("Waiting for DB file to exist...");
        usleep(200000);
    }

    // Setup shared memory for pricing table
    key_t key = ftok(pricing_db_path, 65);   // generate a unique key
    if (key == -1) { perror("ftok"); exit(1); }

    int shmid = shmget(key, sizeof(shm_pricing_block_t), 0666 | IPC_CREAT);
    if (shmid == -1) { perror("shmget"); exit(1); }

    // Load pricing table into shared memory
    shm_ptr = shmat(shmid, NULL, 0);
    if (shm_ptr == (void *) -1) { perror("shmat"); exit(1); }

    shm_ptr->ready = 0;
    // Protection for shared memory mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // allow use between processes
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    // initialize mutex
    pthread_mutex_init(&shm_ptr->shm_mutex, &attr);
    pthread_mutex_init(&shm_ptr->log_mutex, &attr);

    pthread_mutexattr_destroy(&attr);

    // fill table safely
    pthread_mutex_lock(&shm_ptr->shm_mutex);
    shm_ptr->num_pricing_areas = load_pricing_into_shm(pricing_db, shm_ptr->table);
    pthread_mutex_unlock(&shm_ptr->shm_mutex);

    watch_db_file(pricing_db_path, pricing_db);

    sqlite3_close(pricing_db);
    shmdt(shm_ptr); // detach

    exit(0);
}

/**
 * @brief Creates the pricing database and populates it with default values if it doesn't exist.
 * @return sqlite3* Pointer to the opened SQLite database connection, or NULL on failure.
 */
sqlite3* create_pricing_db(void)
{
    char *err_msg = 0;
    char sql[512];
    sqlite3 *pricing_db;
    int existed = (access(pricing_db_path, F_OK) != -1);

    if (sqlite3_open_v2(pricing_db_path, &pricing_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK) {
        if (existed) {
            log_event("Opened existing database of pricing.");
            return pricing_db;
        } else {
            log_event("Creating new PRICING database.");
        }
    } else {
        fprintf(stderr, "Can't open DB: %s\n", sqlite3_errmsg(pricing_db));
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
    } 

    // Fill table
    for (int i = 0; i < 81; i += 20)
    {
        for (int j = 0; j < 81; j += 20)
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

/**
 * @brief Loads data from the SQLite pricing table into the shared memory structure.
 * @param db Pointer to the active SQLite database.
 * @param table Pointer to the destination pricing table in shared memory.
 * @return int The number of pricing areas successfully loaded.
 */
int load_pricing_into_shm(sqlite3 *db, pricing_entry_t *table) {
    shm_ptr->ready = 0;
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
    shm_ptr->ready = 1;
    log_event("PRICING SHM loaded successfully");

    return i;
}

/**
 * @brief Uses inotify to monitor the database file for changes.
 * * This function blocks and waits for an IN_CLOSE_WRITE event. It validates the 
 * change using nanosecond-precision modification timestamps to prevent 
 * unnecessary reloads from "ghost" access events.
 * @param path String path to the database file.
 * @param db Pointer to the active SQLite database connection.
 */
void watch_db_file(const char *path, sqlite3 *db) {

    int fd = inotify_init();
    if (fd < 0) { perror("inotify_init"); exit(1); }

    int wd = inotify_add_watch(fd, path, IN_CLOSE_WRITE);
    if (wd < 0) { perror("inotify_add_watch"); exit(1); }

    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "Watching %s for updates...", path);
    log_event(log_msg);

    struct stat st;
    stat(path, &st);
    // Store the last modification time
    struct timespec last_mtime = st.st_mtim;

    char buffer[sizeof(struct inotify_event) + NAME_MAX + 1];

    while (1) {
        int length = read(fd, buffer, sizeof(buffer));
        if (length < (int)sizeof(struct inotify_event)) continue;

        stat(path, &st);
        // Check if the modification time has changed
        if (st.st_mtim.tv_sec == last_mtime.tv_sec && 
            st.st_mtim.tv_nsec == last_mtime.tv_nsec) {
            // It's a ghost event (file closed without data changes)
            continue; 
        }
        last_mtime = st.st_mtim; // Update last known modification time

        // Perform the reload
        pthread_mutex_lock(&shm_ptr->shm_mutex);
        shm_ptr->num_pricing_areas = load_pricing_into_shm(db, shm_ptr->table);
        pthread_mutex_unlock(&shm_ptr->shm_mutex);

        log_event("Database modified: SHM updated!");
    }
}