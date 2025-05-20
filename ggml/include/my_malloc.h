#include <stddef.h>

// handle C++
#ifdef __cplusplus
extern "C" {
#endif
void* my_malloc(size_t size);
void my_free(void *ptr);
#ifdef __cplusplus
}
#endif