#include "../inc/common.h"
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Uso: ./operatore <tipo: 1=Primi, 2=Secondi, 3=Caffe> <config_file>\n");
        exit(1);
    }
    
    int type = atoi(argv[1]);
    const char *config_file = argv[2];

    config_t cfg;
    memset(&cfg, 0, sizeof(config_t));
    load_config(config_file, &cfg);

    srand(getpid() ^ time(NULL));

    int msgid = msgget(MSG_KEY, 0666);
    int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, NUM_SEMAPHORES, 0666);
    if (msgid < 0 || shmid < 0 || semid < 0) exit(1);
    
    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);

    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};

    int sem_station;
    long msg_type_to_receive;
    int tempo_medio;
    int var_percent = 50; 

    if (type == 1) { 
        sem_station = SEM_PRIMI; 
        msg_type_to_receive = TYPE_PRIMI; 
        tempo_medio = cfg.avg_srvc_primi; 
    }
    else if (type == 2) { 
        sem_station = SEM_SECONDI; 
        msg_type_to_receive = TYPE_SECONDI; 
        tempo_medio = cfg.avg_srvc_secondi; 
    }
    else if (type == 3) { 
        sem_station = SEM_COFFEE; 
        msg_type_to_receive = TYPE_COFFEE; 
        tempo_medio = cfg.avg_srvc_coffee; 
        var_percent = 80; 
    }
    else exit(1);

    // Acquisizione fisica della postazione
    struct sembuf seat_lock = {sem_station, -1, SEM_UNDO};
    semop(semid, &seat_lock, 1);

    // Segnala di essere pronto alla barriera
    struct sembuf sync_signal = {SEM_SYNC, 1, 0};
    semop(semid, &sync_signal, 1);

    int pause_fatte = 0;
    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    int running = 1;

    while(running && msgrcv(msgid, &req, msg_size, msg_type_to_receive, 0) != -1) {
        
        // 1. Calcolo del tempo reale (con varianza)
        int delta = (tempo_medio * var_percent) / 100;
        int actual_time = (tempo_medio - delta) + (rand() % (2 * delta + 1));
        long micro_secs_per_unit = cfg.n_nano_secs / 1000;
        usleep(actual_time * micro_secs_per_unit); 

        // 2. Controllo porzioni e Risposta
        semop(semid, &mutex_lock, 1);
        int porzioni_ok = 0;
        
        if (type == 1 && shm_ptr->primi[req.indice_piatto].porzioni_rimanenti > 0) {
            shm_ptr->primi[req.indice_piatto].porzioni_rimanenti--;
            porzioni_ok = 1;
            shm_ptr->dishes_served[0]++; 
            shm_ptr->daily_dishes_served[0]++; // Punto 4: Statistica Giornaliera
        } 
        else if (type == 2 && shm_ptr->secondi[req.indice_piatto].porzioni_rimanenti > 0) {
            shm_ptr->secondi[req.indice_piatto].porzioni_rimanenti--;
            porzioni_ok = 1;
            shm_ptr->dishes_served[1]++; 
            shm_ptr->daily_dishes_served[1]++; // Punto 4: Statistica Giornaliera
        } 
        else if (type == 3) {
            porzioni_ok = 1; 
            shm_ptr->dishes_served[2]++; 
            shm_ptr->daily_dishes_served[2]++; // Punto 4: Statistica Giornaliera
        }
        semop(semid, &mutex_unlock, 1);

        res.mtype = req.sender_pid;
        res.status = porzioni_ok; 
        
        if (msgsnd(msgid, &res, msg_size, 0) == -1) {
            running = 0; 
            continue;
        }

        // 3. Gestione Pausa (dopo aver servito il cliente)
        if (pause_fatte < cfg.nof_pause && (rand() % 10 == 0)) { 
            semop(semid, &mutex_lock, 1);
            if (shm_ptr->active_ops[type] > 1) { 
                shm_ptr->active_ops[type]--; 
                
                shm_ptr->total_pauses++; 
                shm_ptr->daily_pauses++; // Punto 4: Statistica Giornaliera
                
                semop(semid, &mutex_unlock, 1);
                
                // Rilascia la postazione fisica (SEM_UNDO assicura pulizia se crasha in pausa)
                struct sembuf seat_unlock = {sem_station, 1, SEM_UNDO};
                semop(semid, &seat_unlock, 1);
                
                // Dorme per 10 "minuti" simulati
                usleep(10 * cfg.n_nano_secs / 1000); 
                
                // Cerca di riprendere il posto
                semop(semid, &seat_lock, 1);
                
                semop(semid, &mutex_lock, 1);
                shm_ptr->active_ops[type]++;
                semop(semid, &mutex_unlock, 1);
                
                pause_fatte++;
            } else {
                semop(semid, &mutex_unlock, 1);
            }
        }
    }

    shmdt(shm_ptr); // Unlinking della shared memory
    return 0;
}