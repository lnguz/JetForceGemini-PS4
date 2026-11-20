// Jet Force Gemini PS4: POSIX semaphores that work on the console.
//
// OpenOrbis' libc.a (musl) and libkernel both define sem_*; libc.a comes first in the link, and musl's semaphores wait
// with the Linux futex syscall, which the PS4 kernel does not have: sem_wait returned without waiting, the runtime's
// thread hand-offs (moodycamel::LightweightSemaphore) fell apart and the scheduler threw "No threads left to run!".
// The link wraps every sem_* function (--wrap=sem_wait ...) onto this implementation on libkernel's pthread mutex and
// condition variable. The sem_t storage holds a pointer to the real state.
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned int count;
} Ps4Sem;

_Static_assert(sizeof(sem_t) >= sizeof(Ps4Sem *), "sem_t too small to hold a pointer");

static Ps4Sem *get(sem_t *sem) {
    Ps4Sem *s;
    memcpy(&s, sem, sizeof s);
    return s;
}

int __wrap_sem_init(sem_t *sem, int pshared, unsigned int value) {
    (void)pshared;
    Ps4Sem *s = (Ps4Sem *)calloc(1, sizeof *s);
    if (!s) {
        errno = ENOMEM;
        return -1;
    }
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->count = value;
    memcpy(sem, &s, sizeof s);
    return 0;
}

// A semaphore is often destroyed right after the wait it was posted for returns (the runtime's per-task `parsed`
// semaphore lives on the waiting thread's stack). The poster may still be inside pthread_mutex_unlock on it at that
// moment, so freeing it there let the next semaphore reuse memory still being touched: once in a thousand or so
// graphics tasks a post was lost and the game hung waiting for a task the graphics thread never took. Destroyed
// semaphores wait in this queue and are freed only after kGraveyard later destroys.
enum { kGraveyard = 4096 };
static Ps4Sem *g_graveyard[kGraveyard];
static unsigned g_graveyard_next, g_graveyard_count;
static pthread_mutex_t g_graveyard_mutex = PTHREAD_MUTEX_INITIALIZER;

int __wrap_sem_destroy(sem_t *sem) {
    Ps4Sem *s = get(sem);
    if (!s) return 0;
    memset(sem, 0, sizeof(Ps4Sem *));
    Ps4Sem *oldest = NULL;
    pthread_mutex_lock(&g_graveyard_mutex);
    if (g_graveyard_count == kGraveyard) {
        oldest = g_graveyard[g_graveyard_next];
    } else {
        g_graveyard_count++;
    }
    g_graveyard[g_graveyard_next] = s;
    g_graveyard_next = (g_graveyard_next + 1) % kGraveyard;
    pthread_mutex_unlock(&g_graveyard_mutex);
    if (oldest) {
        pthread_cond_destroy(&oldest->cond);
        pthread_mutex_destroy(&oldest->mutex);
        free(oldest);
    }
    return 0;
}

int __wrap_sem_wait(sem_t *sem) {
    Ps4Sem *s = get(sem);
    pthread_mutex_lock(&s->mutex);
    while (s->count == 0) pthread_cond_wait(&s->cond, &s->mutex);
    s->count--;
    pthread_mutex_unlock(&s->mutex);
    return 0;
}

int __wrap_sem_trywait(sem_t *sem) {
    Ps4Sem *s = get(sem);
    pthread_mutex_lock(&s->mutex);
    int ok = s->count > 0;
    if (ok) s->count--;
    pthread_mutex_unlock(&s->mutex);
    if (!ok) {
        errno = EAGAIN;
        return -1;
    }
    return 0;
}

// The deadline is CLOCK_REALTIME (moodycamel computes it with clock_gettime(CLOCK_REALTIME)). libkernel's
// pthread_cond_timedwait does not measure it against the same clock (the runtime's timers never fired), so the wait
// polls the count against the deadline itself, sleeping in short steps.
int __wrap_sem_timedwait(sem_t *sem, const struct timespec *abstime) {
    Ps4Sem *s = get(sem);
    for (;;) {
        pthread_mutex_lock(&s->mutex);
        if (s->count > 0) {
            s->count--;
            pthread_mutex_unlock(&s->mutex);
            return 0;
        }
        pthread_mutex_unlock(&s->mutex);
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        const long long left_ns = (long long)(abstime->tv_sec - now.tv_sec) * 1000000000LL + (abstime->tv_nsec - now.tv_nsec);
        if (left_ns <= 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        usleep(left_ns > 200000 ? 200 : (unsigned)(left_ns / 1000 + 1));
    }
}

int __wrap_sem_post(sem_t *sem) {
    Ps4Sem *s = get(sem);
    pthread_mutex_lock(&s->mutex);
    s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    return 0;
}

int __wrap_sem_getvalue(sem_t *sem, int *value) {
    Ps4Sem *s = get(sem);
    pthread_mutex_lock(&s->mutex);
    *value = (int)s->count;
    pthread_mutex_unlock(&s->mutex);
    return 0;
}

// Start-up self-test (logged): what each clock id returns and whether a 50 ms timed semaphore wait takes 50 ms.
#include "ps4_platform.h"
void ps4_clock_selftest(void) {
    for (int id = 0; id <= 4; id++) {
        struct timespec a = {0, 0};
        int rc = clock_gettime((clockid_t)id, &a);
        ps4_log("clock %d: rc %d, %lld.%09ld", id, rc, (long long)a.tv_sec, a.tv_nsec);
    }
    sem_t sem;
    __wrap_sem_init(&sem, 0, 0);
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 50000000;
    if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    const double start = ps4_time_seconds();
    int rc = __wrap_sem_timedwait(&sem, &deadline);
    ps4_log("timed wait 50 ms: rc %d, took %.1f ms", rc, (ps4_time_seconds() - start) * 1000.0);
    __wrap_sem_destroy(&sem);
}

// Clock ids: OpenOrbis' headers (musl) use Linux's numbering, the PS4 kernel FreeBSD's. Linux CLOCK_MONOTONIC (1) is
// FreeBSD CLOCK_VIRTUAL (the process's CPU time: libc++'s steady_clock stood still and the runtime's timers never
// fired). The link wraps clock_gettime (--wrap=clock_gettime) and maps the Linux monotonic clocks to FreeBSD's (4).
int __real_clock_gettime(clockid_t id, struct timespec *ts);
int __wrap_clock_gettime(clockid_t id, struct timespec *ts) {
    switch ((int)id) {
    case 1: // CLOCK_MONOTONIC
    case 4: // CLOCK_MONOTONIC_RAW
    case 6: // CLOCK_MONOTONIC_COARSE
    case 7: // CLOCK_BOOTTIME
        return __real_clock_gettime((clockid_t)4, ts);
    case 5: // CLOCK_REALTIME_COARSE
        return __real_clock_gettime((clockid_t)0, ts);
    default:
        return __real_clock_gettime(id, ts);
    }
}
