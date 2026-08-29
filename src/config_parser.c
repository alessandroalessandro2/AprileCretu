#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../inc/config_parser.h"

static void check_env_override(config_t *cfg) {
    char *val;
    if ((val = getenv("NOF_WORKERS"))) cfg->nof_workers = atoi(val);
    if ((val = getenv("NOF_USERS"))) cfg->nof_users = atoi(val);
    if ((val = getenv("SIM_DURATION"))) cfg->sim_duration = atoi(val);
    if ((val = getenv("OVERLOAD_THRESHOLD"))) cfg->overload_threshold = atoi(val);
    if ((val = getenv("N_NANO_SECS"))) cfg->n_nano_secs = atol(val);
}

void load_config(const char *filename, config_t *cfg) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("[Info] File di configurazione non trovato, si usano default ed env vars");
    } else {
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            if (line[0] == '\n' || line[0] == '#') continue;
            char key[128];
            long value;
            if (sscanf(line, "%127[^=]=%ld", key, &value) == 2) {
                if (strcmp(key, "NOF_WORKERS") == 0) cfg->nof_workers = value;
                else if (strcmp(key, "NOF_USERS") == 0) cfg->nof_users = value;
                else if (strcmp(key, "AVG_SRVC_PRIMI") == 0) cfg->avg_srvc_primi = value;
                else if (strcmp(key, "AVG_SRVC_SECONDI") == 0 || strcmp(key, "AVG_SRVC_MAIN_COURSE") == 0) cfg->avg_srvc_secondi = value;
                else if (strcmp(key, "AVG_SRVC_COFFEE") == 0) cfg->avg_srvc_coffee = value;
                else if (strcmp(key, "AVG_SRVC_CASSA") == 0) cfg->avg_srvc_cassa = value;
                else if (strcmp(key, "NOF_WK_SEATS_PRIMI") == 0) cfg->nof_wk_seats_primi = value;
                else if (strcmp(key, "NOF_WK_SEATS_SECONDI") == 0) cfg->nof_wk_seats_secondi = value;
                else if (strcmp(key, "NOF_WK_SEATS_COFFEE") == 0) cfg->nof_wk_seats_coffee = value;
                else if (strcmp(key, "NOF_WK_SEATS_CASSA") == 0) cfg->nof_wk_seats_cassa = value;
                else if (strcmp(key, "NOF_TABLE_SEATS") == 0) cfg->nof_table_seats = value;
                else if (strcmp(key, "MAX_PORZIONI_PRIMI") == 0) cfg->max_porzioni_primi = value;
                else if (strcmp(key, "MAX_PORZIONI_SECONDI") == 0) cfg->max_porzioni_secondi = value;
                else if (strcmp(key, "AVG_REFILL_PRIMI") == 0) cfg->avg_refill_primi = value;
                else if (strcmp(key, "AVG_REFILL_SECONDI") == 0) cfg->avg_refill_secondi = value;
                else if (strcmp(key, "REFILL_PERIOD") == 0) cfg->refill_period = value; 
                else if (strcmp(key, "PRICE_PRIMI") == 0) cfg->price_primi = value;
                else if (strcmp(key, "PRICE_SECONDI") == 0) cfg->price_secondi = value;
                else if (strcmp(key, "PRICE_COFFEE") == 0) cfg->price_coffee = value;
                else if (strcmp(key, "SIM_DURATION") == 0) cfg->sim_duration = value;
                else if (strcmp(key, "OVERLOAD_THRESHOLD") == 0) cfg->overload_threshold = value;
                else if (strcmp(key, "NOF_PAUSE") == 0) cfg->nof_pause = value; 
                else if (strcmp(key, "N_NANO_SECS") == 0) cfg->n_nano_secs = value;
            }
        }
        fclose(file);
    }

    // Override con eventuali variabili d'ambiente
    check_env_override(cfg);

    // Valori di default di sicurezza
    if (cfg->n_nano_secs == 0) cfg->n_nano_secs = 100000000; // 100ms
    if (cfg->sim_duration == 0) cfg->sim_duration = 3;
    if (cfg->overload_threshold == 0) cfg->overload_threshold = 15;
    if (cfg->nof_users == 0) cfg->nof_users = 20;
    if (cfg->nof_workers == 0) cfg->nof_workers = 5;
    if (cfg->nof_table_seats == 0) cfg->nof_table_seats = 15;
    
    if (cfg->max_porzioni_primi == 0) cfg->max_porzioni_primi = 20;
    if (cfg->max_porzioni_secondi == 0) cfg->max_porzioni_secondi = 20;
    if (cfg->avg_refill_primi == 0) cfg->avg_refill_primi = 10;
    if (cfg->avg_refill_secondi == 0) cfg->avg_refill_secondi = 10;
    if (cfg->refill_period == 0) cfg->refill_period = 10; 
    
    if (cfg->nof_wk_seats_primi == 0) cfg->nof_wk_seats_primi = 2;
    if (cfg->nof_wk_seats_secondi == 0) cfg->nof_wk_seats_secondi = 2;
    if (cfg->nof_wk_seats_coffee == 0) cfg->nof_wk_seats_coffee = 1;
    if (cfg->nof_wk_seats_cassa == 0) cfg->nof_wk_seats_cassa = 2;
    
    if (cfg->nof_pause == 0) cfg->nof_pause = 2; 
    if (cfg->avg_srvc_primi == 0) cfg->avg_srvc_primi = 30; 
    if (cfg->avg_srvc_secondi == 0) cfg->avg_srvc_secondi = 40; 
    if (cfg->avg_srvc_coffee == 0) cfg->avg_srvc_coffee = 15;
    if (cfg->avg_srvc_cassa == 0) cfg->avg_srvc_cassa = 20; 
    
    if (cfg->price_primi == 0) cfg->price_primi = 4;
    if (cfg->price_secondi == 0) cfg->price_secondi = 5;
    if (cfg->price_coffee == 0) cfg->price_coffee = 1;
}