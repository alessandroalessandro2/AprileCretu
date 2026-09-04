#include "../inc/common.h"

int shmid, semid, msgid;
shared_data_t *shm_ptr;

void load_menu(const char *filename, shared_data_t *shm) {
    FILE *file = fopen(filename, "r");
    if (!file) { perror("[Errore] Impossibile aprire menu.txt"); exit(1); }
    
    char line[256];
    shm->num_primi = 0; shm->num_secondi = 0; shm->num_contorni = 0; shm->num_caffe = 0;
    
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '\n' || line[0] == '#') continue;
        char type[16], name[32];
        int price;
        if (sscanf(line, "%15[^,],%31[^,],%d", type, name, &price) == 3) {
            if (strcmp(type, "PRIMO") == 0 && shm->num_primi < MAX_PIATTI) {
                strncpy(shm->primi[shm->num_primi].name, name, 31);
                shm->primi[shm->num_primi].price = price;
                shm->num_primi++;
            } else if (strcmp(type, "SECONDO") == 0 && shm->num_secondi < MAX_PIATTI) {
                strncpy(shm->secondi[shm->num_secondi].name, name, 31);
                shm->secondi[shm->num_secondi].price = price;
                shm->num_secondi++;
            } else if (strcmp(type, "CONTORNO") == 0 && shm->num_contorni < MAX_PIATTI) {
                strncpy(shm->contorni[shm->num_contorni].name, name, 31);
                shm->contorni[shm->num_contorni].price = price;
                shm->num_contorni++;
            } else if (strcmp(type, "CAFFE") == 0 && shm->num_caffe < 4) {
                strncpy(shm->caffe[shm->num_caffe].name, name, 31);
                shm->caffe[shm->num_caffe].price = price;
                shm->num_caffe++;
            } else if (strcmp(type, "DOLCE") == 0) {
                strncpy(shm->dolce.name, name, 31);
                shm->dolce.price = price;
            }
        }
    }
    fclose(file);
    printf("[Responsabile] Menu caricato: %d Primi, %d Secondi, %d Contorni, %d Caffe.\n", 
           shm->num_primi, shm->num_secondi, shm->num_contorni, shm->num_caffe);
}

void cleanup(int sig) {
    (void)sig;
    shmdt(shm_ptr);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);
    exit(0);
}

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.conf";
    config_t cfg;
    memset(&cfg, 0, sizeof(config_t));
    load_config(config_file, &cfg);
    
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    
    shmid = shmget(SHM_KEY, sizeof(shared_data_t), IPC_CREAT | 0666);
    shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    memset(shm_ptr, 0, sizeof(shared_data_t));
    
    shm_ptr->sim_running = 1;
    load_menu("menu.txt", shm_ptr);
    
    semid = semget(SEM_KEY, 8, IPC_CREAT | 0666); 
    msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
    
    union semun arg;
    arg.val = 1; semctl(semid, SEM_MUTEX, SETVAL, arg);
    arg.val = cfg.nof_table_seats;     semctl(semid, SEM_TAVOLI, SETVAL, arg); 
    arg.val = cfg.nof_wk_seats_primi;  semctl(semid, SEM_PRIMI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_secondi;semctl(semid, SEM_SECONDI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_coffee; semctl(semid, SEM_COFFEE, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_cassa;  semctl(semid, SEM_CASSA, SETVAL, arg);
    arg.val = 0; semctl(semid, SEM_DAY_START, SETVAL, arg); 
    arg.val = 0; semctl(semid, SEM_DAY_END, SETVAL, arg); 

    // CREAZIONE DI TUTTI I PROCESSI (UTENTI INCLUSI) ALL'INIZIO
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
    printf("[Responsabile] Creati %d processi totali. Inizio turni...\n", total_processes);
    
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
        shm_ptr->day_ended = 0;
        shm_ptr->sim_time = 0;
        shm_ptr->daily_users_served = 0;
        shm_ptr->daily_users_dropped = 0;
        shm_ptr->daily_revenue = 0;
        shm_ptr->daily_pauses = 0;

        // ASSEGNAZIONE OPERATORI (Privilegia il servizio più lento)
        memset(shm_ptr->op_assignment, 0, sizeof(shm_ptr->op_assignment));
        shm_ptr->op_assignment[0] = TYPE_PRIMI;
        shm_ptr->op_assignment[1] = TYPE_SECONDI;
        shm_ptr->op_assignment[2] = TYPE_COFFEE;
        
        int max_type = TYPE_PRIMI;
        int max_time = cfg.avg_srvc_primi;
        if(cfg.avg_srvc_secondi > max_time) { max_time = cfg.avg_srvc_secondi; max_type = TYPE_SECONDI; }
        if(cfg.avg_srvc_coffee > max_time)  { max_time = cfg.avg_srvc_coffee; max_type = TYPE_COFFEE; }
        for(int i = 3; i < cfg.nof_workers; i++) {
            shm_ptr->op_assignment[i] = max_type;
        }

        // REFILL INIZIALE
        for(int i=0; i<shm_ptr->num_primi; i++) shm_ptr->primi[i].porzioni_rimanenti = cfg.avg_refill_primi;
        for(int i=0; i<shm_ptr->num_secondi; i++) shm_ptr->secondi[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        for(int i=0; i<shm_ptr->num_contorni; i++) shm_ptr->contorni[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        semop(semid, &mutex_unlock, 1);

        // SVEGLIA TUTTI I PROCESSI PER IL NUOVO GIORNO
        struct sembuf start_day = {SEM_DAY_START, total_processes, 0};
        semop(semid, &start_day, 1);
        
        // CICLO DEL TEMPO SIMULATO DELLA GIORNATA
        int t = 0;
        while (t < DAY_LENGTH && shm_ptr->sim_running) {
            usleep(cfg.n_nano_secs / 1000); 
            
            semop(semid, &mutex_lock, 1);
            shm_ptr->sim_time++;
            
            // REFILL PERIODICO
            if (shm_ptr->sim_time % cfg.refill_period == 0) {
                for(int i=0; i<shm_ptr->num_primi; i++) {
                    shm_ptr->primi[i].porzioni_rimanenti += cfg.avg_refill_primi;
                    if(shm_ptr->primi[i].porzioni_rimanenti > cfg.max_porzioni_primi) shm_ptr->primi[i].porzioni_rimanenti = cfg.max_porzioni_primi;
                }
                for(int i=0; i<shm_ptr->num_secondi; i++) {
                    shm_ptr->secondi[i].porzioni_rimanenti += cfg.avg_refill_secondi;
                    if(shm_ptr->secondi[i].porzioni_rimanenti > cfg.max_porzioni_secondi) shm_ptr->secondi[i].porzioni_rimanenti = cfg.max_porzioni_secondi;
                }
            }
            
            // CONTROLLO OVERLOAD (Strettamente maggiore)
            if (shm_ptr->users_waiting > cfg.overload_threshold) {
                causa_terminazione = "OVERLOAD (Superata la soglia di utenti in attesa)";
                printf("\n[!!! ALLARME !!!] %s.\n", causa_terminazione);
                shm_ptr->sim_running = 0; 
            }
            semop(semid, &mutex_unlock, 1);
            t++;
        }
        
        // CHIUSURA GIORNATA
        semop(semid, &mutex_lock, 1);
        shm_ptr->day_ended = 1;
        
        // CALCOLO SPRECHI (Wasted)
        for(int i=0; i<shm_ptr->num_primi; i++) shm_ptr->total_dishes_wasted[0] += shm_ptr->primi[i].porzioni_rimanenti;
        for(int i=0; i<shm_ptr->num_secondi; i++) shm_ptr->total_dishes_wasted[1] += shm_ptr->secondi[i].porzioni_rimanenti;
        for(int i=0; i<shm_ptr->num_contorni; i++) shm_ptr->total_dishes_wasted[2] += shm_ptr->contorni[i].porzioni_rimanenti;

        printf("\n--- FINE GIORNO %d ---\n", shm_ptr->current_day);
        printf("[Statistiche Giornaliere] Serviti: %d, Rinunce: %d, Incasso: %d euro, Pause: %d\n", 
               shm_ptr->daily_users_served, shm_ptr->daily_users_dropped, shm_ptr->daily_revenue, shm_ptr->daily_pauses);
        
        shm_ptr->current_day++;
        semop(semid, &mutex_unlock, 1);

        // ATTENDE CHE TUTTI I PROCESSI ABBIANO RICONOSCIUTO LA FINE DELLA GIORNATA
        struct sembuf end_day = {SEM_DAY_END, -total_processes, 0};
        semop(semid, &end_day, 1);
    }
    
    signal(SIGTERM, SIG_IGN);
    
    // STAMPA STATISTICHE FINALI COMPLETE
    int giorni_effettivi = (shm_ptr->current_day > 1) ? (shm_ptr->current_day - 1) : 1;

    printf("\n=================================\n");
    printf("     STATISTICHE FINALI MENSA    \n");
    printf("=================================\n");
    printf("Causa Terminazione: %s\n", causa_terminazione);
    printf("Utenti serviti totali: %d (Media/gg: %d)\n", shm_ptr->total_users_served, shm_ptr->total_users_served / giorni_effettivi);
    printf("Utenti non serviti (rinunciatari): %d (Media/gg: %d)\n", shm_ptr->total_users_dropped, shm_ptr->total_users_dropped / giorni_effettivi);
    printf("Incasso totale: %d euro (Media/gg: %d euro)\n", shm_ptr->total_revenue, shm_ptr->total_revenue / giorni_effettivi);
    printf("Piatti distribuiti - Primi: %d, Secondi: %d, Contorni: %d, Caffe: %d\n", 
           shm_ptr->total_dishes_served[0], shm_ptr->total_dishes_served[1], shm_ptr->total_dishes_served[2], shm_ptr->total_dishes_served[3]);
    printf("Piatti sprecati - Primi: %d, Secondi: %d, Contorni: %d\n", 
           shm_ptr->total_dishes_wasted[0], shm_ptr->total_dishes_wasted[1], shm_ptr->total_dishes_wasted[2]);
    printf("Pause totali staff: %d (Media/gg: %d)\n", shm_ptr->total_pauses, shm_ptr->total_pauses / giorni_effettivi);
    
    printf("\n--- TEMPI MEDI DI ATTESA ---\n");
    int tot_wt = 0, tot_wc = 0;
    for(int i=1; i<=4; i++) {
        tot_wt += shm_ptr->wait_time_stazioni[i];
        tot_wc += shm_ptr->wait_count_stazioni[i];
        if(shm_ptr->wait_count_stazioni[i] > 0)
            printf("Stazione %d: %d tick\n", i, shm_ptr->wait_time_stazioni[i] / shm_ptr->wait_count_stazioni[i]);
    }
    if(tot_wc > 0) printf("Attesa media complessiva: %d tick\n", tot_wt / tot_wc);
    printf("=================================\n");
    
    kill(0, SIGTERM);
    while (wait(NULL) > 0);
    cleanup(0);
    
    return 0;
}