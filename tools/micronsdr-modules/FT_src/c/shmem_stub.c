/* lib/shmem.f90 declares the QSharedMemory wrappers of WSJT-X; the FT8 decoder only calls
   shmem_lock / shmem_unlock (around its spectrum output to the GUI): nothing to lock here */
#include <stdbool.h>
#include <stddef.h>
bool shmem_create(int size) { (void)size; return false; }
void shmem_setkey(const char *key) { (void)key; }
bool shmem_attach(void) { return false; }
void *shmem_address(void) { return NULL; }
int shmem_size(void) { return 0; }
bool shmem_lock(void) { return true; }
bool shmem_unlock(void) { return true; }
bool shmem_detach(void) { return true; }
