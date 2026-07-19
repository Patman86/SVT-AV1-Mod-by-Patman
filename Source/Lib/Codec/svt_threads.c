/*
* Copyright(c) 2019 Intel Corporation
*
* This source code is subject to the terms of the BSD 2 Clause License and
* the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
* was not distributed with this source code in the LICENSE file, you can
* obtain it at https://www.aomedia.org/license/software-license. If the Alliance for Open
* Media Patent License 1.0 was not distributed with this source code in the
* PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.
*/

// Summary:
// EbThreads contains wrappers functions that hide
// platform specific objects such as threads, semaphores,
// and mutexs.  The goal is to eliminiate platform #define
// in the code.

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define EB_THREAD_SANITIZER_ENABLED 1
#endif
#endif

#ifndef EB_THREAD_SANITIZER_ENABLED
#define EB_THREAD_SANITIZER_ENABLED 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define EB_COLD __attribute__((cold))
#define EB_LIKELY(x) __builtin_expect(!!(x), 1)
#define EB_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define EB_COLD
#define EB_LIKELY(x) (x)
#define EB_UNLIKELY(x) (x)
#endif

#ifndef SVT_THREAD_STACK_SIZE
#define SVT_THREAD_STACK_SIZE (4u * 1024u * 1024u)
#endif

/****************************************
* Universal Includes
****************************************/
#include <stdlib.h>
#include <string.h>
#include "svt_threads.h"
#include "svt_log.h"

/****************************************
* Win32 Includes
****************************************/
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#endif // _WIN32

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

#if PRINTF_TIME
#include <stdarg.h>
#ifdef _WIN32
void printfTime(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    SVT_LOG(" [%i ms]\t", ((int32_t)clock()));
    vprintf(fmt, args);
    va_end(args);
}
#endif
#endif

#ifndef _WIN32
static void *dummy_func(void *arg) {
    (void)arg;
    return NULL;
}

// These can stay with pthread_once_t since this is specific to pthreads implementation
static pthread_once_t checked_once = PTHREAD_ONCE_INIT;
static bool           can_use_prio = false;

static EB_COLD void check_set_prio(void) {
    /*
    * We can only use realtime priority if we are running as root, so
    * check if geteuid() == 0 (meaning either root or sudo).
    * If we don't do this check, we will eventually run into memory
    * issues if the encoder is uninitialized and re-initialized multiple
    * times in one executable due to a bug in glibc.
    * https://sourceware.org/bugzilla/show_bug.cgi?id=19511
    *
    * We still need to exclude the case of thread sanitizer because we
    * run the test as root inside the container and trying to change
    * the thread priority will __always__ fail the thread sanitizer.
    * https://github.com/google/sanitizers/issues/1088
    */
    if (EB_THREAD_SANITIZER_ENABLED || geteuid() != 0) {
        return;
    }

    pthread_attr_t attr;
    int            ret;

    ret = pthread_attr_init(&attr);
    if (ret) {
        SVT_WARN("Failed to initialize thread attributes: %s\n", strerror(ret));
        return;
    }

    struct sched_param param;
    ret = pthread_attr_getschedparam(&attr, &param);
    if (ret) {
        SVT_WARN("Failed to get thread priority: %s\n", strerror(ret));
        goto end;
    }

    param.sched_priority = 99;
    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret) {
        SVT_WARN("Failed to set thread priority: %s\n", strerror(ret));
        goto end;
    }

    pthread_t th;
    ret = pthread_create(&th, &attr, dummy_func, NULL);
    if (ret) {
        SVT_WARN("Failed to create thread: %s\n", strerror(ret));
        goto end;
    }

    can_use_prio = true;
    (void)pthread_join(th, NULL);

end:
    ret = pthread_attr_destroy(&attr);
    if (ret) {
        SVT_WARN("Failed to destroy thread attributes: %s\n", strerror(ret));
    }
}
#endif

/****************************************
* svt_create_thread
****************************************/
EbHandle svt_create_thread(void *thread_function(void *), void *thread_context) {
    EbHandle thread_handle = NULL;

#ifdef _WIN32

    thread_handle = (EbHandle)CreateThread(
        NULL,                                 // default security attributes
        0,                                    // default stack size
        (LPTHREAD_START_ROUTINE)thread_function, // function to be tied to the new thread
        thread_context,                       // context to be tied to the new thread
        0,                                    // thread active when created
        NULL);                                // new thread ID

#else
    pthread_attr_t attr;
    pthread_t     *th  = NULL;
    int            ret = pthread_once(&checked_once, check_set_prio);

    if (EB_UNLIKELY(ret)) {
        SVT_ERROR("Failed to run pthread_once to check if we can set priority: %s\n",
                  strerror(ret));
        return NULL;
    }

    ret = pthread_attr_init(&attr);
    if (EB_UNLIKELY(ret)) {
        SVT_ERROR("Failed to initialize thread attributes: %s\n", strerror(ret));
        return NULL;
    }

    if (can_use_prio) {
        // As described in https://docs.oracle.com/cd/E19455-01/806-5257/attrib-16/index.html
        struct sched_param param;
        ret = pthread_attr_getschedparam(&attr, &param);
        if (ret) {
            SVT_WARN("Failed to get thread priority: %s\n", strerror(ret));
        } else {
            param.sched_priority = 99;
            ret                  = pthread_attr_setschedparam(&attr, &param);
            if (ret) {
                SVT_WARN("Failed to set thread priority: %s\n", strerror(ret));
            }
        }
    }

    {
        size_t stack_size = SVT_THREAD_STACK_SIZE;
#ifdef PTHREAD_STACK_MIN
        if (stack_size < PTHREAD_STACK_MIN)
            stack_size = PTHREAD_STACK_MIN;
#endif
        ret = pthread_attr_setstacksize(&attr, stack_size);
        if (ret) {
            SVT_WARN("Failed to set thread stack size to %zu: %s\n",
                     stack_size, strerror(ret));
        }
    }

    th = malloc(sizeof(*th));
    if (EB_UNLIKELY(th == NULL)) {
        SVT_ERROR("Failed to allocate thread handle\n");
        goto fail_attr;
    }

    ret = pthread_create(th, &attr, thread_function, thread_context);
    if (EB_UNLIKELY(ret)) {
        SVT_ERROR("Failed to create thread: %s\n", strerror(ret));
        goto fail_th;
    }

    ret = pthread_attr_destroy(&attr);
    if (ret) {
        SVT_WARN("Failed to destroy thread attributes: %s\n", strerror(ret));
    }

    thread_handle = th;
#endif // _WIN32

    return thread_handle;

#ifndef _WIN32
fail_th:
    free(th);
fail_attr:
    ret = pthread_attr_destroy(&attr);
    if (ret) {
        SVT_WARN("Failed to destroy thread attributes: %s\n", strerror(ret));
    }
    return NULL;
#endif
}

/****************************************
* svt_destroy_thread
****************************************/
EbErrorType svt_destroy_thread(EbHandle thread_handle) {
    EbErrorType error_return;

#ifdef _WIN32
    WaitForSingleObject(thread_handle, INFINITE);
    error_return = CloseHandle(thread_handle) ? EB_ErrorNone : EB_ErrorDestroyThreadFailed;
#else
    error_return = pthread_join(*((pthread_t *)thread_handle), NULL) ? EB_ErrorDestroyThreadFailed
                                                                     : EB_ErrorNone;
    free(thread_handle);
#endif // _WIN32

    return error_return;
}

/***************************************
* svt_create_semaphore
***************************************/
EbHandle svt_create_semaphore(uint32_t initial_count, uint32_t max_count) {
    EbHandle semaphore_handle = NULL;

#if defined(_WIN32)
    semaphore_handle = (EbHandle)CreateSemaphore(NULL,          // default security attributes
                                                 initial_count, // initial semaphore count
                                                 max_count,     // maximum semaphore count
                                                 NULL);         // semaphore is not named
#elif defined(__APPLE__)
    UNUSED(max_count);
    semaphore_handle = (EbHandle)dispatch_semaphore_create(initial_count);
#else
    UNUSED(max_count);

    semaphore_handle = (sem_t *)malloc(sizeof(sem_t));
    if (semaphore_handle != NULL) {
        if (sem_init((sem_t *)semaphore_handle, // semaphore handle
                     0,                         // shared semaphore (not local)
                     initial_count)) {          // initial count
            SVT_ERROR("Failed to initialize semaphore: %s\n", strerror(errno));
            free(semaphore_handle);
            semaphore_handle = NULL;
        }
    }
#endif

    return semaphore_handle;
}

/***************************************
* svt_post_semaphore
***************************************/
EbErrorType svt_post_semaphore(EbHandle semaphore_handle) {
    EbErrorType return_error;

#ifdef _WIN32
    return_error = !ReleaseSemaphore(semaphore_handle, // semaphore handle
                                     1,                // amount to increment the semaphore
                                     NULL)             // pointer to previous count (optional)
                       ? EB_ErrorSemaphoreUnresponsive
                       : EB_ErrorNone;
#elif defined(__APPLE__)
    dispatch_semaphore_signal((dispatch_semaphore_t)semaphore_handle);
    return_error = EB_ErrorNone;
#else
    return_error = sem_post((sem_t *)semaphore_handle) ? EB_ErrorSemaphoreUnresponsive
                                                       : EB_ErrorNone;
#endif

    return return_error;
}

/***************************************
* svt_block_on_semaphore
***************************************/
EbErrorType svt_block_on_semaphore(EbHandle semaphore_handle) {
    EbErrorType return_error;

#ifdef _WIN32
    return_error = WaitForSingleObject((HANDLE)semaphore_handle, INFINITE)
                       ? EB_ErrorSemaphoreUnresponsive
                       : EB_ErrorNone;
#elif defined(__APPLE__)
    return_error = dispatch_semaphore_wait((dispatch_semaphore_t)semaphore_handle,
                                           DISPATCH_TIME_FOREVER)
                       ? EB_ErrorSemaphoreUnresponsive
                       : EB_ErrorNone;
#else
    int ret;
    do {
        ret = sem_wait((sem_t *)semaphore_handle);
    } while (ret == -1 && errno == EINTR);
    return_error = ret ? EB_ErrorSemaphoreUnresponsive : EB_ErrorNone;
#endif

    return return_error;
}

/***************************************
* svt_destroy_semaphore
***************************************/
EbErrorType svt_destroy_semaphore(EbHandle semaphore_handle) {
    EbErrorType return_error;

#ifdef _WIN32
    return_error = !CloseHandle((HANDLE)semaphore_handle) ? EB_ErrorDestroySemaphoreFailed
                                                          : EB_ErrorNone;
#elif defined(__APPLE__)
    dispatch_release((dispatch_semaphore_t)semaphore_handle);
    return_error = EB_ErrorNone;
#else
    return_error = sem_destroy((sem_t *)semaphore_handle) ? EB_ErrorDestroySemaphoreFailed
                                                          : EB_ErrorNone;
    free(semaphore_handle);
#endif

    return return_error;
}

/***************************************
* svt_create_mutex
***************************************/
EbHandle svt_create_mutex(void) {
    EbHandle mutex_handle = NULL;

#ifdef _WIN32
    mutex_handle = (EbHandle)CreateMutex(NULL,  // default security attributes
                                         false, // false := not initially owned
                                         NULL); // mutex is not named

#else
    mutex_handle = (EbHandle)malloc(sizeof(pthread_mutex_t));

    if (mutex_handle != NULL) {
        int ret = pthread_mutex_init((pthread_mutex_t *)mutex_handle,
                                     NULL); // default attributes
        if (ret) {
            SVT_ERROR("Failed to initialize mutex: %s\n", strerror(ret));
            free(mutex_handle);
            mutex_handle = NULL;
        }
    }
#endif

    return mutex_handle;
}

/***************************************
* svt_release_mutex
***************************************/
EbErrorType svt_release_mutex(EbHandle mutex_handle) {
    EbErrorType return_error;

#ifdef _WIN32
    return_error = !ReleaseMutex((HANDLE)mutex_handle) ? EB_ErrorMutexUnresponsive
                                                       : EB_ErrorNone;
#else
    return_error = pthread_mutex_unlock((pthread_mutex_t *)mutex_handle) ? EB_ErrorMutexUnresponsive
                                                                         : EB_ErrorNone;
#endif

    return return_error;
}

/***************************************
* svt_block_on_mutex
***************************************/
EbErrorType svt_block_on_mutex(EbHandle mutex_handle) {
    EbErrorType return_error;

#ifdef _WIN32
    return_error = WaitForSingleObject((HANDLE)mutex_handle, INFINITE) ? EB_ErrorMutexUnresponsive
                                                                       : EB_ErrorNone;
#else
    return_error = pthread_mutex_lock((pthread_mutex_t *)mutex_handle) ? EB_ErrorMutexUnresponsive
                                                                       : EB_ErrorNone;
#endif

    return return_error;
}

/***************************************
* svt_destroy_mutex
***************************************/
EbErrorType svt_destroy_mutex(EbHandle mutex_handle) {
    EbErrorType return_error;

#ifdef _WIN32
    return_error = CloseHandle((HANDLE)mutex_handle) ? EB_ErrorDestroyMutexFailed : EB_ErrorNone;
#else
    return_error = pthread_mutex_destroy((pthread_mutex_t *)mutex_handle) ? EB_ErrorDestroyMutexFailed
                                                                          : EB_ErrorNone;
    free(mutex_handle);
#endif

    return return_error;
}

/*
set an atomic variable to an input value
*/
void svt_aom_atomic_set_u32(AtomicVarU32 *var, uint32_t in) {
    svt_block_on_mutex(var->mutex);
    var->obj = in;
    svt_release_mutex(var->mutex);
}

/*
create condition variable

Condition variables are synchronization primitives that enable
threads to wait until a particular condition occurs.
Condition variables enable threads to atomically release
a lock(mutex) and enter the sleeping state.
it could be seen as a combined: wait and release mutex
*/
EbErrorType svt_create_cond_var(CondVar *cond_var) {
    cond_var->val = 0;

#ifdef _WIN32
    InitializeCriticalSection(&cond_var->cs);
    InitializeConditionVariable(&cond_var->cv);
    return EB_ErrorNone;
#else
    int ret = pthread_mutex_init(&cond_var->m_mutex, NULL);
    if (ret) {
        return ret;
    }

    ret = pthread_cond_init(&cond_var->m_cond, NULL);
    if (ret) {
        (void)pthread_mutex_destroy(&cond_var->m_mutex);
        return ret;
    }

    return EB_ErrorNone;
#endif
}

/*
set a condition variable to the new value
*/
EbErrorType svt_set_cond_var(CondVar *cond_var, int32_t newval) {
#ifdef _WIN32
    EnterCriticalSection(&cond_var->cs);
    cond_var->val = newval;
    WakeAllConditionVariable(&cond_var->cv);
    LeaveCriticalSection(&cond_var->cs);
    return EB_ErrorNone;
#else
    int ret = pthread_mutex_lock(&cond_var->m_mutex);
    if (ret)
        return ret;

    cond_var->val = newval;

    ret = pthread_cond_broadcast(&cond_var->m_cond);
    {
        int unlock_ret = pthread_mutex_unlock(&cond_var->m_mutex);
        if (!ret && unlock_ret)
            ret = unlock_ret;
    }

    return ret;
#endif
}

/*
wait until the cond variable changes to a value
different than input
*/
EbErrorType svt_wait_cond_var(CondVar *cond_var, int32_t input) {
#ifdef _WIN32
    EnterCriticalSection(&cond_var->cs);
    while (cond_var->val == input) {
        SleepConditionVariableCS(&cond_var->cv, &cond_var->cs, INFINITE);
    }
    LeaveCriticalSection(&cond_var->cs);
    return EB_ErrorNone;
#else
    int ret = pthread_mutex_lock(&cond_var->m_mutex);
    if (ret)
        return ret;

    while (cond_var->val == input) {
        ret = pthread_cond_wait(&cond_var->m_cond, &cond_var->m_mutex);
        if (ret) {
            (void)pthread_mutex_unlock(&cond_var->m_mutex);
            return ret;
        }
    }

    ret = pthread_mutex_unlock(&cond_var->m_mutex);
    return ret;
#endif
}

void svt_run_once(OnceType *once_control, OnceFn init_routine) {
#ifdef _WIN32
    InitOnceExecuteOnce(once_control, init_routine, NULL, NULL);
#else
    pthread_once(once_control, init_routine);
#endif
}
