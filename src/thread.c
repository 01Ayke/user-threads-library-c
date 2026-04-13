#include "thread.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <ucontext.h>
#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#else
#define VALGRIND_STACK_REGISTER(sp, sz) 0
#define VALGRIND_STACK_DEREGISTER(id) ((void)(id))
#endif
// TODO: enlever les valeurs magiques dans les codes d'erreurs

#define STACK_SIZE (4096)

enum thread_state
{
  READY,
  RUNNING,
  BLOCKED,
  DONE
};

struct thread_struct
{
  enum thread_state state;
  void *retval;

  ucontext_t ctx;
  void *stack;
  int vg_stack_id;

  void *(*func) (void *);
  void *funcarg;

  int joined;
  struct thread_struct *joiner;
  struct thread_struct *waiting_for;

  int heap_allocated;

  TAILQ_ENTRY (thread_struct) entries;
  TAILQ_ENTRY (thread_struct)
  mutex_entries;
};

TAILQ_HEAD (thread_queue, thread_struct);
static struct thread_queue ready_queue;

static struct thread_struct main_thread;
static struct thread_struct *current_thread = NULL;

static ucontext_t cleanup_ctx;
static char cleanup_stack[STACK_SIZE];
static struct thread_struct *last_exited_thread = NULL;

static struct thread_struct *
scheduler_pick_next (void)
{
  struct thread_struct *next;

  if (TAILQ_EMPTY (&ready_queue))
    return NULL;

  next = TAILQ_FIRST (&ready_queue);
  TAILQ_REMOVE (&ready_queue, next, entries);
  return next;
}

static void
thread_wrapper (void)
{
  void *ret = current_thread->func (current_thread->funcarg);
  thread_exit (ret);
}
static void
free_thread (struct thread_struct *t)
{
  if (t == NULL)
    return;

  if (t->stack != NULL)
    {
      VALGRIND_STACK_DEREGISTER (t->vg_stack_id);
      free (t->stack);
      t->stack = NULL;
    }

  if (t->heap_allocated)
    free (t);
}

__attribute__ ((destructor)) static void
cleanup_routine (void)
{
  struct thread_struct *t;

  if (last_exited_thread != NULL)
    {
      free_thread (last_exited_thread);
      last_exited_thread = NULL;
    }

  while (!TAILQ_EMPTY (&ready_queue))
    {
      t = TAILQ_FIRST (&ready_queue);
      TAILQ_REMOVE (&ready_queue, t, entries);
      free_thread (t);
    }

  exit (0);
}

__attribute__ ((constructor)) static void
thread_init (void)
{
  TAILQ_INIT (&ready_queue);

  main_thread.state = RUNNING;
  main_thread.retval = NULL;
  main_thread.stack = NULL;
  main_thread.vg_stack_id = 0;
  main_thread.func = NULL;
  main_thread.funcarg = NULL;
  main_thread.joined = 0;
  main_thread.joiner = NULL;
  main_thread.waiting_for = NULL;
  main_thread.heap_allocated = 0;

  getcontext (&cleanup_ctx);
  cleanup_ctx.uc_stack.ss_sp = cleanup_stack;
  cleanup_ctx.uc_stack.ss_size = sizeof (cleanup_stack);
  cleanup_ctx.uc_stack.ss_flags = 0;
  cleanup_ctx.uc_link = NULL;
  makecontext (&cleanup_ctx, cleanup_routine, 0);

  getcontext (&main_thread.ctx);
  current_thread = &main_thread;
}

thread_t
thread_self (void)
{
  return (thread_t)current_thread;
}

int
thread_create (thread_t *newthread, void *(*func) (void *), void *funcarg)
{
  struct thread_struct *t;

  if (newthread == NULL || func == NULL)
    return -1;

  t = malloc (sizeof (*t));
  if (t == NULL)
    return -2;

  t->stack = malloc (STACK_SIZE);
  if (t->stack == NULL)
    {
      free (t);
      return -3;
    }

  t->state = READY;
  t->retval = NULL;
  t->func = func;
  t->funcarg = funcarg;
  t->joined = 0;
  t->joiner = NULL;
  t->waiting_for = NULL;
  t->heap_allocated = 1;

  getcontext (&t->ctx);
  t->ctx.uc_stack.ss_sp = t->stack;
  t->ctx.uc_stack.ss_size = STACK_SIZE;
  t->ctx.uc_stack.ss_flags = 0;
  t->ctx.uc_link = NULL;

  t->vg_stack_id = VALGRIND_STACK_REGISTER (t->ctx.uc_stack.ss_sp,
                                            (char *)t->ctx.uc_stack.ss_sp
                                                + t->ctx.uc_stack.ss_size);

  makecontext (&t->ctx, thread_wrapper, 0);

  TAILQ_INSERT_TAIL (&ready_queue, t, entries);
  *newthread = (thread_t)t;
  return 0;
}

int
thread_yield (void)
{
  struct thread_struct *prev;
  struct thread_struct *next;

  prev = current_thread;
  next = scheduler_pick_next ();

  if (next == NULL)
    return 0;

  if (prev->state == RUNNING)
    {
      prev->state = READY;
      TAILQ_INSERT_TAIL (&ready_queue, prev, entries);
    }

  next->state = RUNNING;
  current_thread = next;

  swapcontext (&prev->ctx, &next->ctx);
  return 0;
}

void
thread_exit (void *retval)
{
  struct thread_struct *prev;
  struct thread_struct *next;

  prev = current_thread;
  prev->retval = retval;
  prev->state = DONE;

  // Reveiller le thread qui attendait eventuellement sur nous
  if (prev->joiner != NULL)
    {
      if (prev->joiner->state == BLOCKED)
        {
          prev->joiner->state = READY;
          TAILQ_INSERT_TAIL (&ready_queue, prev->joiner, entries);
        }
    }

  next = scheduler_pick_next ();

  if (next == NULL)
    {
      if (prev->heap_allocated)
        last_exited_thread = prev;
      else
        last_exited_thread = NULL;

      current_thread = NULL;
      setcontext (&cleanup_ctx);
      abort ();
    }

  next->state = RUNNING;
  current_thread = next;

  setcontext (&next->ctx);
  __builtin_unreachable ();
}

int
thread_join (thread_t thread, void **retval)
{
  struct thread_struct *target;
  struct thread_struct *self;
  struct thread_struct *next;

  target = (struct thread_struct *)thread;
  self = current_thread;

  if (target == NULL)
    return -1;

  if (target == self)
    return -2;

  if (target->joined)
    return -3;

#ifdef DEADLOCK_DETECTION
  // INFO: Deadlock detection
  struct thread_struct *cursor;
  if (target->state != DONE)
    {
      cursor = target;
      while (cursor != NULL)
        {
          if (cursor->waiting_for == self)
            return EDEADLK;
          cursor = cursor->waiting_for;
        }
    }
#endif

  target->joined = 1;

  if (target->state != DONE)
    {
      target->joiner = self;
      self->waiting_for = target;
      self->state = BLOCKED;

      next = scheduler_pick_next ();
      if (next == NULL)
        {
          self->state = RUNNING;
          self->waiting_for = NULL;
          target->joiner = NULL;
          target->joined = 0;
          return -4;
        }

      next->state = RUNNING;
      current_thread = next;

      swapcontext (&self->ctx, &next->ctx);
      self->waiting_for = NULL;
    }

  if (target->state != DONE)
    return -5;

  if (retval != NULL)
    *retval = target->retval;

  if (target->stack != NULL)
    {
      VALGRIND_STACK_DEREGISTER (target->vg_stack_id);
      free (target->stack);
      target->stack = NULL;
    }

  if (target->heap_allocated)
    free (target);
  return 0;
}

int
thread_mutex_init (thread_mutex_t *mutex)
{
  mutex->locked = 0;
  TAILQ_INIT (&mutex->wait_queue);
  return 0;
}

int
thread_mutex_destroy (thread_mutex_t *mutex)
{
  // erreur si des threads attendent encore sur ce mutex
  return TAILQ_EMPTY (&mutex->wait_queue) ? 0 : -1;
}

int
thread_mutex_lock (thread_mutex_t *mutex)
{
  struct thread_struct *self;
  struct thread_struct *next;

  if (!mutex->locked)
    {
      mutex->locked = 1;
      return 0;
    }

  // mutex pris -> on se bloque dans la wait_queue du mutex
  self = current_thread;
  self->state = BLOCKED;
  TAILQ_INSERT_TAIL (&mutex->wait_queue, self, mutex_entries);

  next = scheduler_pick_next ();
  if (next == NULL)
    {
      // ready queue vide + thread courant bloqué -> deadlock
      TAILQ_REMOVE (&mutex->wait_queue, self, mutex_entries);
      self->state = RUNNING;
      return EDEADLK;
    }

  next->state = RUNNING;
  current_thread = next;
  swapcontext (&self->ctx, &next->ctx);
  return 0;
}

int
thread_mutex_unlock (thread_mutex_t *mutex)
{
  struct thread_struct *waiter;

  if (!TAILQ_EMPTY (&mutex->wait_queue))
    {
      // transfert direct de propriété au premier thread en attente
      waiter = TAILQ_FIRST (&mutex->wait_queue);
      TAILQ_REMOVE (&mutex->wait_queue, waiter, mutex_entries);
      waiter->state = READY;
      TAILQ_INSERT_TAIL (&ready_queue, waiter, entries);
      // INFO: locked reste à 1 car le waiter en est maintenant propriétaire */
    }
  else
    {
      mutex->locked = 0;
    }
  return 0;
}
