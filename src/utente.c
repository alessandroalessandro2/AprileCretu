#include "../inc/common.h"

void wait_and_track(int msgid, msg_t *req, msg_t *res, size_t msg_size, int type, pid_t mypid, shared_data_t *shm_ptr, int semid) {
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
    
    semop(semid, &mutex_lock, 1);
    req->enqueue_time = shm_ptr->sim_time;
    shm_ptr->queue_lengths[type]++;
    semop(semid, &mutex_unlock, 1);
    
    msgsnd(msgid, req, msg_size, 0);
    msgrcv(msgid, res, msg_size, mypid, 0);
    
    semop(semid, &mutex_lock, 1);
    shm_ptr->queue_lengths[type]--;
    if (res->status != STATUS_CLOSED) {
        shm_ptr->wait_time_stazioni[type] += res->queue_wait;
        shm_ptr->wait_count_stazioni[type]++;
        shm_ptr->daily_wait_time_stazioni[type] += res->queue_wait;
        shm_ptr->daily_wait_count_stazioni[type]++;
    }
    semop(semid, &mutex_unlock, 1);
}

int try_food(int msgid, msg_t *req, msg_t *res, size_t msg_size, int type, int num_piatti, pid_t mypid, shared_data_t *shm_ptr, int semid, int *costo) {
    if (num_piatti == 0) return 0;
    int start_idx = rand() % num_piatti;
    for(int attempt = 0; attempt < num_piatti; attempt++) {
        req->mtype = type; req->sender_pid = mypid; req->is_dolce = 0;
        req->indice_piatto = (start_idx + attempt) % num_piatti;
        
        wait_and_track(msgid, req, res, msg_size, type, mypid, shm_ptr, semid);
        
        if (res->status == STATUS_SERVED) {
            if (type == TYPE_PRIMI) *costo += shm_ptr->primi[req->indice_piatto].price;
            else if (type == TYPE_SECONDI) *costo += shm_ptr->secondi[req->indice_piatto].price;
            return 1;
        } else if (res->status == STATUS_CLOSED) {
            return 0; 
        }
    }
    return 0; 
}

int main() {
    srand(getpid() ^ time(NULL));
    int msgid = msgget(MSG_KEY, 0666); int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, 9, 0666);
    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0); pid_t mypid = getpid();
    
    msg_t req, res; size_t msg_size = sizeof(msg_t) - sizeof(long);
    
    while(1) {
        struct sembuf signal_ready = {SEM_READY, 1, 0}; semop(semid, &signal_ready, 1);
        struct sembuf wait_start = {SEM_DAY_START, -1, 0}; semop(semid, &wait_start, 1);
        if (!shm_ptr->sim_running) break;

        struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
        struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
        semop(semid, &mutex_lock, 1); shm_ptr->users_in_mensa++; semop(semid, &mutex_unlock, 1);
        
        int sim_active = 1, totale_da_pagare = 0, piatti_acquistati = 0;
        
        int order[2];
        if (shm_ptr->queue_lengths[TYPE_PRIMI] > shm_ptr->queue_lengths[TYPE_SECONDI] + 2) {
            order[0] = TYPE_SECONDI; order[1] = TYPE_PRIMI;
        } else {
            order[0] = TYPE_PRIMI; order[1] = TYPE_SECONDI;
        }

        // --- 1. PRIMI e SECONDI ---
        for (int i = 0; i < 2; i++) {
            if (!sim_active || shm_ptr->day_ended) break;
            
            int curr_type = order[i];
            if (curr_type == TYPE_PRIMI) {
                if(try_food(msgid, &req, &res, msg_size, TYPE_PRIMI, shm_ptr->num_primi, mypid, shm_ptr, semid, &totale_da_pagare)) 
                    piatti_acquistati++;
            } else if (curr_type == TYPE_SECONDI) {
                if(try_food(msgid, &req, &res, msg_size, TYPE_SECONDI, shm_ptr->num_secondi, mypid, shm_ptr, semid, &totale_da_pagare)) 
                    piatti_acquistati++;
            }
        }
        
        if (sim_active && totale_da_pagare == 0) sim_active = 0; 
        
        // --- 2. DOLCE & CAFFE' ---
        if (sim_active && !shm_ptr->day_ended && (rand() % 2 == 0)) {
            req.mtype = TYPE_COFFEE; req.sender_pid = mypid; 
            req.is_dolce = (rand() % 2 == 0) ? 1 : 0; 
            req.indice_piatto = (req.is_dolce) ? 0 : rand() % shm_ptr->num_caffe; 
            
            wait_and_track(msgid, &req, &res, msg_size, TYPE_COFFEE, mypid, shm_ptr, semid);
            if (res.status == STATUS_SERVED) {
                totale_da_pagare += (req.is_dolce) ? shm_ptr->dolce.price : shm_ptr->caffe[req.indice_piatto].price;
                piatti_acquistati++;
            } else if (res.status == STATUS_CLOSED) sim_active = 0;
        }
        
        // --- CASSA ---
        if (sim_active && !shm_ptr->day_ended) {
            req.mtype = TYPE_CASSA; req.sender_pid = mypid; req.importo = totale_da_pagare;
            wait_and_track(msgid, &req, &res, msg_size, TYPE_CASSA, mypid, shm_ptr, semid);
            if (res.status == STATUS_CLOSED) sim_active = 0;
        }
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->users_in_mensa--;
        if (!sim_active) { shm_ptr->daily_users_dropped++; shm_ptr->total_users_dropped++; }
        semop(semid, &mutex_unlock, 1);

        // --- TAVOLO ---
        if (sim_active) {
            struct sembuf table_wait = {SEM_TAVOLI, -1, SEM_UNDO}; struct sembuf table_signal = {SEM_TAVOLI, 1, SEM_UNDO};
            if (semop(semid, &table_wait, 1) != -1) {
                int eat_time = 100000 + (piatti_acquistati * 150000); usleep(eat_time);
                semop(semid, &table_signal, 1);
            }
        }
        struct sembuf end_day = {SEM_DAY_END, 1, 0}; semop(semid, &end_day, 1);
    }
    
    shmdt(shm_ptr); return 0;
}