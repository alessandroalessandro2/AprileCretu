#include "../inc/common.h"

int shmid, semid, msgid;
shared_data_t *shm_ptr;

void load_menu(const char *filename, shared_data_t *shm) {
    FILE *file = fopen(filename, "r");
    if (!file) { perror("[Errore] Impossibile aprire menu.txt"); exit(1); }
    
    char line[256];
    shm->num_primi = 0;
    shm->num_secondi = 0;
    
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
            else if (strcmp(type, "CAFFE") == 0) {
                strncpy(shm->caffe[0].name, name, 31);
                shm->caffe[0].price = price;
            }
        }
    }
    fclose(file);
    printf("[Responsabile] Menu caricato: %d primi, %d secondi.\n", shm->num_primi, shm->num_secondi);
}

void cleanup(int sig) {
    (void)sig;
    printf("\n[Responsabile] Pulizia risorse IPC in corso...\n");
    shmdt(shm_ptr);
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
    
    memset(shm_ptr->active_ops, 0, sizeof(shm_ptr->active_ops));
    shm_ptr->active_ops[TYPE_CASSA] = 1; // 1 Cassiere
    shm_ptr->total_pauses = 0;
    
    load_menu("menu.txt", shm_ptr);
    
    // CORREZIONE 2: Creazione di 7 semafori
    semid = semget(SEM_KEY, 7, IPC_CREAT | 0666); 
    msgid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if(semid < 0 || msgid < 0) exit(1);
    
    union semun arg;
    arg.val = 1; semctl(semid, SEM_MUTEX, SETVAL, arg);
    // CORREZIONE: Assegnazione corretta dalle cfg
    arg.val = cfg.nof_table_seats;     semctl(semid, SEM_TAVOLI, SETVAL, arg); 
    arg.val = cfg.nof_wk_seats_primi;  semctl(semid, SEM_PRIMI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_secondi;semctl(semid, SEM_SECONDI, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_coffee; semctl(semid, SEM_COFFEE, SETVAL, arg);
    arg.val = cfg.nof_wk_seats_cassa;  semctl(semid, SEM_CASSA, SETVAL, arg);
    arg.val = 0; semctl(semid, SEM_SYNC, SETVAL, arg); 

    char tipo_operatore[2];
    for(int i = 0; i < cfg.nof_workers; i++) {
        int my_type = (i % 3) + 1; // 1, 2, 3
        shm_ptr->active_ops[my_type]++;          
        sprintf(tipo_operatore, "%d", my_type);
        
        if(fork() == 0) {
            execl("./bin/operatore", "operatore", tipo_operatore, config_file, NULL);
            perror("[Errore] execl operatore fallita");
            exit(1);
        }
    }
    
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
    
    int DAY_LENGTH = 50; 
    int current_day = 1;
    const char* causa_terminazione = "TIMEOUT (Raggiunto limite di giorni impostato)";

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
        while (t < DAY_LENGTH && shm_ptr->sim_running) {
            usleep(cfg.n_nano_secs / 1000); // Ritardo simulazione 
            
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
            
            // CORREZIONE 6: Calcolo dei piatti sprecati
            shm_ptr->dishes_wasted[0] += shm_ptr->portions_primi;
            shm_ptr->dishes_wasted[1] += shm_ptr->portions_secondi;

            printf("[Statistiche Giornaliere] Utenti serviti: %d, Incasso totale: %d euro\n", shm_ptr->users_served, shm_ptr->revenue);
            printf("[In coda ora] Utenti: %d\n", shm_ptr->users_waiting);
            
            // CORREZIONE 8: Aggiornamento Causa Terminazione
            if (shm_ptr->users_waiting >= cfg.overload_threshold) {
                causa_terminazione = "OVERLOAD (Troppi utenti in coda, superata la soglia)";
                printf("\n[!!! ALLARME !!!] %s.\n", causa_terminazione);
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
    
    // CORREZIONE 5: Statistiche Finali Complete
    int giorni_effettivi = current_day > 1 ? current_day - 1 : 1;

    printf("\n=================================\n");
    printf("     STATISTICHE FINALI MENSA    \n");
    printf("=================================\n");
    printf("Causa Terminazione: %s\n", causa_terminazione);
    printf("Utenti serviti totali: %d (Media/gg: %d)\n", shm_ptr->users_served, shm_ptr->users_served / giorni_effettivi);
    printf("Utenti non serviti (rinunciatari): %d (Media/gg: %d)\n", shm_ptr->users_dropped, shm_ptr->users_dropped / giorni_effettivi);
    printf("Incasso totale: %d euro (Media/gg: %d euro)\n", shm_ptr->revenue, shm_ptr->revenue / giorni_effettivi);
    printf("Piatti serviti - Primi: %d, Secondi: %d, Caffe: %d\n", shm_ptr->dishes_served[0], shm_ptr->dishes_served[1], shm_ptr->dishes_served[2]);
    printf("Piatti sprecati - Primi: %d, Secondi: %d\n", shm_ptr->dishes_wasted[0], shm_ptr->dishes_wasted[1]);
    printf("Pause totali staff: %d\n", shm_ptr->total_pauses);
    printf("=================================\n");
    
    kill(0, SIGTERM);
    while (wait(NULL) > 0);
    cleanup(0);
    
    return 0;
}