/** general_funcs.c
 *  @brief General utility functions for the parking TCP server.
 */
#include "general_funcs.h"

/** Logging function:
 * @brief Logs messages to a file with a timestamp.
 * @param message The string to log.
 */
void log_event(const char *message) {
    // If SHM isn't attached yet, print to console instead of .log file
    if (shm_ptr == NULL || shm_ptr == (void*)-1) {
        printf("[Early Log] %s\n", message);
        return;
    }

    pthread_mutex_lock(&shm_ptr->log_mutex);
    FILE *logfile = fopen("server_system.log", "a");
    if (logfile) {
        time_t now = time(NULL);
        char *timestamp = ctime(&now);
        timestamp[strlen(timestamp) - 1] = '\0';
        fprintf(logfile, "[%s] %s\n", timestamp, message);
        fclose(logfile);
    }
    pthread_mutex_unlock(&shm_ptr->log_mutex);
}

/** Read configurations:
  * @brief  Read the .config file into global expressions.
  * @param  server_port Pointer to store server port.
  * @param  server_ip Pointer to store server IP.
  * @param  pricing_db_path Pointer to store pricing DB path.
  * @retval None
  */
void read_configurations(int *server_port, char *server_ip, char *pricing_db_path){
    FILE *cfg = fopen("server.config", "r");
    if (cfg) {
        fscanf(cfg, "SERVER_PORT %d\n", server_port);
        fscanf(cfg, "SERVER_IP %s\n", server_ip);
        server_ip[strcspn(server_ip, "\r\n")] = '\0';
        fscanf(cfg, "PRICING_DB %s\n", pricing_db_path);
        fclose(cfg);
    } else {
        perror("Could not open config file");
        exit(1);
    }
}