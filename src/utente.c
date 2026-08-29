#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "../inc/common.h"
#include <time.h>
// Funzione di utilità per calcolare il delta tempo di attesa in secondi
double get_elapsed_time(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int main() {
    srand(getpid() ^ time(NULL));

    int msgid = msgget(MSG_KEY, 0666);
    int shmid = shmget(SHM_KEY, sizeof(shared_data_t), 0666);
    
    // Corretto per usare 7 semafori totali (NUM_SEMAPHORES)
    int semid = semget(SEM_KEY, NUM_SEMAPHORES, 0666); 
    if (msgid < 0 || shmid < 0 || semid < 0) exit(1);

    shared_data_t *shm_ptr = (shared_data_t*) shmat(shmid, NULL, 0);

    pid_t mypid = getpid();
    msg_t req, res;
    size_t msg_size = sizeof(msg_t) - sizeof(long);

    struct sembuf mutex_lock = {SEM_MUTEX, -1, SEM_UNDO};
    struct sembuf mutex_unlock = {SEM_MUTEX, 1, SEM_UNDO};

    // --- SEGNALA DI ESSERE PRONTO (Barriera di Sincronizzazione) ---
    struct sembuf sync_signal = {SEM_SYNC, 1, 0};
    semop(semid, &sync_signal, 1);

    // Incrementa utenti in attesa all'ingresso nella mensa
    semop(semid, &mutex_lock, 1);
    shm_ptr->users_waiting++;
    semop(semid, &mutex_unlock, 1);

    int sim_active = 1; 
    int totale_da_pagare = 0; 
    
    int choice = (rand() % 3) + 1; // 1=Primi, 2=Secondi, 3=Completo
    struct timespec start_wait, end_wait;

    // --- PRIMI ---
    if (choice == 1 || choice == 3) {
        int servito = 0;
        do {
            if (shm_ptr->num_primi > 0) {
                req.indice_piatto = rand() % shm_ptr->num_primi;
            }
            req.mtype = TYPE_PRIMI;
            req.sender_pid = mypid;
            
            clock_gettime(CLOCK_MONOTONIC, &start_wait);
            if (msgsnd(msgid, &req, msg_size, 0) == -1) { 
                sim_active = 0; 
            } 
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { 
                sim_active = 0; 
            } 
            else {
                clock_gettime(CLOCK_MONOTONIC, &end_wait);
                double elapsed = get_elapsed_time(start_wait, end_wait);
                
                // Salva le statistiche di attesa
                semop(semid, &mutex_lock, 1);
                shm_ptr->total_wait_time += elapsed;
                shm_ptr->wait_time_stazioni[TYPE_PRIMI] += elapsed;
                shm_ptr->wait_count_stazioni[TYPE_PRIMI]++;
                semop(semid, &mutex_unlock, 1);

                if (res.status == 1) { 
                    totale_da_pagare += shm_ptr->primi[req.indice_piatto].price;
                    servito = 1; 
                }
            }
        } while (servito == 0 && sim_active == 1);
    }

    // --- SECONDI ---
    if (sim_active == 1 && (choice == 2 || choice == 3)) {
        int servito = 0;
        do {
            if (shm_ptr->num_secondi > 0) {
                req.indice_piatto = rand() % shm_ptr->num_secondi;
            }
            req.mtype = TYPE_SECONDI;
            req.sender_pid = mypid;
            
            clock_gettime(CLOCK_MONOTONIC, &start_wait);
            if (msgsnd(msgid, &req, msg_size, 0) == -1) {
                sim_active = 0; 
            }
            else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) {
                sim_active = 0; 
            }
            else {
                clock_gettime(CLOCK_MONOTONIC, &end_wait);
                double elapsed = get_elapsed_time(start_wait, end_wait);
                
                semop(semid, &mutex_lock, 1);
                shm_ptr->total_wait_time += elapsed;
                shm_ptr->wait_time_stazioni[TYPE_SECONDI] += elapsed;
                shm_ptr->wait_count_stazioni[TYPE_SECONDI]++;
                semop(semid, &mutex_unlock, 1);

                if (res.status == 1) {
                    totale_da_pagare += shm_ptr->secondi[req.indice_piatto].price;
                    servito = 1;
                }
            }
        } while (servito == 0 && sim_active == 1);
    }

    // --- CAFFE' (50% probabilità) ---
    if (sim_active == 1 && (rand() % 2 == 0) && shm_ptr->num_caffe > 0) {
        req.mtype = TYPE_COFFEE;
        req.sender_pid = mypid;
        req.indice_piatto = rand() % shm_ptr->num_caffe; // Sceglie un caffè tra i 4 disponibili
        
        clock_gettime(CLOCK_MONOTONIC, &start_wait);
        if (msgsnd(msgid, &req, msg_size, 0) == -1) {
            sim_active = 0;
        } 
        else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) {
            sim_active = 0;
        } 
        else {
            clock_gettime(CLOCK_MONOTONIC, &end_wait);
            double elapsed = get_elapsed_time(start_wait, end_wait);
            
            semop(semid, &mutex_lock, 1);
            shm_ptr->total_wait_time += elapsed;
            shm_ptr->wait_time_stazioni[TYPE_COFFEE] += elapsed;
            shm_ptr->wait_count_stazioni[TYPE_COFFEE]++;
            semop(semid, &mutex_unlock, 1);

            if (res.status == 1) {
                // Bug corretto: accesso tramite array
                totale_da_pagare += shm_ptr->caffe[req.indice_piatto].price; 
            }
        }
    }

    // --- CASSA ---
    if (sim_active == 1) {
        req.mtype = TYPE_CASSA;
        req.sender_pid = mypid;
        req.importo = totale_da_pagare; 
        
        clock_gettime(CLOCK_MONOTONIC, &start_wait);
        if (msgsnd(msgid, &req, msg_size, 0) == -1) {
            sim_active = 0;
        } 
        else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) {
            sim_active = 0;
        }
        else {
            clock_gettime(CLOCK_MONOTONIC, &end_wait);
            double elapsed = get_elapsed_time(start_wait, end_wait);
            
            semop(semid, &mutex_lock, 1);
            shm_ptr->total_wait_time += elapsed;
            shm_ptr->wait_time_stazioni[TYPE_CASSA] += elapsed;
            shm_ptr->wait_count_stazioni[TYPE_CASSA]++;
            semop(semid, &mutex_unlock, 1);
        }
    }

    // --- TAVOLO ---
    if (sim_active == 1) {
        // Rimuove se stesso dalla coda
        semop(semid, &mutex_lock, 1);
        if (shm_ptr->users_waiting > 0) {
            shm_ptr->users_waiting--;
        }
        semop(semid, &mutex_unlock, 1);

        struct sembuf table_wait = {SEM_TAVOLI, -1, SEM_UNDO};
        struct sembuf table_signal = {SEM_TAVOLI, 1, SEM_UNDO};

        // Occupa il tavolo per mangiare
        if (semop(semid, &table_wait, 1) != -1) {
            // Mangia per un tempo (semplificato)
            int eat_time = 200000 + (rand() % 300000);
            usleep(eat_time);
            semop(semid, &table_signal, 1); // Libera il tavolo (SEM_UNDO garantisce rilascio anche su crash)
        }
    } 

    shmdt(shm_ptr);
    return 0;
}