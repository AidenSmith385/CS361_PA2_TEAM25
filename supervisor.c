//---------------------------------------------------------------------
// Assignment : PA-02 Concurrent Processes & IPC
// Date       : 10/25/25
// Author     : Aiden Smith and Braden Drake
//----------------------------------------------------------------------

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <fcntl.h>

#include "wrappers.h"
#include "message.h"
#include "shmem.h"

int main(int argc, char **argv) {
    // Wrong number of arguments
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <N> <shm_key> <msg_key> <SEM_DONE> <SEM_PRINT>\n", argv[0]);
        return 1;
    }

    // Get num of factories, shm and msgQ keys, and sem names
    int N = atoi(argv[1]);
    key_t shmkey = (key_t)atoi(argv[2]);
    key_t msgkey = (key_t)atoi(argv[3]);
    const char *SEM_DONE_NAME  = argv[4];
    const char *SEM_PRINT_NAME = argv[5];

    // Get and attach to shared memory
    int shmid = Shmget(shmkey, SHMEM_SIZE, S_IRUSR | S_IWUSR);
    shData *shm = (shData*)Shmat(shmid, NULL, 0);

    // Get message queue
    int msgid = Msgget(msgkey, S_IRUSR | S_IWUSR);

    // Rendezvous
    sem_t *sem_done = Sem_open2(SEM_DONE_NAME, 0);
    sem_t *sem_print = Sem_open2(SEM_PRINT_NAME, 0);

    // Allocate arrays for the factories' parts and iterations
    int *parts = calloc(N + 1, sizeof(int));
    int *iters = calloc(N + 1, sizeof(int));
    if (!parts || !iters) {
        perror("calloc");
        return 2;
    }

    printf("\nSUPERVISOR: Started\n");

    // Recieve production and completion messages
    int active = N;
    while (active > 0) {
        msgBuf m;
        ssize_t n = msgrcv(msgid, &m, MSG_INFO_SIZE, 0, 0);
        if (n < 0) {
            perror("supervisor msgrcv");
            continue;
        }

        if (m.purpose == PRODUCTION_MSG) {
            printf("SUPERVISOR: Factory # %2d produced  %3d parts in %4d milliSecs\n",
                   m.facID, m.partsMade, m.duration);
            parts[m.facID] += m.partsMade;
            iters[m.facID] += 1;
        } else if (m.purpose == COMPLETION_MSG) {
            printf("SUPERVISOR: Factory # %2d        COMPLETED its task\n", m.facID);
            active--;
            shm->activeFactories -= 1;
        }
        fflush(stdout);
    }

    // Rendezvous
    printf("\nSUPERVISOR: Manufacturing is complete. Awaiting permission to print final report\n");
    Sem_post(sem_done);   // done
    Sem_wait(sem_print);  // wait for Sales

    // Final report
    printf("\n****** SUPERVISOR: Final Report ******\n");
    int grand = 0;
    for (int i = 1; i <= N; i++) {
        printf("Factory # %2d made a total of %4d parts in %5d iterations\n", i, parts[i], iters[i]);
        grand += parts[i];
    }
    printf("==============================\n");
    printf("Grand total parts made = %5d   vs  order size of %5d\n", grand, shm->order_size);
    fflush(stdout);

    // Close semaphores
    Sem_close(sem_done);
    Sem_close(sem_print);

    // Detach shared memory
    Shmdt(shm);

    // Free mem
    free(parts);
    free(iters);
    return 0;
}
