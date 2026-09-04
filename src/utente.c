#include "../inc/common.h"

int main() {
    srand(getpid() ^ time(NULL));
    int msgid = msgget(MSG_KEY, 0666);
    int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, NUM_SEMAPHORES, 0666);
    if (msgid < 0 || shmid < 0 || semid < 0) exit(1);
    
    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    pid_t mypid = getpid();
    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
    
    struct sembuf sync_signal = {SEM_SYNC, 1, 0};
    semop(semid, &sync_signal, 1);
    
    int local_day = 0;
    while (1) {
        // Aspetta l'avvio del giorno o il giorno successivo
        while (shm_ptr->current_day == local_day && shm_ptr->sim_running) {
            usleep(10000);
        }
        if (!shm_ptr->sim_running) break;
        local_day = shm_ptr->current_day;
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->users_waiting++;
        semop(semid, &mutex_unlock, 1);
        
        int sim_active = 1; 
        int totale_da_pagare = 0;
        int choice = (rand() % 3) + 1; 
        
        // --- PRIMI ---
        if (choice == 1 || choice == 3) {
            int servito = 0;
            int start_time = shm_ptr->sim_time;
            do {
                if (shm_ptr->num_primi > 0) req.indice_piatto = rand() % shm_ptr->num_primi;
                req.mtype = TYPE_PRIMI; req.sender_pid = mypid; req.sub_type = 0;
                
                if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; }
                else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; }
                else if (res.status == 1) { totale_da_pagare += shm_ptr->price_primi; servito = 1; } 
                else { if (shm_ptr->portions_primi <= 0) break; }
            } while (servito == 0 && sim_active == 1);
            
            if (sim_active) {
                int wait_time = shm_ptr->sim_time - start_time;
                if(wait_time < 0) wait_time = 0;
                semop(semid, &mutex_lock, 1);
                shm_ptr->total_wait_time += wait_time; shm_ptr->wait_time_stazioni[TYPE_PRIMI] += wait_time; shm_ptr->wait_count_stazioni[TYPE_PRIMI]++;
                semop(semid, &mutex_unlock, 1);
            }
        }
        
        // --- SECONDI E CONTORNI ---
        if (sim_active == 1 && (choice == 2 || choice == 3)) {
            int servito_sec = 0;
            int start_time = shm_ptr->sim_time;
            do {
                if (shm_ptr->num_secondi > 0) req.indice_piatto = rand() % shm_ptr->num_secondi;
                req.mtype = TYPE_SECONDI; req.sender_pid = mypid; req.sub_type = 0; // Secondo
                
                if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; }
                else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; }
                else if (res.status == 1) { totale_da_pagare += shm_ptr->price_secondi; servito_sec = 1; }
                else { if (shm_ptr->portions_secondi <= 0) break; }
            } while (servito_sec == 0 && sim_active == 1);
            
            int servito_cont = 0;
            if (sim_active == 1) {
                do {
                    if (shm_ptr->num_contorni > 0) req.indice_piatto = rand() % shm_ptr->num_contorni;
                    req.mtype = TYPE_SECONDI; req.sender_pid = mypid; req.sub_type = 1; // Contorno
                    
                    if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; }
                    else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; }
                    else if (res.status == 1) { servito_cont = 1; } // Contorno è incluso nel prezzo del secondo
                    else { if (shm_ptr->portions_contorni <= 0) break; }
                } while (servito_cont == 0 && sim_active == 1);
            }
            
            if (sim_active) {
                int wait_time = shm_ptr->sim_time - start_time;
                if(wait_time < 0) wait_time = 0;
                semop(semid, &mutex_lock, 1);
                shm_ptr->total_wait_time += wait_time; shm_ptr->wait_time_stazioni[TYPE_SECONDI] += wait_time; shm_ptr->wait_count_stazioni[TYPE_SECONDI]++;
                semop(semid, &mutex_unlock, 1);
            }
        }
        
        // --- DOLCE (50% probabilità) ---
        if (sim_active == 1 && (rand() % 2 == 0)) {
            int start_time = shm_ptr->sim_time;
            if (shm_ptr->num_dolce > 0) req.indice_piatto = rand() % shm_ptr->num_dolce;
            req.mtype = TYPE_COFFEE; req.sender_pid = mypid; req.sub_type = 1; // Dolce
            
            if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; }
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; }
            else if (res.status == 1) { totale_da_pagare += shm_ptr->price_coffee; } // Valutato a pari prezzo config
            if (sim_active) {
                int wait_time = shm_ptr->sim_time - start_time;
                if(wait_time < 0) wait_time = 0;
                semop(semid, &mutex_lock, 1);
                shm_ptr->total_wait_time += wait_time; shm_ptr->wait_time_stazioni[TYPE_COFFEE] += wait_time; shm_ptr->wait_count_stazioni[TYPE_COFFEE]++;
                semop(semid, &mutex_unlock, 1);
            }
        }
        
        // --- CAFFE' (50% probabilità) ---
        if (sim_active == 1 && (rand() % 2 == 0)) {
            int start_time = shm_ptr->sim_time;
            if (shm_ptr->num_caffe > 0) req.indice_piatto = rand() % shm_ptr->num_caffe;
            req.mtype = TYPE_COFFEE; req.sender_pid = mypid; req.sub_type = 0; // Caffè
            
            if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; }
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; }
            else if (res.status == 1) { totale_da_pagare += shm_ptr->price_coffee; }
            if (sim_active) {
                int wait_time = shm_ptr->sim_time - start_time;
                if(wait_time < 0) wait_time = 0;
                semop(semid, &mutex_lock, 1);
                shm_ptr->total_wait_time += wait_time; shm_ptr->wait_time_stazioni[TYPE_COFFEE] += wait_time; shm_ptr->wait_count_stazioni[TYPE_COFFEE]++;
                semop(semid, &mutex_unlock, 1);
            }
        }
        
        // --- CASSA ---
        if (sim_active == 1) {
            int start_time = shm_ptr->sim_time;
            req.mtype = TYPE_CASSA; req.sender_pid = mypid; req.importo = totale_da_pagare;
            
            if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; }
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; }
            
            if (sim_active) {
                int wait_time = shm_ptr->sim_time - start_time;
                if(wait_time < 0) wait_time = 0;
                semop(semid, &mutex_lock, 1);
                shm_ptr->total_wait_time += wait_time; shm_ptr->wait_time_stazioni[TYPE_CASSA] += wait_time; shm_ptr->wait_count_stazioni[TYPE_CASSA]++;
                semop(semid, &mutex_unlock, 1);
            }
        }
        
        // --- TAVOLO ---
        if (sim_active == 1) {
            semop(semid, &mutex_lock, 1);
            shm_ptr->users_waiting--;
            semop(semid, &mutex_unlock, 1);
            
            struct sembuf table_wait = {SEM_TAVOLI, -1, SEM_UNDO};
            struct sembuf table_signal = {SEM_TAVOLI, 1, SEM_UNDO};
            
            if (semop(semid, &table_wait, 1) != -1) {
                int eat_time = 100000 + (totale_da_pagare * 10000); // Tempo mangiata proporzionale a quanti piatti e' costato
                usleep(eat_time);
                semop(semid, &table_signal, 1);
            }
        }
        
        semop(semid, &mutex_lock, 1);
        shm_ptr->users_done_today++;
        semop(semid, &mutex_unlock, 1);
    }
    
    shmdt(shm_ptr);
    return 0;
}