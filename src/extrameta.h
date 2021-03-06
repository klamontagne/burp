#ifndef _EXTRAMETA_H
#define _EXTRAMETA_H

#define META_ACCESS_ACL		'A'
#define META_DEFAULT_ACL	'D'
#define META_XATTR		'X'
#define META_XATTR_BSD		'B'

extern int has_extrameta(const char *path, char cmd);
extern int get_extrameta(const char *path, struct stat *statp, char **extrameta, size_t *elen, struct cntr *cntr);
extern int set_extrameta(const char *path, char cmd, struct stat *statp, const char *extrameta, size_t metalen, struct cntr *cntr);

#endif // _EXTRAMETA_H
