#include "../inc/common.h"

int shmid, semid, msgid;
shared_data_t *shm_ptr;

void load_menu(const char *filename, shared_data_t *shm) {
    FILE *file = fopen(filename, "r");
    if (!file) { perror("[Errore] Impossibile aprire menu.txt"); exit(1); }
    char line[256];
    shm->num_primi = 0; shm->num_secondi = 0;
    shm->num_contorni = 0; shm->num_dolce = 0; shm->num_caffe = 0;
    
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '\n' || line[0] == '#') continue;
        char type[16], name[32];
        int price;
        if (sscanf(line, "%15[^,],%31[^,],%d", type, name, &price) == 3) {
            if (strcmp(type, "PRIMO") == 0 && shm->num_primi < MAX_PIATTI) {
                strncpy(shm->primi[shm->num_primi].name, name, 31);
                shm->num_primi++;
            } else if (strcmp(type, "SECONDO") == 0 && shm->num_secondi < MAX_PIATTI) {
                strncpy(shm->secondi[shm->num_secondi].name, name, 31);
                shm->num_secondi++;
            } else if (strcmp(type, "CONTORNO") == 0 && shm->num_contorni < MAX_PIATTI) {
                strncpy(shm->contorni[shm->num_contorni].name, name, 31);
                shm->num_contorni++;
            } else if (strcmp(type, "DOLCE") == 0 && shm->num_dolce < MAX_PIATTI) {
                strncpy(shm->dolce[shm->num_dolce].name, name, 31);
                shm->num_dolce++;
            } else if (strcmp(type, "CAFFE") == 0 && shm->num_caffe < MAX_PIATTI) {
                strncpy(shm->caffe[shm->num_caffe].name, name, 31);
                shm->num_caffe++;
            }
        }
    }
    fclose(file);
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
    if(shmid < 0) exit(1);
    shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    memset(shm_ptr, 0, sizeof(shared_data_t));
    
    shm_ptr->sim_running = 1;
    shm_ptr->price_primi = cfg.price_primi;
    shm_ptr->price_secondi = cfg.price_secondi;
    shm_ptr->price_coffee = cfg.price_coffee;
    
    load_menu("menu.txt", shm_ptr);
    
    semid = semget(SEM_KEY, NUM_SEMAPHORES, IPC_CREAT | 0666);
    msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
    
    union semun arg;
    arg.val = 1;  semctl(semid, SEM_MUTEX, SETVAL, arg);
    arg.val = cfg.nof_table_seats; semctl(semid, SEM_TAVOLI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_primi; semctl(semid, SEM_PRIMI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_secondi; semctl(semid, SEM_SECONDI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_coffee; semctl(semid, SEM_COFFEE, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_cassa; semctl(semid, SEM_CASSA, SETVAL, arg);
    arg.val = 0;  semctl(semid, SEM_SYNC, SETVAL, arg); 

    // CREAZIONE OPERATORI (una volta sola, passiamo l'ID invece della stazione fissa)
    for(int i = 0; i < cfg.nof_workers; i++) {
        char id_str[16];
        sprintf(id_str, "%d", i);
        if(fork() == 0) {
            execl("./bin/operatore", "operatore", id_str, config_file, NULL);
            exit(1);
        }
    }
    
    // CREAZIONE CASSIERE
    if(fork() == 0) { execl("./bin/cassiere", "cassiere", config_file, NULL); exit(1); }
    
    // CREAZIONE UTENTI (una volta sola come richiesto)
    for (int i = 0; i < cfg.nof_users; i++) {
        if (fork() == 0) { execl("./bin/utente", "utente", NULL); exit(1); }
    }
    
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
    
    // --- BARRIERA DI INIZIALIZZAZIONE GLOBALE STAFF + UTENTI ---
    int total_procs = cfg.nof_workers + 1 + cfg.nof_users;
    printf("[Responsabile] Attesa inizializzazione di %d processi (Operatori + Cassiere + Utenti)...\n", total_procs);
    struct sembuf sync_wait = {SEM_SYNC, -total_procs, 0};
    semop(semid, &sync_wait, 1);
    
    printf("[Responsabile] Staff e Utenti pronti. LA SIMULAZIONE PARTE ORA!\n");
    
    int DAY_LENGTH = 50; 
    int current_day = 1;
    
    while (current_day <= cfg.sim_duration && shm_ptr->sim_running) {
        printf("\n=========================================\n");
        printf("   INIZIO GIORNO %d (di %d)\n", current_day, cfg.sim_duration);
        
        // Assegnazione intelligente all'inizio della giornata (CORREZIONE 4 e 8)
        int base_times[3] = {cfg.avg_srvc_primi, cfg.avg_srvc_secondi, cfg.avg_srvc_coffee};
        int assigned[3] = {1, 1, 1}; // minimo 1 per stazione
        int remaining = cfg.nof_workers - 3;
        while (remaining > 0) {
            double max_eff = -1.0;
            int best_st = 0;
            for (int j = 0; j < 3; j++) {
                double eff = (double)base_times[j] / assigned[j];
                if (eff > max_eff) { max_eff = eff; best_st = j; }
            }
            assigned[best_st]++;
            remaining--;
        }
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->active_ops[TYPE_PRIMI] = assigned[0];
        shm_ptr->active_ops[TYPE_SECONDI] = assigned[1];
        shm_ptr->active_ops[TYPE_COFFEE] = assigned[2];
        
        int w_idx = 0;
        for(int j=0; j<assigned[0]; j++) shm_ptr->worker_station[w_idx++] = TYPE_PRIMI;
        for(int j=0; j<assigned[1]; j++) shm_ptr->worker_station[w_idx++] = TYPE_SECONDI;
        for(int j=0; j<assigned[2]; j++) shm_ptr->worker_station[w_idx++] = TYPE_COFFEE;

        // Inizializza al valore AVG_REFILL invece che a MAX all'inizio della giornata (CORREZIONE 2)
        shm_ptr->portions_primi = cfg.avg_refill_primi;
        shm_ptr->portions_secondi = cfg.avg_refill_secondi;
        shm_ptr->portions_contorni = cfg.avg_refill_secondi;
        for(int i=0; i<shm_ptr->num_primi; i++) shm_ptr->primi[i].porzioni_rimanenti = cfg.avg_refill_primi;
        for(int i=0; i<shm_ptr->num_secondi; i++) shm_ptr->secondi[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        for(int i=0; i<shm_ptr->num_contorni; i++) shm_ptr->contorni[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        for(int i=0; i<shm_ptr->num_dolce; i++) shm_ptr->dolce[i].porzioni_rimanenti = cfg.avg_refill_secondi;
        
        shm_ptr->users_done_today = 0;
        shm_ptr->current_day = current_day; // SVEGLIA TUTTI GLI UTENTI E OPERATORI!
        semop(semid, &mutex_unlock, 1);
        
        int t = 0;
        while (t < DAY_LENGTH && shm_ptr->sim_running) {
            usleep(cfg.n_nano_secs / 1000); 
            semop(semid, &mutex_lock, 1);
            shm_ptr->sim_time++;
            
            // Refill porta a MAX_PORZIONI
            if (shm_ptr->sim_time % cfg.refill_period == 0) {
                shm_ptr->portions_primi = cfg.max_porzioni_primi;
                shm_ptr->portions_secondi = cfg.max_porzioni_secondi;
                shm_ptr->portions_contorni = cfg.max_porzioni_secondi;
                for(int i=0; i<shm_ptr->num_primi; i++) shm_ptr->primi[i].porzioni_rimanenti = cfg.max_porzioni_primi;
                for(int i=0; i<shm_ptr->num_secondi; i++) shm_ptr->secondi[i].porzioni_rimanenti = cfg.max_porzioni_secondi;
                for(int i=0; i<shm_ptr->num_contorni; i++) shm_ptr->contorni[i].porzioni_rimanenti = cfg.max_porzioni_secondi;
                for(int i=0; i<shm_ptr->num_dolce; i++) shm_ptr->dolce[i].porzioni_rimanenti = cfg.max_porzioni_secondi;
            }
            semop(semid, &mutex_unlock, 1);
            t++;
        }
        
        // --- FINE DELLA GIORNATA: CONTROLLI ---
        if (shm_ptr->sim_running) {
            semop(semid, &mutex_lock, 1);
            printf("\n--- FINE GIORNO %d ---\n", current_day);
            printf("[Statistiche Totali] Utenti serviti: %d, Incasso: %d euro\n", shm_ptr->users_served, shm_ptr->revenue);
            
            // Controllo Overload (CORREZIONE > invece di >=)
            if (shm_ptr->users_waiting > cfg.overload_threshold) {
                printf("\n[!!! ALLARME !!!] OVERLOAD raggiunto (%d in attesa, limite %d).\n",
                        shm_ptr->users_waiting, cfg.overload_threshold);
                shm_ptr->sim_running = 0;
            } else {
                if (shm_ptr->users_waiting > 0) {
                    shm_ptr->users_dropped += shm_ptr->users_waiting;
                    shm_ptr->users_waiting = 0; 
                }
            }
            
            printf("[Tempi Medi (sim_time)] Primi: %.1f | Secondi+Contorni: %.1f | Caffè+Dolce: %.1f | Cassa: %.1f\n",
                shm_ptr->wait_count_stazioni[TYPE_PRIMI] > 0 ? shm_ptr->wait_time_stazioni[TYPE_PRIMI]/shm_ptr->wait_count_stazioni[TYPE_PRIMI] : 0.0,
                shm_ptr->wait_count_stazioni[TYPE_SECONDI] > 0 ? shm_ptr->wait_time_stazioni[TYPE_SECONDI]/shm_ptr->wait_count_stazioni[TYPE_SECONDI] : 0.0,
                shm_ptr->wait_count_stazioni[TYPE_COFFEE] > 0 ? shm_ptr->wait_time_stazioni[TYPE_COFFEE]/shm_ptr->wait_count_stazioni[TYPE_COFFEE] : 0.0,
                shm_ptr->wait_count_stazioni[TYPE_CASSA] > 0 ? shm_ptr->wait_time_stazioni[TYPE_CASSA]/shm_ptr->wait_count_stazioni[TYPE_CASSA] : 0.0
            );
            semop(semid, &mutex_unlock, 1);
            
            // Permettiamo alla coda IPC di svuotarsi e ai processi utente di finire il ciclo giornaliero
            while (shm_ptr->sim_running) {
                semop(semid, &mutex_lock, 1);
                int done = shm_ptr->users_done_today;
                semop(semid, &mutex_unlock, 1);
                if (done >= cfg.nof_users) break;
                usleep(10000);
            }
            
            // Manda segnale di fine giornata per resettare le code IPC degli operatori
            if (shm_ptr->sim_running) {
                size_t msg_size = sizeof(msg_t) - sizeof(long);
                msg_t end_msg = {0}; end_msg.sender_pid = 0; end_msg.status = 0;
                for(int i=0; i<assigned[0]; i++) { end_msg.mtype = TYPE_PRIMI; msgsnd(msgid, &end_msg, msg_size, 0); }
                for(int i=0; i<assigned[1]; i++) { end_msg.mtype = TYPE_SECONDI; msgsnd(msgid, &end_msg, msg_size, 0); }
                for(int i=0; i<assigned[2]; i++) { end_msg.mtype = TYPE_COFFEE; msgsnd(msgid, &end_msg, msg_size, 0); }
            }
            current_day++;
        }
    }
    
    printf("\n=================================\n");
    printf("     STATISTICHE MENSA OASI      \n");
    printf("=================================\n");
    printf("Utenti serviti (hanno pagato): %d\n", shm_ptr->users_served);
    printf("Incasso totale: %d euro\n", shm_ptr->revenue);
    printf("Piatti serviti -> Primi: %d, Secondi: %d, Contorni: %d, Dolce: %d, Caffè: %d\n", 
           shm_ptr->dishes_served[0], shm_ptr->dishes_served[1], shm_ptr->dishes_served[2], shm_ptr->dishes_served[3], shm_ptr->dishes_served[4]);
    printf("Pause totali godute dallo staff: %d\n", shm_ptr->total_pauses);
    printf("Utenti rinunciatari (fine giornata): %d\n", shm_ptr->users_dropped);
    printf("=================================\n");
    
    kill(0, SIGTERM);
    while (wait(NULL) > 0);
    cleanup(0);
    return 0;
}