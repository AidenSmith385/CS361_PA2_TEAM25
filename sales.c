//---------------------------------------------------------------------
// Assignment : PA-02 Concurrent Processes & IPC
// Date       : 10/25/25
// Author     : Aiden Smith and Braden Drake
//----------------------------------------------------------------------

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <semaphore.h>

#include "wrappers.h"
#include "message.h"
#include "shmem.h"

// Unique and fixed semaphores for consistant communication
static const char *SEM_SHM_NAME = "/Team25_shm_mutex";
static const char *SEM_LOG_NAME = "/Team25_log_mutex";
static const char *SEM_DONE_NAME = "/Team25_done";
static const char *SEM_PRINT_NAME = "/Team25_print";

// cleanup and sig handling defaults
static int g_shmid = -1;
static int g_msgid = -1;
static shData *g_shm = NULL;
static sem_t *g_sem_shm = NULL;
static sem_t *g_sem_log = NULL;
static sem_t *g_sem_done = NULL;
static sem_t *g_sem_print = NULL;

static pid_t g_children[MAXFACTORIES + 1];
static int g_num_children = 0;

static void clean_ipc(void) {
    if (g_sem_shm) {
    Sem_close(g_sem_shm);
    Sem_unlink(SEM_SHM_NAME);
    }

    if (g_sem_log) {
        Sem_close(g_sem_log);
        Sem_unlink(SEM_LOG_NAME);
    }

    if (g_sem_done) {
        Sem_close(g_sem_done);
        Sem_unlink(SEM_DONE_NAME);
    }

    if (g_sem_print) {
        Sem_close(g_sem_print);
        Sem_unlink(SEM_PRINT_NAME);
    }

    if (g_shm) {
        Shmdt(g_shm);
        g_shm = NULL;
    }

    if (g_shmid >= 0) {
        shmctl(g_shmid, IPC_RMID, NULL);
        g_shmid = -1;
    }

    if (g_msgid >= 0) {
        msgctl(g_msgid, IPC_RMID, NULL);
        g_msgid = -1;
    }

}

static void kill_children(void) {
    for (int i = 0; i < g_num_children; i++) {
        if (g_children[i] > 0) {
            kill(g_children[i], SIGKILL);
        }
    }
}

// kill clean and exit
static void sig_handler(int sig) {
    (void)sig;
    kill_children(); 
    clean_ipc();
    _exit(128);
}

static key_t make_key(char token) {
    key_t k = ftok(".", token);
    if (k == (key_t)-1) {
        return k;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <num_factories> <order_size>\n", argv[0]);
        return 1;
    }
    int N = atoi(argv[1]);
    int order = atoi(argv[2]);
    if (N <= 0 || N > MAXFACTORIES || order <= 0) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;    
    }

    sigactionWrapper(SIGINT,  sig_handler);
    sigactionWrapper(SIGTERM, sig_handler);

    // Create IPC objects
    key_t shm_key = make_key('S');
    key_t msg_key = make_key('Q');

    g_shmid = Shmget(shm_key, SHMEM_SIZE, IPC_CREAT | IPC_EXCL | 0600);
    g_shm   = (shData*)Shmat(g_shmid, NULL, 0);

    g_shm->order_size = order;
    g_shm->made = 0;
    g_shm->remain = order;
    g_shm->activeFactories = N;

    g_msgid = Msgget(msg_key, IPC_CREAT | 0600);

    // Create named semaphores
    g_sem_shm = Sem_open(SEM_SHM_NAME,   O_CREAT | O_EXCL, 0600, 1);
    g_sem_log = Sem_open(SEM_LOG_NAME,   O_CREAT | O_EXCL, 0600, 1);
    g_sem_done = Sem_open(SEM_DONE_NAME,  O_CREAT | O_EXCL, 0600, 0);
    g_sem_print = Sem_open(SEM_PRINT_NAME, O_CREAT | O_EXCL, 0600, 0);

    // Seed random once (portable)
    srand((unsigned)time(NULL));

    // Launch supervisor (stdout -> supervisor.log)
    pid_t pid = Fork();
    if (pid == 0) {
        FILE *fp = freopen("supervisor.log", "w", stdout);
        if (!fp) _exit(2);
        char nbuf[16], shmkeybuf[32], msgkeybuf[32];
        snprintf(nbuf, sizeof(nbuf), "%d", N);
        snprintf(shmkeybuf, sizeof(shmkeybuf), "%d", (int)shm_key);
        snprintf(msgkeybuf, sizeof(msgkeybuf), "%d", (int)msg_key);
        execlp("./supervisor", "supervisor",
               nbuf, shmkeybuf, msgkeybuf,
               SEM_DONE_NAME, SEM_PRINT_NAME,
               (char*)NULL);
        _exit(2);
    }
    g_children[g_num_children++] = pid;

    printf("SALES: Will Request an Order of Size = %d parts\n", order);
    printf("Creating %d Factor(ies)\n", N);
    // Launch N factories
    for (int i = 1; i <= N; i++) {
        int capacity = (int)(rand()%41) + 10;
        int duration = (int)(rand()%701) + 500;
        pid = Fork();
        if (pid == 0) {
            FILE *fp = freopen("factory.log", "a", stdout);
            if (!fp) _exit(2);
            char idbuf[16], capbuf[16], durbuf[16], shmkeybuf[32], msgkeybuf[32];
            snprintf(idbuf, sizeof(idbuf), "%d", i);
            snprintf(capbuf, sizeof(capbuf), "%d", capacity);
            snprintf(durbuf, sizeof(durbuf), "%d", duration);
            snprintf(shmkeybuf, sizeof(shmkeybuf), "%d", (int)shm_key);
            snprintf(msgkeybuf, sizeof(msgkeybuf), "%d", (int)msg_key);
            execlp("./factory", "factory",
                   idbuf, capbuf, durbuf,
                   shmkeybuf, msgkeybuf,
                   SEM_SHM_NAME, SEM_LOG_NAME,
                   (char*)NULL);
            _exit(2);
        }
        g_children[g_num_children++] = pid;

        printf("SALES: Factory # %2d was created, with Capacity= %3d and Duration= %4d\n", i, capacity, duration);
        fflush(stdout);
    }

    // Wait for supervisor
    Sem_wait(g_sem_done);
    puts("SALES: Supervisor says all Factories have completed their mission");

    sleep(2);
    puts("SALES: Permission granted to print final report");
    Sem_post(g_sem_print);

    // Reap
    for (int i = 0; i < g_num_children; i++) {
        int status; waitpid(g_children[i], &status, 0);
    }

    clean_ipc();
    puts("SALES: Cleaning up after the Supervisor Factory Process");
    return 0;
}
