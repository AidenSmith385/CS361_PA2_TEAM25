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

// Unique and fixed semaphores for consistent communication
#define SEM_SHM_NAME          "/Team25_shm_mutex"
#define SEM_LOG_NAME          "/Team25_log_mutex"
#define SEM_DONE_NAME         "/Team25_done"
#define SEM_PRINT_NAME        "/Team25_print"

// cleanup and sig handling defaults
static int shmid = -1;
static int msgid = -1;
shData *p_shm;
sem_t *sem_shm, *sem_log, *sem_done, *sem_print;

static pid_t children[MAXFACTORIES + 1];
static int num_children = 0;

// Close and unlink semaphores, remove shared
// memory, and destroy message queue
static void clean_ipc(void) {
    // Close semaphores
    Sem_close(sem_shm);
    Sem_close(sem_log);
    Sem_close(sem_done);
    Sem_close(sem_print);

    // Unlink semaphores
    Sem_unlink(SEM_SHM_NAME);
    Sem_unlink(SEM_LOG_NAME);
    Sem_unlink(SEM_DONE_NAME);
    Sem_unlink(SEM_PRINT_NAME);

    // Detach shm
    Shmdt(p_shm);

    // Destroy shm
    shmctl(shmid, IPC_RMID, NULL);

    // Destroy message queue
    if (msgid >= 0) {
        msgctl(msgid, IPC_RMID, NULL);
        msgid = -1;
    }

}

// Kills all children
static void kill_children(void) {
    for (int i = 0; i < num_children; i++) {
        if (children[i] > 0) {
            kill(children[i], SIGKILL);
        }
    }
}

// Kill, cleanup, and exit
static void sig_handler(int sig) {
    (void)sig;
    kill_children();
    clean_ipc();
    _exit(0);
}

// Makes a key
static key_t make_key(char token) {
    key_t k = ftok("shmem.h", token);
    if (k == (key_t)-1) {
        perror("Failed to make key");
        exit(1);
    }
    return k;
}

int main(int argc, char **argv) {
    // Wrong number of arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <num_factories> <order_size>\n", argv[0]);
        return 1;
    }

    // Get num of factories and order size
    int N = atoi(argv[1]);
    int order = atoi(argv[2]);

    // Invalid arguments
    if (N <= 0 || N > MAXFACTORIES || order <= 0) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;    
    }

    // Create IPC objects
    key_t shm_key = make_key('S');
    key_t msg_key = make_key('Q');

    // Get and attach shared memory
    shmid = Shmget(shm_key, SHMEM_SIZE, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    p_shm   = (shData*)Shmat(shmid, NULL, 0);

    // Set the fields of the shared memory
    p_shm->order_size = order;
    p_shm->made = 0;
    p_shm->remain = order;
    p_shm->activeFactories = N;

    // Get message queue
    msgid = Msgget(msg_key, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

    // Create named semaphores
    sem_shm = Sem_open(SEM_SHM_NAME,   O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);
    sem_log = Sem_open(SEM_LOG_NAME,   O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1);
    sem_done = Sem_open(SEM_DONE_NAME,  O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
    sem_print = Sem_open(SEM_PRINT_NAME, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);

    // Seed random once (portable)
    srand((unsigned)time(NULL));

    // Launch supervisor (stdout -> supervisor.log)
    pid_t pid = Fork();

    // If supervisor, supervisor executes and
    // redirects stdout to supervisor.log
    if (pid == 0) {
        // Creates supervisor.log, write only, create+truncate
        int fd = open("supervisor.log", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (fd < 0) _exit(2);

        // Redirect stdout to supervisor.log
        dup2(fd, STDOUT_FILENO);
        close(fd);

        // Set argument buffers
        char nbuf[16], shmkeybuf[32], msgkeybuf[32];
        snprintf(nbuf, sizeof(nbuf), "%d", N);
        snprintf(shmkeybuf, sizeof(shmkeybuf), "%d", (int)shm_key);
        snprintf(msgkeybuf, sizeof(msgkeybuf), "%d", (int)msg_key);

        // Passes num of factories, shared memory and
        // message queue keys, and sem names
        execlp("./supervisor", "supervisor",
               nbuf, shmkeybuf, msgkeybuf,
               SEM_DONE_NAME, SEM_PRINT_NAME,
               (char*)NULL);
        _exit(2);
    }

    // Adds pid of supervisor
    children[num_children++] = pid;

    printf("SALES: Will Request an Order of Size = %d parts\n", order);
    printf("Creating %d Factory(ies)\n", N);

    // Launch N factories
    for (int i = 1; i <= N; i++) {
        // Capacity is a random integer between 10 and 50
        int capacity = (int)(rand()%41) + 10;
        // Duration is a random integer between 500 and 1200
        int duration = (int)(rand()%701) + 500;

        // Launch a factory
        pid = Fork();
        // If factory, factory executes and
        // redirects stdout to factory.log
        if (pid == 0) {
            // Creates factory.log, write only, create+append
            // to ensure they don't write over each other
            int fd = open("factory.log", O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
            if (fd < 0) _exit(2);

            // Redirect stdout to factory.log
            dup2(fd, STDOUT_FILENO);
            close(fd);

            // Set argument buffers
            char idbuf[16], capbuf[16], durbuf[16], shmkeybuf[32], msgkeybuf[32];
            snprintf(idbuf, sizeof(idbuf), "%d", i);
            snprintf(capbuf, sizeof(capbuf), "%d", capacity);
            snprintf(durbuf, sizeof(durbuf), "%d", duration);
            snprintf(shmkeybuf, sizeof(shmkeybuf), "%d", (int)shm_key);
            snprintf(msgkeybuf, sizeof(msgkeybuf), "%d", (int)msg_key);
        
            // Passes factory number, capacity, duration,
            // shm and msgQ keys, and sem names
            execlp("./factory", "factory",
                   idbuf, capbuf, durbuf,
                   shmkeybuf, msgkeybuf,
                   SEM_SHM_NAME, SEM_LOG_NAME,
                   (char*)NULL);
            _exit(2);
        }

        // Add each factory's pid
        children[num_children++] = pid;

        printf("SALES: Factory # %2d was created, with Capacity= %3d and Duration= %4d\n", i, capacity, duration);
        fflush(stdout);
    }

    // Handle SIGINT and SIGTERM
    sigactionWrapper(SIGINT,  sig_handler);
    sigactionWrapper(SIGTERM, sig_handler);

    // Wait for supervisor
    Sem_wait(sem_done);
    puts("SALES: Supervisor says all Factories have completed their mission");

    // Sleep for 2 seconds
    sleep(2);
    puts("SALES: Permission granted to print final report");
    Sem_post(sem_print);

    // Reap
    for (int i = 0; i < num_children; i++) {
        waitpid(children[i], NULL, 0);
    }

    // Cleanup IPCs
    clean_ipc();
    puts("SALES: Cleaning up after the Supervisor Factory Process");
    exit(0);
}
