#include "../inc/common.h"
#include <time.h>

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.conf";

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

    // Acquisizione fisica della postazione cassa
    struct sembuf seat_lock = {SEM_CASSA, -1, SEM_UNDO};
    semop(semid, &seat_lock, 1);

    // Segnala di essere pronto alla barriera
    struct sembuf sync_signal = {SEM_SYNC, 1, 0};
    semop(semid, &sync_signal, 1);

    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    int tempo_medio = cfg.avg_srvc_cassa;

    int running = 1;

    while(running && msgrcv(msgid, &req, msg_size, TYPE_CASSA, 0) != -1) {
        
        // 1. Calcolo del tempo reale proporzionale (+-20% di variazione)
        int delta = (tempo_medio * 20) / 100;
        int actual_time = (tempo_medio - delta) + (rand() % (2 * delta + 1));
        long micro_secs_per_unit = cfg.n_nano_secs / 1000;
        usleep(actual_time * micro_secs_per_unit); 
        
        // 2. Aggiornamento Statistiche (Globali e Giornaliere) in Mutua Esclusione
        semop(semid, &mutex_lock, 1);
        
        shm_ptr->revenue += req.importo; 
        shm_ptr->daily_revenue += req.importo; // Punto 4
        
        shm_ptr->users_served++; 
        shm_ptr->daily_users_served++; // Punto 4
        
        semop(semid, &mutex_unlock, 1);

        // 3. Risposta al cliente (Scontrino emesso)
        res.mtype = req.sender_pid;
        res.status = 1; 
        
        if (msgsnd(msgid, &res, msg_size, 0) == -1) {
            running = 0; 
        }
    }
    
    shmdt(shm_ptr); // Detach dalla shared memory al termine[cite: 3]
    return 0;
}