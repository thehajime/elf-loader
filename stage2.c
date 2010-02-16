#include "system.h"
#include "vdl.h"
#include "vdl-utils.h"
#include "vdl-alloc.h"
#include "vdl-log.h"
#include "vdl-list.h"
#include "vdl-reloc.h"
#include "vdl-dl.h"
#include "glibc.h"
#include "valgrind.h"
#include "gdb.h"
#include "machine.h"
#include "stage2.h"
#include "vdl-gc.h"
#include "vdl-tls.h"
#include "vdl-init-fini.h"
#include "vdl-sort.h"
#include "vdl-context.h"


static struct VdlList *
system_search_dirs_new (void)
{
  return vdl_utils_splitpath (machine_get_system_search_dirs ());
}

static struct VdlFile *
interpreter_new (unsigned long load_base, 
		 const char *pt_interp,
		 struct VdlContext *context)
{
  /* We make many assumptions here:
   *   - The loader is an ET_DYN
   *   - The loader has been compile-time linked at base address 0
   *   - The first PT_LOAD map of the interpreter contains the elf header 
   *     and program headers.
   *   
   * Consequently, we can infer that the load_base in fact points to
   * the first PT_LOAD map of the interpreter which means that load_base
   * in fact points to the elf header itself.
   */
  ElfW(Ehdr) *header = (ElfW(Ehdr) *)load_base;
  ElfW(Phdr) *phdr = (ElfW(Phdr) *) (header->e_phoff + load_base);
  struct VdlFileInfo info;
  if (!vdl_get_file_info (header->e_phnum, phdr, &info))
    {
      VDL_LOG_ERROR ("Could not obtain file info for interpreter\n", 1);
      goto error;
    }
  const char *full_filename;
  if (pt_interp == 0)
    {
      full_filename = LDSO_SONAME;
    }
  else
    {
      // It's important to initialize the filename of the interpreter
      // entry in the linkmap to the PT_INTERP of the main binary for
      // gdb. gdb initializes its first linkmap with an entry which describes
      // the loader with a filename equal to PT_INTERP so, if we don't use
      // the same value, gdb will incorrectly believe that the loader entry
      // has been removed which can lead to certain bad things to happen
      // in the first call to r_debug_state.
      full_filename = pt_interp;
    }
  struct VdlFile *file = vdl_file_new (load_base, &info, 
				       full_filename,
				       LDSO_SONAME,
				       context);
  // the interpreter has already been reloced during stage1, so, 
  // we must be careful to not relocate it twice.
  file->reloced = 1;

  // XXX: theoretically, there is no need to map deps for this
  if (!vdl_file_map_deps (file, 0))
    {
      goto error;
    }

  file->count++;
  return file;
 error:
  return 0;
}

struct VdlList *
ld_preload_list_new (struct VdlContext *context, const char **envp)
{
  // add the LD_PRELOAD binary if it is specified somewhere.
  // We must do this _before_ adding the dependencies of the main 
  // binary to the link map to ensure that the symbol scope of 
  // the main binary is correct, that is, that symbols are 
  // resolved first within the LD_PRELOAD binary, before every
  // other library, but after the main binary itself.
  struct VdlList *retval = vdl_list_new ();
  const char *ld_preload = vdl_utils_getenv (envp, "LD_PRELOAD");
  struct VdlList *list = vdl_utils_strsplit (ld_preload, ':');
  void **cur;
  for (cur = vdl_list_begin (list); 
       cur != vdl_list_end (list); 
       cur = vdl_list_next (cur))
    {
      char *filename = *cur;
      // search the requested program
      char *ld_preload_filename = vdl_search_filename (filename, 0, 0);
      if (ld_preload_filename == 0)
	{
	  VDL_LOG_ERROR ("Could not find LD_PRELOAD: %s\n", filename);
	  goto error;
	}
      // map it in memory.
      struct VdlFile *ld_preload_file = vdl_file_map_single (context, ld_preload_filename, 
							     ld_preload);
      if (ld_preload_file == 0)
	{
	  VDL_LOG_ERROR ("Unable to load LD_PRELOAD: %s\n", ld_preload_filename);
	  vdl_alloc_free (ld_preload_filename);
	  goto error;
	}
      vdl_alloc_free (ld_preload_filename);
      ld_preload_file->count++;
      vdl_list_push_back (retval, ld_preload_file);
    }
 error:
  vdl_utils_str_list_delete (list);
  return retval;
}

static void
setup_env_vars (const char **envp)
{
  // populate search_dirs from LD_LIBRARY_PATH
  const char *ld_lib_path = vdl_utils_getenv (envp, "LD_LIBRARY_PATH");
  struct VdlList *list = vdl_utils_splitpath (ld_lib_path);
  vdl_list_insert_range (g_vdl.search_dirs,
			 vdl_list_begin (g_vdl.search_dirs),
			 vdl_list_begin (list),
			 vdl_list_end (list));
  vdl_list_delete (list);

  // setup logging from LD_LOG
  const char *ld_log = vdl_utils_getenv (envp, "LD_LOG");
  vdl_log_set (ld_log);

  // setup bind_now from LD_BIND_NOW
  const char *bind_now = vdl_utils_getenv (envp, "LD_BIND_NOW");
  if (bind_now != 0)
    {
      g_vdl.bind_now = 1;
    }
}

static char *get_pt_interp (struct VdlFile *main, ElfW(Phdr) *phdr, unsigned long phnum)
{
  // XXX will not work when main exec is loader itself
  ElfW(Phdr) *pt_interp = vdl_utils_search_phdr (phdr, phnum, PT_INTERP);
  if (pt_interp == 0)
    {
      return 0;
    }
  return (char*)(main->load_base + pt_interp->p_vaddr);
}

struct Stage2Output
stage2_initialize (struct Stage2Input input)
{
  struct Stage2Output output;

  g_vdl.search_dirs = system_search_dirs_new ();
  g_vdl.contexts = vdl_list_new ();
  g_vdl.errors = vdl_list_new ();

  setup_env_vars ((const char**)input.program_envp);

  struct VdlFile *main_file;
  struct VdlContext *context;
  const char *pt_interp;
  // here, the file is already mapped so, we just create the 
  // right data structure
  struct VdlFileInfo info;
  VDL_LOG_ASSERT (vdl_get_file_info (input.program_phnum, input.program_phdr, &info),
		  "Unable to obtain information about main program");
  
  // The load base of the main program is easy to calculate as the difference
  // between the PT_PHDR vaddr and its real address in memory.
  unsigned long load_base = ((unsigned long)input.program_phdr) - input.program_phdr->p_vaddr;
  
  context = vdl_context_new (input.program_argc,
			     input.program_argv,
			     input.program_envp);
  
  // the filename for the main exec is "" for gdb.
  main_file = vdl_file_new (load_base,
			    &info,
			    "",
			    input.program_argv[0],
			    context);
  output.n_argv_skipped = 0;
  pt_interp = get_pt_interp (main_file, input.program_phdr, 
			     input.program_phnum);
  main_file->count++;
  main_file->is_executable = 1;
  gdb_initialize (main_file);
  struct VdlList *loaded = vdl_list_new ();
  vdl_list_push_back (loaded, main_file);

  // add the interpreter itself to the link map to ensure that it is
  // recorded somewhere. We must add it to the link map only after
  // the main binary because gdb assumes that the first entry in the
  // link map is the main binary itself. We don't add it to the global 
  // scope.
  struct VdlFile *interpreter;
  interpreter = interpreter_new (input.interpreter_load_base,
				 pt_interp,
				 context);
  vdl_list_push_back (loaded, interpreter);

  struct VdlList *ld_preload = ld_preload_list_new (context, (const char **)input.program_envp);
  vdl_list_insert_range (loaded, vdl_list_end (loaded),
			 vdl_list_begin (ld_preload),
			 vdl_list_end (ld_preload));

  VDL_LOG_ASSERT (vdl_file_map_deps (main_file, loaded), 
		  "Unable to map dependencies of main file");

  // The global scope is defined as being made of the main binary
  // and all its dependencies, breadth-first, with duplicate items removed.
  vdl_list_push_back (context->global_scope, main_file);
  // of course, the ld_preload binaries must be in there if needed.
  vdl_list_insert_range (context->global_scope,
			 vdl_list_end (context->global_scope),
			 vdl_list_begin (ld_preload),
			 vdl_list_end (ld_preload));
  struct VdlList *all_deps = vdl_sort_deps_breadth_first (main_file);
  vdl_list_insert_range (context->global_scope,
			 vdl_list_end (context->global_scope),
			 vdl_list_begin (all_deps),
			 vdl_list_end (all_deps));
  vdl_list_delete (all_deps);
  vdl_list_unicize (context->global_scope);

  vdl_list_delete (ld_preload);

  // We need to do this before relocation because the TLS-type relocations 
  // need tls information.
  vdl_tls_file_initialize_static (loaded);

  // We either setup the GOT for lazy symbol resolution
  // or we perform binding for all symbols now if LD_BIND_NOW is set
  vdl_reloc (loaded, g_vdl.bind_now);

  // Once relocations are done, we can initialize the tls blocks
  // and the dtv. We need to wait post-reloc because the tls
  // template area used to initialize the tls blocks is likely 
  // to be modified during relocation processing.
  unsigned long tcb = vdl_tls_tcb_allocate ();
  vdl_tls_tcb_initialize (tcb, input.sysinfo);
  vdl_tls_dtv_allocate (tcb);
  vdl_tls_dtv_initialize (tcb);
  // configure the current thread to use this TCB as a thread pointer
  machine_thread_pointer_set (tcb);

  // Note that we must invoke this method to notify gdb that we have
  // a valid linkmap only _after_ relocations have been done (if you do
  // it before, gdb gets confused) and _before_ the initializers are 
  // run (to allow the user to debug the initializers).
  gdb_notify ();

  // patch glibc functions which need to be overriden.
  // This is really a hack I am not very proud of.
  glibc_patch (loaded);

  // glibc-specific crap to avoid segfault in initializer
  glibc_initialize ();

  valgrind_initialize ();

  // Finally, call init functions
  struct VdlList *call_init = vdl_sort_call_init (loaded);
  vdl_init_fini_call_init (call_init);
  vdl_list_delete (call_init);

  vdl_list_delete (loaded);

  unsigned long entry = vdl_file_get_entry_point (main_file);
  if (entry == 0)
    {
      VDL_LOG_ERROR ("Zero entry point: nothing to do in %s\n", main_file->name);
      goto error;
    }
  glibc_startup_finished ();

  output.entry_point = entry;
  return output;
error:
  system_exit (-6);
  return output; // quiet compiler
}

void stage2_freeres (void)
{
  VDL_LOG_FUNCTION ("");
  // We know, that we will _not_ be called again after we return
  // from this function so, we can cleanup everything _except_ for the
  // code/data memory mappings because the caller code segment would be 
  // unmapped and that would trigger interesting crashes upon return
  // from this function. When we return, the caller is going to call
  // the exit_group syscall.

  struct VdlList *link_map = vdl_utils_list_global_linkmap_new ();
      
  vdl_files_delete (link_map, false);
      
  vdl_utils_str_list_delete (g_vdl.search_dirs);
  g_vdl.search_dirs = 0;

  vdl_list_delete (link_map);

  unsigned long tcb = machine_thread_pointer_get ();
  vdl_tls_dtv_deallocate (tcb);
  vdl_tls_tcb_deallocate (tcb);

  {
    void **i;
    for (i = vdl_list_begin (g_vdl.errors);
	 i != vdl_list_end (g_vdl.errors);
	 i = vdl_list_next (i))
      {
	struct VdlError *error = *i;
	vdl_alloc_free (error->prev_error);
	vdl_alloc_free (error->error);
	vdl_alloc_delete (error);
      }
    vdl_list_delete (g_vdl.errors);
  }
  vdl_list_delete (g_vdl.contexts);
  g_vdl.contexts = 0;
}

void
stage2_finalize (void)
{
  // The only thing we need to do here is to invoke the destructors
  // in the correct order.
  struct VdlList *link_map = vdl_utils_list_global_linkmap_new ();
  struct VdlList *call_fini = vdl_sort_call_fini (link_map);
  vdl_init_fini_call_fini (call_fini);
  vdl_list_delete (call_fini);
  vdl_list_delete (link_map);
}
