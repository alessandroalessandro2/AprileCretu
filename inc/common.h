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

// INDICI SEMAFORI (Totale: 7)
#define SEM_MUTEX   0
#define SEM_TAVOLI  1
#define SEM_PRIMI   2
#define SEM_SECONDI 3
#define SEM_COFFEE  4
#define SEM_CASSA   5
#define SEM_SYNC    6
#define NUM_SEMAPHORES 7

#define TYPE_PRIMI    1
#define TYPE_SECONDI  2
#define TYPE_COFFEE   3
#define TYPE_CASSA    4

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
    int sub_type; // 0 = Main (Primo/Secondo/Caffè), 1 = Side (Contorno/Dolce)
} msg_t;

#define MAX_PIATTI 10
#define MAX_WORKERS 100

typedef struct {
    char name[32];
    int price;
    int porzioni_rimanenti;
} menu_item_t;

typedef struct {
    int sim_time;
    int current_day;              
    int sim_running;                
    
    int users_served;          
    int users_dropped;         
    int users_waiting;         
    int users_done_today;
    int revenue;                    

    // Piatti serviti: [0]Primi, [1]Secondi, [2]Contorni, [3]Dolce, [4]Caffè
    int dishes_served[5];      
    int dishes_wasted[5];           

    int portions_primi;        
    int portions_secondi;
    int portions_contorni;      

    int active_ops[5];      
    int worker_station[MAX_WORKERS]; 
    int total_pauses;          

    menu_item_t primi[MAX_PIATTI];
    menu_item_t secondi[MAX_PIATTI];
    menu_item_t contorni[MAX_PIATTI];
    menu_item_t dolce[MAX_PIATTI];
    menu_item_t caffe[MAX_PIATTI];
    
    int num_primi;
    int num_secondi;
    int num_contorni;
    int num_dolce;
    int num_caffe;

    // Prezzi prelevati dalla configurazione
    int price_primi;
    int price_secondi;
    int price_coffee;

    // Gestione statistiche tempi
    double total_wait_time;
    double wait_time_stazioni[5]; 
    int wait_count_stazioni[5];
} shared_data_t;

#endif // COMMON_H