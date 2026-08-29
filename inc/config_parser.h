#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

typedef struct {
    int nof_workers;
    int nof_users;
    
    // Tempi di servizio medi
    int avg_srvc_primi;
    int avg_srvc_secondi; 
    int avg_srvc_coffee;
    int avg_srvc_cassa;
    
    // Postazioni di lavoro fisiche per stazione
    int nof_wk_seats_primi;
    int nof_wk_seats_secondi;
    int nof_wk_seats_coffee;
    int nof_wk_seats_cassa;
    
    // Capienza tavoli
    int nof_table_seats;
    
    // Porzioni e Refill
    int max_porzioni_primi;   
    int max_porzioni_secondi; 
    int avg_refill_primi;
    int avg_refill_secondi;
    int refill_period;       
    
    // Prezzi
    int price_primi;
    int price_secondi;
    int price_coffee;

    // Parametri Generali
    int sim_duration;
    int overload_threshold;
    int nof_pause;           
    long n_nano_secs;
} config_t;

void load_config(const char *filename, config_t *cfg);

#endif // CONFIG_PARSER_H