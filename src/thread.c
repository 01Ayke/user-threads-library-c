#include "thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <ucontext.h>
#include <valgrind/valgrind.h>

#define STACK_SIZE (512 * 1024)

enum thread_state { READY, RUNNING, BLOCKED, DONE };

struct thread_struct {
  enum thread_state state;
  void *retval;

  ucontext_t ctx;
  void *stack;
  int vg_stack_id;

  void *(*func)(void *);
  void *funcarg;

  int joined;
  struct thread_struct *joiner;

  TAILQ_ENTRY(thread_struct) entries;
};

TAILQ_HEAD(thread_queue, thread_struct);
static struct thread_queue ready_queue;

static struct thread_struct main_thread;
static struct thread_struct *current_thread = NULL;

static struct thread_struct *scheduler_pick_next(void) {
  struct thread_struct *next;

  if (TAILQ_EMPTY(&ready_queue))
    return NULL;

  next = TAILQ_FIRST(&ready_queue);
  TAILQ_REMOVE(&ready_queue, next, entries);
  return next;
}

static void thread_wrapper(void) {
  void *ret = current_thread->func(current_thread->funcarg);
  thread_exit(ret);
}

__attribute__((constructor))

static void
thread_init(void) {
  TAILQ_INIT(&ready_queue);

  main_thread.state = RUNNING;
  main_thread.retval = NULL;
  main_thread.stack = NULL;
  main_thread.vg_stack_id = 0;
  main_thread.func = NULL;
  main_thread.funcarg = NULL;
  main_thread.joined = 0;
  main_thread.joiner = NULL;

  getcontext(&main_thread.ctx);
  current_thread = &main_thread;
}

thread_t thread_self(void) { return (thread_t)current_thread; }

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg) {
  struct thread_struct *t;

  if (newthread == NULL || func == NULL)
    return -1;

  t = malloc(sizeof(*t));
  if (t == NULL)
    return -1;

  t->stack = malloc(STACK_SIZE);
  if (t->stack == NULL) {
    free(t);
    return -1;
  }

  t->state = READY;
  t->retval = NULL;
  t->func = func;
  t->funcarg = funcarg;
  t->joined = 0;
  t->joiner = NULL;

  getcontext(&t->ctx);
  t->ctx.uc_stack.ss_sp = t->stack;
  t->ctx.uc_stack.ss_size = STACK_SIZE;
  t->ctx.uc_stack.ss_flags = 0;
  t->ctx.uc_link = NULL;

  t->vg_stack_id = VALGRIND_STACK_REGISTER(t->ctx.uc_stack.ss_sp,
                                           (char *)t->ctx.uc_stack.ss_sp +
                                               t->ctx.uc_stack.ss_size);

  makecontext(&t->ctx, thread_wrapper, 0);

  TAILQ_INSERT_TAIL(&ready_queue, t, entries);
  *newthread = (thread_t)t;

  return 0;
}

int thread_yield(void) {
  struct thread_struct *prev;
  struct thread_struct *next;

  prev = current_thread;
  next = scheduler_pick_next();

  if (next == NULL)
    return 0;

  if (prev->state == RUNNING) {
    prev->state = READY;
    TAILQ_INSERT_TAIL(&ready_queue, prev, entries);
  }

  next->state = RUNNING;
  current_thread = next;

  swapcontext(&prev->ctx, &next->ctx);
  return 0;
}

void thread_exit(void *retval) {
  struct thread_struct *prev;
  struct thread_struct *next;

  prev = current_thread;
  prev->retval = retval;
  prev->state = DONE;

  // Reveiller le thread qui attendait eventuellement sur nous
  if (prev->joiner != NULL) {
    if (prev->joiner->state == BLOCKED) {
      prev->joiner->state = READY;
      TAILQ_INSERT_TAIL(&ready_queue, prev->joiner, entries);
    }
  }

  next = scheduler_pick_next();

  if (next == NULL) {

    exit(0);
  }

  next->state = RUNNING;
  current_thread = next;

  setcontext(&next->ctx);

  abort();
}

int thread_join(thread_t thread, void **retval) {
  struct thread_struct *target;
  struct thread_struct *self;
  struct thread_struct *next;

  target = (struct thread_struct *)thread;
  self = current_thread;

  if (target == NULL)
    return -1;

  if (target == self)
    return -1;

  if (target->joined)
    return -1;

  target->joined = 1;

  if (target->state != DONE) {
    target->joiner = self;

    self->state = BLOCKED;

    next = scheduler_pick_next();
    if (next == NULL) {
      self->state = RUNNING;
      target->joiner = NULL;
      target->joined = 0;
      return -1;
    }

    next->state = RUNNING;
    current_thread = next;

    swapcontext(&self->ctx, &next->ctx);
  }

  if (target->state != DONE)
    return -1;

  if (retval != NULL)
    *retval = target->retval;

  if (target->stack != NULL) {
    VALGRIND_STACK_DEREGISTER(target->vg_stack_id);
    free(target->stack);
    target->stack = NULL;
  }

  free(target);
  return 0;
}

int thread_mutex_init(thread_mutex_t *mutex) {
  (void)mutex;
  return 0;
}

int thread_mutex_destroy(thread_mutex_t *mutex) {
  (void)mutex;
  return 0;
}

int thread_mutex_lock(thread_mutex_t *mutex) {
  (void)mutex;
  return 0;
}

int thread_mutex_unlock(thread_mutex_t *mutex) {
  (void)mutex;
  return 0;
}
