#include "filedescriptor.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef HAVE_STRUCT_H
#include <struct.h>
#else
#define fldsiz(type, member) sizeof(((struct type*)0)->member)
#endif

struct node {
  int fd;
  union slot* next;
};

union slot {
  struct sfs_fd s;
  struct node n;
};

/**
 * a slab is a linked list of slots
 *
 * if a slot is allocated, its memory is interpreted as a `struct sfs_fd`
 *
 * if a slot is unallocated, its memory is a `struct node` in a freelist of
 * free descriptors
 */
struct slab {
  struct slab* next;
  union slot data[4088];
};

struct pool {
  struct slab* slab_head;
  union slot* freelist;
};

void* sfs_filedescriptor_pool_init() {
  struct pool* pool = malloc(sizeof(struct pool));
  if (pool == NULL) {
    return NULL;
  }

  pool->slab_head = malloc(sizeof(struct slab));
  if (pool->slab_head == NULL) {
    free(pool);
    return NULL;
  }
  pool->slab_head->next = NULL;

  int num_slots = fldsiz(slab, data) / sizeof(union slot);
  for (int i = 0; i < num_slots - 1; ++i) {
    pool->slab_head->data[i].n.fd = i;
    pool->slab_head->data[i].n.next = &pool->slab_head->data[i + 1];
  }
  pool->slab_head->data[num_slots - 1].n.fd = num_slots - 1;
  pool->slab_head->data[num_slots - 1].n.next = NULL;

  pool->freelist = &pool->slab_head->data[0];

  return pool;
}

void sfs_filedescriptor_pool_deinit(void* arg) {
  struct pool* pool = (struct pool*)arg;

  assert(pool != NULL);
  assert(pool->slab_head != NULL);
  struct slab* next = pool->slab_head->next;
  while (next != NULL) {
    free(pool->slab_head);
    pool->slab_head = next;
    next = next->next;
  }
}

/**
 * takes a freelist node and returns a valid filedescriptor
 */
static struct sfs_fd* init_slot_as_sfs_fd(union slot* s) {
  int fd = s->n.fd;
  s->s.fd = fd;

  // TODO: do other fd init here

  return &s->s;
}

struct sfs_fd* sfs_filedescriptor_allocate(void* arg) {
  struct pool* pool = (struct pool*)arg;
  assert(pool != NULL);

  if (pool->freelist != NULL) {
    union slot* s = pool->freelist;
    pool->freelist = pool->freelist->n.next;
    return init_slot_as_sfs_fd(s);
  }

  // iterate to the end of the list and increment `num_fds`
  int slots_per_slab = fldsiz(slab, data) / sizeof(union slot);
  int num_fds_already = slots_per_slab;
  struct slab* it = pool->slab_head;
  while (it->next != NULL) {
    num_fds_already += slots_per_slab;
    it = it->next;
  }

  struct slab* new_slab = malloc(sizeof(struct slab));
  if (new_slab == NULL) {
    return NULL;
  }
  new_slab->next = NULL;
  it->next = new_slab;

  for (int i = 0; i < slots_per_slab - 1; ++i) {
    new_slab->data[i].n.fd = num_fds_already + i;
    new_slab->data[i].n.next = &new_slab->data[i + 1];
  }
  new_slab->data[slots_per_slab - 1].n.fd =
      num_fds_already + slots_per_slab - 1;
  new_slab->data[slots_per_slab - 1].n.next = NULL;

  // update freelist and do evil tail recurse which will work now
  pool->freelist = &new_slab->data[0];
  return sfs_filedescriptor_allocate(pool);
}

struct sfs_fd* sfs_filedescriptor_get_from_fd(void* arg, int fd) {
  struct pool* pool = (struct pool*)arg;
  assert(pool != NULL);
  assert(fd >= 0);

  int slots_per_slab = fldsiz(slab, data) / sizeof(union slot);
  int slab_index = fd / slots_per_slab;
  int slot_index = fd % slots_per_slab;

  struct slab* s = pool->slab_head;
  for (int i = 0; i < slab_index; ++i) {
    if (s == NULL) {
      return NULL;
    }
    s = s->next;
  }
  if (s == NULL) {
    return NULL;
  }

  return &s->data[slot_index].s;
}

void sfs_filedescriptor_free(void* arg, struct sfs_fd* fd) {
  struct pool* pool = (struct pool*)arg;
  assert(pool != NULL);

  union slot* s = (union slot*)((char*)fd - offsetof(union slot, s));
  int int_fd = s->s.fd;
  s->n.fd = int_fd;
  s->n.next = pool->freelist;
  pool->freelist = s;
}
