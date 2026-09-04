#include "../inc/common.h"

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.conf";
    config_t cfg; memset(&cfg, 0, sizeof(config_t)); load_config(config_file, &cfg);
    srand(getpid() ^ time(NULL));
    int msgid = msgget(MSG_KEY, 0666); int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, 9, 0666);
    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
    struct sembuf acq_station = {SEM_CASSA, -1, SEM_UNDO};
    struct sembuf rel_station = {SEM_CASSA, 1, SEM_UNDO};
    msg_t req, res; size_t msg_size = sizeof(msg_t) - sizeof(long);
    
    while(1) {
        struct sembuf signal_ready = {SEM_READY, 1, 0}; semop(semid, &signal_ready, 1);
        struct sembuf wait_start = {SEM_DAY_START, -1, 0}; semop(semid, &wait_start, 1);
        if (!shm_ptr->sim_running) break;

        semop(semid, &acq_station, 1);
        semop(semid, &mutex_lock, 1);
        shm_ptr->active_ops[TYPE_CASSA]++; 
        shm_ptr->daily_active_ops++; 
        shm_ptr->cassiere_has_worked = 1; 
        semop(semid, &mutex_unlock, 1);

        while(1) {
            if (msgrcv(msgid, &req, msg_size, TYPE_CASSA, IPC_NOWAIT) != -1) {
                semop(semid, &mutex_lock, 1);
                shm_ptr->queue_lengths[TYPE_CASSA]--; // Scala la coda subito
                int queue_wait = shm_ptr->sim_time - req.enqueue_time;
                if (queue_wait < 0) queue_wait = 0;
                semop(semid, &mutex_unlock, 1);

                if (shm_ptr->day_ended) { res.mtype = req.sender_pid; res.status = STATUS_CLOSED; msgsnd(msgid, &res, msg_size, 0); continue; }
                
                long base_us = (long)cfg.avg_srvc_cassa * (cfg.n_nano_secs / 1000);
                long delta_us = (base_us * 20) / 100;
                long actual_us = (base_us - delta_us) + (rand() % (2 * delta_us + 1));
                usleep(actual_us); 
                
                semop(semid, &mutex_lock, 1);
                shm_ptr->daily_revenue += req.importo; shm_ptr->total_revenue += req.importo;
                shm_ptr->daily_users_served++; shm_ptr->total_users_served++;
                semop(semid, &mutex_unlock, 1);
                
                res.mtype = req.sender_pid; res.status = STATUS_SERVED; res.queue_wait = queue_wait; 
                msgsnd(msgid, &res, msg_size, 0);
            } else {
                if (errno == ENOMSG) { if (shm_ptr->day_ended) break; usleep(10000); } else break;
            }
        }
        semop(semid, &mutex_lock, 1); shm_ptr->active_ops[TYPE_CASSA]--; semop(semid, &mutex_unlock, 1);
        semop(semid, &rel_station, 1);
        struct sembuf end_day = {SEM_DAY_END, 1, 0}; semop(semid, &end_day, 1);
    }
    shmdt(shm_ptr); return 0;
}