#include "machine.h"
#include "mdl-elf.h"
#include "mdl.h"
#include "config.h"
#include <sys/mman.h>

static int do_lookup_and_log (const char *symbol_name,
			      const struct MappedFile *file,
			      enum LookupFlag flags,
			      struct SymbolMatch *match)
{
  if (!mdl_elf_symbol_lookup (symbol_name, file, flags, match))
    {
      MDL_LOG_SYMBOL_FAIL (symbol_name, file);
      // if the symbol resolution has failed, it could
      // be that it's not a big deal.
      return 0;
    }
  MDL_LOG_SYMBOL_OK (symbol_name, file, match);
  return 1;
}

void machine_perform_relocation (const struct MappedFile *file,
				 const ElfW(Rel) *rel,
				 const ElfW(Sym) *sym,
				 const char *symbol_name)
{
  MDL_LOG_FUNCTION ("file=%s, symbol_name=%s, off=0x%x, type=0x%x", 
		    file->filename, (symbol_name != 0)?symbol_name:"", 
		    rel->r_offset,
		    ELFW_R_TYPE (rel->r_info));
  unsigned long type = ELFW_R_TYPE (rel->r_info);
  unsigned long *reloc_addr = (unsigned long*) (rel->r_offset + file->load_base);

  if (type == R_386_JMP_SLOT || 
      type == R_386_GLOB_DAT ||
      type == R_386_32)
    {
      struct SymbolMatch match;
      if (!do_lookup_and_log (symbol_name, file, 0, &match))
	{
	  return;
	}
      *reloc_addr = match.file->load_base + match.symbol->st_value;
    }
  else if (type == R_386_RELATIVE)
    {
      *reloc_addr += file->load_base;
    }
  else if (type == R_386_COPY)
    {
      struct SymbolMatch match;
      // for R_*_COPY relocations, we must use the
      // LOOKUP_NO_EXEC flag to avoid looking up the symbol
      // in the main binary.
      if (!do_lookup_and_log (symbol_name, file, LOOKUP_NO_EXEC, &match))
	{
	  return;
	}
      MDL_ASSERT (match.symbol->st_size == sym->st_size,
		  "Symbols don't have the same size: likely a recipe for disaster.");
      mdl_memcpy (reloc_addr, 
		  (void*)(match.file->load_base + match.symbol->st_value),
		  match.symbol->st_size);
    }
  else if (type == R_386_TLS_TPOFF)
    {
      unsigned long v;
      if (symbol_name != 0)
	{
	  struct SymbolMatch match;
	  if (!do_lookup_and_log (symbol_name, file, 0, &match))
	    {
	      return;
	    }
	  MDL_ASSERT (match.file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
	  MDL_ASSERT (ELFW_ST_TYPE (match.symbol->st_info) == STT_TLS,
		      "Target symbol is not a tls symbol ??");
	  v = match.file->tls_offset + match.symbol->st_value;
	}
      else
	{
	  v = file->tls_offset + sym->st_value;
	}
      *reloc_addr += v;
    }
  else if (type == R_386_TLS_DTPMOD32)
    {
      unsigned long v;
      if (symbol_name != 0)
	{
	  struct SymbolMatch match;
	  if (!do_lookup_and_log (symbol_name, file, 0, &match))
	    {
	      return;
	    }
	  MDL_ASSERT (match.file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
	  v = match.file->tls_index;
	}
      else
	{
	  v = file->tls_index;
	}
      *reloc_addr = v;
    }
  else if (type == R_386_TLS_DTPOFF32)
    {
      unsigned long v;
      if (symbol_name != 0)
	{
	  struct SymbolMatch match;
	  if (!do_lookup_and_log (symbol_name, file, 0, &match))
	    {
	      return;
	    }
	  MDL_ASSERT (match.file->has_tls,
		      "Module which contains target symbol does not have a TLS block ??");
	  v = match.symbol->st_value;
	}
      else
	{
	  v = sym->st_value;
	}
      *reloc_addr = v;
    }
  else
    {
      MDL_LOG_RELOC (rel);
    }
}

void machine_insert_trampoline (unsigned long from, unsigned long to)
{
  MDL_LOG_FUNCTION ("from=0x%x, to=0x%x", from, to);
  // In this code, we assume that the target symbol is bigger than
  // our jump and that none of that code is running yet so, we don't have
  // to worry about modifying a piece of code which is running already.
  unsigned long page_start = from / 4096 * 4096;
  system_mprotect ((void*)page_start, 4096, PROT_WRITE);
  signed long delta = to;
  delta -= from + 5;
  unsigned long delta_unsigned = delta;
  unsigned char *buffer = (unsigned char *)from;
  buffer[0] = 0xe9;
  buffer[1] = (delta_unsigned >> 0) & 0xff;
  buffer[2] = (delta_unsigned >> 8) & 0xff;
  buffer[3] = (delta_unsigned >> 16) & 0xff;
  buffer[4] = (delta_unsigned >> 24) & 0xff;
  system_mprotect ((void *)page_start, 4096, PROT_READ | PROT_EXEC);
}

void machine_tcb_allocate_and_set (unsigned long tcb_size)
{
  unsigned long total_size = tcb_size + CONFIG_TCB_SIZE;
  unsigned long buffer = (unsigned long) mdl_malloc (total_size);
  mdl_memset ((void*)buffer, 0, total_size);
  unsigned long tcb = buffer + tcb_size;
  mdl_memcpy ((void*)(tcb+CONFIG_TCB_TCB_OFFSET), &tcb, sizeof (tcb));
  mdl_memcpy ((void*)(tcb+CONFIG_TCB_SELF_OFFSET), &tcb, sizeof (tcb));
  struct user_desc desc;
  mdl_memset (&desc, 0, sizeof (desc));
  desc.entry_number = -1; // ask kernel to allocate an entry number
  desc.base_addr = tcb;
  desc.limit = 0xfffff; // maximum memory address in number of pages (4K) -> 4GB
  desc.seg_32bit = 1;

  desc.contents = 0;
  desc.read_exec_only = 0;
  desc.limit_in_pages = 1;
  desc.seg_not_present = 0;
  desc.useable = 1;
  
  int status = system_set_thread_area (&desc);
  MDL_ASSERT (status == 0, "Unable to set TCB");

  // set_thread_area allocated an entry in the GDT and returned
  // the index associated to this entry. So, now, we associate
  // %gs with this newly-allocated entry.
  // Bits 3 to 15 indicate the entry index.
  // Bit 2 is set to 0 to indicate that we address the GDT through
  // this segment selector.
  // Bits 0 to 1 indicate the privilege level requested (here, 3,
  // is the least privileged level)
  int gs = (desc.entry_number << 3) | 3;
  asm ("movw %w0, %%gs" :: "q" (gs));
}
void machine_tcb_set_dtv (unsigned long *dtv)
{
  unsigned long tcb = machine_tcb_get ();
  mdl_memcpy ((void*)(tcb+CONFIG_TCB_DTV_OFFSET), &dtv, sizeof (dtv));
}
void machine_tcb_set_sysinfo (unsigned long sysinfo)
{
  unsigned long tcb = machine_tcb_get ();
  mdl_memcpy ((void*)(tcb+CONFIG_TCB_SYSINFO_OFFSET), &sysinfo, sizeof (sysinfo));
}
unsigned long machine_tcb_get (void)
{
  unsigned long value;
  asm ("movl %%gs:0,%0" : "=r" (value) :);
  return value;
}
unsigned long *machine_tcb_get_dtv (void)
{
  unsigned long tcb = machine_tcb_get ();
  unsigned long dtv;
  mdl_memcpy (&dtv, (void*)(tcb+CONFIG_TCB_DTV_OFFSET), sizeof (dtv));
  return (unsigned long *)dtv;
}
unsigned long machine_tcb_get_sysinfo (void)
{
  unsigned long tcb = machine_tcb_get ();
  unsigned long sysinfo;
  mdl_memcpy (&sysinfo, (void*)(tcb+CONFIG_TCB_SYSINFO_OFFSET), sizeof (sysinfo));
  return sysinfo;
}

