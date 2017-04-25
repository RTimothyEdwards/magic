/*************************************************************************
 *
 *  (c) 1997 California Institute of Technology
 *  Department of Computer Science
 *  Pasadena, CA 91125.
 *  All Rights Reserved
 *
 *  $Id: child.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/
#include <stdio.h>
#include <string.h>
#ifndef	SYSV
#include <sys/wait.h>
#endif /* SYSV */
#include <errno.h>

#include "utils/utils.h"
#include "utils/malloc.h"


/*------------------------------------------------------------------------
 *  Internal list management
 *------------------------------------------------------------------------*/

struct Wait_List {
  int pid;
  int status;
  int pending;
  struct Wait_List *next;
};

static struct Wait_List *wl = NULL;

static
void
make_finished (pid, status)
     int pid;
     int *status;
{
  struct Wait_List *l;

  l = wl;
  while (l) {
    if (l->pid == pid) {
      l->pending = 0;
      l->status = *status;
      return;
    }
    l = l->next;
  }
  return;
}

static
void
add_pending_to_list (pid)
     int pid;
{
  struct Wait_List *l;

  l = (struct Wait_List *) mallocMagic((unsigned)(sizeof(struct Wait_List)));
  l->next = wl;
  l->pid = pid;
  l->status = -1;
  l->pending = 1;
  wl = l;
  return;
}

static
int
find_pid (pid,status)
     int pid;
     int *status;
{
  struct Wait_List *l;
  l = wl;
  while (l) {
    if (l->pid == pid) {
      *status = l->status;
      return l->pending;
    }
    l = l->next;
  }
  return -1;
}

static
int
get_next (status)
     int *status;
{
  struct Wait_List *l, *prev;
  int pid;

  prev = NULL;
  l = wl;
  while (l) {
    if (!l->pending) {
      pid = l->pid;
      *status = l->status;
      if (prev)
	prev->next = l->next;
      else
	wl = l->next;
      freeMagic(l);
      return pid;
    }
    prev = l;
    l = l->next;
  }
  return -1;
}

static
void
delete_from_list (pid)
     int pid;
{
  struct Wait_List *l, *prev;
  
  prev = NULL;
  l = wl;
  while (l) {
    if (l->pid == pid) {
      if (prev)
	prev->next = l->next;
      else
	wl = l->next;
      freeMagic(l);
      return;
    }
    prev = l;
    l = l->next;
  }
}

     

/*------------------------------------------------------------------------
 *
 *  Wait --
 *
 *      Wait for a process to terminate. Returns the pid that you waited
 *      for, along with the exit status in *status.
 *
 *      Returns -1 on an attempt to wait for a pid which wasn't ever
 *      forked.
 *
 *      If you want to wait for a particular pid, use WaitPid instead
 *      of Wait.
 *
 *  Results:
 *      The pid that finished, along with the status.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

int
Wait (status)
     int *status;
{
  int pid;
  int p_status = 0;

  pid = get_next(&p_status);
  if (pid != -1) {
    if (status)
      *status = p_status;
    return pid;
  }
  if (wl) {
    do {
	pid = wait(&p_status);
    } while (pid < 0 && errno == EINTR);
    delete_from_list (pid);
    if (status)
      *status = p_status;
    return pid;
  }
  else
    /* nothing to wait for */
    return -1;
}


/*------------------------------------------------------------------------
 *
 *  WaitPid --
 *
 *      Wait for a particular process to terminate.
 *
 *      Returns -1 on an attempt to wait for a pid which wasn't ever
 *      forked.
 *
 *      If you want to wait for a particular pid, use WaitPid instead
 *      of Wait.
 *
 *  Results:
 *      The pid that finished, along with the status.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

int
WaitPid (pid,status)
     int pid;
     int *status;
{
  int stat;
  int n_pid, n_status;

  stat = find_pid (pid,&n_status);
  if (stat == -1)
    return -1;
  if (stat == 0) {
    delete_from_list (pid);
    if (status)
      *status = n_status;
    return 1;
  }
  do {
    do {
	n_pid = wait(&n_status);
    } while (n_pid < 0 && errno == EINTR);
    make_finished (n_pid, &n_status);
  } while (n_pid != pid && n_pid != -1);
  if (n_pid == -1) return -1;
  delete_from_list (pid);
  if (status)
    *status = n_status;
  return 1;
}


/*------------------------------------------------------------------------
 *
 *  ForkChildAdd --
 *
 *      Fork, along with updating the wait list structure.
 *
 *  Results:
 *      The pid that finished, along with the status.
 *
 *  Side effects:
 *      None.
 *
 *------------------------------------------------------------------------
 */

void
ForkChildAdd (pid)
     int pid;
{
  add_pending_to_list (pid);
}

