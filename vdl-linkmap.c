#include "vdl-linkmap.h"
#include "vdl-list.h"
#include "vdl.h"
#include "vdl-file.h"
#include "vdl-log.h"

void vdl_linkmap_append (struct VdlFile *file)
{
  if (g_vdl.link_map == 0)
    {
      g_vdl.link_map = file;
      return;
    }
  struct VdlFile *cur = g_vdl.link_map;
  while (cur->next != 0 && cur != file)
    {
      cur = cur->next;
    }
  if (cur == file)
    {
      return;
    }
  cur->next = file;
  file->prev = cur;
  file->next = 0;
  g_vdl.n_added++;
}
void vdl_linkmap_append_range (void **begin, void **end)
{
  void **i;
  for (i = begin; i != end; i = vdl_list_next (i))
    {
      vdl_linkmap_append (*i);
    }
}
void vdl_linkmap_remove (struct VdlFile *file)
{
  // first, remove them from the global link_map
  struct VdlFile *next = file->next;
  struct VdlFile *prev = file->prev;
  file->next = 0;
  file->prev = 0;
  if (prev == 0)
    {
      g_vdl.link_map = next;
    }
  else
    {
      prev->next = next;
    }
  if (next != 0)
    {
      next->prev = prev;
    }
  g_vdl.n_removed++;
}
void vdl_linkmap_remove_range (void **begin, void **end)
{
  void **i;
  for (i = begin; i != end; i = vdl_list_next (i))
    {
      vdl_linkmap_remove (*i);
    }
}

struct VdlList *vdl_linkmap_copy (void)
{
  struct VdlList *list = vdl_list_new ();
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      vdl_list_push_back (list, cur);
    }
  return list;
}

void vdl_linkmap_print (void)
{
  struct VdlFile *cur;
  for (cur = g_vdl.link_map; cur != 0; cur = cur->next)
    {
      vdl_log_printf (VDL_LOG_PRINT, 
		      "load_base=0x%x , file=%s\n", 
		      cur->load_base, 
		      cur->filename);
    }
}
