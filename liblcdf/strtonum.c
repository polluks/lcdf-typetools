#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "strtonum.h"
#include <stdlib.h>
#ifdef BROKEN_STRTOD
# define strtod good_strtod
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* A faster strtod which only calls the real strtod if the string has a
   fractional part. */

double
strtonumber(const char *f, char **endf)
{
  long v = strtol((char *)f, endf, 10);

  /* handle any possible decimal part */
  if (**endf == '.' || **endf == 'E' || **endf == 'e')
    return strtod((char *)f, endf);
  else
    return v;
}

#ifdef __cplusplus
}
#endif
