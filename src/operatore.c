#include "../inc/common.h"



int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    int type = atoi(argv[1]); 
    const char *config_file = (argc > 2) ? argv[2] : "config.conf";

    config_t cfg;
    memset(&cfg, 0, sizeof(config_t));
    load_config(config_file, &cfg);

    srand(getpid() ^ time(NULL));

    int msgid = msgget(MSG_KEY, 0666);
    int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, 5, 0666);
    if (msgid < 0 || shmid < 0 || semid < 0) exit(1);
    
    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);

    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};

    // --- SEGNALA DI ESSERE PRONTO ---
    struct sembuf sync_signal = {SEM_SYNC, 1, 0};
    semop(semid, &sync_signal, 1);

    int tempo_medio = 10;
    if (type == TYPE_PRIMI) tempo_medio = cfg.avg_srvc_primi;
    else if (type == TYPE_SECONDI) tempo_medio = cfg.avg_srvc_secondi;

    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    
    // Contatore pause per questo specifico operatore
    int pauses_taken = 0; 

    int running = 1;
    
    // Il ciclo continua finché 'running' è vero E la ricezione del messaggio ha successo (non è -1)
    while (running && msgrcv(msgid, &req, msg_size, type, 0) != -1) {
        
        // 2. SCALA LE PORZIONI IN MUTUA ESCLUSIONE
        semop(semid, &mutex_lock, 1);
        int servito = 0;
        
        if (type == TYPE_PRIMI) {
            if (shm_ptr->primi[req.indice_piatto].porzioni_rimanenti > 0) {
                shm_ptr->primi[req.indice_piatto].porzioni_rimanenti--;
                shm_ptr->dishes_served[0]++; 
                servito = 1;
            }
        } 
        else if (type == TYPE_SECONDI) {
            if (shm_ptr->secondi[req.indice_piatto].porzioni_rimanenti > 0) {
                shm_ptr->secondi[req.indice_piatto].porzioni_rimanenti--;
                shm_ptr->dishes_served[1]++; 
                servito = 1;
            }
        } 
        else if (type == TYPE_COFFEE) {
            shm_ptr->dishes_served[2]++; 
            servito = 1;
        }
        semop(semid, &mutex_unlock, 1);
        
        // 3. SIMULA TEMPO DI PREPARAZIONE
        if (servito == 1) {
            int percentuale = 50; 
            if (type == TYPE_COFFEE) percentuale = 80;
            
            int delta = (tempo_medio * percentuale) / 100; 
            int actual_time = (tempo_medio - delta) + (rand() % (2 * delta + 1));
            usleep(actual_time * 10000); 
        }

        // 4. RISPONDE AL CLIENTE
        res.mtype = req.sender_pid; 
        res.status = servito;       
        
       
        if (msgsnd(msgid, &res, msg_size, 0) == -1) {
            running = 0; 
        } else {
            // --- 5. GESTIONE PAUSA (Solo se tutto è andato bene) ---
            if (pauses_taken < cfg.nof_pause) {
                if (rand() % 100 < 10) { 
                    semop(semid, &mutex_lock, 1);
                    if (shm_ptr->active_ops[type] > 1) { 
                        shm_ptr->active_ops[type]--; 
                        shm_ptr->total_pauses++; 
                        semop(semid, &mutex_unlock, 1);
                        
                        usleep(300000); 
                        pauses_taken++;
                        
                        semop(semid, &mutex_lock, 1);
                        shm_ptr->active_ops[type]++; 
                        semop(semid, &mutex_unlock, 1);
                    } else {
                        semop(semid, &mutex_unlock, 1);
                    }
                }
            }
        }
    }

    shmdt(shm_ptr);
    return 0;
}