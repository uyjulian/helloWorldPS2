#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>  // For sleep function
#include <kernel.h>

// Function to calculate the time difference between two timespec structures
struct timespec timespec_diff(struct timespec start, struct timespec end) {
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0) {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    } else {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}

int main() {
    // I want to create a program that uses 100 times usleep function with values between 7500 and 12000
    // then check if the sleep function is working properly comparing times before and after the sleep function
    // and then print the difference between the times
    struct timespec start, end, timeDifference;
    int random, got, diff, biggerDiff, biggerRandom, biggerGot;
    biggerDiff = 0;
    for (int i = 0; i < 1000; i++) {
        random = rand() % 4500 + 7500;
        clock_gettime(CLOCK_MONOTONIC, &start);
        usleep(random);
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        timeDifference = timespec_diff(start, end);
        got = (timeDifference.tv_sec * 1000000000 + timeDifference.tv_nsec) / 1000;
        diff = abs(abs(random) - abs(got));
        if (diff > biggerDiff) {
            biggerDiff = diff;
            biggerRandom = random;
            biggerGot = got;
        }
        printf("Requested %d: got %d ==> diff: %d\n", random, got, diff);
    }

    printf("Bigger diff: %d, requested: %d, got: %d\n", biggerDiff, biggerRandom, biggerGot);
    SleepThread();
    
    return 0;
}
