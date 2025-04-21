#ifndef MEM_H
#define MEM_H

#include "types.h"

#define MEM_TEMP_OBJECTS_MAX 8
#ifdef __vita__
#define MEM_HUNK_BYTES (16 * 1024 * 1024)
#else
#define MEM_HUNK_BYTES (4 * 1024 * 1024)
#endif

void *mem_bump(uint32_t size);
void *mem_mark(void);
void mem_reset(void *p);

void *mem_temp_alloc(uint32_t size);
void mem_temp_free(void *p);
void mem_temp_check(void);

#endif
