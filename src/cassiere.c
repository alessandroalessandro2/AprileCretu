#include "../inc/common.h"

int main(int argc, char *argv[]) {
    const char *config_file = (argc > 1) ? argv[1] : "config.conf";
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
    
    struct sembuf acq_station = {SEM_CASSA, -1, SEM_UNDO};
    struct sembuf rel_station = {SEM_CASSA, 1, SEM_UNDO};

    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    int tempo_medio = cfg.avg_srvc_cassa;
    
    while(1) {
        struct sembuf start_day = {SEM_DAY_START, -1, 0};
        semop(semid, &start_day, 1);
        if (!shm_ptr->sim_running) break;

        while(1) {
            if (msgrcv(msgid, &req, msg_size, TYPE_CASSA, IPC_NOWAIT) != -1) {
                if (shm_ptr->day_ended) {
                    res.mtype = req.sender_pid;
                    res.status = 0;
                    msgsnd(msgid, &res, msg_size, 0);
                    continue;
                }

                semop(semid, &acq_station, 1);
                
                int delta = (tempo_medio * 20) / 100;
                int actual_time = (tempo_medio - delta) + (rand() % (2 * delta + 1));
                usleep(actual_time * (cfg.n_nano_secs / 1000)); 
                
                semop(semid, &mutex_lock, 1);
                shm_ptr->daily_revenue += req.importo;
                shm_ptr->total_revenue += req.importo;
                shm_ptr->daily_users_served++;
                shm_ptr->total_users_served++;
                semop(semid, &mutex_unlock, 1);
                
                semop(semid, &rel_station, 1);
                
                res.mtype = req.sender_pid;
                res.status = 1;
                msgsnd(msgid, &res, msg_size, 0);
            } else {
                if (errno == ENOMSG) {
                    if (shm_ptr->day_ended) break;
                    usleep(10000);
                } else break;
            }
        }
        struct sembuf end_day = {SEM_DAY_END, 1, 0};
        semop(semid, &end_day, 1);
    }
    
    shmdt(shm_ptr);
    return 0;
}