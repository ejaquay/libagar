/*	Public domain	*/

#ifndef _MAPEDIT_H_
#define _MAPEDIT_H_

#include "config/enable_nls.h"
#ifdef ENABLE_NLS
# include <libintl.h>
# define _(String) gettext(String)
# define gettext_noop(String) (String)
# define N_(String) gettext_noop(String)
#else
# undef _
# undef N_
# define _(s) (s)
# define N_(s) (s)
#endif

#endif /* _MAPEDIT_H_ */
