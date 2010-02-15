#include "vdl-reloc.h"
#include "machine.h"
#include "vdl-log.h"
#include "vdl-sort.h"
#include "vdl-list.h"
#include "vdl-lookup.h"
#include "vdl-utils.h"
#include "vdl-mem.h"
#include <stdbool.h>

static bool
sym_to_ver_req (struct VdlFile *file,
		unsigned long index,
		const char **ver_name,
		const char **ver_filename)
{
  const char *dt_strtab = (const char *)vdl_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Half) *dt_versym = (ElfW(Half)*)vdl_file_get_dynamic_p (file, DT_VERSYM);
  ElfW(Verneed) *dt_verneed = (ElfW(Verneed)*)vdl_file_get_dynamic_p (file, DT_VERNEED);
  unsigned long dt_verneednum = vdl_file_get_dynamic_v (file, DT_VERNEEDNUM);
  ElfW(Verdef) *dt_verdef = (ElfW(Verdef)*)vdl_file_get_dynamic_p (file, DT_VERDEF);
  unsigned long dt_verdefnum = vdl_file_get_dynamic_v (file, DT_VERDEFNUM);

  if (dt_strtab != 0 && dt_versym != 0)
    {
      // the same offset used to look in the symbol table (dt_symtab)
      // is an offset in the version table (dt_versym).
      // dt_versym contains a set of 15bit indexes and 
      // 1bit flags packed into 16 bits. When the upper bit is
      // set, the associated symbol is 'hidden', that is, it
      // cannot be referenced from outside of the object.
      ElfW(Half) ver_ndx = dt_versym[index];
      if (ver_ndx & 0x8000)
	{
	  return false;
	}
      // search the version needed whose vna_other is equal to ver_ndx.
      if (dt_verneed != 0 && dt_verneednum != 0)
	{
	  ElfW(Verneed) *cur, *prev;
	  for (cur = dt_verneed, prev = 0; 
	       cur != prev; 
	       prev = cur, cur = (ElfW(Verneed) *)(((unsigned long)cur)+cur->vn_next))
	    {
	      VDL_LOG_ASSERT (cur->vn_version == 1, "version number invalid for Verneed");
	      ElfW(Vernaux) *cur_aux, *prev_aux;
	      for (cur_aux = (ElfW(Vernaux)*)(((unsigned long)cur)+cur->vn_aux), prev_aux = 0;
		   cur_aux != prev_aux; 
		   prev_aux = cur_aux, cur_aux = (ElfW(Vernaux)*)(((unsigned long)cur_aux)+cur_aux->vna_next))
		{
		  if (cur_aux->vna_other == ver_ndx)
		    {
		      *ver_name = dt_strtab + cur_aux->vna_name;
		      *ver_filename = dt_strtab + cur->vn_file;
		      return true;
		    }
		}
	    }
	}
      // ok, there is no match for the requested version in the verneed array so,
      // we look in the verdef array.
      // search the version whose vd_ndx is equal to ver_ndx
      if (dt_verdef != 0 && dt_verdefnum != 0)
	{
	  ElfW(Verdef) *cur, *prev;
	  for (prev = 0, cur = dt_verdef; cur != prev;
	       prev = cur, cur = (ElfW(Verdef)*)(((unsigned long)cur)+cur->vd_next))
	    {
	      VDL_LOG_ASSERT (cur->vd_version == 1, "version number invalid for Verdef");
	      if (cur->vd_ndx == ver_ndx)
		{
		  ElfW(Verdaux) *verdaux = (ElfW(Verdaux)*)(((unsigned long)cur)+cur->vd_aux);
		  *ver_name = dt_strtab + verdaux->vda_name;
		  // the filename comes from the base definition (i.e., the first entry)
		  // in the verdef array.
		  ElfW(Verdef) *base = &dt_verdef[0];
		  ElfW(Verdaux) *base_verdaux = (ElfW(Verdaux)*)(((unsigned long)base)+base[0].vd_aux);
		  *ver_filename = dt_strtab + base_verdaux->vda_name;
		  return true;
		}
	    }
	}
    }
  return false;
}

static unsigned long
do_process_reloc (struct VdlFile *file, 
		  unsigned long reloc_type, unsigned long *reloc_addr,
		  unsigned long reloc_addend, unsigned long reloc_sym)
{
  const char *dt_strtab = (const char *)vdl_file_get_dynamic_p (file, DT_STRTAB);
  ElfW(Sym) *dt_symtab = (ElfW(Sym)*)vdl_file_get_dynamic_p (file, DT_SYMTAB);
  if (dt_strtab == 0 || dt_symtab == 0)
    {
      return 0;
    }
  ElfW(Sym) *sym = &dt_symtab[reloc_sym];

  VDL_LOG_FUNCTION ("file=%s, type=%s, addr=0x%lx, addend=0x%lx, sym=%s", 
		    file->filename, machine_reloc_type_to_str (reloc_type), 
		    reloc_addr, reloc_addend, reloc_sym == 0?"0":dt_strtab + sym->st_name);

  if (!machine_reloc_is_relative (reloc_type) &&
      sym->st_name != 0)
    {
      const char *symbol_name = dt_strtab + sym->st_name;
      int flags = 0;
      if (machine_reloc_is_copy (reloc_type))
	{
	  // for R_*_COPY relocations, we must use the
	  // LOOKUP_NO_EXEC flag to avoid looking up the symbol
	  // in the main binary.
	  flags |= VDL_LOOKUP_NO_EXEC;
	}
      const char *ver_name = 0;
      const char *ver_filename = 0;
      sym_to_ver_req (file, reloc_sym, &ver_name, &ver_filename);

      struct VdlLookupResult result;
      result = vdl_lookup (file, symbol_name, ver_name, ver_filename, flags);
      if (!result.found)
	{
	  if (ELFW_ST_BIND (sym->st_info) == STB_WEAK)
	    {
	      // The symbol we are trying to resolve is marked weak so,
	      // if we can't find it, it's not an error.
	    }
	  else
	    {
	      // This is really a hard failure. We do not assert
	      // to emulate the glibc behavior
	      VDL_LOG_SYMBOL_FAIL (symbol_name, file);
	    }
	  return 0;
	}
      VDL_LOG_SYMBOL_OK (symbol_name, file, result);
      if (machine_reloc_is_copy (reloc_type))
	{
	  // we handle R_*_COPY relocs ourselves
	  VDL_LOG_ASSERT (result.symbol->st_size == sym->st_size,
			  "Symbols don't have the same size: likely a recipe for disaster.");
	  vdl_memcpy (reloc_addr, 
		      (void*)(result.file->load_base + result.symbol->st_value),
		      result.symbol->st_size);
	}
      else
	{
	  machine_reloc_with_match (reloc_addr, reloc_type, reloc_addend, &result);
	}
    }
  else
    {
      machine_reloc_without_match (file, reloc_addr, reloc_type, reloc_addend, sym);
    }
  return *reloc_addr;
}

static unsigned long
process_rel (struct VdlFile *file, ElfW(Rel) *rel)
{
  unsigned long reloc_type = ELFW_R_TYPE (rel->r_info);
  unsigned long *reloc_addr = (unsigned long*) (file->load_base + rel->r_offset);
  unsigned long reloc_addend = 0;
  unsigned long reloc_sym = ELFW_R_SYM (rel->r_info);

  return do_process_reloc (file, reloc_type, reloc_addr, reloc_addend, reloc_sym);
}

static unsigned long
process_rela (struct VdlFile *file, ElfW(Rela) *rela)
{
  unsigned long reloc_type = ELFW_R_TYPE (rela->r_info);
  unsigned long *reloc_addr = (unsigned long*) (file->load_base + rela->r_offset);
  unsigned long reloc_addend = rela->r_addend;
  unsigned long reloc_sym = ELFW_R_SYM (rela->r_info);

  return do_process_reloc (file, reloc_type, reloc_addr, reloc_addend, reloc_sym);
}

static void
reloc_jmprel (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  unsigned long dt_jmprel = vdl_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = vdl_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = vdl_file_get_dynamic_v (file, DT_PLTRELSZ);  
  if ((dt_pltrel != DT_REL && dt_pltrel != DT_RELA) ||
      dt_pltrelsz == 0 || 
      dt_jmprel == 0)
    {
      return;
    }
  if (dt_pltrel == DT_REL)
    {
      int i;
      for (i = 0; i < dt_pltrelsz/sizeof(ElfW(Rel)); i++)
	{
	  ElfW(Rel) *rel = &(((ElfW(Rel)*)dt_jmprel)[i]);
	  process_rel (file, rel);
	}
    }
  else
    {
      int i;
      for (i = 0; i < dt_pltrelsz/sizeof(ElfW(Rela)); i++)
	{
	  ElfW(Rela) *rela = &(((ElfW(Rela)*)dt_jmprel)[i]);
	  process_rela (file, rela);
	}
    }
}
unsigned long 
vdl_reloc_offset_jmprel (struct VdlFile *file, 
			 unsigned long offset)
{
  futex_lock (&g_vdl.futex);
  unsigned long dt_jmprel = vdl_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = vdl_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = vdl_file_get_dynamic_v (file, DT_PLTRELSZ);
  
  if ((dt_pltrel != DT_REL && dt_pltrel != DT_RELA) || 
      dt_pltrelsz == 0 || 
      dt_jmprel == 0)
    {
      futex_unlock (&g_vdl.futex);
      return 0;
    }
  VDL_LOG_ASSERT (offset < dt_pltrelsz, 
		  "Relocation entry not within range");

  unsigned long symbol;
  if (dt_pltrel == DT_REL)
    {
      ElfW(Rel) *rel = (ElfW(Rel)*)(dt_jmprel+offset);
      symbol = process_rel (file, rel);
    }
  else
    {
      ElfW(Rela) *rela = (ElfW(Rela)*)(dt_jmprel+offset);
      symbol = process_rela (file, rela);
    }
  futex_unlock (&g_vdl.futex);
  return symbol;
}

unsigned long 
vdl_reloc_index_jmprel (struct VdlFile *file, 
			unsigned long index)
{
  VDL_LOG_FUNCTION ("file=%s, index=%lu", file->name, index);
  futex_lock (&g_vdl.futex);
  unsigned long dt_jmprel = vdl_file_get_dynamic_p (file, DT_JMPREL);
  unsigned long dt_pltrel = vdl_file_get_dynamic_v (file, DT_PLTREL);
  unsigned long dt_pltrelsz = vdl_file_get_dynamic_v (file, DT_PLTRELSZ);
  
  if ((dt_pltrel != DT_REL && dt_pltrel != DT_RELA) || 
      dt_pltrelsz == 0 || 
      dt_jmprel == 0)
    {
      futex_unlock (&g_vdl.futex);
      return 0;
    }
  unsigned long symbol;
  if (dt_pltrel == DT_REL)
    {
      VDL_LOG_ASSERT (index < dt_pltrelsz / sizeof(ElfW(Rel)), 
		      "Relocation entry not within range");
      ElfW(Rel) *rel = &((ElfW(Rel)*)dt_jmprel)[index];
      symbol = process_rel (file, rel);
    }
  else
    {
      VDL_LOG_ASSERT (index < dt_pltrelsz / sizeof(ElfW(Rela)), 
		      "Relocation entry not within range");
      ElfW(Rela) *rela = &((ElfW(Rela)*)dt_jmprel)[index];
      symbol = process_rela (file, rela);
    }
  futex_unlock (&g_vdl.futex);
  return symbol;
}


static void
reloc_dtrel (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  ElfW(Rel) *dt_rel = (ElfW(Rel)*)vdl_file_get_dynamic_p (file, DT_REL);
  unsigned long dt_relsz = vdl_file_get_dynamic_v (file, DT_RELSZ);
  unsigned long dt_relent = vdl_file_get_dynamic_v (file, DT_RELENT);
  if (dt_rel == 0 || dt_relsz == 0 || dt_relent == 0)
    {
      return;
    }
  uint32_t i;
  for (i = 0; i < dt_relsz/dt_relent; i++)
    {
      ElfW(Rel) *rel = &dt_rel[i];
      process_rel (file, rel);
    }
}

static void
reloc_dtrela (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);
  ElfW(Rela) *dt_rela = (ElfW(Rela)*)vdl_file_get_dynamic_p (file, DT_RELA);
  unsigned long dt_relasz = vdl_file_get_dynamic_v (file, DT_RELASZ);
  unsigned long dt_relaent = vdl_file_get_dynamic_v (file, DT_RELAENT);
  if (dt_rela == 0 || dt_relasz == 0 || dt_relaent == 0)
    {
      return;
    }
  uint32_t i;
  for (i = 0; i < dt_relasz/dt_relaent; i++)
    {
      ElfW(Rela) *rela = &dt_rela[i];
      process_rela (file, rela);
    }
}

static void
do_reloc (struct VdlFile *file, int now)
{
  if (file->reloced)
    {
      return;
    }
  file->reloced = 1;

  reloc_dtrel (file);
  reloc_dtrela (file);
  if (now)
    {
      // perform full PLT relocs _now_
      reloc_jmprel (file);
    }
  else
    {
      machine_lazy_reloc (file);
    }
}

void vdl_reloc (struct VdlList *files, int now)
{
  struct VdlList *sorted = vdl_sort_increasing_depth (files);
  vdl_list_reverse (sorted);
  void **cur;
  for (cur = vdl_list_begin (sorted);
       cur != vdl_list_end (sorted);
       cur = vdl_list_next (cur))
    {
      do_reloc (*cur, now);
    }
  vdl_list_delete (sorted);
}
