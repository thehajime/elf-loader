#include "vdl-file-list.h"
#include "vdl-utils.h"
#include "vdl-log.h"

void vdl_file_list_free (struct VdlFileList *list)
{
  struct VdlFileList *cur, *next;
  cur = list;
  next = 0;
  while (cur != 0)
    {
      next = cur->next;
      vdl_utils_delete (cur);
      cur = next;
    }
}
struct VdlFileList *vdl_file_list_copy (struct VdlFileList *list)
{
  struct VdlFileList *copy = 0;
  struct VdlFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      copy = vdl_file_list_append_one (copy, cur->item);
    }
  return copy;
}

struct VdlFileList *vdl_file_list_append_one (struct VdlFileList *list, 
					      struct VdlFile *item)
{
  if (list == 0)
    {
      list = vdl_utils_new (struct VdlFileList);
      list->next = 0;
      list->item = item;
      return list;
    }
  struct VdlFileList *cur = list;
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  cur->next = vdl_utils_new (struct VdlFileList);
  cur->next->item = item;
  cur->next->next = 0;
  return list;
}
struct VdlFileList *
vdl_file_list_prepend_one (struct VdlFileList *list, 
				 struct VdlFile *item)
{
  struct VdlFileList *new_start = vdl_utils_new (struct VdlFileList);
  new_start->next = list;
  new_start->item = item;
  return new_start;
}
struct VdlFileList *
vdl_file_list_remove (struct VdlFileList *list, 
		      struct VdlFileList *item)
{
  struct VdlFileList *cur = list, *prev = 0;
  while (cur != 0)
    {
      struct VdlFileList *next = cur->next;
      if (cur == item)
	{
	  if (prev == 0)
	    {
	      return next;
	    }
	  else
	    {
	      prev->next = next;
	      return list;
	    }
	}
      cur = next;
    }
  return list;
}
struct VdlFileList *
vdl_file_list_free_one (struct VdlFileList *list, 
			struct VdlFile *item)
{
  struct VdlFileList *cur = list, *prev = 0;
  while (cur != 0)
    {
      struct VdlFileList *next = cur->next;
      if (cur->item == item)
	{
	  vdl_utils_delete (cur);
	  if (prev == 0)
	    {
	      return next;
	    }
	  else
	    {
	      prev->next = next;
	      return list;
	    }
	}
      prev = cur;
      cur = next;
    }
  return list;
}

static struct VdlFileList *
vdl_file_list_get_end (struct VdlFileList *start)
{
  if (start == 0)
    {
      return 0;
    }
  struct VdlFileList *cur = start;
  while (cur->next != 0)
    {
      cur = cur->next;
    }
  return cur;
}
struct VdlFileList *vdl_file_list_append (struct VdlFileList *start, 
					     struct VdlFileList *last)
{
  if (start == 0)
    {
      return last;
    }
  struct VdlFileList *end = vdl_file_list_get_end (start);
  end->next = last;
  return start;
}
struct VdlFileList *vdl_file_list_reverse (struct VdlFileList *start)
{
  VDL_LOG_FUNCTION ("start=%p", start);
  struct VdlFileList *ret = 0, *cur, *next;
  for (cur = start; cur != 0; cur = next)
    {
      next = cur->next;
      cur->next = ret;
      ret = cur;
    }
  return ret;
}

void vdl_file_list_unicize (struct VdlFileList *list)
{
  struct VdlFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      struct VdlFileList *tmp, *prev;
      prev = cur;
      tmp = cur->next;
      while (tmp != 0)
	{
	  if (tmp->item == cur->item)
	    {
	      // skip this item.
	      struct VdlFileList *next = tmp->next;
	      vdl_utils_delete (tmp);
	      prev->next = next;
	      tmp = next;
	    }
	  else
	    {
	      prev = tmp;
	      tmp = tmp->next;
	    }
	}
    }
}
struct VdlFileList *
vdl_file_list_find (struct VdlFileList *list,
		    struct VdlFile *item)
{
  struct VdlFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      if (cur->item == item)
	{
	  return cur;
	}
    }
  return 0;
}
void vdl_file_list_print (struct VdlFileList *list)
{
  struct VdlFileList *cur;
  for (cur = list; cur != 0; cur = cur->next)
    {
      vdl_log_printf (VDL_LOG_PRINT, 
		      "\"%s\" ", cur->item->filename);
    }
  vdl_log_printf (VDL_LOG_PRINT, "\n");
}
