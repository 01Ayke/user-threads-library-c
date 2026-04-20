#include "thread.h"
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#else
#define VALGRIND_STACK_REGISTER(sp, sz) 0
#define VALGRIND_STACK_DEREGISTER(id) ((void)(id))
#endif

#define STACK_SIZE (4096)

/*
 * x86_64 glibc jmp_buf layout:
 *   [6] = rsp (pointer-mangled)
 *   [7] = rip (pointer-mangled)
 * Mangling: stored = rol17(value ^ cookie), cookie at %fs:0x30
 */
#define JB_RSP 6
#define JB_PC  7

static uintptr_t jmp_cookie;

static uintptr_t
ptr_mangle (uintptr_t ptr)
{
  uintptr_t x = ptr ^ jmp_cookie;
  return (x << 17) | (x >> 47); /* rotate left 17 */
}

/* Set up a fresh jmp_buf to start func on the given stack. */
static void
setup_jmp_buf (jmp_buf buf, void *stack, size_t stack_size, void (*func) (void))
{
  uintptr_t sp;
  memset (buf, 0, sizeof (jmp_buf));
  sp = (uintptr_t) stack + stack_size;
  sp &= ~(uintptr_t) 15; /* 16-byte align */
  sp -= 8;               /* x86-64 ABI: rsp = 16n-8 at function entry */
  ((uintptr_t *) buf)[JB_RSP] = ptr_mangle (sp);
  ((uintptr_t *) buf)[JB_PC] = ptr_mangle ((uintptr_t) func);
}

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

  jmp_buf ctx;
  void *stack;
  int vg_stack_id;

  void *(*func) (void *);
  void *funcarg;

  int joined;
  struct thread_struct *joiner;
  struct thread_struct *waiting_for;

  int heap_allocated;

  TAILQ_ENTRY (thread_struct) entries;
  TAILQ_ENTRY (thread_struct) mutex_entries;
};

TAILQ_HEAD (thread_queue, thread_struct);
static struct thread_queue ready_queue;

static struct thread_struct main_thread;
static struct thread_struct *current_thread = NULL;

static jmp_buf cleanup_ctx;
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
  /* Read glibc pointer-mangling cookie (x86-64: stored at %fs:0x30) */
  __asm__ volatile ("mov %%fs:0x30, %0" : "=r" (jmp_cookie));

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

  setup_jmp_buf (cleanup_ctx, cleanup_stack, sizeof (cleanup_stack),
                 cleanup_routine);

  current_thread = &main_thread;
}

thread_t
thread_self (void)
{
  return (thread_t) current_thread;
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

  t->vg_stack_id
    = VALGRIND_STACK_REGISTER (t->stack, (char *) t->stack + STACK_SIZE);
  setup_jmp_buf (t->ctx, t->stack, STACK_SIZE, thread_wrapper);

  TAILQ_INSERT_TAIL (&ready_queue, t, entries);
  *newthread = (thread_t) t;
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

  if (setjmp (prev->ctx) == 0)
    longjmp (next->ctx, 1);
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

  if (prev->joiner != NULL && prev->joiner->state == BLOCKED)
    {
      prev->joiner->state = READY;
      TAILQ_INSERT_TAIL (&ready_queue, prev->joiner, entries);
    }

  next = scheduler_pick_next ();

  if (next == NULL)
    {
      if (prev->heap_allocated)
        last_exited_thread = prev;
      else
        last_exited_thread = NULL;
      current_thread = NULL;
      longjmp (cleanup_ctx, 1);
      abort ();
    }

  next->state = RUNNING;
  current_thread = next;
  longjmp (next->ctx, 1);
  __builtin_unreachable ();
}

int
thread_join (thread_t thread, void **retval)
{
  struct thread_struct *target;
  struct thread_struct *self;
  struct thread_struct *next;

  target = (struct thread_struct *) thread;
  self = current_thread;

  if (target == NULL)
    return -1;

  if (target == self)
    return -2;

  if (target->joined)
    return -3;

#ifdef DEADLOCK_DETECTION
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

      if (setjmp (self->ctx) == 0)
        longjmp (next->ctx, 1);
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

  self = current_thread;
  self->state = BLOCKED;
  TAILQ_INSERT_TAIL (&mutex->wait_queue, self, mutex_entries);

  next = scheduler_pick_next ();
  if (next == NULL)
    {
      TAILQ_REMOVE (&mutex->wait_queue, self, mutex_entries);
      self->state = RUNNING;
      return EDEADLK;
    }

  next->state = RUNNING;
  current_thread = next;

  if (setjmp (self->ctx) == 0)
    longjmp (next->ctx, 1);
  return 0;
}

int
thread_mutex_unlock (thread_mutex_t *mutex)
{
  struct thread_struct *waiter;

  if (!TAILQ_EMPTY (&mutex->wait_queue))
    {
      waiter = TAILQ_FIRST (&mutex->wait_queue);
      TAILQ_REMOVE (&mutex->wait_queue, waiter, mutex_entries);
      waiter->state = READY;
      TAILQ_INSERT_TAIL (&ready_queue, waiter, entries);
    }
  else
    {
      mutex->locked = 0;
    }
  return 0;
}
