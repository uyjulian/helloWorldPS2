#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h> // For sleep function

#define NUM_THREADS 20

void* thread_function(void* thread_id) {
    long tid = (long)thread_id;
    char message[100];

    while (1) {
        int* ptr = (int*)malloc(sizeof(int));
        if (ptr == NULL) {
            fprintf(stderr, "==> Thread %ld: Memory allocation failed\n", tid);
            pthread_exit(NULL);
        }

        // Use the allocated memory
        *ptr = tid;

        // Print the thread ID, allocated memory address, and value stored in the allocated memory
        printf("Thread %ld: allocated memory at address %p with value %d\n", tid, (void*)ptr, *ptr);

        // Free the allocated memory
        free(ptr);

        // Sleep for a while before the next iteration
        sleep(1);
    }

    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int rc;
    long t;

    // fioInit();
    
    // Create threads
    for (t = 0; t < NUM_THREADS; t++) {
        rc = pthread_create(&threads[t], NULL, thread_function, (void*)t);
        if (rc) {
            fprintf(stderr, "Error: unable to create thread, %d\n", rc);
            exit(-1);
        }
    }

    // Join threads
    for (t = 0; t < NUM_THREADS; t++) {
        rc = pthread_join(threads[t], NULL);
        if (rc) {
            fprintf(stderr, "Error: unable to join thread, %d\n", rc);
            exit(-1);
        }
    }

    return 0;
}
