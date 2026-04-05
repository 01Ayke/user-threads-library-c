#include "thread.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define ELEM_PER_THREAD 2048UL
#define LEAF_SIZE 1024UL

struct sort_task
{
  unsigned int *arr;
  unsigned int *tmp;
  size_t lo;
  size_t hi;
  int budget;
};

static int
cmp_u32 (const void *a, const void *b)
{
  const unsigned int va = *(const unsigned int *)a;
  const unsigned int vb = *(const unsigned int *)b;

  if (va < vb)
    return -1;
  if (va > vb)
    return 1;
  return 0;
}

static void
merge_ranges (unsigned int *arr, unsigned int *tmp, size_t lo, size_t mid,
              size_t hi)
{
  size_t i = lo;
  size_t j = mid;
  size_t k = lo;

  while (i < mid && j < hi)
    {
      if (arr[i] <= arr[j])
        tmp[k++] = arr[i++];
      else
        tmp[k++] = arr[j++];
    }

  while (i < mid)
    tmp[k++] = arr[i++];
  while (j < hi)
    tmp[k++] = arr[j++];

  memcpy (&arr[lo], &tmp[lo], (hi - lo) * sizeof (*arr));
}

static void *
sort_worker (void *arg)
{
  struct sort_task *task = (struct sort_task *)arg;
  size_t len = task->hi - task->lo;

  if (len <= 1)
    return NULL;

  if (task->budget <= 1 || len <= LEAF_SIZE)
    {
      qsort (&task->arr[task->lo], len, sizeof (task->arr[0]), cmp_u32);
      return NULL;
    }

  size_t mid = task->lo + (len / 2);
  int left_budget = task->budget / 2;
  int right_budget = task->budget - left_budget;
  struct sort_task *left_task = malloc (sizeof (*left_task));
  struct sort_task right_task;
  thread_t th;
  int err;

  assert (left_task != NULL);
  left_task->arr = task->arr;
  left_task->tmp = task->tmp;
  left_task->lo = task->lo;
  left_task->hi = mid;
  left_task->budget = left_budget;

  right_task.arr = task->arr;
  right_task.tmp = task->tmp;
  right_task.lo = mid;
  right_task.hi = task->hi;
  right_task.budget = right_budget;

  err = thread_create (&th, sort_worker, left_task);
  assert (!err);

  sort_worker (&right_task);

  err = thread_join (th, NULL);
  assert (!err);
  free (left_task);

  merge_ranges (task->arr, task->tmp, task->lo, mid, task->hi);
  return NULL;
}

int
main (int argc, char *argv[])
{
  int nthreads;
  size_t n;
  unsigned int *arr;
  unsigned int *tmp;
  size_t i;
  unsigned long checksum_before = 0;
  unsigned long checksum_after = 0;
  struct sort_task root;
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
  tmp = malloc (n * sizeof (*tmp));
  if (arr == NULL || tmp == NULL)
    {
      free (arr);
      free (tmp);
      return EXIT_FAILURE;
    }

  for (i = 0; i < n; i++)
    {
      arr[i] = (unsigned int)((1103515245ULL * (i + 12345) + 12345) & 0x7fffffff);
      checksum_before += arr[i];
    }

  root.arr = arr;
  root.tmp = tmp;
  root.lo = 0;
  root.hi = n;
  root.budget = nthreads;

  gettimeofday (&tv1, NULL);
  sort_worker (&root);
  gettimeofday (&tv2, NULL);

  us = (tv2.tv_sec - tv1.tv_sec) * 1000000 + (tv2.tv_usec - tv1.tv_usec);

  for (i = 1; i < n; i++)
    {
      if (arr[i - 1] > arr[i])
        {
          printf ("tri invalide à i=%lu\n", (unsigned long)i);
          free (arr);
          free (tmp);
          return EXIT_FAILURE;
        }
    }

  for (i = 0; i < n; i++)
    checksum_after += arr[i];

  if (checksum_before != checksum_after)
    {
      printf ("checksum invalide: %lu vs %lu\n", checksum_before, checksum_after);
      free (arr);
      free (tmp);
      return EXIT_FAILURE;
    }

  printf ("tri fusion parallèle n=%lu avec budget=%d en %lu us\n",
          (unsigned long)n, nthreads, us);
  printf ("GRAPH;34;%d;%lu\n", nthreads, us);

  free (arr);
  free (tmp);
  return EXIT_SUCCESS;
}
