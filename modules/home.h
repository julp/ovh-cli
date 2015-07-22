#ifndef HOME_H

# include <limits.h>
# if !defined(MAXPATHLEN) && defined(PATH_MAX)
#  define MAXPATHLEN PATH_MAX
# endif /* !MAXPATHLEN && PATH_MAX */

bool build_path_from_home(const char *, char *, size_t);

#endif /* HOME_H */
