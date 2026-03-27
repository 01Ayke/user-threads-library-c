#include <stdio.h>
#include <assert.h>
#include "thread.h"



static void * thfunc(void *id)
{


  printf("%s terminé\n", (char*) id);
  thread_exit(NULL);
  return (void*) 0xdeadbeef; /* unreachable, shut up the compiler */
}

int main()
{
  thread_t th1;
  void *res;
  int err, i;

  err = thread_create(&th1, thfunc, "fils1");
  assert(!err);;



  err = thread_join(th1, &res);
  assert(!err);
  assert(res == NULL);

  printf("main terminé\n");
  return 0;
}