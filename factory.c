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
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>

#include "wrappers.h"
#include "message.h"
#include "shmem.h"

int main(int argc, char **argv) {
    if (argc != 8) {
        fprintf(stderr, "Usage: %s <id> <capacity> <duration_ms> <shm_key> <msg_key> <SEM_SHM> <SEM_LOG>\n", argv[0]);
        return 1;
    }

    int id = atoi(argv[1]);
    int capacity = atoi(argv[2]);
    int duration = atoi(argv[3]);
    key_t shmkey = (key_t)atoi(argv[4]);
    key_t msgkey = (key_t)atoi(argv[5]);
    const char *SEM_SHM_NAME = argv[6];
    const char *SEM_LOG_NAME = argv[7];

    // Attach to shared memory
    int shmid = Shmget(shmkey, SHMEM_SIZE, 0600);
    shData *shm  = (shData*)Shmat(shmid, NULL, 0);

    int msgid = Msgget(msgkey, 0600);

    // named semaphores
    sem_t *sem_shm = Sem_open2(SEM_SHM_NAME, 0);
    sem_t *sem_log = Sem_open2(SEM_LOG_NAME, 0);

    Sem_wait(sem_log);
    printf("Factory # %2d: STARTED.  My Capacity = %3d, in %4d milliSeconds\n", id, capacity, duration);
    fflush(stdout);
    Sem_post(sem_log);

    int iterations = 0;
    int total_made_by_me = 0;

    for (;;) {
        int to_make = 0;

        Sem_wait(sem_shm);
        if (shm->remain > 0) {
            to_make = (shm->remain >= capacity) ? capacity : shm->remain;
            shm->remain -= to_make;
            shm->made += to_make;
        }
        Sem_post(sem_shm);

        if (to_make == 0)
            break;

        // Log to the shared factory.log
        Sem_wait(sem_log);
        printf("Factory # %2d: Going to make %3d parts in %4d milliSecs\n", id, to_make, duration);
        fflush(stdout);
        Sem_post(sem_log);

        Usleep((useconds_t)duration * 1000);

        // message to supervisor
        msgBuf m;
        m.mtype = 1;
        m.purpose = PRODUCTION_MSG;
        m.facID = id;
        m.capacity = capacity;
        m.partsMade = to_make;
        m.duration = duration;
        if (msgsnd(msgid, &m, MSG_INFO_SIZE, 0) < 0) {
            perror("factory msgsnd(PRODUCTION)");
        }

        iterations++;
        total_made_by_me += to_make;
    }

    // completion
    msgBuf done;
    memset(&done, 0, sizeof(done));
    done.mtype = 1;
    done.purpose = COMPLETION_MSG;
    done.facID = id;
    if (msgsnd(msgid, &done, MSG_INFO_SIZE, 0) < 0) {
        perror("factory msgsnd(COMPLETION)");
    }

    Sem_wait(sem_log);
    printf(">>> Factory # %2d: Terminating after making total of %4d parts in %3d iterations\n", id, total_made_by_me, iterations);
    fflush(stdout);
    Sem_post(sem_log);

    Shmdt(shm);
    return 0;
}
