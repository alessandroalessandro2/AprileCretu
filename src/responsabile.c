#include "../inc/common.h"

int shmid, semid, msgid;
shared_data_t *shm_ptr;

void load_menu(const char *filename, shared_data_t *shm) {
    FILE *file = fopen(filename, "r");
    if (!file) { perror("[Errore] Impossibile aprire menu.txt"); exit(1); }
    char line[256];
    shm->num_primi = 0;
    shm->num_secondi = 0;
    shm->num_caffe = 0;

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '\n' || line[0] == '#') continue;
        char type[16], name[32];
        int price;
        if (sscanf(line, "%15[^,],%31[^,],%d", type, name, &price) == 3) {
            if (strcmp(type, "PRIMO") == 0 && shm->num_primi < MAX_PIATTI) {
                strncpy(shm->primi[shm->num_primi].name, name, 31);
                shm->primi[shm->num_primi].price = price;
                shm->primi[shm->num_primi].porzioni_rimanenti = 20;
                shm->num_primi++;
            } 
            else if (strcmp(type, "SECONDO") == 0 && shm->num_secondi < MAX_PIATTI) {
                strncpy(shm->secondi[shm->num_secondi].name, name, 31);
                shm->secondi[shm->num_secondi].price = price;
                shm->secondi[shm->num_secondi].porzioni_rimanenti = 20;
                shm->num_secondi++;
            } 
            else if (strcmp(type, "DOLCE") == 0) {
                strncpy(shm->dolce.name, name, 31);
                shm->dolce.price = price;
            } 
            else if (strcmp(type, "CAFFE") == 0 && shm->num_caffe < 4) {
                strncpy(shm->caffe[shm->num_caffe].name, name, 31);
                shm->caffe[shm->num_caffe].price = price;
                shm->num_caffe++;
            }
        }
    }
    fclose(file);
    printf("[Responsabile] Menu caricato: %d primi, %d secondi, %d caffe.\n", 
           shm->num_primi, shm->num_secondi, shm->num_caffe);
}

void cleanup(int sig) {
    (void)sig; 
    printf("\n[Responsabile] Pulizia risorse IPC in corso...\n");
    if (shm_ptr) shmdt(shm_ptr);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);
    printf("[Responsabile] Terminazione completata.\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.conf";
    config_t cfg;
    memset(&cfg, 0, sizeof(config_t));
    
    printf("[Responsabile] Avvio simulazione Oasi del Golfo. Config: %s\n", config_file);
    load_config(config_file, &cfg);

    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    shmid = shmget(SHM_KEY, sizeof(shared_data_t), IPC_CREAT | 0666);
    if(shmid < 0) { perror("shmget"); exit(1); }
    shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    memset(shm_ptr, 0, sizeof(shared_data_t));
    
    shm_ptr->sim_running = 1;
    shm_ptr->portions_primi = cfg.max_porzioni_primi;
    shm_ptr->portions_secondi = cfg.max_porzioni_secondi;
    
    // Inizializzazione pulita
    for(int i=0; i<5; i++) shm_ptr->active_ops[i] = 0;
    shm_ptr->total_pauses = 0;

    load_menu("menu.txt", shm_ptr);

    semid = semget(SEM_KEY, NUM_SEMAPHORES, IPC_CREAT | 0666); 
    msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if(semid < 0 || msgid < 0) {
        perror("IPC get failed");
        cleanup(0);
    }

    union semun arg;
    arg.val = 1;                    semctl(semid, SEM_MUTEX, SETVAL, arg);   
    arg.val = cfg.nof_table_seats;  semctl(semid, SEM_TAVOLI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_primi; semctl(semid, SEM_PRIMI, SETVAL, arg);   
    arg.val = cfg.nof_wk_seats_secondi; semctl(semid, SEM_SECONDI, SETVAL, arg); 
    arg.val = cfg.nof_wk_seats_coffee; semctl(semid, SEM_COFFEE, SETVAL, arg); 
    arg.val = cfg.nof_wk_seats_cassa; semctl(semid, SEM_CASSA, SETVAL, arg);   
    arg.val = 0;                    semctl(semid, SEM_SYNC, SETVAL, arg); 

    // --- PUNTO 1: ASSEGNAZIONE INTELLIGENTE DELLO STAFF ---
    if (cfg.nof_workers < 4) {
        printf("[Avviso] NOF_WORKERS (%d) insufficiente. Portato al minimo legale di 4.\n", cfg.nof_workers);
        cfg.nof_workers = 4;
    }

    int assigned[5] = {0, 1, 1, 1, 1}; // Almeno 1 per stazione
    int workers_left = cfg.nof_workers - 4;

    double base_times[5] = {0, 
        (double)cfg.avg_srvc_primi, 
        (double)cfg.avg_srvc_secondi, 
        (double)cfg.avg_srvc_coffee, 
        (double)cfg.avg_srvc_cassa
    };

    while (workers_left > 0) {
        int slowest_station = 1;
        double max_effective_time = base_times[1] / assigned[1];
        
        for (int j = 2; j <= 4; j++) {
            double current_effective_time = base_times[j] / assigned[j];
            if (current_effective_time > max_effective_time) {
                max_effective_time = current_effective_time;
                slowest_station = j;
            }
        }
        assigned[slowest_station]++;
        workers_left--;
    }

    printf("[Responsabile] Assegnazione Staff completata (Totale: %d):\n", cfg.nof_workers);
    printf("  -> Primi: %d operatori\n", assigned[1]);
    printf("  -> Secondi: %d operatori\n", assigned[2]);
    printf("  -> Caffe': %d operatori\n", assigned[3]);
    printf("  -> Cassa: %d cassieri\n", assigned[4]);

    char tipo_operatore[2];
    for (int type = 1; type <= 4; type++) {
        for (int k = 0; k < assigned[type]; k++) {
            shm_ptr->active_ops[type]++; 
            
            if (fork() == 0) {
                if (type == TYPE_CASSA) {
                    execl("./bin/cassiere", "cassiere", config_file, NULL);
                    perror("[Errore] execl cassiere fallita");
                } else {
                    sprintf(tipo_operatore, "%d", type);
                    execl("./bin/operatore", "operatore", tipo_operatore, config_file, NULL);
                    perror("[Errore] execl operatore fallita");
                }
                exit(1);
            }
        }
    }

    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};

    int total_staff = cfg.nof_workers; 
    printf("[Responsabile] Attesa inizializzazione dello staff (%d processi)...\n", total_staff);
    
    struct sembuf sync_wait = {SEM_SYNC, -total_staff, 0};
    semop(semid, &sync_wait, 1);
    
    printf("[Responsabile] Staff pronto. LA SIMULAZIONE PARTE ORA!\n");

    int DAY_LENGTH_MINUTES = 180; // Esempio: 3 ore virtuali di apertura
    int current_day = 1;

    while (current_day <= cfg.sim_duration && shm_ptr->sim_running) {
        printf("\n=========================================\n");
        printf("   INIZIO GIORNO %d (di %d)\n", current_day, cfg.sim_duration);
        printf("=========================================\n");

        printf("[Responsabile] Apertura porte: arrivano %d nuovi utenti!\n", cfg.nof_users);
        for (int i = 0; i < cfg.nof_users; i++) {
            if (fork() == 0) {
                execl("./bin/utente", "utente", NULL);
                perror("[Errore] execl utente fallita");
                exit(1);
            }
        }

        int t = 0;
        while (t < DAY_LENGTH_MINUTES && shm_ptr->sim_running) {
            usleep(cfg.n_nano_secs / 1000); 
            
            semop(semid, &mutex_lock, 1); 
            shm_ptr->sim_time++;
            
            if (shm_ptr->sim_time > 0 && shm_ptr->sim_time % cfg.refill_period == 0) {
                // Punto 2: Refill effettivo di tutte le porzioni dei piatti nei menu
                for (int i = 0; i < shm_ptr->num_primi; i++) {
                    shm_ptr->primi[i].porzioni_rimanenti = cfg.max_porzioni_primi;
                }
                for (int i = 0; i < shm_ptr->num_secondi; i++) {
                    shm_ptr->secondi[i].porzioni_rimanenti = cfg.max_porzioni_secondi;
                }
                printf("[CUCINA] Ding! REFILL EFFETTUATO (minuto simulato %d)\n", shm_ptr->sim_time);
            }
            semop(semid, &mutex_unlock, 1); 
            t++;
        }

        // --- PUNTO 4: STATISTICHE GIORNALIERE E RESET ---
        if (shm_ptr->sim_running) {
            semop(semid, &mutex_lock, 1);
            printf("\n--- FINE GIORNO %d ---\n", current_day);
            printf("[In coda ora] Utenti: %d\n", shm_ptr->users_waiting);

            if (shm_ptr->users_waiting >= cfg.overload_threshold) {
                printf("\n[!!! ALLARME !!!] OVERLOAD raggiunto (%d in attesa, limite %d).\n", 
                       shm_ptr->users_waiting, cfg.overload_threshold);
                printf("Il Responsabile decreta la CHIUSURA DEFINITIVA del locale.\n");
                shm_ptr->sim_running = 0; 
            } else {
                if (shm_ptr->users_waiting > 0) {
                    printf("[Info] %d utenti rimasti in coda rinunciano e se ne vanno.\n", shm_ptr->users_waiting);
                    shm_ptr->users_dropped += shm_ptr->users_waiting;
                    shm_ptr->daily_users_dropped += shm_ptr->users_waiting;
                    shm_ptr->users_waiting = 0; 
                }
            }

            // --- STAMPA STATISTICHE GIORNALIERE ---
            printf("\n=== STATISTICHE GIORNALIERE (Giorno %d) ===\n", current_day);
            printf("Utenti serviti oggi: %d\n", shm_ptr->daily_users_served);
            printf("Utenti non serviti oggi: %d\n", shm_ptr->daily_users_dropped);
            printf("Piatti distribuiti oggi - Primi: %d, Secondi: %d, Caffe: %d\n", 
                   shm_ptr->daily_dishes_served[0], shm_ptr->daily_dishes_served[1], shm_ptr->daily_dishes_served[2]);
            
            double daily_avg_wait = (shm_ptr->daily_wait_count > 0) ? 
                                    (shm_ptr->daily_total_wait_time / shm_ptr->daily_wait_count) : 0;
            printf("Tempo medio di attesa oggi: %.2f secondi simulati\n", daily_avg_wait);
            
            // Conta operatori attivi oggi (chi ha fatto almeno 1 servizio o è in postazione)
            int ops_active_today = 0;
            for(int i=1; i<=4; i++) if (shm_ptr->active_ops[i] > 0) ops_active_today += shm_ptr->active_ops[i];
            printf("Operatori attivi oggi: %d\n", ops_active_today);
            printf("Pause effettuate oggi: %d\n", shm_ptr->daily_pauses);
            printf("Ricavo giornaliero: %d euro\n", shm_ptr->daily_revenue);

            // --- RESET CONTATORI GIORNALIERI ---
            shm_ptr->daily_users_served = 0;
            shm_ptr->daily_users_dropped = 0;
            shm_ptr->daily_revenue = 0;
            shm_ptr->daily_dishes_served[0] = 0;
            shm_ptr->daily_dishes_served[1] = 0;
            shm_ptr->daily_dishes_served[2] = 0;
            shm_ptr->daily_pauses = 0;
            shm_ptr->daily_total_wait_time = 0;
            shm_ptr->daily_wait_count = 0;

            semop(semid, &mutex_unlock, 1);
            current_day++;
        }
    }

    signal(SIGTERM, SIG_IGN);
    
    // --- PUNTO 4: STAMPA STATISTICHE GLOBALI FINALI ---
    printf("\n==========================================\n");
    printf("     STATISTICHE GLOBALI MENSA OASI       \n");
    printf("==========================================\n");
    printf("Giorni simulati: %d (su %d)\n", current_day - 1, cfg.sim_duration);
    printf("Utenti serviti totali: %d\n", shm_ptr->users_served);
    printf("Utenti non serviti totali: %d\n", shm_ptr->users_dropped);
    
    double avg_users = (current_day > 1) ? (double)shm_ptr->users_served / (current_day - 1) : shm_ptr->users_served;
    double avg_dropped = (current_day > 1) ? (double)shm_ptr->users_dropped / (current_day - 1) : shm_ptr->users_dropped;
    printf("Utenti serviti in media al giorno: %.1f\n", avg_users);
    printf("Utenti non serviti in media al giorno: %.1f\n", avg_dropped);
    
    printf("\n--- DISTRIBUZIONE PIATTI ---\n");
    printf("Piatti serviti totali - Primi: %d, Secondi: %d, Caffe: %d\n", 
           shm_ptr->dishes_served[0], shm_ptr->dishes_served[1], shm_ptr->dishes_served[2]);
    printf("Piatti serviti in media/giorno - Primi: %.1f, Secondi: %.1f, Caffe: %.1f\n", 
           (current_day > 1) ? (double)shm_ptr->dishes_served[0]/(current_day-1) : shm_ptr->dishes_served[0], 
           (current_day > 1) ? (double)shm_ptr->dishes_served[1]/(current_day-1) : shm_ptr->dishes_served[1], 
           (current_day > 1) ? (double)shm_ptr->dishes_served[2]/(current_day-1) : shm_ptr->dishes_served[2]);
    
    // Calcolo Avanzati (Totale porzioni create nei refill - Totale servite)
    int num_refills = (DAY_LENGTH_MINUTES / cfg.refill_period);
    int tot_porzioni_primi = cfg.max_porzioni_primi * num_refills * (current_day - 1);
    int tot_porzioni_secondi = cfg.max_porzioni_secondi * num_refills * (current_day - 1);
    printf("Piatti avanzati totali - Primi: %d, Secondi: %d\n", 
           tot_porzioni_primi - shm_ptr->dishes_served[0], 
           tot_porzioni_secondi - shm_ptr->dishes_served[1]);

    printf("\n--- TEMPI DI ATTESA ---\n");
    double total_avg_wait = (shm_ptr->users_served > 0) ? (shm_ptr->total_wait_time / shm_ptr->users_served) : 0;
    printf("Tempo medio attesa complessivo: %.2f sec\n", total_avg_wait);
    
    printf("\n--- PERSONALE ED INCASSI ---\n");
    printf("Operatori attivi durante la simulazione: %d\n", cfg.nof_workers);
    printf("Pause totali effettuate: %d (Media/giorno: %.1f)\n", 
           shm_ptr->total_pauses, (current_day > 1) ? (double)shm_ptr->total_pauses / (current_day-1) : shm_ptr->total_pauses);
    printf("Ricavo totale simulazione: %d euro\n", shm_ptr->revenue);
    printf("Ricavo medio per giornata: %.2f euro\n", (current_day > 1) ? (double)shm_ptr->revenue / (current_day-1) : shm_ptr->revenue);
    printf("==========================================\n");
    
    kill(0, SIGTERM); 
    while (wait(NULL) > 0); 
    cleanup(0);
    return 0;
}