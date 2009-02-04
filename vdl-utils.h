#ifndef VDL_UTILS_H
#define VDL_UTILS_H

#include <sys/types.h>
#include "vdl.h"

void vdl_utils_linkmap_print (void);


// allocate/free memory
void *vdl_utils_malloc (size_t size);
void vdl_utils_free (void *buffer, size_t size);
#define vdl_utils_new(type) \
  (type *) vdl_utils_malloc (sizeof (type))
#define vdl_utils_delete(v) \
  vdl_utils_free (v, sizeof(*v))

// string manipulation functions
int vdl_utils_strisequal (const char *a, const char *b);
int vdl_utils_strlen (const char *str);
char *vdl_utils_strdup (const char *str);
void vdl_utils_memcpy (void *dst, const void *src, size_t len);
void vdl_utils_memset(void *s, int c, size_t n);
char *vdl_utils_strconcat (const char *str, ...);
const char *vdl_utils_getenv (const char **envp, const char *value);

// convenience function
int vdl_utils_exists (const char *filename);

// manipulate string lists.
struct VdlStringList *vdl_utils_strsplit (const char *value, char separator);
void vdl_utils_str_list_free (struct VdlStringList *list);
struct VdlStringList *vdl_utils_str_list_reverse (struct VdlStringList *list);
struct VdlStringList * vdl_utils_str_list_append (struct VdlStringList *start, struct VdlStringList *end);

// logging

// manipulate lists of files
void vdl_utils_file_list_free (struct VdlFileList *list);
struct VdlFileList *vdl_utils_file_list_copy (struct VdlFileList *list);
struct VdlFileList *vdl_utils_file_list_append_one (struct VdlFileList *list, 
						 struct VdlFile *item);
struct VdlFileList *vdl_utils_file_list_append (struct VdlFileList *start, 
					     struct VdlFileList *end);
void vdl_utils_file_list_unicize (struct VdlFileList *list);
unsigned long vdl_utils_align_down (unsigned long v, unsigned long align);
unsigned long vdl_utils_align_up (unsigned long v, unsigned long align);

#define vdl_utils_max(a,b)(((a)>(b))?(a):(b))

ElfW(Phdr) *vdl_utils_search_phdr (ElfW(Phdr) *phdr, int phnum, int type);

#endif /* VDL_UTILS_H */