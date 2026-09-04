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
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include "config_parser.h"

#define SHM_KEY 0x1A2B
#define SEM_KEY 0x3C4D
#define MSG_KEY 0x5E6F

// INDICI SEMAFORI
#define SEM_MUTEX       0
#define SEM_TAVOLI      1
#define SEM_PRIMI       2
#define SEM_SECONDI     3
#define SEM_COFFEE      4
#define SEM_CASSA       5
#define SEM_READY       6
#define SEM_DAY_START   7
#define SEM_DAY_END     8

#define TYPE_PRIMI    1
#define TYPE_SECONDI  2
#define TYPE_COFFEE   3
#define TYPE_CASSA    4

#define MAX_WORKERS 20
#define MAX_PIATTI 10

// STATI MESSAGGIO
#define STATUS_CLOSED    0
#define STATUS_EXHAUSTED 1
#define STATUS_SERVED    2

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

typedef struct {
    long mtype;
    int sender_pid;
    int status;
    int importo;
    int indice_piatto;
    int is_dolce;
    
    // Novità per il calcolo preciso del tempo in coda
    int enqueue_time;
    int queue_wait;
} msg_t;

typedef struct {
    char name[32];
    int price;
    int porzioni_rimanenti;
} menu_item_t;

typedef struct {
    int sim_time;
    int sim_running;
    int day_ended;
    int current_day;

    int op_assignment[MAX_WORKERS];
    int queue_lengths[5];
    int active_ops[5]; 

    // Menu
    menu_item_t primi[MAX_PIATTI];
    menu_item_t secondi[MAX_PIATTI];
    menu_item_t contorni[MAX_PIATTI];
    menu_item_t caffe[4];
    menu_item_t dolce;
    
    int num_primi;
    int num_secondi;
    int num_contorni;
    int num_caffe;

    // --- Statistiche Giornaliere ---
    int daily_users_served;
    int daily_users_dropped;
    int daily_revenue;
    int daily_pauses;
    int daily_active_ops;
    int daily_dishes_served[5];
    int daily_dishes_wasted[3];
    int daily_wait_time_stazioni[5];
    int daily_wait_count_stazioni[5];

    // --- Statistiche Totali ---
    int total_users_served;
    int total_users_dropped;
    int total_revenue;
    int total_pauses;
    int total_active_ops;
    int total_dishes_served[5]; // 0=Primi, 1=Secondi, 2=Contorni, 3=Caffe, 4=Dolci
    int total_dishes_wasted[3]; // 0=Primi, 1=Secondi, 2=Contorni
    int wait_time_stazioni[5];
    int wait_count_stazioni[5];

} shared_data_t;

#endif // COMMON_H