#include "vdl-fini.h"
#include "vdl-utils.h"
#include "vdl-log.h"
#include "vdl-context.h"
#include "vdl-file.h"
#include "vdl-list.h"

typedef void (*fini_function) (void);

static void
call_fini (struct VdlFile *file)
{
  VDL_LOG_FUNCTION ("file=%s", file->name);

  VDL_LOG_ASSERT (!file->fini_called, "file has already been deinitialized");
  if (!file->init_called)
    {
      // if we were never initialized properly no need to do any work
      return;
    }
  // mark the file as finalized
  file->fini_called = 1;

  // Gather information from the .dynamic section
  DtFini dt_fini = file->dt_fini;
  DtFini *dt_fini_array = file->dt_fini_array;
  unsigned long dt_fini_arraysz = file->dt_fini_arraysz;

  // First, invoke the newer DT_FINI_ARRAY functions.
  // The address of the functions to call is stored as
  // an array of pointers pointed to by DT_FINI_ARRAY
  if (dt_fini_array != 0)
    {
      int i;
      for (i = dt_fini_arraysz / sizeof (DtFini) - 1; i > 0; i--)
	{
	  (dt_fini_array[i]) ();
	}
    }

  // Then, invoke the old-style DT_FINI function.
  // The address of the function to call is stored in
  // the DT_FINI tag, here: dt_fini.
  if (dt_fini != 0)
    {
      dt_fini ();
    }

  vdl_context_notify (file->context, file, VDL_EVENT_DESTROYED);
}

void vdl_fini_call (struct VdlList *files)
{
  vdl_list_iterate (files, (void(*)(void*))call_fini);
}
