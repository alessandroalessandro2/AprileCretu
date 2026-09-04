// Forza la visibilità delle macro POSIX per gli editor testardi
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "../inc/common.h"
#include <time.h>

double get_elapsed_time(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

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
    
    // Segnala di essere pronto alla barriera di inizio giornata
    semop(semid, &sync_signal, 1);

    // Entra in mensa
    semop(semid, &mutex_lock, 1);
    shm_ptr->users_waiting++;
    semop(semid, &mutex_unlock, 1);

    int sim_active = 1; 
    int totale_da_pagare = 0; 
    int piatti_acquistati = 0; // Contatore per il pasto al tavolo (Punto 3)
    
    int choice = (rand() % 3) + 1; // 1=Primi, 2=Secondi, 3=Completo
    struct timespec start_wait, end_wait;

    // --- PRIMI ---
    if (choice == 1 || choice == 3) {
        int servito = 0;
        do {
            // Controllo disponibilità preventiva per evitare infinite loops (Punto 3)
            semop(semid, &mutex_lock, 1);
            int tot_rimanenti = 0;
            for(int i = 0; i < shm_ptr->num_primi; i++) {
                tot_rimanenti += shm_ptr->primi[i].porzioni_rimanenti;
            }
            semop(semid, &mutex_unlock, 1);
            
            // Se tutti i primi sono finiti, rinuncia a questa portata
            if (tot_rimanenti == 0) break; 

            req.indice_piatto = rand() % shm_ptr->num_primi;
            req.mtype = TYPE_PRIMI;
            req.sender_pid = mypid;
            
            clock_gettime(CLOCK_MONOTONIC, &start_wait);
            if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; break; } 
            if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; break; } 
            
            clock_gettime(CLOCK_MONOTONIC, &end_wait);
            double elapsed = get_elapsed_time(start_wait, end_wait);
            
            semop(semid, &mutex_lock, 1);
            shm_ptr->total_wait_time += elapsed;
            shm_ptr->daily_total_wait_time += elapsed; // Punto 4
            shm_ptr->wait_time_stazioni[TYPE_PRIMI] += elapsed;
            shm_ptr->wait_count_stazioni[TYPE_PRIMI]++;
            shm_ptr->daily_wait_count++; // Punto 4
            semop(semid, &mutex_unlock, 1);

            if (res.status == 1) { 
                totale_da_pagare += shm_ptr->primi[req.indice_piatto].price;
                servito = 1; 
                piatti_acquistati++;
            }
        } while (servito == 0 && sim_active == 1);
    }

    // --- SECONDI ---
    if (sim_active == 1 && (choice == 2 || choice == 3)) {
        int servito = 0;
        do {
            // Controllo disponibilità preventiva
            semop(semid, &mutex_lock, 1);
            int tot_rimanenti = 0;
            for(int i = 0; i < shm_ptr->num_secondi; i++) {
                tot_rimanenti += shm_ptr->secondi[i].porzioni_rimanenti;
            }
            semop(semid, &mutex_unlock, 1);
            
            if (tot_rimanenti == 0) break; 

            req.indice_piatto = rand() % shm_ptr->num_secondi;
            req.mtype = TYPE_SECONDI;
            req.sender_pid = mypid;
            
            clock_gettime(CLOCK_MONOTONIC, &start_wait);
            if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; break; }
            if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; break; }
            
            clock_gettime(CLOCK_MONOTONIC, &end_wait);
            double elapsed = get_elapsed_time(start_wait, end_wait);
            
            semop(semid, &mutex_lock, 1);
            shm_ptr->total_wait_time += elapsed;
            shm_ptr->daily_total_wait_time += elapsed; // Punto 4
            shm_ptr->wait_time_stazioni[TYPE_SECONDI] += elapsed;
            shm_ptr->wait_count_stazioni[TYPE_SECONDI]++;
            shm_ptr->daily_wait_count++; // Punto 4
            semop(semid, &mutex_unlock, 1);

            if (res.status == 1) {
                totale_da_pagare += shm_ptr->secondi[req.indice_piatto].price;
                servito = 1;
                piatti_acquistati++;
            }
        } while (servito == 0 && sim_active == 1);
    }

    // --- CONTROLLO RINUNCIA (Sezione 5.5 del Bando) ---
    // Se non è riuscito a prendere né un primo né un secondo (e la simulazione è attiva)
    if (sim_active == 1 && piatti_acquistati == 0) {
        semop(semid, &mutex_lock, 1);
        if (shm_ptr->users_waiting > 0) shm_ptr->users_waiting--;
        shm_ptr->users_dropped++; 
        shm_ptr->daily_users_dropped++; // Punto 4
        semop(semid, &mutex_unlock, 1);
        shmdt(shm_ptr); 
        return 0; // Termina 
    }

    // --- CAFFE' (50% probabilità) ---
    if (sim_active == 1 && (rand() % 2 == 0) && shm_ptr->num_caffe > 0) {
        req.mtype = TYPE_COFFEE;
        req.sender_pid = mypid;
        req.indice_piatto = rand() % shm_ptr->num_caffe; 
        
        clock_gettime(CLOCK_MONOTONIC, &start_wait);
        if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; } 
        else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; } 
        else {
            clock_gettime(CLOCK_MONOTONIC, &end_wait);
            double elapsed = get_elapsed_time(start_wait, end_wait);
            
            semop(semid, &mutex_lock, 1);
            shm_ptr->total_wait_time += elapsed;
            shm_ptr->daily_total_wait_time += elapsed; // Punto 4
            shm_ptr->wait_time_stazioni[TYPE_COFFEE] += elapsed;
            shm_ptr->wait_count_stazioni[TYPE_COFFEE]++;
            shm_ptr->daily_wait_count++; // Punto 4
            semop(semid, &mutex_unlock, 1);

            if (res.status == 1) {
                totale_da_pagare += shm_ptr->caffe[req.indice_piatto].price; 
                piatti_acquistati++; 
            }
        }
    }

    // --- CASSA ---
    if (sim_active == 1) {
        req.mtype = TYPE_CASSA;
        req.sender_pid = mypid;
        req.importo = totale_da_pagare; 
        
        clock_gettime(CLOCK_MONOTONIC, &start_wait);
        if (msgsnd(msgid, &req, msg_size, 0) == -1) { sim_active = 0; } 
        else if (msgrcv(msgid, &res, msg_size, mypid, 0) == -1) { sim_active = 0; }
        else {
            clock_gettime(CLOCK_MONOTONIC, &end_wait);
            double elapsed = get_elapsed_time(start_wait, end_wait);
            
            semop(semid, &mutex_lock, 1);
            shm_ptr->total_wait_time += elapsed;
            shm_ptr->daily_total_wait_time += elapsed; // Punto 4
            shm_ptr->wait_time_stazioni[TYPE_CASSA] += elapsed;
            shm_ptr->wait_count_stazioni[TYPE_CASSA]++;
            shm_ptr->daily_wait_count++; // Punto 4
            semop(semid, &mutex_unlock, 1);
        }
    }

    // --- TAVOLO ---
    if (sim_active == 1) {
        // Togliti dalla coda in piedi della mensa
        semop(semid, &mutex_lock, 1);
        if (shm_ptr->users_waiting > 0) shm_ptr->users_waiting--;
        semop(semid, &mutex_unlock, 1);

        struct sembuf table_wait = {SEM_TAVOLI, -1, SEM_UNDO};
        struct sembuf table_signal = {SEM_TAVOLI, 1, SEM_UNDO};

        // Si siede al tavolo (se c'è posto, altrimenti aspetta qui)
        if (semop(semid, &table_wait, 1) != -1) {
            // Mangia per un tempo PROPORZIONALE al numero di piatti acquistati
            int base_eat_time = 150000 + (rand() % 100000); // 150-250ms per piatto
            usleep(base_eat_time * piatti_acquistati);
            
            semop(semid, &table_signal, 1); // Libera il tavolo per il prossimo
        }
    } 

    shmdt(shm_ptr);
    return 0;
}