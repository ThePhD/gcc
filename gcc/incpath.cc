/* Set up combined include path chain for the preprocessor.
   Copyright (C) 1986-2026 Free Software Foundation, Inc.

   Broken out of cppinit.c and cppfiles.c and rewritten Mar 2003.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "cpplib.h"
#include "prefix.h"
#include "intl.h"
#include "incpath.h"
#include "cppdefault.h"
#include "xregex.h"

/* Microsoft Windows does not natively support inodes.
   VMS has non-numeric inodes.  */
#ifdef VMS
# define INO_T_EQ(A, B) (!memcmp (&(A), &(B), sizeof (A)))
# define INO_T_COPY(DEST, SRC) memcpy (&(DEST), &(SRC), sizeof (SRC))
#elif !defined (HOST_LACKS_INODE_NUMBERS)
# define INO_T_EQ(A, B) ((A) == (B))
# define INO_T_COPY(DEST, SRC) (DEST) = (SRC)
#endif

#if defined INO_T_EQ
#define DIRS_EQ(A, B) ((A)->dev == (B)->dev \
	&& INO_T_EQ ((A)->ino, (B)->ino))
#else
#define DIRS_EQ(A, B) (!filename_cmp ((A)->canonical_name, (B)->canonical_name))
#endif

#ifndef HOST_STAT_FOR_64BIT_INODES
#define HOST_STAT_FOR_64BIT_INODES stat
#endif

static const char dir_separator_str[] = { DIR_SEPARATOR, 0 };

static void add_env_var_paths (const char *, incpath_kind);
static void add_standard_paths (const char *, const char *, const char *, int);
static void free_path (struct cpp_dir *, int);
static void merge_include_chains (const char *, cpp_reader *, int);
static void add_sysroot_to_chain (const char *, int);
static struct cpp_dir *remove_duplicates (cpp_reader *, struct cpp_dir *,
					  struct cpp_dir *, struct cpp_dir *,
					  int);

/* Include chains heads and tails.  */
static struct cpp_dir *heads[INC_MAX];
static struct cpp_dir *tails[INC_MAX];
/* #depend inputs and patterns.  */
static struct cpp_dep_pattern *deps_head;
static struct cpp_dep_pattern *deps_tail;

static bool quote_ignores_source_dir;
enum { REASON_QUIET = 0, REASON_NOENT, REASON_DUP, REASON_DUP_SYS };

/* Free an element of the include chain, possibly giving a reason.  */
static void
free_path (struct cpp_dir *path, int reason)
{
  switch (reason)
    {
    case REASON_DUP:
    case REASON_DUP_SYS:
      fprintf (stderr, _("ignoring duplicate directory \"%s\"\n"), path->name);
      if (reason == REASON_DUP_SYS)
	fprintf (stderr,
 _("  as it is a non-system directory that duplicates a system directory\n"));
      break;

    case REASON_NOENT:
      fprintf (stderr, _("ignoring nonexistent directory \"%s\"\n"),
	       path->name);
      break;

    case REASON_QUIET:
    default:
      break;
    }

  free (path->name);
  free (path);
}

/* Read ENV_VAR for a PATH_SEPARATOR-separated list of file names; and
   append all the names to the search path CHAIN.  */
static void
add_env_var_paths (const char *env_var, incpath_kind chain)
{
  char *p, *q, *path;

  q = getenv (env_var);

  if (!q)
    return;

  for (p = q; *q; p = q + 1)
    {
      q = p;
      while (*q != 0 && *q != PATH_SEPARATOR)
	q++;

      if (p == q)
	path = xstrdup (".");
      else
	{
	  path = XNEWVEC (char, q - p + 1);
	  memcpy (path, p, q - p);
	  path[q - p] = '\0';
	}

      add_path (path, chain, chain == INC_SYSTEM, false);
    }
}

/* Append the standard include chain defined in cppdefault.cc.  */
static void
add_standard_paths (const char *sysroot, const char *iprefix,
		    const char *imultilib, int cxx_stdinc)
{
  const struct default_include *p;
  int relocated = cpp_relocated ();
  size_t len;

  if (iprefix && (len = cpp_GCC_INCLUDE_DIR_len) != 0)
    {
      /* Look for directories that start with the standard prefix.
	 "Translate" them, i.e. replace /usr/local/lib/gcc... with
	 IPREFIX and search them first.  */
      for (p = cpp_include_defaults; p->fname; p++)
	{
	  if (p->cplusplus == 0
	      || (cxx_stdinc && (p->cplusplus == flag_stdlib_kind)))
	    {
	      /* Should we be translating sysrooted dirs too?  Assume
		 that iprefix and sysroot are mutually exclusive, for
		 now.  */
	      if (sysroot && p->add_sysroot)
		continue;
	      if (!filename_ncmp (p->fname, cpp_GCC_INCLUDE_DIR, len))
		{
		  char *str = concat (iprefix, p->fname + len, NULL);
		  if (p->multilib == 1 && imultilib)
		    str = reconcat (str, str, dir_separator_str,
				    imultilib, NULL);
		  else if (p->multilib == 2)
		    {
		      if (!imultiarch)
			{
			  free (str);
			  continue;
			}
		      str = reconcat (str, str, dir_separator_str,
				      imultiarch, NULL);
		    }
		  add_path (str, INC_SYSTEM, p->cxx_aware, false);
		}
	    }
	}
    }

  for (p = cpp_include_defaults; p->fname; p++)
    {
      if (p->cplusplus == 0
	  || (cxx_stdinc && (p->cplusplus == flag_stdlib_kind)))
	{
	  char *str;

	  /* Should this directory start with the sysroot?  */
	  if (sysroot && p->add_sysroot)
	    {
	      char *sysroot_no_trailing_dir_separator = xstrdup (sysroot);
	      size_t sysroot_len = strlen (sysroot);

	      if (sysroot_len > 0 && sysroot[sysroot_len - 1] == DIR_SEPARATOR)
		sysroot_no_trailing_dir_separator[sysroot_len - 1] = '\0';
	      str = concat (sysroot_no_trailing_dir_separator, p->fname, NULL);
	      free (sysroot_no_trailing_dir_separator);
	    }
	  else if (!p->add_sysroot && relocated
		   && !filename_ncmp (p->fname, cpp_PREFIX, cpp_PREFIX_len))
	    {
 	      static const char *relocated_prefix;
	      char *ostr;
	      /* If this path starts with the configure-time prefix,
		 but the compiler has been relocated, replace it
		 with the run-time prefix.  The run-time exec prefix
		 is GCC_EXEC_PREFIX.  Compute the path from there back
		 to the toplevel prefix.  */
	      if (!relocated_prefix)
		{
		  char *dummy;
		  /* Make relative prefix expects the first argument
		     to be a program, not a directory.  */
		  dummy = concat (gcc_exec_prefix, "dummy", NULL);
		  relocated_prefix
		    = make_relative_prefix (dummy,
					    cpp_EXEC_PREFIX,
					    cpp_PREFIX);
		  free (dummy);
		}
	      ostr = concat (relocated_prefix,
			     p->fname + cpp_PREFIX_len,
			     NULL);
	      str = update_path (ostr, p->component);
	      free (ostr);
	    }
	  else
	    str = update_path (p->fname, p->component);

	  if (p->multilib == 1 && imultilib)
	    str = reconcat (str, str, dir_separator_str, imultilib, NULL);
	  else if (p->multilib == 2)
	    {
	      if (!imultiarch)
		{
		  free (str);
		  continue;
		}
	      str = reconcat (str, str, dir_separator_str, imultiarch, NULL);
	    }

	  add_path (str, INC_SYSTEM, p->cxx_aware, false);
	}
    }
}

/* For each duplicate path in chain HEAD, keep just the first one.
   Remove each path in chain HEAD that also exists in chain SYSTEM.
   Set the NEXT pointer of the last path in the resulting chain to
   JOIN, unless it duplicates JOIN in which case the last path is
   removed.  Return the head of the resulting chain.  Any of HEAD,
   JOIN and SYSTEM can be NULL.  */

static struct cpp_dir *
remove_duplicates (cpp_reader *pfile, struct cpp_dir *head,
		   struct cpp_dir *system, struct cpp_dir *join,
		   int verbose)
{
  struct cpp_dir **pcur, *tmp, *cur;
  struct HOST_STAT_FOR_64BIT_INODES st;

  for (pcur = &head; *pcur; )
    {
      int reason = REASON_QUIET;

      cur = *pcur;

      if (HOST_STAT_FOR_64BIT_INODES (cur->name, &st))
	{
	  /* Dirs that don't exist or have denied permissions are
	     silently ignored, unless verbose.  */
	  if ((errno != ENOENT) && (errno != EPERM))
	    cpp_errno (pfile, CPP_DL_ERROR, cur->name);
	  else
	    {
	      /* If -Wmissing-include-dirs is given, warn.  */
	      cpp_options *opts = cpp_get_options (pfile);
	      if (opts->warn_missing_include_dirs && cur->user_supplied_p)
		cpp_warning (pfile, CPP_W_MISSING_INCLUDE_DIRS, "%s: %s",
			     cur->name, xstrerror (errno));
	      reason = REASON_NOENT;
	    }
	}
      else if (!S_ISDIR (st.st_mode))
	cpp_error_with_line (pfile, CPP_DL_WARNING, 0, 0,
			     "%s: not a directory", cur->name);
      else
	{
#if defined (INO_T_COPY)
	  INO_T_COPY (cur->ino, st.st_ino);
	  cur->dev  = st.st_dev;
#endif

	  /* Remove this one if it is in the system chain.  */
	  reason = REASON_DUP_SYS;
	  for (tmp = system; tmp; tmp = tmp->next)
	   if (DIRS_EQ (tmp, cur) && cur->construct == tmp->construct)
	      break;

	  if (!tmp)
	    {
	      /* Duplicate of something earlier in the same chain?  */
	      reason = REASON_DUP;
	      for (tmp = head; tmp != cur; tmp = tmp->next)
	       if (DIRS_EQ (cur, tmp) && cur->construct == tmp->construct)
		  break;

	      if (tmp == cur
		  /* Last in the chain and duplicate of JOIN?  */
		  && !(cur->next == NULL && join
		       && DIRS_EQ (cur, join)
		       && cur->construct == join->construct))
		{
		  /* Unique, so keep this directory.  */
		  pcur = &cur->next;
		  continue;
		}
	    }
	}

      /* Remove this entry from the chain.  */
      *pcur = cur->next;
      free_path (cur, verbose ? reason : REASON_QUIET);
    }

  *pcur = join;
  return head;
}

/* Add SYSROOT to any user-supplied paths in CHAIN starting with
   "=" or "$SYSROOT".  */

static void
add_sysroot_to_chain (const char *sysroot, int chain)
{
  struct cpp_dir *p;

  for (p = heads[chain]; p != NULL; p = p->next)
    {
      if (p->user_supplied_p)
	{
	  if (p->name[0] == '=')
	    p->name = concat (sysroot, p->name + 1, NULL);
	  if (startswith (p->name, "$SYSROOT"))
	    p->name = concat (sysroot, p->name + strlen ("$SYSROOT"), NULL);
	}
    }
}

/* Merge the four include chains together in the order quote, bracket,
   system, after.  Remove duplicate dirs (determined in
   system-specific manner).

   We can't just merge the lists and then uniquify them because then
   we may lose directories from the <> search path that should be
   there; consider -iquote foo -iquote bar -Ifoo -Iquux.  It is
   however safe to treat -iquote bar -iquote foo -Ifoo -Iquux as if
   written -iquote bar -Ifoo -Iquux.  */

static void
merge_include_chains (const char *sysroot, cpp_reader *pfile, int verbose)
{
  /* Add the sysroot to user-supplied paths starting with "=".  */
  if (sysroot)
    {
      add_sysroot_to_chain (sysroot, INC_QUOTE);
      add_sysroot_to_chain (sysroot, INC_BRACKET);
      add_sysroot_to_chain (sysroot, INC_SYSTEM);
      add_sysroot_to_chain (sysroot, INC_AFTER);
      add_sysroot_to_chain (sysroot, INC_EMBED);
    }

  /* Join the SYSTEM and AFTER chains.  Remove duplicates in the
     resulting SYSTEM chain.  */
  if (heads[INC_SYSTEM])
    tails[INC_SYSTEM]->next = heads[INC_AFTER];
  else
    heads[INC_SYSTEM] = heads[INC_AFTER];
  heads[INC_SYSTEM]
    = remove_duplicates (pfile, heads[INC_SYSTEM], 0, 0, verbose);

  /* Remove duplicates from BRACKET that are in itself or SYSTEM, and
     join it to SYSTEM.  */
  heads[INC_BRACKET]
    = remove_duplicates (pfile, heads[INC_BRACKET], heads[INC_SYSTEM],
			 heads[INC_SYSTEM], verbose);

  /* Remove duplicates from QUOTE that are in itself or SYSTEM, and
     join it to BRACKET.  */
  heads[INC_QUOTE]
    = remove_duplicates (pfile, heads[INC_QUOTE], heads[INC_SYSTEM],
			 heads[INC_BRACKET], verbose);

  /* Remove duplicates from EMBED that are in itself.  */
  heads[INC_EMBED]
    = remove_duplicates (pfile, heads[INC_EMBED], 0, 0, verbose);

  /* If verbose, print the list of dirs to search.  */
  if (verbose)
    {
      struct cpp_dir *p;

      fprintf (stderr, _("#include \"...\" search starts here:\n"));
      for (p = heads[INC_QUOTE];; p = p->next)
	{
	  if (p == heads[INC_BRACKET])
	    fprintf (stderr, _("#include <...> search starts here:\n"));
	  if (!p)
	    break;
	  fprintf (stderr, " %s\n", p->name);
	}
      fprintf (stderr, _("End of search list.\n"));
      if (heads[INC_EMBED])
	{
	  fprintf (stderr, _("#embed <...> search starts here:\n"));
	  for (p = heads[INC_EMBED]; p; p = p->next)
	    fprintf (stderr, " %s\n", p->name);
	  fprintf (stderr, _("End of #embed search list.\n"));
	}
    }
}

/* Use given -I paths for #include "..." but not #include <...>, and
   don't search the directory of the present file for #include "...".
   (Note that -I. -I- is not the same as the default setup; -I. uses
   the compiler's working dir.)  */
void
split_quote_chain (void)
{
  if (heads[INC_QUOTE])
    free_path (heads[INC_QUOTE], REASON_QUIET);
  if (tails[INC_QUOTE])
    free_path (tails[INC_QUOTE], REASON_QUIET);
  heads[INC_QUOTE] = heads[INC_BRACKET];
  tails[INC_QUOTE] = tails[INC_BRACKET];
  heads[INC_BRACKET] = NULL;
  tails[INC_BRACKET] = NULL;
  /* This is NOT redundant.  */
  quote_ignores_source_dir = true;
}

/* Add P to the chain specified by CHAIN.  */

void
add_cpp_dir_path (cpp_dir *p, incpath_kind chain)
{
  if (tails[chain])
    tails[chain]->next = p;
  else
    heads[chain] = p;
  tails[chain] = p;
}

/* Add PATH to the include chain CHAIN. PATH must be malloc-ed and
   NUL-terminated.  */
void
add_path (char *path, incpath_kind chain, int cxx_aware, bool user_supplied_p)
{
  cpp_dir *p;
  size_t pathlen = strlen (path);

#if defined (HAVE_DOS_BASED_FILE_SYSTEM)
  /* Remove unnecessary trailing slashes.  On some versions of MS
     Windows, trailing  _forward_ slashes cause no problems for stat().
     On newer versions, stat() does not recognize a directory that ends
     in a '\\' or '/', unless it is a drive root dir, such as "c:/",
     where it is obligatory.  */
  char* end = path + pathlen - 1;
  /* Preserve the lead '/' or lead "c:/".  */
  char* start = path + (pathlen > 2 && path[1] == ':' ? 3 : 1);

  for (; end > start && IS_DIR_SEPARATOR (*end); end--)
    *end = 0;
  pathlen = end - path;
#endif

  p = XNEW (cpp_dir);
  p->next = NULL;
  p->name = path;
  p->len = pathlen;
#ifndef INO_T_EQ
  p->canonical_name = lrealpath (path);
#endif
  if (chain == INC_SYSTEM || chain == INC_AFTER)
    p->sysp = 1 + !cxx_aware;
  else
    p->sysp = 0;
  p->construct = 0;
  p->user_supplied_p = user_supplied_p;

  add_cpp_dir_path (p, chain);
}

/* Exported function to handle include chain merging, duplicate
   removal, and registration with cpplib.  */
void
register_include_chains (cpp_reader *pfile, const char *sysroot,
			 const char *iprefix, const char *imultilib,
			 int stdinc, int cxx_stdinc, int verbose)
{
  static const char *const lang_env_vars[] =
    { "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
      "OBJC_INCLUDE_PATH", "OBJCPLUS_INCLUDE_PATH" };
  cpp_options *cpp_opts = cpp_get_options (pfile);
  size_t idx = (cpp_opts->objc ? 2 : 0);

  if (cpp_opts->cplusplus)
    idx++;
  else
    cxx_stdinc = false;

  /* CPATH and language-dependent environment variables may add to the
     include chain.  */
  add_env_var_paths ("CPATH", INC_BRACKET);
  add_env_var_paths (lang_env_vars[idx], INC_SYSTEM);

  target_c_incpath.extra_pre_includes (sysroot, iprefix, stdinc);

  /* Finally chain on the standard directories.  */
  if (stdinc)
    add_standard_paths (sysroot, iprefix, imultilib, cxx_stdinc);

  target_c_incpath.extra_includes (sysroot, iprefix, stdinc);

  merge_include_chains (sysroot, pfile, verbose);

  cpp_set_include_chains (pfile, heads[INC_QUOTE], heads[INC_BRACKET],
			  heads[INC_EMBED], quote_ignores_source_dir);
}

/* Return the current chain of cpp dirs.  */

struct cpp_dir *
get_added_cpp_dirs (incpath_kind chain)
{
  return heads[chain];
}

void
add_cpp_dep_pattern (cpp_dep_pattern *dep)
{
  if (deps_tail)
    deps_tail->next = dep;
  else
    deps_head = dep;
  dep->next = NULL;
  deps_tail = dep;
}

cpp_dep_pattern *
get_cpp_dep_patterns (void)
{
  return deps_head;
}

bool
cpp_dep_pattern_check_one (cpp_dep_pattern *dep,
			   const char *input, unsigned int input_len,
			   const char *real_path, unsigned int real_path_len)
{
  if (!cpp_dep_pattern_simple_check_one (dep, real_path, real_path_len))
    return false;
  if (input == NULL || input_len == 0)
    return false;
  /* For the input, it must match AND be front-anchored according
     to the regular expression's setup. The RE is already end-anchored,
     our only job now is to just check if it matched at the start, which
     saves us from having to store 2 regices to test the two different
     paths.  */
  int input_match_at = re_search (dep->pattern_regex, input, input_len,
				      0, input_len, NULL);
  return input_match_at == 0;
}

bool
cpp_dep_pattern_check_from (cpp_dep_pattern *dep,
			    const char *input, unsigned int input_len,
			    const char *real_path, unsigned int real_path_len)
{
  cpp_dep_pattern *p = dep;
  for (; p != NULL; p = p->next)
    {
      if (cpp_dep_pattern_check_one (p, input, input_len,
				     real_path, real_path_len))
	return true;
    }
  return false;
}

bool
cpp_dep_pattern_check (const char *input, unsigned int input_len,
		       const char *real_path, unsigned int real_path_len)
{
  return cpp_dep_pattern_check_from (get_cpp_dep_patterns (),
				     input, input_len,
				     real_path, real_path_len);
}

bool
cpp_dep_pattern_simple_check (const char *real_path,
			      unsigned int real_path_len)
{
  return cpp_dep_pattern_simple_check_from (get_cpp_dep_patterns (),
					    real_path, real_path_len);
}

/* Append the file name to the directory to create the path, but don't
   turn / into // or // into ///; // may be a namespace escape.  */
static char *
append_file_n_to_dir (const char *fname, size_t fname_len, cpp_dir *dir)
{
  return append_file_n_to_dir_name_n (fname, fname_len, dir->name, dir->len);
}

bool
search_path_kind (const char *path, unsigned int path_len, incpath_kind kind,
		  const char *maybe_lookup_dir, const char **result_name,
		  cpp_dir **found_dir)
{
  return search_path_with_dir (path, path_len, heads[kind],
			       maybe_lookup_dir, result_name, found_dir);
}

bool
search_path_with_dir (const char *path, unsigned int path_len,
		      cpp_dir *dir_list, const char *maybe_lookup_dir,
		      const char **result_name, cpp_dir **found_dir)
{
  if (path_len == 0 || (maybe_lookup_dir == NULL && dir_list == NULL))
    {
      /* Nothing to do.  */
      if (found_dir)
	*found_dir = NULL;
      return false;
    }
  if (IS_ANY_ABSOLUTE_PATH_N(path, path_len))
    {
      /* There is nothing to put in found_dir, since it was absolute.  */
      struct stat buf;
      if (stat(path, &buf) == 0)
	{
	  if (found_dir)
	    *found_dir = NULL;
	  return true;
	}
      return false;
    }

  char *current_path = NULL;
  bool result = false;
  if (maybe_lookup_dir)
    {
      /* Just try to see if it exists on the local path first.  */
      current_path = append_file_n_to_dir_name (path, path_len,
					   maybe_lookup_dir);
      struct stat buf;
      if (stat (current_path, &buf) == 0)
	{
	  if (found_dir)
	    *found_dir = NULL;
	  result = true;
	  goto done;
	}
      free ((void*)current_path);
      current_path = NULL;
    }

  for (cpp_dir* dir = dir_list; dir; dir = dir->next)
    {
      // FIXME: construct takes 2 null-term paths. would need to be fixed.
      if (dir->construct)
	current_path = dir->construct (path, dir);
      else
	current_path = append_file_n_to_dir (path, path_len, dir);
      struct stat buf;
      if (stat (current_path, &buf) == 0)
	{
	  if (found_dir)
	    *found_dir = dir;
	  result = true;
	  goto done;
	}
      free ((void*)current_path);
      current_path = NULL;
    }
done:
  if (result_name == NULL || !result)
    {
      free ((void*)current_path);
    }

  if (result_name != NULL)
    *result_name = result ? current_path : NULL;
  return result;
}

#if !(defined TARGET_EXTRA_INCLUDES) || !(defined TARGET_EXTRA_PRE_INCLUDES)
static void hook_void_charptr_charptr_int (const char *sysroot ATTRIBUTE_UNUSED,
					   const char *iprefix ATTRIBUTE_UNUSED,
					   int stdinc ATTRIBUTE_UNUSED)
{
}
#endif

#ifndef TARGET_EXTRA_INCLUDES
#define TARGET_EXTRA_INCLUDES hook_void_charptr_charptr_int
#endif
#ifndef TARGET_EXTRA_PRE_INCLUDES
#define TARGET_EXTRA_PRE_INCLUDES hook_void_charptr_charptr_int
#endif

struct target_c_incpath_s target_c_incpath = { TARGET_EXTRA_PRE_INCLUDES, TARGET_EXTRA_INCLUDES };
