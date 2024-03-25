// SPDX-License-Identifier: Zlib
//
// Copyright (c) 2024 Antonio Niño Díaz

#include <stdio.h>

#include <nds.h>
#include <nds/cothread.h>

#include <sys/lock.h>

void *__aeabi_read_tp(void);

struct __lock {
    comutex_t mutex;
    int recursion;
    void *thread_owner;
    bool used;
};

__attribute__((noinline, noreturn))
THUMB_CODE static void retarget_lock_crash(void)
{
    // This function causes an undefined instruction exception to crash the CPU
    // in a controlled way.
    //
    // Opcodes defined as "Undefined Instruction" by the ARM architecture:
    //
    // - Thumb: 0xE800 | (ignored & 0x7FF)
    // - ARM:   0x?6000010 | (ignored & 0x1FFFFEF)
    //
    // It's better to use Thumb because that way the ARM7 can jump to it with a
    // BL instruction instead of BL+BX (BLX only exists in the ARM9).
    asm volatile inline(".hword 0xE800 | 0xBAD");
    __builtin_unreachable();
}

#define MAX_LOCKS   (FOPEN_MAX + 1)
static struct __lock locks[MAX_LOCKS];

struct __lock __lock___libc_recursive_mutex;

void __retarget_lock_init_recursive(_LOCK_T *lock)
{
    int selection = -1;
    for (int i = 0; i < MAX_LOCKS; i++)
    {
        if (!locks[i].used)
        {
            selection = i;
            break;
        }
    }

    if (selection == -1)
        retarget_lock_crash();

    *lock = &(locks[selection]);

    comutex_init(&((*lock)->mutex));
    (*lock)->recursion = 0;
    (*lock)->thread_owner = NULL; //__aeabi_read_tp();
    (*lock)->used = true;
}

void __retarget_lock_close_recursive(_LOCK_T lock)
{
    for (int i = 0; i < MAX_LOCKS; i++)
    {
        if (&locks[i] == lock)
        {
            locks[i].used = false;
            return;
        }
    }

    retarget_lock_crash();
}

void __retarget_lock_acquire_recursive(_LOCK_T lock)
{
    void *this_thread = __aeabi_read_tp();

    // Loop until this thread owns the lock, or no thread owns it
    while (1)
    {
        bool acquired = false;

        comutex_acquire(&(lock->mutex));

        if (lock->thread_owner == this_thread)
        {
            lock->recursion++;
            acquired = true;
        }
        else if (lock->thread_owner == NULL)
        {
            lock->thread_owner = this_thread;
            lock->recursion++;
            acquired = true;
        }

        comutex_release(&(lock->mutex));

        if (acquired)
            break;

        cothread_yield();
    }
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
    void *this_thread = __aeabi_read_tp();
    bool acquired = false;

    comutex_acquire(&(lock->mutex));

    if (lock->thread_owner == this_thread)
    {
        lock->recursion++;
        acquired = true;
    }
    else if (lock->thread_owner == NULL)
    {
        lock->thread_owner = this_thread;
        lock->recursion++;
        acquired = true;
    }

    comutex_release(&(lock->mutex));

    return acquired;
}

void __retarget_lock_release_recursive(_LOCK_T lock)
{
    void *this_thread = __aeabi_read_tp();

    comutex_acquire(&(lock->mutex));

    if (lock->thread_owner != this_thread)
        retarget_lock_crash();

    lock->recursion--;

    if (lock->recursion == 0)
        lock->thread_owner = NULL;

    comutex_release(&(lock->mutex));
}

void __retarget_lock_init(_LOCK_T *lock)
{
    __retarget_lock_init_recursive(lock);
}

void __retarget_lock_close(_LOCK_T lock)
{
    __retarget_lock_close_recursive(lock);
}

void __retarget_lock_acquire(_LOCK_T lock)
{
    __retarget_lock_acquire_recursive(lock);
}

int __retarget_lock_try_acquire(_LOCK_T lock)
{
    return __retarget_lock_try_acquire_recursive(lock);
}

void __retarget_lock_release(_LOCK_T lock)
{
    __retarget_lock_release_recursive(lock);
}
