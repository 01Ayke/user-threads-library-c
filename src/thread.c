#include "thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/ucontext.h>
#include <ucontext.h>

#define STACK_SIZE (512 * 1024)

enum thread_state {
  NEW,
  READY,
  RUNNABLE,
  RUNNING,
  BLOCKED,
  ZOMBIE,
  DONE
};

struct thread_struct {
  enum thread_state state;
  void *retval;
  ucontext_t ctx;
  void *stack;
  TAILQ_ENTRY(thread_struct) entries;

};

//
TAILQ_HEAD(thread_queue, thread_struct) ready_queue;
static struct thread_struct *current_thread = NULL;

//
__attribute__((constructor))
static void thread_init(void) {
  TAILQ_INIT(&ready_queue);
  struct thread_struct *m = malloc(sizeof(struct thread_struct));
  m->stack  = NULL;
  m->retval = NULL;
  m->state  = RUNNING;
  getcontext(&m->ctx);
  current_thread = m;
}

__attribute__((destructor))
static void thread_destroy(void) {
  free(current_thread);
}

//
thread_t thread_self(void) {
  return (thread_t) current_thread;
}

//
static void thread_start(void *(*func)(void *), void *arg) {
  void *ret = func(arg);
  thread_exit(ret);
}

//
int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg) {
  struct thread_struct *t = malloc(sizeof(struct thread_struct));
  if (!t) return -1;
  t->stack = malloc(STACK_SIZE);
  if (!t->stack) {
    free(t);
    return -1;
  }
  getcontext(&t->ctx);
  t->ctx.uc_stack.ss_sp   = t->stack;
  t->ctx.uc_stack.ss_size = STACK_SIZE;
  t->ctx.uc_stack.ss_flags = 0;
  t->ctx.uc_link          = NULL;
  t->retval = NULL;
  t->state  = READY;
  makecontext(&t->ctx, (void (*)(void)) thread_start, 2, func, funcarg);
  TAILQ_INSERT_TAIL(&ready_queue, t, entries);
  *newthread = (thread_t) t;
  return 0;
}

//
int thread_yield(void) {
  struct thread_struct *prev = current_thread;
  if (TAILQ_EMPTY(&ready_queue)) return 0;
  struct thread_struct *next = TAILQ_FIRST(&ready_queue);
  TAILQ_REMOVE(&ready_queue, next, entries);

  if (prev->state == RUNNING) {
    prev->state = READY;
    TAILQ_INSERT_TAIL(&ready_queue, prev, entries);
  }

  next->state = RUNNING;
  current_thread = next;
  swapcontext(&prev->ctx, &next->ctx);
  return 0;
}

//
void thread_exit(void *retval) {
  current_thread->retval = retval;
  current_thread->state = DONE;
  if (!TAILQ_EMPTY(&ready_queue)) {
    struct thread_struct *next = TAILQ_FIRST(&ready_queue);
    TAILQ_REMOVE(&ready_queue, next, entries);
    next->state = RUNNING;
    current_thread = next;
    setcontext(&next->ctx);
  }
  if (TAILQ_EMPTY(&ready_queue)) exit(0);
}

//
int thread_join(thread_t thread, void **retval) {
  struct thread_struct *t = (struct thread_struct *) thread;
  while (t->state != DONE) thread_yield();
  if (retval) *retval = t->retval;
  free(t->stack);
  free(t);
  return 0;
}

//
int thread_mutex_init(thread_mutex_t *mutex) { (void)mutex; return 0; }
//
int thread_mutex_destroy(thread_mutex_t *mutex) { (void)mutex; return 0; }
//
int thread_mutex_lock(thread_mutex_t *mutex) { (void)mutex; return 0; }
//
int thread_mutex_unlock(thread_mutex_t *mutex) { (void)mutex; return 0; }