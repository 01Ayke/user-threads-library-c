#include "thread.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#define ELEM_PER_THREAD 4096UL
#define LEAF_SIZE 2048UL

struct sum_task
{
  const unsigned int *arr;
  size_t lo;
  size_t hi;
  int budget;
};

static unsigned long
sum_sequential (const unsigned int *arr, size_t lo, size_t hi)
{
  size_t i;
  unsigned long sum = 0;

  for (i = lo; i < hi; i++)
    sum += arr[i];
  return sum;
}

static void *
sum_worker (void *arg)
{
  struct sum_task *task = (struct sum_task *)arg;
  size_t len = task->hi - task->lo;

  if (task->budget <= 1 || len <= LEAF_SIZE)
    return (void *)sum_sequential (task->arr, task->lo, task->hi);

  size_t mid = task->lo + (len / 2);
  int left_budget = task->budget / 2;
  int right_budget = task->budget - left_budget;
  struct sum_task *left_task = malloc (sizeof (*left_task));
  struct sum_task right_task;
  thread_t th;
  int err;
  void *left_res = NULL;
  unsigned long right_sum;

  assert (left_task != NULL);
  left_task->arr = task->arr;
  left_task->lo = task->lo;
  left_task->hi = mid;
  left_task->budget = left_budget;

  right_task.arr = task->arr;
  right_task.lo = mid;
  right_task.hi = task->hi;
  right_task.budget = right_budget;

  err = thread_create (&th, sum_worker, left_task);
  assert (!err);

  right_sum = (unsigned long)sum_worker (&right_task);

  err = thread_join (th, &left_res);
  assert (!err);
  free (left_task);

  return (void *)((unsigned long)left_res + right_sum);
}

int
main (int argc, char *argv[])
{
  int nthreads;
  size_t n;
  unsigned int *arr;
  size_t i;
  struct sum_task root;
  unsigned long expected;
  unsigned long result;
  struct timeval tv1, tv2;
  unsigned long us;

  if (argc < 2)
    {
      printf ("argument manquant: nombre de threads\n");
      return EXIT_FAILURE;
    }

  nthreads = atoi (argv[1]);
  if (nthreads <= 0)
    {
      printf ("nombre de threads invalide\n");
      return EXIT_FAILURE;
    }

  n = (size_t)nthreads * ELEM_PER_THREAD;
  arr = malloc (n * sizeof (*arr));
  if (arr == NULL)
    return EXIT_FAILURE;

  for (i = 0; i < n; i++)
    arr[i] = (unsigned int)((i % 97) + 1);

  expected = sum_sequential (arr, 0, n);

  root.arr = arr;
  root.lo = 0;
  root.hi = n;
  root.budget = nthreads;

  gettimeofday (&tv1, NULL);
  result = (unsigned long)sum_worker (&root);
  gettimeofday (&tv2, NULL);

  us = (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec);

  if (result != expected)
    {
      printf ("somme invalide: %lu (attendu %lu)\n", result, expected);
      free (arr);
      return EXIT_FAILURE;
    }

  printf ("somme tableau n=%lu avec budget=%d en %lu us\n", (unsigned long)n,
          nthreads, us);
  printf ("GRAPH;24;%d;%lu\n", nthreads, us);

  free (arr);
  return EXIT_SUCCESS;
}
