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
                // Gestione dell'array per i 4 tipi di caffè
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
    
    // Inizializzazione operatori attivi
    shm_ptr->active_ops[1] = 0; 
    shm_ptr->active_ops[2] = 0; 
    shm_ptr->active_ops[3] = 0; 
    shm_ptr->active_ops[4] = 1; 
    shm_ptr->total_pauses = 0;

    load_menu("menu.txt", shm_ptr);

    // Corretto a NUM_SEMAPHORES (7 semafori)
    semid = semget(SEM_KEY, NUM_SEMAPHORES, IPC_CREAT | 0666); 
    msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if(semid < 0 || msgid < 0) {
        perror("IPC get failed");
        cleanup(0);
    }

    union semun arg;
    arg.val = 1;                    semctl(semid, SEM_MUTEX, SETVAL, arg);   
    arg.val = cfg.nof_table_seats;  semctl(semid, SEM_TAVOLI, SETVAL, arg); // Reso dinamico 
    arg.val = cfg.nof_wk_seats_primi; semctl(semid, SEM_PRIMI, SETVAL, arg);   
    arg.val = cfg.nof_wk_seats_secondi; semctl(semid, SEM_SECONDI, SETVAL, arg); 
    arg.val = cfg.nof_wk_seats_coffee; semctl(semid, SEM_COFFEE, SETVAL, arg); // Nuovo semaforo
    arg.val = cfg.nof_wk_seats_cassa; semctl(semid, SEM_CASSA, SETVAL, arg);   
    arg.val = 0;                    semctl(semid, SEM_SYNC, SETVAL, arg); 

    // CREAZIONE OPERATORI
    char tipo_operatore[2];
    for(int i = 0; i < cfg.nof_workers; i++) {
        int my_type = (i % 3) + 1;
        shm_ptr->active_ops[my_type]++; 
        
        sprintf(tipo_operatore, "%d", my_type); 
        if(fork() == 0) {
            execl("./bin/operatore", "operatore", tipo_operatore, config_file, NULL);
            perror("[Errore] execl operatore fallita");
            exit(1); 
        }
    }
    
    // CREAZIONE CASSIERE
    if(fork() == 0) {
        execl("./bin/cassiere", "cassiere", config_file, NULL);
        perror("[Errore] execl cassiere fallita");
        exit(1);
    }

    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};

    int total_staff = cfg.nof_workers + 1; 
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
            // Tempo scalato correttamente sui nano secondi (convertiti in microsecondi per usleep)
            usleep(cfg.n_nano_secs / 1000); 
            
            semop(semid, &mutex_lock, 1); 
            shm_ptr->sim_time++;
            
            if (shm_ptr->sim_time % cfg.refill_period == 0) {
                shm_ptr->portions_primi = cfg.max_porzioni_primi;
                shm_ptr->portions_secondi = cfg.max_porzioni_secondi;
                printf("[CUCINA] Ding! REFILL EFFETTUATO (minuto simulato %d)\n", shm_ptr->sim_time);
            }
            semop(semid, &mutex_unlock, 1); 
            t++;
        }

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
                    shm_ptr->users_waiting = 0; 
                }
            }
            semop(semid, &mutex_unlock, 1);
            current_day++;
        }
    }

    signal(SIGTERM, SIG_IGN);
    
    printf("\n=================================\n");
    printf("     STATISTICHE MENSA OASI      \n");
    printf("=================================\n");
    printf("Utenti serviti (hanno pagato): %d\n", shm_ptr->users_served);
    printf("Incasso totale: %d euro\n", shm_ptr->revenue);
    printf("Piatti di primi serviti: %d\n", shm_ptr->dishes_served[0]);
    printf("Piatti di secondi serviti: %d\n", shm_ptr->dishes_served[1]);
    printf("Caffè serviti: %d\n", shm_ptr->dishes_served[2]);
    printf("Pause totali godute dallo staff: %d\n", shm_ptr->total_pauses);
    printf("Utenti rinunciatari: %d\n", shm_ptr->users_dropped);
    printf("=================================\n");
    
    kill(0, SIGTERM); 
    while (wait(NULL) > 0); 
    cleanup(0);
    return 0;
}