#include "filedescriptor.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <struct.h>

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

static struct slab* slab_head = NULL;
static union slot* freelist = NULL;

int sfs_filedescriptor_init() {
  slab_head = malloc(sizeof(struct slab));
  if (slab_head == NULL) {
    return -1;
  }
  slab_head->next = NULL;

  int num_slots = fldsiz(slab, data) / sizeof(union slot);
  for (int i = 0; i < num_slots - 1; ++i) {
    slab_head->data[i].n.fd = i;
    slab_head->data[i].n.next = &slab_head->data[i + 1];
  }
  slab_head->data[num_slots - 1].n.fd = num_slots - 1;
  slab_head->data[num_slots - 1].n.next = NULL;

  freelist = &slab_head->data[0];

  return 0;
}

void sfs_filedescriptor_deinit() {
  assert(slab_head != NULL);
  struct slab* next = slab_head->next;
  while (next != NULL) {
    free(slab_head);
    slab_head = next;
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

struct sfs_fd* sfs_filedescriptor_allocate() {
  if (freelist != NULL) {
    union slot* s = freelist;
    freelist = freelist->n.next;
    return init_slot_as_sfs_fd(s);
  }

  // iterate to the end of the list and increment `num_fds`
  int slots_per_slab = fldsiz(slab, data) / sizeof(union slot);
  int num_fds_already = slots_per_slab;
  struct slab* it = slab_head;
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
  freelist = &new_slab->data[0];
  return sfs_filedescriptor_allocate();
}

void sfs_filedescriptor_free(struct sfs_fd* fd) {
  union slot* s = (union slot*)((char*)fd - offsetof(union slot, s));
  int int_fd = s->s.fd;
  s->n.fd = int_fd;
  s->n.next = freelist;
  freelist = s;
}
