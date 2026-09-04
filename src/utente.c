#include "../inc/common.h"

int main() {
    srand(getpid() ^ time(NULL));
    
    int msgid = msgget(MSG_KEY, 0666);
    int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, 7, 0666);
    if (msgid < 0 || shmid < 0 || semid < 0) exit(1);
    
    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);
    pid_t mypid = getpid();
    
    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);
    
    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};
    
    struct sembuf sync_signal = {SEM_SYNC, 1, 0};
    semop(semid, &sync_signal, 1);
    
    semop(semid, &mutex_lock, 1);
    shm_ptr->users_waiting++;
    // Legge le code per decidere dove andare
    int code_primi = shm_ptr->queue_lengths[TYPE_PRIMI];
    int code_secondi = shm_ptr->queue_lengths[TYPE_SECONDI];
    semop(semid, &mutex_unlock, 1);
    
    int sim_active = 1;
    int totale_da_pagare = 0;
    int piatti_acquistati = 0;

    // CORREZIONE 1: Scelta in base alle code
    int choice = 3; // Completo di base
    if (code_primi > code_secondi + 2) choice = 2; // Va solo ai secondi
    else if (code_secondi > code_primi + 2) choice = 1; // Va solo ai primi
    
    // --- PRIMI ---
    if (choice == 1 || choice == 3) {
        int servito = 0;
        do {
            if (shm_ptr->num_primi > 0) req.indice_piatto = rand() % shm_ptr->num_primi;
            req.mtype = TYPE_PRIMI;
            req.sender_pid = mypid;
            
            // Entra in coda
            semop(semid, &mutex_lock, 1); shm_ptr->queue_lengths[TYPE_PRIMI]++; semop(semid, &mutex_unlock, 1);
            
            if (msgsnd(msgid, &req, msg_size, 0) == -1) sim_active = 0;
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) sim_active = 0;
            else if (res.status == 1) {
                totale_da_pagare += shm_ptr->primi[req.indice_piatto].price;
                piatti_acquistati++;
                servito = 1;
            }
            
            // Esce dalla coda
            semop(semid, &mutex_lock, 1); shm_ptr->queue_lengths[TYPE_PRIMI]--; semop(semid, &mutex_unlock, 1);
        } while (servito == 0 && sim_active == 1);
    }
    
    // --- SECONDI ---
    if (sim_active == 1 && (choice == 2 || choice == 3)) {
        int servito = 0;
        do {
            if (shm_ptr->num_secondi > 0) req.indice_piatto = rand() % shm_ptr->num_secondi;
            req.mtype = TYPE_SECONDI;
            req.sender_pid = mypid;
            
            semop(semid, &mutex_lock, 1); shm_ptr->queue_lengths[TYPE_SECONDI]++; semop(semid, &mutex_unlock, 1);
            
            if (msgsnd(msgid, &req, msg_size, 0) == -1) sim_active = 0;
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) sim_active = 0;
            else if (res.status == 1) {
                totale_da_pagare += shm_ptr->secondi[req.indice_piatto].price;
                piatti_acquistati++;
                servito = 1;
            }
            
            semop(semid, &mutex_lock, 1); shm_ptr->queue_lengths[TYPE_SECONDI]--; semop(semid, &mutex_unlock, 1);
        } while (servito == 0 && sim_active == 1);
    }
    
    // CORREZIONE 10: Se non ha preso nulla, desiste ed esce
    if (sim_active == 1 && totale_da_pagare == 0) {
        sim_active = 0; 
        semop(semid, &mutex_lock, 1);
        shm_ptr->users_dropped++;
        shm_ptr->users_waiting--;
        semop(semid, &mutex_unlock, 1);
    }
    
    // --- CAFFE' (50% probabilità) ---
    if (sim_active == 1 && (rand() % 2 == 0)) {
        req.mtype = TYPE_COFFEE;
        req.sender_pid = mypid;
        
        semop(semid, &mutex_lock, 1); shm_ptr->queue_lengths[TYPE_COFFEE]++; semop(semid, &mutex_unlock, 1);
        
        if (msgsnd(msgid, &req, msg_size, 0) == -1) sim_active = 0;
        else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) sim_active = 0;
        else if (res.status == 1) {
            totale_da_pagare += shm_ptr->caffe[0].price; // Prende un caffè a caso
            piatti_acquistati++;
        }
        
        semop(semid, &mutex_lock, 1); shm_ptr->queue_lengths[TYPE_COFFEE]--; semop(semid, &mutex_unlock, 1);
    }
    
    // --- CASSA ---
    if (sim_active == 1) {
        req.mtype = TYPE_CASSA;
        req.sender_pid = mypid;
        req.importo = totale_da_pagare;
        
        semop(semid, &mutex_lock, 1); shm_ptr->queue_lengths[TYPE_CASSA]++; semop(semid, &mutex_unlock, 1);
        
        if (msgsnd(msgid, &req, msg_size, 0) == -1) sim_active = 0;
        else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) sim_active = 0;
        
        semop(semid, &mutex_lock, 1); shm_ptr->queue_lengths[TYPE_CASSA]--; semop(semid, &mutex_unlock, 1);
    }
    
    // --- TAVOLO ---
    if (sim_active == 1) {
        semop(semid, &mutex_lock, 1);
        shm_ptr->users_waiting--;
        semop(semid, &mutex_unlock, 1);
        
        // CORREZIONE 3: Dolce illimitato prima di sedersi al tavolo
        if (rand() % 100 < 30) {
            totale_da_pagare += shm_ptr->dolce.price;
            piatti_acquistati++;
        }

        struct sembuf table_wait = {SEM_TAVOLI, -1, SEM_UNDO};
        struct sembuf table_signal = {SEM_TAVOLI, 1, SEM_UNDO};
        
        if (semop(semid, &table_wait, 1) != -1) {
            // CORREZIONE 4: Tempo proporzionale al num. piatti, non al prezzo
            int eat_time = 100000 + (piatti_acquistati * 150000); 
            usleep(eat_time);
            semop(semid, &table_signal, 1);
        }
    }
    
    shmdt(shm_ptr);
    return 0;
}