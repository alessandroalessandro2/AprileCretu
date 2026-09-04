#include "../inc/common.h"

int shmid, semid, msgid;
shared_data_t *shm_ptr;

void load_menu_and_prices(const char *filename, shared_data_t *shm, config_t *cfg) {
    FILE *file = fopen(filename, "r");
    if (!file) { perror("Impossibile aprire menu.txt"); exit(1); }
    char line[256];
    shm->num_primi = 0; shm->num_secondi = 0; shm->num_contorni = 0; shm->num_caffe = 0;
    
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '\n' || line[0] == '#') continue;
        char type[16], name[32]; int dummy_price;
        if (sscanf(line, "%15[^,],%31[^,],%d", type, name, &dummy_price) == 3) {
            if (strcmp(type, "PRIMO") == 0 && shm->num_primi < MAX_PIATTI) {
                strncpy(shm->primi[shm->num_primi].name, name, 31);
                shm->primi[shm->num_primi].price = cfg->price_primi;
                shm->num_primi++;
            } else if (strcmp(type, "SECONDO") == 0 && shm->num_secondi < MAX_PIATTI) {
                strncpy(shm->secondi[shm->num_secondi].name, name, 31);
                shm->secondi[shm->num_secondi].price = cfg->price_secondi;
                shm->num_secondi++;
            } else if (strcmp(type, "CONTORNO") == 0 && shm->num_contorni < MAX_PIATTI) {
                strncpy(shm->contorni[shm->num_contorni].name, name, 31);
                shm->contorni[shm->num_contorni].price = 0;
                shm->num_contorni++;
            } else if (strcmp(type, "CAFFE") == 0 && shm->num_caffe < 4) {
                strncpy(shm->caffe[shm->num_caffe].name, name, 31);
                shm->caffe[shm->num_caffe].price = cfg->price_coffee;
                shm->num_caffe++;
            } else if (strcmp(type, "DOLCE") == 0) {
                strncpy(shm->dolce.name, name, 31);
                shm->dolce.price = cfg->price_coffee;
            }
        }
    }
    fclose(file);
}

void cleanup(int sig) {
    (void)sig; shmdt(shm_ptr); shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID, NULL); msgctl(msgid, IPC_RMID, NULL); exit(0);
}

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.conf";
    config_t cfg; memset(&cfg, 0, sizeof(config_t));
    load_config(config_file, &cfg);
    signal(SIGINT, cleanup); signal(SIGTERM, cleanup);
    
    shmid = shmget(SHM_KEY, sizeof(shared_data_t), IPC_CREAT | 0666);
    shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    memset(shm_ptr, 0, sizeof(shared_data_t));
    shm_ptr->sim_running = 1;
    
    load_menu_and_prices("menu.txt", shm_ptr, &cfg);
    
    semid = semget(SEM_KEY, 9, IPC_CREAT | 0666); 
    msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
    union semun arg; arg.val = 1; semctl(semid, SEM_MUTEX, SETVAL, arg);
    arg.val = cfg.nof_table_seats;     semctl(semid, SEM_TAVOLI, SETVAL, arg); 
    arg.val = cfg.nof_wk_seats_primi;  semctl(semid, SEM_PRIMI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_secondi;semctl(semid, SEM_SECONDI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_coffee; semctl(semid, SEM_COFFEE, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_cassa;  semctl(semid, SEM_CASSA, SETVAL, arg);
    arg.val = 0; semctl(semid, SEM_READY, SETVAL, arg); 
    arg.val = 0; semctl(semid, SEM_DAY_START, SETVAL, arg); 
    arg.val = 0; semctl(semid, SEM_DAY_END, SETVAL, arg); 

    char id_str[8];
    for(int i = 0; i < cfg.nof_workers; i++) {
        sprintf(id_str, "%d", i);
        if(fork() == 0) { execl("./bin/operatore", "operatore", id_str, config_file, NULL); exit(1); }
    }
    if(fork() == 0) { execl("./bin/cassiere", "cassiere", config_file, NULL); exit(1); }
    for (int i = 0; i < cfg.nof_users; i++) {
        if(fork() == 0) { execl("./bin/utente", "utente", NULL); exit(1); }
    }
    
    int total_processes = cfg.nof_workers + 1 + cfg.nof_users;
    int DAY_LENGTH = 50; 
    shm_ptr->current_day = 1;
    const char* causa_terminazione = "TIMEOUT (Raggiunto limite di giorni impostato)";
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};

    while (shm_ptr->current_day <= cfg.sim_duration && shm_ptr->sim_running) {
        printf("\n=========================================\n");
        printf("   INIZIO GIORNO %d (di %d)\n", shm_ptr->current_day, cfg.sim_duration);
        printf("=========================================\n");
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->day_ended = 0; shm_ptr->sim_time = 0;
        shm_ptr->daily_users_served = 0; shm_ptr->daily_users_dropped = 0;
        shm_ptr->daily_revenue = 0; shm_ptr->daily_pauses = 0; shm_ptr->daily_active_ops = 0;
        memset(shm_ptr->daily_dishes_served, 0, sizeof(shm_ptr->daily_dishes_served));
        memset(shm_ptr->daily_dishes_wasted, 0, sizeof(shm_ptr->daily_dishes_wasted));
        memset(shm_ptr->daily_wait_time_stazioni, 0, sizeof(shm_ptr->daily_wait_time_stazioni));
        memset(shm_ptr->daily_wait_count_stazioni, 0, sizeof(shm_ptr->daily_wait_count_stazioni));

        memset(shm_ptr->op_assignment, 0, sizeof(shm_ptr->op_assignment));
        shm_ptr->op_assignment[0] = TYPE_PRIMI; shm_ptr->op_assignment[1] = TYPE_SECONDI; shm_ptr->op_assignment[2] = TYPE_COFFEE;
        int max_type = TYPE_PRIMI, max_time = cfg.avg_srvc_primi;
        if(cfg.avg_srvc_secondi > max_time) { max_time = cfg.avg_srvc_secondi; max_type = TYPE_SECONDI; }
        if(cfg.avg_srvc_coffee > max_time)  { max_time = cfg.avg_srvc_coffee; max_type = TYPE_COFFEE; }
        for(int i = 3; i < cfg.nof_workers; i++) shm_ptr->op_assignment[i] = max_type;

        for(int i=0; i<shm_ptr->num_primi; i++) shm_ptr->primi[i].porzioni_rimanenti = cfg.avg_refill_primi;
        for(int i=0; i<shm_ptr->num_secondi; i++) shm_ptr->secondi[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        for(int i=0; i<shm_ptr->num_contorni; i++) shm_ptr->contorni[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        semop(semid, &mutex_unlock, 1);

        struct sembuf wait_ready = {SEM_READY, -total_processes, 0}; semop(semid, &wait_ready, 1); 
        struct sembuf start_day = {SEM_DAY_START, total_processes, 0}; semop(semid, &start_day, 1); 
        
        int t = 0;
        while (t < DAY_LENGTH && shm_ptr->sim_running) {
            usleep(cfg.n_nano_secs / 1000); 
            semop(semid, &mutex_lock, 1);
            shm_ptr->sim_time++;
            
            if (shm_ptr->sim_time % cfg.refill_period == 0) {
                for(int i=0; i<shm_ptr->num_primi; i++) { shm_ptr->primi[i].porzioni_rimanenti += cfg.avg_refill_primi; if(shm_ptr->primi[i].porzioni_rimanenti > cfg.max_porzioni_primi) shm_ptr->primi[i].porzioni_rimanenti = cfg.max_porzioni_primi; }
                for(int i=0; i<shm_ptr->num_secondi; i++) { shm_ptr->secondi[i].porzioni_rimanenti += cfg.avg_refill_secondi; if(shm_ptr->secondi[i].porzioni_rimanenti > cfg.max_porzioni_secondi) shm_ptr->secondi[i].porzioni_rimanenti = cfg.max_porzioni_secondi; }
                for(int i=0; i<shm_ptr->num_contorni; i++) { shm_ptr->contorni[i].porzioni_rimanenti += cfg.avg_refill_secondi; if(shm_ptr->contorni[i].porzioni_rimanenti > cfg.max_porzioni_secondi) shm_ptr->contorni[i].porzioni_rimanenti = cfg.max_porzioni_secondi; }
            }
            
            int waiting = shm_ptr->queue_lengths[TYPE_PRIMI] + shm_ptr->queue_lengths[TYPE_SECONDI] + shm_ptr->queue_lengths[TYPE_COFFEE] + shm_ptr->queue_lengths[TYPE_CASSA];
            if (waiting > cfg.overload_threshold) {
                causa_terminazione = "OVERLOAD (Superata la soglia di utenti in attesa alle stazioni)";
                printf("\n[!!!] %s.\n", causa_terminazione);
                shm_ptr->sim_running = 0; 
            }
            semop(semid, &mutex_unlock, 1);
            t++;
        }
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->day_ended = 1;
        for(int i=0; i<shm_ptr->num_primi; i++) { shm_ptr->daily_dishes_wasted[0] += shm_ptr->primi[i].porzioni_rimanenti; shm_ptr->total_dishes_wasted[0] += shm_ptr->primi[i].porzioni_rimanenti; }
        for(int i=0; i<shm_ptr->num_secondi; i++) { shm_ptr->daily_dishes_wasted[1] += shm_ptr->secondi[i].porzioni_rimanenti; shm_ptr->total_dishes_wasted[1] += shm_ptr->secondi[i].porzioni_rimanenti; }
        for(int i=0; i<shm_ptr->num_contorni; i++) { shm_ptr->daily_dishes_wasted[2] += shm_ptr->contorni[i].porzioni_rimanenti; shm_ptr->total_dishes_wasted[2] += shm_ptr->contorni[i].porzioni_rimanenti; }

        printf("\n--- FINE GIORNO %d ---\n", shm_ptr->current_day);
        printf("[Statistiche Giornaliere]\n");
        printf("  Serviti: %d, Rinunce: %d, Incasso: %d euro, Pause: %d, Op. Attivi: %d\n", 
               shm_ptr->daily_users_served, shm_ptr->daily_users_dropped, shm_ptr->daily_revenue, shm_ptr->daily_pauses, shm_ptr->daily_active_ops);
        printf("  Piatti Distribuiti -> Primi: %d, Secondi: %d, Contorni: %d, Caffe: %d, Dolci: %d\n", 
               shm_ptr->daily_dishes_served[0], shm_ptr->daily_dishes_served[1], shm_ptr->daily_dishes_served[2], shm_ptr->daily_dishes_served[3], shm_ptr->daily_dishes_served[4]);
        printf("  Sprechi Giornalieri-> Primi: %d, Secondi: %d, Contorni: %d, Caffe: 0 (illimitati), Dolci: 0 (illimitati)\n", 
               shm_ptr->daily_dishes_wasted[0], shm_ptr->daily_dishes_wasted[1], shm_ptr->daily_dishes_wasted[2]);
        
        int d_w1 = shm_ptr->daily_wait_count_stazioni[1]; int d_w2 = shm_ptr->daily_wait_count_stazioni[2];
        int d_w3 = shm_ptr->daily_wait_count_stazioni[3]; int d_w4 = shm_ptr->daily_wait_count_stazioni[4];
        printf("  Attesa Media (tick)-> Primi: %d, Secondi: %d, Caffe: %d, Cassa: %d\n",
            d_w1 > 0 ? shm_ptr->daily_wait_time_stazioni[1]/d_w1 : 0, d_w2 > 0 ? shm_ptr->daily_wait_time_stazioni[2]/d_w2 : 0,
            d_w3 > 0 ? shm_ptr->daily_wait_time_stazioni[3]/d_w3 : 0, d_w4 > 0 ? shm_ptr->daily_wait_time_stazioni[4]/d_w4 : 0);
        
        printf("\n[Statistiche Cumulative (Fino al giorno %d)]\n", shm_ptr->current_day);
        printf("  Serviti Totali: %d, Rinunce Totali: %d, Incasso Totale: %d\n", shm_ptr->total_users_served, shm_ptr->total_users_dropped, shm_ptr->total_revenue);

        shm_ptr->current_day++;
        semop(semid, &mutex_unlock, 1);

        struct sembuf end_day = {SEM_DAY_END, -total_processes, 0}; semop(semid, &end_day, 1);
    }
    
    signal(SIGTERM, SIG_IGN);
    int gg = (shm_ptr->current_day > 1) ? (shm_ptr->current_day - 1) : 1;
    
    // Conteggio effettivo degli operatori fisicamente attivi durante la run
    int oper_distinti = 0;
    for(int i=0; i<MAX_WORKERS; i++) {
        if (shm_ptr->worker_has_worked[i]) oper_distinti++;
    }

    printf("\n=================================\n     STATISTICHE FINALI MENSA    \n=================================\n");
    printf("Causa Terminazione: %s\n", causa_terminazione);
    printf("Utenti serviti: %d (Media/gg: %.1f)\n", shm_ptr->total_users_served, (float)shm_ptr->total_users_served / gg);
    printf("Utenti rinunciatari: %d (Media/gg: %.1f)\n", shm_ptr->total_users_dropped, (float)shm_ptr->total_users_dropped / gg);
    printf("Incasso Totale: %d euro (Media/gg: %.1f euro)\n", shm_ptr->total_revenue, (float)shm_ptr->total_revenue / gg);
    printf("Piatti distribuiti - Primi: %d (%.1f/gg), Secondi: %d (%.1f/gg), Contorni: %d (%.1f/gg), Caffe: %d (%.1f/gg), Dolci: %d (%.1f/gg)\n", 
           shm_ptr->total_dishes_served[0], (float)shm_ptr->total_dishes_served[0]/gg,
           shm_ptr->total_dishes_served[1], (float)shm_ptr->total_dishes_served[1]/gg,
           shm_ptr->total_dishes_served[2], (float)shm_ptr->total_dishes_served[2]/gg,
           shm_ptr->total_dishes_served[3], (float)shm_ptr->total_dishes_served[3]/gg,
           shm_ptr->total_dishes_served[4], (float)shm_ptr->total_dishes_served[4]/gg);
    printf("Sprechi - Primi: %d (%.1f/gg), Secondi: %d (%.1f/gg), Contorni: %d (%.1f/gg), Caffe/Dolci: 0 (illimitati)\n", 
           shm_ptr->total_dishes_wasted[0], (float)shm_ptr->total_dishes_wasted[0]/gg,
           shm_ptr->total_dishes_wasted[1], (float)shm_ptr->total_dishes_wasted[1]/gg,
           shm_ptr->total_dishes_wasted[2], (float)shm_ptr->total_dishes_wasted[2]/gg);
    printf("Pause totali: %d (Media/gg: %.1f)\n", shm_ptr->total_pauses, (float)shm_ptr->total_pauses / gg);
    printf("Operatori DISTINTI attivi (almeno un turno): %d\n", oper_distinti);
    
    printf("\n--- TEMPI MEDI DI ATTESA STORICI (tick coda) ---\n");
    int tot_wt = 0, tot_wc = 0;
    for(int i=1; i<=4; i++) {
        tot_wt += shm_ptr->wait_time_stazioni[i]; tot_wc += shm_ptr->wait_count_stazioni[i];
        if(shm_ptr->wait_count_stazioni[i] > 0) printf("Stazione %d: %d\n", i, shm_ptr->wait_time_stazioni[i] / shm_ptr->wait_count_stazioni[i]);
    }
    if(tot_wc > 0) printf("Attesa media complessiva storica: %d\n", tot_wt / tot_wc);
    printf("=================================\n");
    
    kill(0, SIGTERM); while (wait(NULL) > 0); cleanup(0);
    return 0;
}