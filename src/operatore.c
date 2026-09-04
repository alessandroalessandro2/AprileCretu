#include "../inc/common.h"

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    int my_id = atoi(argv[1]);
    
    const char *config_file = (argc > 2) ? argv[2] : "config.conf";
    config_t cfg;
    memset(&cfg, 0, sizeof(config_t));
    load_config(config_file, &cfg);
    
    srand(getpid() ^ time(NULL));
    int msgid = msgget(MSG_KEY, 0666);
    int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, 8, 0666);
    if (msgid < 0 || shmid < 0 || semid < 0) exit(1);
    
    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
    
    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    int pauses_taken = 0;
    
    while (1) {
        // Barriera di inizio giornata
        struct sembuf start_day = {SEM_DAY_START, -1, 0};
        semop(semid, &start_day, 1);
        if (!shm_ptr->sim_running) break;
        
        semop(semid, &mutex_lock, 1);
        int type = shm_ptr->op_assignment[my_id];
        shm_ptr->active_ops[type]++;
        semop(semid, &mutex_unlock, 1);
        
        int tempo_medio = 10;
        int station_sem = -1;
        if (type == TYPE_PRIMI) { tempo_medio = cfg.avg_srvc_primi; station_sem = SEM_PRIMI; }
        else if (type == TYPE_SECONDI) { tempo_medio = cfg.avg_srvc_secondi; station_sem = SEM_SECONDI; }
        else if (type == TYPE_COFFEE) { tempo_medio = cfg.avg_srvc_coffee; station_sem = SEM_COFFEE; }

        struct sembuf acq_station = {station_sem, -1, SEM_UNDO};
        struct sembuf rel_station = {station_sem, 1, SEM_UNDO};

        pauses_taken = 0;

        while (1) {
            // IPC_NOWAIT impedisce il blocco permanente a fine giornata
            if (msgrcv(msgid, &req, msg_size, type, IPC_NOWAIT) != -1) {
                if (shm_ptr->day_ended) {
                    res.mtype = req.sender_pid;
                    res.status = 0; // Mensa chiusa
                    msgsnd(msgid, &res, msg_size, 0);
                    continue;
                }

                if (station_sem != -1) semop(semid, &acq_station, 1);

                semop(semid, &mutex_lock, 1);
                int servito = 0;
                if (type == TYPE_PRIMI && shm_ptr->primi[req.indice_piatto].porzioni_rimanenti > 0) {
                    shm_ptr->primi[req.indice_piatto].porzioni_rimanenti--;
                    shm_ptr->total_dishes_served[0]++;
                    servito = 1;
                } 
                else if (type == TYPE_SECONDI && shm_ptr->secondi[req.indice_piatto].porzioni_rimanenti > 0) {
                    shm_ptr->secondi[req.indice_piatto].porzioni_rimanenti--;
                    shm_ptr->total_dishes_served[1]++;
                    
                    // Se disponibile scala anche il contorno
                    if(shm_ptr->num_contorni > 0 && shm_ptr->contorni[0].porzioni_rimanenti > 0) {
                        shm_ptr->contorni[0].porzioni_rimanenti--;
                        shm_ptr->total_dishes_served[2]++;
                    }
                    servito = 1;
                } 
                else if (type == TYPE_COFFEE) {
                    shm_ptr->total_dishes_served[3]++;
                    servito = 1;
                }
                semop(semid, &mutex_unlock, 1);
                
                if (servito == 1) {
                    int percentuale = (type == TYPE_COFFEE) ? 80 : 50;
                    int delta = (tempo_medio * percentuale) / 100;
                    int actual_time = (tempo_medio - delta) + (rand() % (2 * delta + 1));
                    usleep(actual_time * (cfg.n_nano_secs / 1000));
                }

                if (station_sem != -1) semop(semid, &rel_station, 1);
                
                res.mtype = req.sender_pid;
                res.status = servito;
                msgsnd(msgid, &res, msg_size, 0);
                
                // GESTIONE PAUSA
                if (servito && pauses_taken < cfg.nof_pause && (rand() % 100 < 10)) {
                    semop(semid, &mutex_lock, 1);
                    if (shm_ptr->active_ops[type] > 1) {
                        shm_ptr->active_ops[type]--;
                        shm_ptr->daily_pauses++;
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
            } else {
                if (errno == ENOMSG) {
                    if (shm_ptr->day_ended) break;
                    usleep(10000); // Evita busy waiting
                } else break;
            }
        }
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->active_ops[type]--;
        semop(semid, &mutex_unlock, 1);

        struct sembuf end_day = {SEM_DAY_END, 1, 0};
        semop(semid, &end_day, 1);
    }
    
    shmdt(shm_ptr);
    return 0;
}