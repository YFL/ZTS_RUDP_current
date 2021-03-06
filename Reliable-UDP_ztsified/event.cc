/* event.c -*-mode: C; c-file-style:"cc-mode";-*- */
/*----------------------------------------------------------------------------
File: event.c
Description: Rudp event handling: registering file descriptors and timeouts
and eventloop using the select() system call.
Author: Olof Hagsand and Peter Sj�din
CVS Version: $Id: event.c,v 1.3 2007/05/03 10:46:06 psj Exp $
This is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This softare is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

*--------------------------------------------------------------------------*/
/*
* Register functions to be called either when input on a file descriptor,
* or as a a result of a timeout.
* The callback function has the following signature:
* int fn(int fd, void* arg)
* fd is the file descriptor where the input was received (for timeouts, this
* contains no information).
* arg is an argument given when the callback was registered.
* If the return value of the callback is < 0, it is treated as an unrecoverable
* error, and the program is terminated.
*/

#ifdef HAVE_CONFIG_H
#include "config.h" /* generated by config & autoconf */
#endif

#include <vector>
#include <logging_lock.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include "event.h"

/*
* Internal types to handle eventloop
*/
struct event_data {
    struct event_data *next; /* next in list */
    int (*callback)(int, void*); /* callback function */
    enum {FILE_EVENT, TIME_EVENT} e_type; /* type of event */
    int fd; /* File descriptor */
    zts_timeval timeout; /* Timeout */
    void *callback_arg; /* function argument */
    char id[32]; /* string for identification/debugging */
};

/*
* Internal variables
*/

#define GET_VARIABLE_NAME(x) std::string(#x)

std::mutex fd_handlers_mut;
static event_data *fd_event_handlers = NULL;
std::mutex timeout_handlers_mut;
static event_data *timeout_event_handlers = NULL;

/*
* Sort into internal event list
* Given an absolute timestamp, register function to call.
*/
int
event_timeout(zts_timeval t, int (*fn)(int, void*), void *callback_arg, const char *id)
{
    struct event_data *new_event_handler, *iterator, **list_head_addr;

    new_event_handler = (struct event_data *)malloc(sizeof(struct event_data));
    if (new_event_handler == NULL)
    {
        perror("event_timeout: malloc");
        return -1;
    }
    memset(new_event_handler, 0, sizeof(event_data));
    strcpy(new_event_handler->id, id);
    new_event_handler->callback = fn;
    new_event_handler->callback_arg = callback_arg;
    new_event_handler->e_type = event_data::TIME_EVENT;
    new_event_handler->timeout = t;

    /* Sort into right place */
    LoggingLock timeout_handlers_ll(timeout_handlers_mut, GET_VARIABLE_NAME(timeout_handlers_mut), "./log");
    list_head_addr = &timeout_event_handlers;
    for (iterator = timeout_event_handlers; iterator; iterator = iterator->next)
    {
        if (timercmp(&new_event_handler->timeout, &iterator->timeout, <))
            break;
        list_head_addr = &iterator->next;
    }
    new_event_handler->next = iterator;
    *list_head_addr = new_event_handler;
    return 0;
}

/*
* Deregister a rudp event.
*/
static int
event_delete(event_data **firstp, int (*fn)(int, void*), void *arg)
{
    struct event_data *iterator, **e_prev;

    e_prev = firstp;
    for (iterator = *firstp; iterator; iterator = iterator->next)
    {
        if (fn == iterator->callback && arg == iterator->callback_arg)
        {
            *e_prev = iterator->next;
            free(iterator);
            return 0;
        }
        e_prev = &iterator->next;
    }
    /* Not found */
    return -1;
}

/*
* Deregister a rudp event.
*/
int event_timeout_delete(int (*fn)(int, void*), void *arg)
{
    LoggingLock timeout_handlers_ll(timeout_handlers_mut, GET_VARIABLE_NAME(timeout_handlers_mut), "./log");
    return event_delete(&timeout_event_handlers, fn, arg);
}

/*
* Deregister a file descriptor event.
*/
int event_fd_delete(int (*fn)(int, void*), void *arg)
{
    LoggingLock fd_handlers_ll(fd_handlers_mut, GET_VARIABLE_NAME(fd_handlers_mut), "./log");
    return event_delete(&fd_event_handlers, fn, arg);
}

/*
* Register a callback function when something occurs on a file descriptor.
* When an input event occurs on file desriptor <fd>,
* the function <fn> shall be called with argument <arg>.
* <str> is a debug string for logging.
*/
int event_fd(int fd, int (*fn)(int, void*), void *callback_arg, const char *id)
{
    struct event_data *new_event_handler;

    new_event_handler = (struct event_data *)malloc(sizeof(struct event_data));
    if (new_event_handler==NULL)
    {
        perror("event_fd: malloc");
        return -1;
    }
    memset(new_event_handler, 0, sizeof(struct event_data));
    strcpy(new_event_handler->id, id);
    new_event_handler->fd = fd;
    new_event_handler->callback = fn;
    new_event_handler->callback_arg = callback_arg;
    new_event_handler->e_type = event_data::FILE_EVENT;
    LoggingLock fd_handlers_lg(fd_handlers_mut, GET_VARIABLE_NAME(fd_handlers_mut), "./log");
    new_event_handler->next = fd_event_handlers;
    fd_event_handlers = new_event_handler;
    return 0;
}


/*
* Rudp event loop.
* Dispatch file descriptor events (and timeouts) by invoking callbacks.
*/
int
eventloop()
{
    struct event_data *iterator;
    std::vector<zts_pollfd> fds;
    int n;
    struct timeval time_diff, current_time;

    LoggingLock timeout_handlers_ll(timeout_handlers_mut, GET_VARIABLE_NAME(timeout_handlers_mut), "./log"), fd_handlers_ll(fd_handlers_mut, GET_VARIABLE_NAME(fd_handlers_mut), "./log");
    while (fd_event_handlers || timeout_event_handlers)
    {
        fds.clear();
        for (iterator=fd_event_handlers; iterator; iterator=iterator->next)
        {
            if (iterator->e_type == event_data::FILE_EVENT)
            {
                // FD_SET(iterator->e_fd, &fdset);
                zts_pollfd pollfd;
                pollfd.fd = iterator->fd;
                pollfd.events = 0;
                pollfd.events |= ZTS_POLLIN;
                fds.push_back(pollfd);
            }
        }

        if (timeout_event_handlers)
        {
            gettimeofday(&current_time, NULL);
            timersub(&timeout_event_handlers->timeout, &current_time, &time_diff);
            if (time_diff.tv_sec < 0)
                n = 0;
            else
            {
                // n = select(FD_SETSIZE, &fdset, NULL, NULL, &time_diff);
                int timeout = time_diff.tv_sec * 1000l + time_diff.tv_usec / 1000l;
                n = zts_poll(&fds[0], fds.size(), timeout);
            }
        }
        else
        {
            // n = select(FD_SETSIZE, &fdset, NULL, NULL, NULL);
            n = zts_poll(&fds[0], fds.size(), 0);
        }

        if (n == -1)
        {
            continue;
        }
        if (n == 0 && timeout_event_handlers)
        { /* Timeout */
            iterator = timeout_event_handlers;
            timeout_event_handlers = timeout_event_handlers->next;
            #ifdef DEBUG
                fprintf(stderr, "eventloop: timeout : %s[arg: %x]\n",
                e->e_string, (int)e->e_arg);
            #endif /* DEBUG */
            if ((*iterator->callback)(0, iterator->callback_arg) < 0)
            {
                return -1;
            }
            switch(iterator->e_type)
            {
                case event_data::TIME_EVENT:
                    free(iterator);
                    break;
                default:
                    fprintf(stderr, "eventloop: illegal e_type:%d\n", iterator->e_type);
            }
            continue;
        }
        iterator = fd_event_handlers;
        while (iterator)
        {
            bool readable = false;
            for(auto &fd : fds)
            {
                if(fd.fd == iterator->fd && (fd.revents & ZTS_POLLIN))
                {
                    readable = true;
                }
            }
            if (iterator->e_type == event_data::FILE_EVENT && readable)
            {
                #ifdef DEBUG
                fprintf(stderr, "eventloop: socket rcv: %s[fd: %d arg: %x]\n",
                e->e_string, e->e_fd, (int)e->e_arg);
                #endif /* DEBUG */
                if ((*iterator->callback)(iterator->fd, iterator->callback_arg) < 0)
                {
                    return -1;
                }
            }
            iterator = iterator->next;
        }

        timeout_handlers_ll.unlock();
        fd_handlers_ll.unlock();
        zts_delay_ms(50);
        timeout_handlers_ll.lock();
        fd_handlers_ll.lock();
    }
    #ifdef DEBUG
        fprintf(stderr, "eventloop: returning 0\n");
    #endif /* DEBUG */
    return 0;
}
