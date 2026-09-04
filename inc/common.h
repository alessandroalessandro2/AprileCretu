#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "config_parser.h"

#define SHM_KEY 0x1234
#define SEM_KEY 0x5678
#define MSG_KEY 0x9ABC

// Definizione dei 7 semafori totali
#define NUM_SEMAPHORES 7
#define SEM_MUTEX 0
#define SEM_TAVOLI 1
#define SEM_PRIMI 2
#define SEM_SECONDI 3
#define SEM_COFFEE 4
#define SEM_CASSA 5
#define SEM_SYNC 6

#define TYPE_PRIMI 1
#define TYPE_SECONDI 2
#define TYPE_COFFEE 3
#define TYPE_CASSA 4

#define MAX_PIATTI 10

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

typedef struct {
    char name[32];
    int price;
    int porzioni_rimanenti;
} piatto_t;

typedef struct {
    long mtype;
    pid_t sender_pid;
    int indice_piatto;
    int importo;
    int status;
} msg_t;

typedef struct {
    int sim_running;
    int sim_time;
    
    // Menu
    piatto_t primi[MAX_PIATTI];
    int num_primi;
    piatto_t secondi[MAX_PIATTI];
    int num_secondi;
    piatto_t dolce;
    piatto_t caffe[4];
    int num_caffe;
    
    int portions_primi;
    int portions_secondi;
    
    // Statistiche Globali
    int users_waiting;
    int users_served;
    int users_dropped;
    int revenue;
    int dishes_served[3]; // [0]=Primi, [1]=Secondi, [2]=Caffè
    int active_ops[5];
    int total_pauses;
    
    double total_wait_time;
    double wait_time_stazioni[5];
    int wait_count_stazioni[5];

    // --- STATISTICHE GIORNALIERE (Punto 4 del Bando) ---
    int daily_users_served;
    int daily_users_dropped;
    int daily_revenue;
    int daily_dishes_served[3];
    int daily_pauses;
    double daily_total_wait_time;
    int daily_wait_count;
    
} shared_data_t;

#endif