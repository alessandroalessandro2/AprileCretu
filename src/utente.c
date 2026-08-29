#include "../inc/common.h"

int main() {
    srand(getpid() ^ time(NULL));

    int msgid = msgget(MSG_KEY, 0666);
    int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    int semid = semget(SEM_KEY, 6, 0666); // Ricorda: 6 semafori
    if (msgid < 0 || shmid < 0 || semid < 0) exit(1);

    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);

    pid_t mypid = getpid();
    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);

    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};

    // --- SEGNALA DI ESSERE PRONTO ---
    struct sembuf sync_signal = {SEM_SYNC, 1, 0};
    semop(semid, &sync_signal, 1);

    // Incrementa utenti in attesa (entra nella mensa)
    semop(semid, &mutex_lock, 1);
    shm_ptr->users_waiting++;
    semop(semid, &mutex_unlock, 1);

    int sim_active = 1; 
    int totale_da_pagare = 0; 
    
    int choice = (rand() % 3) + 1; // 1=Primi, 2=Secondi, 3=Completo

    // --- PRIMI ---
    if (choice == 1 || choice == 3) {
        int servito = 0;
        do {
            if (shm_ptr->num_primi > 0) {
                req.indice_piatto = rand() % shm_ptr->num_primi;
            }
            req.mtype = TYPE_PRIMI;
            req.sender_pid = mypid;
            
            if (msgsnd(msgid, &req, msg_size, 0) == -1) { 
                sim_active = 0; 
            } 
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { 
                sim_active = 0; 
            } 
            else if (res.status == 1) { 
                totale_da_pagare += shm_ptr->primi[req.indice_piatto].price;
                servito = 1; 
            }
        } while (servito == 0 && sim_active == 1);
    }

    
    if (sim_active == 1 && (choice == 2 || choice == 3)) {
        int servito = 0;
        do {
            if (shm_ptr->num_secondi > 0) {
                req.indice_piatto = rand() % shm_ptr->num_secondi;
            }
            req.mtype = TYPE_SECONDI;
            req.sender_pid = mypid;
            
            if (msgsnd(msgid, &req, msg_size, 0) == -1) {
                sim_active = 0; 
            }
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) {
                sim_active = 0; 
            }
            else if (res.status == 1) {
                totale_da_pagare += shm_ptr->secondi[req.indice_piatto].price;
                servito = 1;
            }
        } while (servito == 0 && sim_active == 1);
    }

    // --- CAFFE' (50% probabilità) ---
    if (sim_active == 1 && (rand() % 2 == 0)) {
        req.mtype = TYPE_COFFEE;
        req.sender_pid = mypid;
        
        if (msgsnd(msgid, &req, msg_size, 0) == -1) {
            sim_active = 0;
        } 
        else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) {
            sim_active = 0;
        } 
        else if (res.status == 1) {
            totale_da_pagare += shm_ptr->caffe.price;
        }
    }

    // --- CASSA ---
    if (sim_active == 1) {
        req.mtype = TYPE_CASSA;
        req.sender_pid = mypid;
        req.importo = totale_da_pagare; 
        
        if (msgsnd(msgid, &req, msg_size, 0) == -1) {
            sim_active = 0;
        } 
        else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) {
            sim_active = 0;
        }
    }

    // --- TAVOLO ---
    if (sim_active == 1) {
        semop(semid, &mutex_lock, 1);
        shm_ptr->users_waiting--;
        semop(semid, &mutex_unlock, 1);

        struct sembuf table_wait = {SEM_TAVOLI, -1, SEM_UNDO};
        struct sembuf table_signal = {SEM_TAVOLI, 1, SEM_UNDO};

        // Occupa tavolo e mangia
        if (semop(semid, &table_wait, 1) != -1) {
            int eat_time = 200000 + (rand() % 300000);
            usleep(eat_time);
            semop(semid, &table_signal, 1);
        }
    } 

    shmdt(shm_ptr);
    return 0;
}