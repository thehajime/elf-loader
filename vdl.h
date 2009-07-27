#ifndef VDL_H
#define VDL_H

#include <stdint.h>
#include <sys/types.h>
#include <elf.h>
#include <link.h>
#include "alloc.h"
#include "system.h"
#include "futex.h"

#if __ELF_NATIVE_CLASS == 32
#define ELFW_R_SYM ELF32_R_SYM
#define ELFW_R_TYPE ELF32_R_TYPE
#define ELFW_ST_BIND(val) ELF32_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF32_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF32_ST_INFO(bind,type)
#else
#define ELFW_R_SYM ELF64_R_SYM
#define ELFW_R_TYPE ELF64_R_TYPE
#define ELFW_ST_BIND(val) ELF64_ST_BIND(val)
#define ELFW_ST_TYPE(val) ELF64_ST_TYPE(val)
#define ELFW_ST_INFO(bind, type) ELF64_ST_INFO(bind,type)
#endif


struct VdlFileList
{
  struct VdlFile *item;
  struct VdlFileList *next;
};

enum VdlLookupType
{
  // indicates that lookups within this object should be performed
  // using the global scope only and that local scope should be ignored.
  LOOKUP_GLOBAL_ONLY,
  LOOKUP_GLOBAL_LOCAL,
  LOOKUP_LOCAL_GLOBAL,
  LOOKUP_LOCAL_ONLY,
};

// the file_ prefix indicates that this variable identifies
// a file offset from the start of the file.
// the mem_ prefix indicates that this variable indentifies
// a pointer in memory
// the _align postfix indicates that this variable identifies
// a variable aligned to the underlying aligning constraint.
struct VdlFileMap
{
  unsigned long file_start_align;
  unsigned long file_size_align;
  // mem_start_align is the memory equivalent of file_start_align
  unsigned long mem_start_align;
  // mem_size_align is the memory equivalent of file_size_align
  unsigned long mem_size_align;
  // mem_zero_start locates the start of a zero-memset area.
  unsigned long mem_zero_start;
  unsigned long mem_zero_size;
  // mem_anon_start_align locates the start of a set of 
  // zero-initialized anon pages.
  unsigned long mem_anon_start_align;
  unsigned long mem_anon_size_align;
};

struct VdlFileInfo
{
  // vaddr of DYNAMIC program header
  unsigned long dynamic;

  struct VdlFileMap ro_map;
  struct VdlFileMap rw_map;
};

enum {
  VDL_GC_BLACK = 0,
  VDL_GC_GREY = 1,
  VDL_GC_WHITE = 2
};


struct VdlFile
{
  // The following fields are part of the ABI. Don't change them
  unsigned long load_base;
  // the fullname of this file.
  char *filename;
  // pointer to the PT_DYNAMIC area
  unsigned long dynamic;
  struct VdlFile *next;
  struct VdlFile *prev;

  // The following fields are not part of the ABI
  // This count indicates how many users hold a reference
  // to this file either because the file has been dlopened
  // (the dlopen increases the ref count), or because this
  // file is a dependency of another file, or because this
  // file was loaded during the loader initialization.
  uint32_t count;
  char *name;
  dev_t st_dev;
  ino_t st_ino;
  struct VdlFileMap ro_map;
  struct VdlFileMap rw_map;
  // indicates if the deps field has been initialized correctly
  uint32_t deps_initialized : 1;
  // indicates if the has_tls field has been initialized correctly
  uint32_t tls_initialized : 1;
  // indicates if the ELF initializers of this file
  // have been called.
  uint32_t init_called : 1;
  // indicates if the ELF finalizers of this file
  // have been called.
  uint32_t fini_called : 1;
  // indicates if this file has been relocated
  uint32_t reloced : 1;
  // indicates if we patched this file for some
  // nastly glibc-isms.
  uint32_t patched : 1;
  // indicates if this represents the main executable.
  uint32_t is_executable : 1;
  uint32_t gc_color : 2;
  // indicates if this file has a TLS program entry
  // If so, all tls_-prefixed variables are valid.
  uint32_t has_tls : 1;
  // indicates whether this file is part of the static TLS
  // block
  uint32_t tls_is_static : 1;
  // start of TLS block template
  unsigned long tls_tmpl_start;
  // size of TLS block template
  unsigned long tls_tmpl_size;
  // the generation number when the tls template
  // of this number was initialized
  unsigned long tls_tmpl_gen;
  // size of TLS block zero area, located
  // right after the area initialized with the
  // TLS block template
  unsigned long tls_init_zero_size;
  // alignment requirements for the TLS block area
  unsigned long tls_align;
  // TLS module index associated to this file
  // this is the index in each thread's DTV
  // XXX: this member _must_ be at the same offset as l_tls_modid 
  // in the glibc linkmap to allow gdb to work (gdb accesses this
  // field for tls variable lookups)
  unsigned long tls_index;
  // offset from thread pointer to this module
  // this field is valid only for modules which
  // are loaded at startup.
  signed long tls_offset;
  // the list of objects in which we resolved a symbol 
  // from a GOT/PLT relocation. This field is used
  // during garbage collection from vdl_gc to detect
  // the set of references an object holds to another one
  // and thus avoid unloading an object which is held as a
  // reference by another object.
  struct VdlFileList *gc_symbols_resolved_in;
  enum VdlLookupType lookup_type;
  struct VdlContext *context;
  struct VdlFileList *local_scope;
  // list of files this file depends upon. 
  // equivalent to the content of DT_NEEDED.
  struct VdlFileList *deps;
  uint32_t gc_depth;
};

struct VdlStringList
{
  char *str;
  struct VdlStringList *next;
};
enum VdlState {
  VDL_CONSISTENT,
  VDL_ADD,
  VDL_DELETE
};

struct VdlContext
{
  struct VdlContext *prev;
  struct VdlContext *next;
  struct VdlFileList *global_scope;
  // return the symbol to lookup instead of the input symbol
  const char *(*remap_symbol) (const char *name);
  // return the library to lookup instead of the input library
  const char *(*remap_lib) (const char *name);
  // These variables are used by all .init functions
  // _some_ libc .init functions make use of these
  // 3 arguments so, even though no one else uses them, 
  // we have to pass them around.
  // The arrays below are private copies exclusively used
  // by the loader.
  int argc;
  char **argv;
  char **envp;  
};

struct Vdl
{
  // the following fields are part of the gdb/libc ABI. Don't touch them.
  int version; // always 1
  struct VdlFile *link_map;
  void (*breakpoint)(void);
  enum VdlState state;
  unsigned long interpreter_load_base;
  // the following fields are not part of the ABI
  uint32_t logging;
  // The list of directories to search for binaries
  // in DT_NEEDED entries.
  struct VdlStringList *search_dirs;
  // The data structure used by the memory allocator
  // all heap memory allocations through vdl_alloc
  // and vdl_utils_free end up here.
  struct Alloc alloc;
  uint32_t bind_now : 1;
  struct VdlContext *contexts;
  unsigned long tls_gen;
  unsigned long tls_static_size;
  unsigned long tls_static_align;
  unsigned long tls_n_dtv;
  struct futex futex;
};


extern struct Vdl g_vdl;

struct VdlContext *vdl_context_new (int argc, const char **argv, const char **envp);
void vdl_context_delete (struct VdlContext *context);
struct VdlFile *vdl_file_new (unsigned long load_base,
				 const struct VdlFileInfo *info,
				 const char *filename, 
				 const char *name,
				 struct VdlContext *context);
void vdl_files_delete (struct VdlFileList *files);
struct VdlFile *vdl_file_map_single (struct VdlContext *context, 
				    const char *filename, 
				    const char *name);
struct VdlFile *vdl_file_map_single_maybe (struct VdlContext *context,
					   const char *requested_filename,
					   struct VdlFileList **loaded);
int vdl_file_map_deps (struct VdlFile *item, struct VdlFileList **loaded);
unsigned long vdl_file_get_entry_point (struct VdlFile *file);

struct VdlFile *vdl_file_new_main (unsigned long phnum,
				   ElfW(Phdr)*phdr,
				   int argc, 
				   const char **argv, 
				   const char **envp);

ElfW(Dyn) *vdl_file_get_dynamic (const struct VdlFile *file, unsigned long tag);

char *vdl_search_filename (const char *name);
int vdl_get_file_info (uint32_t phnum,
		       ElfW(Phdr) *phdr,
		       struct VdlFileInfo *info);
ElfW(Dyn) *vdl_file_get_dynamic (const struct VdlFile *file, unsigned long tag);
unsigned long vdl_file_get_dynamic_v (const struct VdlFile *file, unsigned long tag);
unsigned long vdl_file_get_dynamic_p (const struct VdlFile *file, unsigned long tag);
#endif /* VDL_H */
