// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; i++)
    initlock(&kmems[i].lock, "kmem");
  freerange(end, (void*)PHYSTOP); 
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  
  push_off();
  int id = cpuid();
  pop_off();
  
  acquire(&kmems[id].lock); // 不用把内存归还
  r->next = kmems[id].freelist;
  kmems[id].freelist = r;
  release(&kmems[id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off(); // 获取cpuid时要关中断
  int id = cpuid();
  pop_off();
  
  acquire(&kmems[id].lock);
  r = kmems[id].freelist;
  if(r) // 如果列表不为空就直接从列表中获取
    kmems[id].freelist = r->next;
  else // 如果列表为空则需要从其他CPU的列表中获取，在用完后直接放回自己的列表
  {
    for (int i = 0; i < NCPU; i++)
    {
      if (i == id) // 要判断是否是自己的CPU，避免重复上锁导致死锁
       continue;
      acquire(&kmems[i].lock);
      if (!kmems[i].freelist)
      {
        release(&kmems[i].lock);
        continue;
      }
      r = kmems[i].freelist; // 从其他cpu的列表获取
      kmems[i].freelist = r->next; // 从列表中删除
      release(&kmems[i].lock);
      break;
    }
  }
  release(&kmems[id].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}