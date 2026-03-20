#include "thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/ucontext.h>
#include <ucontext.h>

enum thread_state {
  NEW,
  RUNNABLE,
  RUNNING,
  BLOCKED,
  ZOMBIE,
  TERMINATED,
};

struct thread_struct {
  enum thread_state state;
  void *retval;
  ucontext_t ctx;
  void *stack; // pointer to allocated stack memory
};

static thread_t current_thread;

TAILQ_HEAD(runq, thread_t);
struct runq runq_head;
void thread_init(void) { TAILQ_INIT(&runq_head); }

thread_t thread_self(void) { return current_thread; }

int thread_create(thread_t *newthread, void *(*func)(void *), void *funcarg) {}

/* passer la main à un autre thread.
 */
extern int thread_yield(void);

/* attendre la fin d'exécution d'un thread.
 * la valeur renvoyée par le thread est placée dans *retval.
 * si retval est NULL, la valeur de retour est ignorée.
 */
extern int thread_join(thread_t thread, void **retval);

/* terminer le thread courant en renvoyant la valeur de retour retval.
 * cette fonction ne retourne jamais.
 *
 * L'attribut noreturn aide le compilateur à optimiser le code de
 * l'application (élimination de code mort). Attention à ne pas mettre
 * cet attribut dans votre interface tant que votre thread_exit()
 * n'est pas correctement implémenté (il ne doit jamais retourner).
 */
extern void thread_exit(void *retval) __attribute__((__noreturn__));
