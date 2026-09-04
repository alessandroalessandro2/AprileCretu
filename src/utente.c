#include "../inc/common.h"

void wait_and_track(int msgid, msg_t *req, msg_t *res, size_t msg_size, int type, pid_t mypid, shared_data_t *shm_ptr, int semid) {
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
    
    semop(semid, &mutex_lock, 1);
    int start_time = shm_ptr->sim_time;
    shm_ptr->queue_lengths[type]++;
    semop(semid, &mutex_unlock, 1);
    
    msgsnd(msgid, req, msg_size, 0);
    msgrcv(msgid, res, msg_size, mypid, 0);
    
    semop(semid, &mutex_lock, 1);
    int wait_time = shm_ptr->sim_time - start_time;
    if(wait_time > 0) {
        shm_ptr->wait_time_stazioni[type] += wait_time;
        shm_ptr->wait_count_stazioni[type]++;
    }
    shm_ptr->queue_lengths[type]--;
    semop(semid, &mutex_unlock, 1);
}

int main() {
    srand(getpid() ^ time(NULL));
    int msgid = msgget(MSG_KEY, 0666);
    int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, 8, 0666);
    if (msgid < 0 || shmid < 0 || semid < 0) exit(1);
    
    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    pid_t mypid = getpid();
    
    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
    
    while(1) {
        struct sembuf start_day = {SEM_DAY_START, -1, 0};
        semop(semid, &start_day, 1);
        if (!shm_ptr->sim_running) break;

        semop(semid, &mutex_lock, 1);
        shm_ptr->users_waiting++;
        int code_primi = shm_ptr->queue_lengths[TYPE_PRIMI];
        int code_secondi = shm_ptr->queue_lengths[TYPE_SECONDI];
        semop(semid, &mutex_unlock, 1);
        
        int sim_active = 1;
        int totale_da_pagare = 0;
        int piatti_acquistati = 0;

        int choice = 3; 
        if (code_primi > code_secondi + 2) choice = 2; 
        else if (code_secondi > code_primi + 2) choice = 1; 
        
        // --- PRIMI ---
        if (choice == 1 || choice == 3) {
            if (shm_ptr->day_ended) sim_active = 0;
            else {
                if (shm_ptr->num_primi > 0) req.indice_piatto = rand() % shm_ptr->num_primi;
                req.mtype = TYPE_PRIMI;
                req.sender_pid = mypid;
                
                wait_and_track(msgid, &req, &res, msg_size, TYPE_PRIMI, mypid, shm_ptr, semid);
                if (res.status == 1) { totale_da_pagare += shm_ptr->primi[req.indice_piatto].price; piatti_acquistati++; }
                else if (res.status == 0) sim_active = 0; // Se status 0, la mensa ha chiuso a metà attesa
            }
        }
        
        // --- SECONDI (e CONTORNI) ---
        if (sim_active && (choice == 2 || choice == 3)) {
            if (shm_ptr->day_ended) sim_active = 0;
            else {
                if (shm_ptr->num_secondi > 0) req.indice_piatto = rand() % shm_ptr->num_secondi;
                req.mtype = TYPE_SECONDI;
                req.sender_pid = mypid;
                
                wait_and_track(msgid, &req, &res, msg_size, TYPE_SECONDI, mypid, shm_ptr, semid);
                if (res.status == 1) { 
                    totale_da_pagare += shm_ptr->secondi[req.indice_piatto].price; 
                    if(shm_ptr->num_contorni > 0) totale_da_pagare += shm_ptr->contorni[0].price; // Prezzo contorno
                    piatti_acquistati++; 
                } else if (res.status == 0) sim_active = 0;
            }
        }
        
        // Se a questo punto non ha acquistato nulla (porzioni finite o chiusura) ABBANDONA
        if (sim_active && totale_da_pagare == 0) sim_active = 0; 
        
        // --- CAFFE' ---
        if (sim_active && (rand() % 2 == 0)) {
            if (shm_ptr->day_ended) sim_active = 0;
            else {
                req.mtype = TYPE_COFFEE;
                req.sender_pid = mypid;
                
                wait_and_track(msgid, &req, &res, msg_size, TYPE_COFFEE, mypid, shm_ptr, semid);
                if (res.status == 1) {
                    int c_idx = rand() % shm_ptr->num_caffe;
                    totale_da_pagare += shm_ptr->caffe[c_idx].price; 
                    piatti_acquistati++;
                } else if (res.status == 0) sim_active = 0;
            }
        }
        
        // --- DOLCE E CASSA ---
        if (sim_active) {
            if (shm_ptr->day_ended) sim_active = 0;
            else {
                // Il dolce viene pagato prima del cassiere
                if (rand() % 100 < 30) {
                    totale_da_pagare += shm_ptr->dolce.price;
                    piatti_acquistati++;
                }

                req.mtype = TYPE_CASSA;
                req.sender_pid = mypid;
                req.importo = totale_da_pagare;
                
                wait_and_track(msgid, &req, &res, msg_size, TYPE_CASSA, mypid, shm_ptr, semid);
                if (res.status == 0) sim_active = 0;
            }
        }
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->users_waiting--;
        if (!sim_active) {
            shm_ptr->daily_users_dropped++;
            shm_ptr->total_users_dropped++;
        }
        semop(semid, &mutex_unlock, 1);

        // --- TAVOLO ---
        if (sim_active) {
            struct sembuf table_wait = {SEM_TAVOLI, -1, SEM_UNDO};
            struct sembuf table_signal = {SEM_TAVOLI, 1, SEM_UNDO};
            if (semop(semid, &table_wait, 1) != -1) {
                int eat_time = 100000 + (piatti_acquistati * 150000); 
                usleep(eat_time);
                semop(semid, &table_signal, 1);
            }
        }

        struct sembuf end_day = {SEM_DAY_END, 1, 0};
        semop(semid, &end_day, 1);
    }
    
    shmdt(shm_ptr);
    return 0;
}