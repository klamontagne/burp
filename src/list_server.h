#ifndef _LIST_SERVER_H
#define _LIST_SERVER_H

extern int check_browsedir(const char *browsedir, char **path, size_t bdlen, char **lastpath);
extern int do_list_server(const char *basedir, const char *backup, const char *listregex, const char *browsedir, const char *client, struct cntr *p1cntr, struct cntr *cntr);

#endif // _LIST_SERVER_H
