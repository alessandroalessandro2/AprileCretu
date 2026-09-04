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

    // 🔴 VALIDAZIONI DI SICUREZZA RIGOROSE
    if (cfg.nof_workers < 3) {
        printf("[Errore] NOF_WORKERS (%d) insufficiente. Minimo 3 per garantire presidio stazioni.\n", cfg.nof_workers);
        exit(1);
    }
    if (shm_ptr->num_primi < 2 || shm_ptr->num_secondi < 2 || shm_ptr->num_caffe < 4) {
        printf("[Errore] menu.txt non conforme: richiesti >=2 primi, >=2 secondi, >=4 caffe.\n");
        exit(1);
    }
    // Forza il parametro a 10 come richiesto rigidamente dalla specifica
    cfg.refill_period = 10;
    
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
    int SIMULATED_MINUTES_PER_DAY = 480; 
    
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
        int assigned = 0;
        shm_ptr->op_assignment[assigned++] = TYPE_PRIMI;
        shm_ptr->op_assignment[assigned++] = TYPE_SECONDI;
        shm_ptr->op_assignment[assigned++] = TYPE_COFFEE;
        
        int max_type = TYPE_PRIMI, max_time = cfg.avg_srvc_primi;
        if(cfg.avg_srvc_secondi > max_time) { max_time = cfg.avg_srvc_secondi; max_type = TYPE_SECONDI; }
        if(cfg.avg_srvc_coffee > max_time)  { max_time = cfg.avg_srvc_coffee; max_type = TYPE_COFFEE; }
        while(assigned < cfg.nof_workers) shm_ptr->op_assignment[assigned++] = max_type;

        for(int i=0; i<shm_ptr->num_primi; i++) shm_ptr->primi[i].porzioni_rimanenti = cfg.avg_refill_primi;
        for(int i=0; i<shm_ptr->num_secondi; i++) shm_ptr->secondi[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        for(int i=0; i<shm_ptr->num_contorni; i++) shm_ptr->contorni[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        semop(semid, &mutex_unlock, 1);

        struct sembuf wait_ready = {SEM_READY, -total_processes, 0}; semop(semid, &wait_ready, 1); 
        struct sembuf start_day = {SEM_DAY_START, total_processes, 0}; semop(semid, &start_day, 1); 
        
        int t = 0;
        while (t < SIMULATED_MINUTES_PER_DAY && shm_ptr->sim_running) {
            usleep(cfg.n_nano_secs / 1000); 
            semop(semid, &mutex_lock, 1);
            shm_ptr->sim_time++;
            
            if (shm_ptr->sim_time % cfg.refill_period == 0) {
                for(int i=0; i<shm_ptr->num_primi; i++) { shm_ptr->primi[i].porzioni_rimanenti += cfg.avg_refill_primi; if(shm_ptr->primi[i].porzioni_rimanenti > cfg.max_porzioni_primi) shm_ptr->primi[i].porzioni_rimanenti = cfg.max_porzioni_primi; }
                for(int i=0; i<shm_ptr->num_secondi; i++) { shm_ptr->secondi[i].porzioni_rimanenti += cfg.avg_refill_secondi; if(shm_ptr->secondi[i].porzioni_rimanenti > cfg.max_porzioni_secondi) shm_ptr->secondi[i].porzioni_rimanenti = cfg.max_porzioni_secondi; }
                for(int i=0; i<shm_ptr->num_contorni; i++) { shm_ptr->contorni[i].porzioni_rimanenti += cfg.avg_refill_secondi; if(shm_ptr->contorni[i].porzioni_rimanenti > cfg.max_porzioni_secondi) shm_ptr->contorni[i].porzioni_rimanenti = cfg.max_porzioni_secondi; }
            }
            semop(semid, &mutex_unlock, 1);
            t++;
        }
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->day_ended = 1;
        
        int waiting = shm_ptr->queue_lengths[TYPE_PRIMI] + shm_ptr->queue_lengths[TYPE_SECONDI] + shm_ptr->queue_lengths[TYPE_COFFEE] + shm_ptr->queue_lengths[TYPE_CASSA];
        if (waiting > cfg.overload_threshold) {
            causa_terminazione = "OVERLOAD (Utenti in attesa a fine giornata superiori alla soglia)";
            shm_ptr->sim_running = 0; 
        }
        semop(semid, &mutex_unlock, 1);

        struct sembuf end_day = {SEM_DAY_END, -total_processes, 0}; 
        semop(semid, &end_day, 1);

        for(int i=0; i<shm_ptr->num_primi; i++) { shm_ptr->daily_dishes_wasted[0] += shm_ptr->primi[i].porzioni_rimanenti; shm_ptr->total_dishes_wasted[0] += shm_ptr->primi[i].porzioni_rimanenti; }
        for(int i=0; i<shm_ptr->num_secondi; i++) { shm_ptr->daily_dishes_wasted[1] += shm_ptr->secondi[i].porzioni_rimanenti; shm_ptr->total_dishes_wasted[1] += shm_ptr->secondi[i].porzioni_rimanenti; }
        for(int i=0; i<shm_ptr->num_contorni; i++) { shm_ptr->daily_dishes_wasted[2] += shm_ptr->contorni[i].porzioni_rimanenti; shm_ptr->total_dishes_wasted[2] += shm_ptr->contorni[i].porzioni_rimanenti; }

        int gg = shm_ptr->current_day;
        int d_w1 = shm_ptr->daily_wait_count_stazioni[1], d_w2 = shm_ptr->daily_wait_count_stazioni[2];
        int d_w3 = shm_ptr->daily_wait_count_stazioni[3], d_w4 = shm_ptr->daily_wait_count_stazioni[4];
        int d_tot_w = d_w1+d_w2+d_w3+d_w4;
        int d_tot_time = shm_ptr->daily_wait_time_stazioni[1] + shm_ptr->daily_wait_time_stazioni[2] + shm_ptr->daily_wait_time_stazioni[3] + shm_ptr->daily_wait_time_stazioni[4];
        
        int t_w1 = shm_ptr->wait_count_stazioni[1], t_w2 = shm_ptr->wait_count_stazioni[2];
        int t_w3 = shm_ptr->wait_count_stazioni[3], t_w4 = shm_ptr->wait_count_stazioni[4];
        int t_tot_w = t_w1+t_w2+t_w3+t_w4;
        int t_tot_time = shm_ptr->wait_time_stazioni[1] + shm_ptr->wait_time_stazioni[2] + shm_ptr->wait_time_stazioni[3] + shm_ptr->wait_time_stazioni[4];

        int oper_distinti = shm_ptr->cassiere_has_worked; 
        for(int i=0; i<MAX_WORKERS; i++) if (shm_ptr->worker_has_worked[i]) oper_distinti++;

        printf("\n--- FINE GIORNO %d ---\n", gg);
        printf(">>> STATISTICHE DELLA GIORNATA <<<\n");
        printf("- Utenti serviti oggi: %d\n", shm_ptr->daily_users_served);
        printf("- Utenti non serviti (rinunce) oggi: %d\n", shm_ptr->daily_users_dropped);
        printf("- Incasso di oggi: %d euro\n", shm_ptr->daily_revenue);
        printf("- Pause effettuate oggi dallo staff: %d\n", shm_ptr->daily_pauses);
        printf("- Operatori attivi oggi: %d\n", shm_ptr->daily_active_ops);
        printf("- Piatti Distribuiti (Oggi): Primi=%d, Secondi=%d, Contorni=%d, Caffe=%d, Dolci=%d\n", 
               shm_ptr->daily_dishes_served[0], shm_ptr->daily_dishes_served[1], shm_ptr->daily_dishes_served[2], shm_ptr->daily_dishes_served[3], shm_ptr->daily_dishes_served[4]);
        printf("- Piatti Avanzati/Sprechi (Oggi): Primi=%d, Secondi=%d, Contorni=%d, Caffe/Dolci=0 (Illimitati)\n", 
               shm_ptr->daily_dishes_wasted[0], shm_ptr->daily_dishes_wasted[1], shm_ptr->daily_dishes_wasted[2]);
        printf("- Attesa Media (Oggi): Primi=%d, Secondi=%d, Caffe=%d, Cassa=%d | Complessiva Oggi: %d\n",
               d_w1 > 0 ? shm_ptr->daily_wait_time_stazioni[1]/d_w1 : 0, d_w2 > 0 ? shm_ptr->daily_wait_time_stazioni[2]/d_w2 : 0,
               d_w3 > 0 ? shm_ptr->daily_wait_time_stazioni[3]/d_w3 : 0, d_w4 > 0 ? shm_ptr->daily_wait_time_stazioni[4]/d_w4 : 0,
               d_tot_w > 0 ? d_tot_time / d_tot_w : 0);

        printf("\n>>> STATISTICHE CUMULATIVE E MEDIE (Fino al giorno %d) <<<\n", gg);
        printf("- Utenti serviti: Totale=%d, Media/gg=%.1f\n", shm_ptr->total_users_served, (float)shm_ptr->total_users_served/gg);
        printf("- Utenti non serviti: Totale=%d, Media/gg=%.1f\n", shm_ptr->total_users_dropped, (float)shm_ptr->total_users_dropped/gg);
        printf("- Incasso: Totale=%d euro, Media/gg=%.1f euro\n", shm_ptr->total_revenue, (float)shm_ptr->total_revenue/gg);
        printf("- Operatori distinti attivi nella simulazione finora: %d\n", oper_distinti);
        printf("- Pause staff: Totale=%d, Media/gg=%.1f\n", shm_ptr->total_pauses, (float)shm_ptr->total_pauses/gg);
        printf("- Piatti Distribuiti (Totali e Medie/gg):\n");
        printf("    Primi: %d (%.1f/gg), Secondi: %d (%.1f/gg), Contorni: %d (%.1f/gg)\n",
               shm_ptr->total_dishes_served[0], (float)shm_ptr->total_dishes_served[0]/gg,
               shm_ptr->total_dishes_served[1], (float)shm_ptr->total_dishes_served[1]/gg,
               shm_ptr->total_dishes_served[2], (float)shm_ptr->total_dishes_served[2]/gg);
        printf("    Caffe: %d (%.1f/gg), Dolci: %d (%.1f/gg)\n",
               shm_ptr->total_dishes_served[3], (float)shm_ptr->total_dishes_served[3]/gg,
               shm_ptr->total_dishes_served[4], (float)shm_ptr->total_dishes_served[4]/gg);
        printf("- Piatti Avanzati (Totali e Medie/gg):\n");
        printf("    Primi: %d (%.1f/gg), Secondi: %d (%.1f/gg), Contorni: %d (%.1f/gg), Caffe/Dolci: 0\n",
               shm_ptr->total_dishes_wasted[0], (float)shm_ptr->total_dishes_wasted[0]/gg,
               shm_ptr->total_dishes_wasted[1], (float)shm_ptr->total_dishes_wasted[1]/gg,
               shm_ptr->total_dishes_wasted[2], (float)shm_ptr->total_dishes_wasted[2]/gg);
        printf("- Attesa Media (Storica): Primi=%d, Secondi=%d, Caffe=%d, Cassa=%d | Complessiva Storica: %d\n",
               t_w1 > 0 ? shm_ptr->wait_time_stazioni[1]/t_w1 : 0, t_w2 > 0 ? shm_ptr->wait_time_stazioni[2]/t_w2 : 0,
               t_w3 > 0 ? shm_ptr->wait_time_stazioni[3]/t_w3 : 0, t_w4 > 0 ? shm_ptr->wait_time_stazioni[4]/t_w4 : 0,
               t_tot_w > 0 ? t_tot_time / t_tot_w : 0);

        shm_ptr->current_day++;
    }
    
    signal(SIGTERM, SIG_IGN);
    printf("\n=======================================================\n");
    printf("  SIMULAZIONE TERMINATA - CAUSA: %s\n", causa_terminazione);
    printf("=======================================================\n");
    
    kill(0, SIGTERM); while (wait(NULL) > 0); cleanup(0);
    return 0;
}