/* Everything about catch/throw catchpoints, for GDB.

   Copyright (C) 1986-2013 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "arch-utils.h"
#include <ctype.h>
#include "breakpoint.h"
#include "gdbcmd.h"
#include "inferior.h"
#include "annotate.h"
#include "valprint.h"
#include "cli/cli-utils.h"
#include "completer.h"
#include "gdb_obstack.h"
#include "mi/mi-common.h"

/* Enums for exception-handling support.  */
enum exception_event_kind
{
  EX_EVENT_THROW,
  EX_EVENT_RETHROW,
  EX_EVENT_CATCH
};

/* A helper function that returns a value indicating the kind of the
   exception catchpoint B.  */

static enum exception_event_kind
classify_exception_breakpoint (struct breakpoint *b)
{
  if (strstr (b->addr_string, "catch") != NULL)
    return EX_EVENT_CATCH;
  else if (strstr (b->addr_string, "rethrow") != NULL)
    return EX_EVENT_RETHROW;
  else
    return EX_EVENT_THROW;
}

static enum print_stop_action
print_it_exception_catchpoint (bpstat bs)
{
  struct ui_out *uiout = current_uiout;
  struct breakpoint *b = bs->breakpoint_at;
  int bp_temp;
  enum exception_event_kind kind = classify_exception_breakpoint (b);

  annotate_catchpoint (b->number);

  bp_temp = b->disposition == disp_del;
  ui_out_text (uiout, 
	       bp_temp ? "Temporary catchpoint "
		       : "Catchpoint ");
  if (!ui_out_is_mi_like_p (uiout))
    ui_out_field_int (uiout, "bkptno", b->number);
  ui_out_text (uiout,
	       (kind == EX_EVENT_THROW ? " (exception thrown), "
		: (kind == EX_EVENT_CATCH ? " (exception caught), "
		   : " (exception rethrown), ")));
  if (ui_out_is_mi_like_p (uiout))
    {
      ui_out_field_string (uiout, "reason", 
			   async_reason_lookup (EXEC_ASYNC_BREAKPOINT_HIT));
      ui_out_field_string (uiout, "disp", bpdisp_text (b->disposition));
      ui_out_field_int (uiout, "bkptno", b->number);
    }
  return PRINT_SRC_AND_LOC;
}

static void
print_one_exception_catchpoint (struct breakpoint *b, 
				struct bp_location **last_loc)
{
  struct value_print_options opts;
  struct ui_out *uiout = current_uiout;
  enum exception_event_kind kind = classify_exception_breakpoint (b);

  get_user_print_options (&opts);
  if (opts.addressprint)
    {
      annotate_field (4);
      if (b->loc == NULL || b->loc->shlib_disabled)
	ui_out_field_string (uiout, "addr", "<PENDING>");
      else
	ui_out_field_core_addr (uiout, "addr",
				b->loc->gdbarch, b->loc->address);
    }
  annotate_field (5);
  if (b->loc)
    *last_loc = b->loc;

  switch (kind)
    {
    case EX_EVENT_THROW:
      ui_out_field_string (uiout, "what", "exception throw");
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string (uiout, "catch-type", "throw");
      break;

    case EX_EVENT_RETHROW:
      ui_out_field_string (uiout, "what", "exception rethrow");
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string (uiout, "catch-type", "rethrow");
      break;

    case EX_EVENT_CATCH:
      ui_out_field_string (uiout, "what", "exception catch");
      if (ui_out_is_mi_like_p (uiout))
	ui_out_field_string (uiout, "catch-type", "catch");
      break;
    }
}

static void
print_mention_exception_catchpoint (struct breakpoint *b)
{
  struct ui_out *uiout = current_uiout;
  int bp_temp;
  enum exception_event_kind kind = classify_exception_breakpoint (b);

  bp_temp = b->disposition == disp_del;
  ui_out_text (uiout, bp_temp ? _("Temporary catchpoint ")
			      : _("Catchpoint "));
  ui_out_field_int (uiout, "bkptno", b->number);
  ui_out_text (uiout, (kind == EX_EVENT_THROW ? _(" (throw)")
		       : (kind == EX_EVENT_CATCH ? _(" (catch)")
			  : _(" (rethrow)"))));
}

/* Implement the "print_recreate" breakpoint_ops method for throw and
   catch catchpoints.  */

static void
print_recreate_exception_catchpoint (struct breakpoint *b, 
				     struct ui_file *fp)
{
  int bp_temp;
  enum exception_event_kind kind = classify_exception_breakpoint (b);

  bp_temp = b->disposition == disp_del;
  fprintf_unfiltered (fp, bp_temp ? "tcatch " : "catch ");
  switch (kind)
    {
    case EX_EVENT_THROW:
      fprintf_unfiltered (fp, "throw");
      break;
    case EX_EVENT_CATCH:
      fprintf_unfiltered (fp, "catch");
      break;
    case EX_EVENT_RETHROW:
      fprintf_unfiltered (fp, "rethrow");
      break;
    }
  print_recreate_thread (b, fp);
}

static struct breakpoint_ops gnu_v3_exception_catchpoint_ops;

static int
handle_gnu_v3_exceptions (int tempflag, char *cond_string,
			  enum exception_event_kind ex_event, int from_tty)
{
  char *trigger_func_name;
 
  if (ex_event == EX_EVENT_CATCH)
    trigger_func_name = "__cxa_begin_catch";
  else if (ex_event == EX_EVENT_RETHROW)
    trigger_func_name = "__cxa_rethrow";
  else
    {
      gdb_assert (ex_event == EX_EVENT_THROW);
      trigger_func_name = "__cxa_throw";
    }

  create_breakpoint (get_current_arch (),
		     trigger_func_name, cond_string, -1, NULL,
		     0 /* condition and thread are valid.  */,
		     tempflag, bp_breakpoint,
		     0,
		     AUTO_BOOLEAN_TRUE /* pending */,
		     &gnu_v3_exception_catchpoint_ops, from_tty,
		     1 /* enabled */,
		     0 /* internal */,
		     0);

  return 1;
}

/* Deal with "catch catch", "catch throw", and "catch rethrow"
   commands.  */

static void
catch_exception_command_1 (enum exception_event_kind ex_event, char *arg,
			   int tempflag, int from_tty)
{
  char *cond_string = NULL;

  if (!arg)
    arg = "";
  arg = skip_spaces (arg);

  cond_string = ep_parse_optional_if_clause (&arg);

  if ((*arg != '\0') && !isspace (*arg))
    error (_("Junk at end of arguments."));

  if (ex_event != EX_EVENT_THROW
      && ex_event != EX_EVENT_CATCH
      && ex_event != EX_EVENT_RETHROW)
    error (_("Unsupported or unknown exception event; cannot catch it"));

  if (handle_gnu_v3_exceptions (tempflag, cond_string, ex_event, from_tty))
    return;

  warning (_("Unsupported with this platform/compiler combination."));
}

/* Implementation of "catch catch" command.  */

static void
catch_catch_command (char *arg, int from_tty, struct cmd_list_element *command)
{
  int tempflag = get_cmd_context (command) == CATCH_TEMPORARY;

  catch_exception_command_1 (EX_EVENT_CATCH, arg, tempflag, from_tty);
}

/* Implementation of "catch throw" command.  */

static void
catch_throw_command (char *arg, int from_tty, struct cmd_list_element *command)
{
  int tempflag = get_cmd_context (command) == CATCH_TEMPORARY;

  catch_exception_command_1 (EX_EVENT_THROW, arg, tempflag, from_tty);
}

/* Implementation of "catch rethrow" command.  */

static void
catch_rethrow_command (char *arg, int from_tty,
		       struct cmd_list_element *command)
{
  int tempflag = get_cmd_context (command) == CATCH_TEMPORARY;

  catch_exception_command_1 (EX_EVENT_RETHROW, arg, tempflag, from_tty);
}



static void
initialize_throw_catchpoint_ops (void)
{
  struct breakpoint_ops *ops;

  initialize_breakpoint_ops ();

  /* GNU v3 exception catchpoints.  */
  ops = &gnu_v3_exception_catchpoint_ops;
  *ops = bkpt_breakpoint_ops;
  ops->print_it = print_it_exception_catchpoint;
  ops->print_one = print_one_exception_catchpoint;
  ops->print_mention = print_mention_exception_catchpoint;
  ops->print_recreate = print_recreate_exception_catchpoint;
}

initialize_file_ftype _initialize_break_catch_throw;

void
_initialize_break_catch_throw (void)
{
  initialize_throw_catchpoint_ops ();

  /* Add catch and tcatch sub-commands.  */
  add_catch_command ("catch", _("\
Catch an exception, when caught."),
		     catch_catch_command,
                     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
  add_catch_command ("throw", _("\
Catch an exception, when thrown."),
		     catch_throw_command,
                     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
  add_catch_command ("rethrow", _("\
Catch an exception, when rethrown."),
		     catch_rethrow_command,
                     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
}