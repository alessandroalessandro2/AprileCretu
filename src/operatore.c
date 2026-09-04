#include "../inc/common.h"

int main(int argc, char *argv[]) {
    if (argc < 3) return 1;
    int my_id = atoi(argv[1]); 
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
    
    struct sembuf sync_signal = {SEM_SYNC, 1, 0};
    semop(semid, &sync_signal, 1);
    
    int pauses_taken = 0; 
    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    
    int local_day = 0;
    while (1) {
        while (shm_ptr->current_day == local_day && shm_ptr->sim_running) {
            usleep(10000);
        }
        if (!shm_ptr->sim_running) break;
        local_day = shm_ptr->current_day;
        
        int my_station = shm_ptr->worker_station[my_id];
        int tempo_medio = 10;
        if (my_station == TYPE_PRIMI) tempo_medio = cfg.avg_srvc_primi;
        else if (my_station == TYPE_SECONDI) tempo_medio = cfg.avg_srvc_secondi;
        else if (my_station == TYPE_COFFEE) tempo_medio = cfg.avg_srvc_coffee;
        
        while (1) {
            if (msgrcv(msgid, &req, msg_size, my_station, 0) == -1) break;
            
            // Messaggio che segnala la fine giornata per svuotare IPC
            if (req.sender_pid == 0) break;
            
            semop(semid, &mutex_lock, 1);
            int servito = 0;
            if (my_station == TYPE_PRIMI) {
                if (shm_ptr->portions_primi > 0 && shm_ptr->primi[req.indice_piatto].porzioni_rimanenti > 0) {
                    shm_ptr->primi[req.indice_piatto].porzioni_rimanenti--;
                    shm_ptr->portions_primi--;
                    shm_ptr->dishes_served[0]++; 
                    servito = 1;
                }
            } else if (my_station == TYPE_SECONDI) {
                if (req.sub_type == 0) { 
                    if (shm_ptr->portions_secondi > 0 && shm_ptr->secondi[req.indice_piatto].porzioni_rimanenti > 0) {
                        shm_ptr->secondi[req.indice_piatto].porzioni_rimanenti--;
                        shm_ptr->portions_secondi--;
                        shm_ptr->dishes_served[1]++; 
                        servito = 1;
                    }
                } else { // Contorno
                    if (shm_ptr->portions_contorni > 0 && shm_ptr->contorni[req.indice_piatto].porzioni_rimanenti > 0) {
                        shm_ptr->contorni[req.indice_piatto].porzioni_rimanenti--;
                        shm_ptr->portions_contorni--;
                        shm_ptr->dishes_served[2]++; 
                        servito = 1;
                    }
                }
            } else if (my_station == TYPE_COFFEE) {
                if (req.sub_type == 0) { // Caffè infinito
                    shm_ptr->dishes_served[4]++; 
                    servito = 1;
                } else { // Dolce
                    if (shm_ptr->dolce[req.indice_piatto].porzioni_rimanenti > 0) {
                        shm_ptr->dolce[req.indice_piatto].porzioni_rimanenti--;
                        shm_ptr->dishes_served[3]++;
                        servito = 1;
                    }
                }
            }
            semop(semid, &mutex_unlock, 1);
            
            if (servito == 1) {
                int percentuale = 50; 
                if (my_station == TYPE_COFFEE) percentuale = 80;
                int delta = (tempo_medio * percentuale) / 100; 
                int actual_time = (tempo_medio - delta) + (rand() % (2 * delta + 1));
                usleep(actual_time * 10000); 
            }
            
            res.mtype = req.sender_pid; 
            res.status = servito;
            msgsnd(msgid, &res, msg_size, 0);
            
            if (servito && pauses_taken < cfg.nof_pause) {
                if (rand() % 100 < 10) { 
                    semop(semid, &mutex_lock, 1);
                    if (shm_ptr->active_ops[my_station] > 1) { 
                        shm_ptr->active_ops[my_station]--; 
                        shm_ptr->total_pauses++; 
                        semop(semid, &mutex_unlock, 1);
                        
                        usleep(100000); 
                        pauses_taken++;
                        
                        semop(semid, &mutex_lock, 1);
                        shm_ptr->active_ops[my_station]++; 
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