#ifndef LIBXML_H

# define LIBXML_H

# include <libxml/parser.h>

char *xmlGetPropAsString(xmlNodePtr, const char *);
uint32_t xmlGetPropAsInt(xmlNodePtr, const char *);

#endif /* !LIBXML_H */
