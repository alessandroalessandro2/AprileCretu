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

// CHIAVI IPC
#define SHM_KEY 0x1A2B
#define SEM_KEY 0x3C4D
#define MSG_KEY 0x5E6F

// INDICI SEMAFORI (Totale: 7)
#define SEM_MUTEX   0  // Mutua esclusione sulla Shared Memory
#define SEM_TAVOLI  1  // Posti a sedere disponibili
#define SEM_PRIMI   2  // Postazioni stazione Primi
#define SEM_SECONDI 3  // Postazioni stazione Secondi
#define SEM_COFFEE  4  // Postazioni stazione Caffè
#define SEM_CASSA   5  // Postazioni Cassa
#define SEM_SYNC    6  // Barriera di sincronizzazione iniziale
#define NUM_SEMAPHORES 7

// TIPI MESSAGGIO PER LE CODE IPC
#define TYPE_PRIMI    1
#define TYPE_SECONDI  2
#define TYPE_COFFEE   3
#define TYPE_CASSA    4

// UNION PER SEMAFORI (Necessaria per semctl su Linux/Mac)
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// STRUTTURA MESSAGGIO
typedef struct {
    long mtype;          // PID destinatario oppure TYPE_STAZIONE
    int sender_pid;
    int status;          // 1 = servito / ok, 0 = esaurito / fallito
    int importo;
    int indice_piatto;
} msg_t;

#define MAX_PIATTI 10

// STRUTTURA PIATTO DEL MENU
typedef struct {
    char name[32];
    int price;
    int porzioni_rimanenti;
} menu_item_t;

// STRUTTURA MEMORIA CONDIVISA
typedef struct {
    int sim_time;             
    int users_served;         
    int users_dropped;        
    int users_waiting;        
    int revenue;               

    // Piatti serviti: [0]=Primi, [1]=Secondi, [2]=Caffè
    int dishes_served[3];     
    // Piatti avanzati/buttati a fine giornata: [0]=Primi, [1]=Secondi, [2]=Caffè
    int dishes_wasted[3];          

    int portions_primi;       
    int portions_secondi;     
    int sim_running;               
    int ready_count;               

    // Contatori operatori attivi per stazione (1=Primi, 2=Secondi, 3=Caffè, 4=Cassa)
    int active_ops[5];     
    int queue_lengths[5]; // NUOVO: Lunghezza delle code per stazione
    int total_pauses;         

    // Variabili per il Menu
    menu_item_t primi[MAX_PIATTI];
    menu_item_t secondi[MAX_PIATTI];
    menu_item_t caffe[4]; 
    menu_item_t dolce; // Dolce illimitato
    
    int num_primi;
    int num_secondi;
    int num_caffe;

    // Statistiche tempi di attesa
    double total_wait_time;
    double wait_time_stazioni[5]; 
    int wait_count_stazioni[5];
} shared_data_t;

#endif // COMMON_H