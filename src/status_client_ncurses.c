/* Client of the server status. Runs on the server machine and connects to the
   burp server to get status information. */

#include "burp.h"
#include "prog.h"
#include "handy.h"
#include "lock.h"
#include "cmd.h"
#include "current_backups_server.h"

#ifdef HAVE_NCURSES_H
#include "ncurses.h"
// So that the sighandler can call endwin():
static enum action actg=ACTION_STATUS;
#endif

#define LEFT_SPACE	3
#define TOP_SPACE	2

//#define DBFP	1
#ifdef DBFP
static FILE *dbfp=NULL;
#endif

static void print_line(const char *string, int row, int col)
{
	int k=0;
	const char *cp=NULL;
#ifdef HAVE_NCURSES_H
	if(actg==ACTION_STATUS)
	{
		while(k<LEFT_SPACE) mvprintw(row+TOP_SPACE, k++, " ");
		for(cp=string; (*cp && k<col); cp++)
			mvprintw(row+TOP_SPACE, k++, "%c", *cp);
		while(k<col) mvprintw(row+TOP_SPACE, k++, " ");
		return;
	}
#endif
	while(k<LEFT_SPACE) { printf(" "); k++; }
	for(cp=string; (*cp && k<col); cp++)
		{ printf("%c", *cp); k++; }
	printf("\n");
}

static char *get_backup_str(const char *s)
{
	static char str[32]="";
	const char *cp=NULL;
	if(!(cp=strchr(s, ' ')))
		snprintf(str, sizeof(str), "never");
	else
	{
		unsigned long backupnum=0;
		backupnum=strtoul(s, NULL, 10);
		snprintf(str, sizeof(str),
			"%07lu %s", backupnum, getdatestr(atol(cp+1)));
	}
	return str;
}

static char *running_status_to_text(char s)
{
	static char ret[16]="";
	switch(s)
	{
		case STATUS_SCANNING:
			snprintf(ret, sizeof(ret), "scanning"); break;
		case STATUS_BACKUP:
			snprintf(ret, sizeof(ret), "backup"); break;
		case STATUS_MERGING:
			snprintf(ret, sizeof(ret), "merging"); break;
		case STATUS_SHUFFLING:
			snprintf(ret, sizeof(ret), "shuffling"); break;
		case STATUS_LISTING:
			snprintf(ret, sizeof(ret), "listing"); break;
		case STATUS_RESTORING:
			snprintf(ret, sizeof(ret), "restoring"); break;
		case STATUS_VERIFYING:
			snprintf(ret, sizeof(ret), "verifying"); break;
		default:
			*ret='\0';
			break;
	}
	return ret;
}

// Returns 1 if it printed a line, 0 otherwise.
static int summary(const char *cntrclient, char status, char phase, const char *path, struct cntr *p1cntr, struct cntr *cntr, char **backups, int count, int row, int col)
{
	char msg[1024]="";

	if(status==STATUS_IDLE)
	{
		snprintf(msg, sizeof(msg),
			"%-14.14s %-14s%s%s", cntrclient, "idle",
			backups && backups[0]?" last backup: ":"",
			backups && backups[0]?backups[0]:"");
	}
	if(status==STATUS_SERVER_CRASHED)
	{
		snprintf(msg, sizeof(msg),
			"%-14.14s %-14s%s%s", cntrclient, "server crashed",
			backups && backups[0]?" last backup: ":"",
			backups && backups[0]?backups[0]:"");
	}
	if(status==STATUS_CLIENT_CRASHED)
	{
		snprintf(msg, sizeof(msg),
			"%-14.14s %-14s%s%s", cntrclient, "client crashed",
			backups && backups[0]?" last backup: ":"",
			backups && backups[0]?backups[0]:"");
	}
	if(status==STATUS_RUNNING)
	{
		char f[64]="";
		char b[64]="";
		const char *s="";
	  	unsigned long long p=0;
		unsigned long long t=0;

		s=running_status_to_text(phase);
		t=cntr->total+cntr->total_same+cntr->total_changed;
		if(p1cntr->total) p=(t*100)/p1cntr->total;
		snprintf(f, sizeof(f), "%llu/%llu %llu%%",
			t, p1cntr->total, p);
		if(cntr->byte)
			snprintf(b, sizeof(b), "%s",
				bytes_to_human(cntr->byte));
		snprintf(msg, sizeof(msg), "%-14.14s %-14s %s%s",
			cntrclient, s, f, b);
	}
	if(*msg)
	{
		print_line(msg, count, col);
		return 1;
	}
	return 0;
}

static void show_all_backups(char **backups, int *x, int col)
{
	int b=0;
	char msg[256]="";
	for(b=0; backups && backups[b]; b++)
	{
		if(!b)
		{
		  snprintf(msg, sizeof(msg), "Backup list: %s", backups[b]);
		  print_line(msg, (*x)++, col);
		}
		else
		{
		  snprintf(msg, sizeof(msg), "             %s", backups[b]);
		  print_line(msg, (*x)++, col);
		}
	}
}

/* for the counters */
static void to_msg(char msg[], size_t s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, s, fmt, ap);
	va_end(ap);
}

static void print_detail(char phase,
	const char *field,
	unsigned long long a,
	unsigned long long b,
	unsigned long long c,
	unsigned long long d,
	unsigned long long e,
	int *x, int col, int percent)
{
	char msg[256]="";
	unsigned long long t=a+b+c;
	if(!field || (!t && !d && !e)) return;

	if(phase==STATUS_RESTORING
	  || phase==STATUS_VERIFYING)
	{
		to_msg(msg, sizeof(msg),
			"% 15s % 9s % 9llu % 9llu",
			field, "", t, e);
	}
	else
	{
		to_msg(msg, sizeof(msg),
			"% 15s % 9llu % 9llu % 9llu % 9llu % 9llu % 9llu",
			field, a, b, c, d, t, e);
	}
	print_line(msg, (*x)++, col);
	if(percent && e)
	{
	  unsigned long long p;
	  p=(t*100)/e;
	  if(phase==STATUS_RESTORING
	    || phase==STATUS_VERIFYING)
	  {
	    to_msg(msg, sizeof(msg), "% 15s % 9s % 9llu%% % 9s",
		"", "", p, "");
	  }
	  else
	  {
	    to_msg(msg, sizeof(msg), "% 15s % 9s % 9s % 9s % 9s % 9llu%% % 9s",
		"", "", "", "", "", p, "");
	  }
	  print_line(msg, (*x)++, col);
	}
}

static void table_header(char phase, int *x, int col)
{
	char msg[256]="";
	if(phase==STATUS_RESTORING
	  || phase==STATUS_VERIFYING)
	{
	  to_msg(msg, sizeof(msg), "% 15s % 9s % 9s % 9s",
	    "", "", "Attempted", "Expected");
	}
	else
	{
	  to_msg(msg, sizeof(msg), "% 15s % 9s % 9s % 9s % 9s % 9s % 9s",
	    "", "New", "Changed", "Unchanged", "Deleted", "Total", "Scanned");
	}
	print_line(msg, (*x)++, col);
}

static void print_detail2(const char *field, unsigned long long value1, const char *value2, int *x, int col)
{
	char msg[256]="";
	if(!field || !value1 || !value2 || !*value2) return;
	snprintf(msg, sizeof(msg), "%s: %llu%s", field, value1, value2);
	print_line(msg, (*x)++, col);
}

static void print_detail3(const char *field, const char *value, int *x, int col)
{
	char msg[256]="";
	if(!field || !value || !*value) return;
	snprintf(msg, sizeof(msg), "%s: %s", field, value);
	print_line(msg, (*x)++, col);
}

static void detail(const char *cntrclient, char status, char phase, const char *path, struct cntr *p1cntr, struct cntr *cntr, char **backups, int row, int col)
{
	int x=0;
	char msg[1024]="";
	const char *tmp=NULL;
	if(cntrclient)
	{
		snprintf(msg, sizeof(msg), "Client: %s", cntrclient);
		print_line(msg, x++, col);
	}
	switch(status)
	{
		case STATUS_IDLE:
		{
			print_line("Status: idle", x++, col);
			show_all_backups(backups, &x, col);
			return;
		}
		case STATUS_SERVER_CRASHED:
		{
			print_line("Status: server crashed", x++, col);
			show_all_backups(backups, &x, col);
			return;
		}
		case STATUS_CLIENT_CRASHED:
		{
			print_line("Status: client crashed", x++, col);
			show_all_backups(backups, &x, col);
			return;
		}
		case STATUS_RUNNING:
		{
			if(phase)
			{
				char msg[64]="";
				snprintf(msg, sizeof(msg),
					"Status: running (%s)",
					running_status_to_text(phase));
				print_line(msg, x++, col);
			}
			break;
		}
	}
	print_line("", x++, col);
	table_header(phase, &x, col);
	print_detail(phase, "Files",
				cntr->file,
				cntr->file_changed,
				cntr->file_same,
				cntr->file_deleted,
				p1cntr->file,
				&x, col, 0);
	print_detail(phase, "Encrypted files",
				cntr->enc,
				cntr->enc_changed,
				cntr->enc_same,
				cntr->enc_deleted,
				p1cntr->enc,
				&x, col, 0);
	print_detail(phase, "Meta data",
				cntr->meta,
				cntr->meta_changed,
				cntr->meta_same,
				cntr->meta_deleted,
				p1cntr->meta,
				&x, col, 0);
	print_detail(phase, "Encrypted meta data",
				cntr->encmeta,
				cntr->encmeta_changed,
				cntr->encmeta_same,
				cntr->encmeta_deleted,
				p1cntr->encmeta,
				&x, col, 0);
	print_detail(phase, "Directories",
				cntr->dir,
				cntr->dir_changed,
				cntr->dir_same,
				cntr->dir_deleted,
				p1cntr->dir,
				&x, col, 0);
	print_detail(phase, "Soft links",
				cntr->slink,
				cntr->slink_changed,
				cntr->slink_same,
				cntr->slink_deleted,
				p1cntr->slink,
				&x, col, 0);
	print_detail(phase, "Hard links",
				cntr->hlink,
				cntr->hlink_changed,
				cntr->hlink_same,
				cntr->hlink_deleted,
				p1cntr->hlink,
				&x, col, 0);
	print_detail(phase, "Special files",
				cntr->special,
				cntr->special_changed,
				cntr->special_same,
				cntr->special_deleted,
				p1cntr->special,
				&x, col, 0);
	print_detail(phase, "Total",
				cntr->gtotal,
				cntr->gtotal_changed,
				cntr->gtotal_same,
				cntr->gtotal_deleted,
				p1cntr->gtotal,
				&x, col, 1);
	print_line("", x++, col);
	print_detail(phase, "Warnings",
				cntr->warning, 0, 0, 0, 0,
				&x, col, 1);

	if(p1cntr->byte)
	{
		tmp=bytes_to_human(p1cntr->byte);
		print_detail2("Bytes estimated", p1cntr->byte, tmp, &x, col);
	}
	if(cntr->byte)
	{
		const char *text=NULL;
		if(phase==STATUS_BACKUP) text="Bytes in backup";
		else if(phase==STATUS_RESTORING) text="Bytes attempted";
		else if(phase==STATUS_VERIFYING) text="Bytes checked";
		tmp=bytes_to_human(cntr->byte);
		if(text) print_detail2(text, cntr->byte, tmp, &x, col);
	}
	if(cntr->recvbyte)
	{
		const char *text=NULL;
		tmp=bytes_to_human(cntr->recvbyte);
		if(phase==STATUS_BACKUP) text="Bytes received";
		if(text) print_detail2(text, cntr->recvbyte, tmp, &x, col);
	}
	if(cntr->sentbyte)
	{
		const char *text=NULL;
		if(phase==STATUS_BACKUP) text="Bytes sent";
		else if(phase==STATUS_RESTORING) text="Bytes sent";
		tmp=bytes_to_human(cntr->sentbyte);
		print_detail2(text, cntr->sentbyte, tmp, &x, col);
	}
	if(p1cntr->start)
	{
		time_t now=0;
		time_t diff=0;
		now=time(NULL);
		diff=now-p1cntr->start;

		print_detail3("Start time", getdatestr(p1cntr->start), &x,col);
		print_detail3("Time taken", time_taken(diff), &x, col);

		if(diff>0)
		{
			unsigned long long bytesleft=0;
			unsigned long long byteswant=0;
			unsigned long long bytesgot=0;
			float bytespersec=0;
			byteswant=p1cntr->byte;
			bytesgot=cntr->byte;
			bytespersec=(float)(bytesgot/diff);
			bytesleft=byteswant-bytesgot;
			if(bytespersec>0)
			{
				time_t timeleft=0;
				timeleft=(time_t)(bytesleft/bytespersec);
				print_detail3("Time left",
					time_taken(timeleft), &x, col);
			}
		}
	}
	if(path && *path)
	{
#ifdef HAVE_NCURSES_H
		if(actg==ACTION_STATUS)
		{
			printw("\n%s\n", path);
			return;
		}
#else
		printf("\n%s\n", path);
#endif
	}
}

static void blank_screen(int row, int col)
{
	int c=0;
	int l=0;
	const char *date=NULL;
	time_t t=time(NULL);
	date=getdatestr(t);
	l=strlen(date);
#ifdef HAVE_NCURSES_H
	if(actg==ACTION_STATUS)
	{
		char v[32]="";
		clear();
		snprintf(v, sizeof(v), " burp monitor %s", VERSION);
		mvprintw(0, 0, v);
		mvprintw(0, col-l-1, date);
		return;
	}
#endif

	printf("\n burp status");
	for(c=0; c<(int)(col-strlen(" burp status")-l-1); c++) printf(" ");
	printf("%s\n\n", date);
}

static int parse_rbuf(const char *rbuf, struct config *conf, int row, int col, int sel, char **client, int *count, int details, const char *sclient, struct cntr *p1cntr, struct cntr *cntr)
{
	char *cp=NULL;
	char *dp=NULL;
	char *copy=NULL;

	if(!(copy=strdup(rbuf)))
	{
		logp("out of memory\n");
		return -1;
	}

	dp=copy;
	*count=0;

	// First, blank the whole screen.
	blank_screen(row, col);
	while((cp=strchr(dp, '\n')))
	{
		char status='\0';
		char phase='\0';
		char *path=NULL;
		char **backups=NULL;
		char *cntrclient=NULL;
		*cp='\0';

		if(str_to_counters(dp, &cntrclient, &status, &phase, &path,
			p1cntr, cntr, &backups))
		{
			free(copy);
			if(path) free(path);
			if(cntrclient) free(cntrclient);
			return -1;
		}

		if(!cntrclient) continue;

		if(details)
		{
			if(*count==sel || sclient)
			{
				if(cntrclient
				  && (!*client
				    || strcmp(cntrclient, *client)))
				{
					if(*client) free(*client);
					*client=strdup(cntrclient);
				}
				if(!sclient
				  || (cntrclient
				    && !strcmp(cntrclient, sclient)))
					detail(cntrclient, status, phase,
						path, p1cntr, cntr,
						backups, 0, col);
			}
		}
		else
		{
			summary(cntrclient, status, phase,
				path, p1cntr, cntr, backups,
				*count, row, col);
		}
		(*count)++;

		dp=cp+1;
		if(path) free(path);
		if(cntrclient) free(cntrclient);
		if(backups)
		{
			int b=0;
			for(b=0; backups && backups[b]; b++) free(backups[b]);
			free(backups);
		}
	}
	if(copy) free(copy);
	return 0;
}

static int need_status(void)
{
	static time_t lasttime=0;
	time_t now=0;
	time_t diff=0;

	// Only ask for an update every second.
	now=time(NULL);
	diff=now-lasttime;
	if(diff<1)
	{
		// In case they fiddled their clock back in time.
		if(diff<0) lasttime=now;
		return 0;
	}
	lasttime=now;
	return 1;
}

static void print_star(int sel)
{
#ifdef HAVE_NCURSES_H
	if(actg==ACTION_STATUS)
	{
		mvprintw(sel+TOP_SPACE, 1, "*");
		return;
	}
#endif
}

// Return 1 if it was shown, -1 on error, 0 otherwise.
static int show_rbuf(const char *rbuf, struct config *conf, int sel, char **client, int *count, int details, const char *sclient, struct cntr *p1cntr, struct cntr *cntr)
{
	int rbuflen=0;
	if(!rbuf) return 0;
	rbuflen=strlen(rbuf);
#ifdef DBFP
	if(dbfp) { fprintf(dbfp, "%s\n", rbuf);  fflush(dbfp); }
#endif

	if(rbuflen>2
		&& rbuf[rbuflen-1]=='\n'
		&& rbuf[rbuflen-2]=='\n')
	{
		int row=24;
		int col=80;
#ifdef HAVE_NCURSES_H
		if(actg==ACTION_STATUS) getmaxyx(stdscr, row, col);
#endif
		if(parse_rbuf(rbuf, conf, row, col,
			sel, client, count, details, sclient, p1cntr, cntr))
				return -1;
		if(sel>=*count) sel=(*count)-1;
		if(!details) print_star(sel);
#ifdef HAVE_NCURSES_H
		if(actg==ACTION_STATUS) refresh();
#endif
		return 1;
	}
	return 0;
}

static int request_status(int fd, const char *client, struct config *conf)
{
	int l;
	char buf[256]="";
	if(client)
	{
		if(conf->backup)
		{
			if(conf->browsedir)
			{
				snprintf(buf, sizeof(buf), "c:%s:b:%s:p:%s\n",
					client, conf->backup, conf->browsedir);
			}
			else if(conf->browsefile)
			{
				snprintf(buf, sizeof(buf), "c:%s:b:%s:f:%s\n",
					client, conf->backup, conf->browsefile);
			}
			else
			{
				snprintf(buf, sizeof(buf), "c:%s:b:%s\n",
					client, conf->backup);
			}
		}
		else
		{
			snprintf(buf, sizeof(buf), "c:%s\n", client);
		}
	}
	else snprintf(buf, sizeof(buf), "\n");
#ifdef DBFP
fprintf(dbfp, "request: %s\n", buf); fflush(dbfp);
#endif
	l=strlen(buf);
	if(write(fd, buf, l)<0) return -1;
	return 0;
}

static void sighandler(int sig)
{
#ifdef HAVE_NCURSES_H
	if(actg==ACTION_STATUS) endwin();
#endif
        logp("got signal: %d\n", sig);
	if(sig==SIGPIPE) logp("Server may have too many active status clients.\n");
        logp("exiting\n");
        exit(1);
}

static void setup_signals(void)
{
	signal(SIGABRT, &sighandler);
	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);
	signal(SIGPIPE, &sighandler);
}

int status_client_ncurses(struct config *conf, enum action act, const char *sclient)
{
	int fd=0;
        int ret=0;
	int sel=0;
	char *rbuf=NULL;
	char buf[512]="";
	int count=0;
	int details=0;
	char *last_rbuf=NULL;
	int srbr=0;
	char *client=NULL;
	int enterpressed=0;
//	int loop=0;
	int reqdone=0;
	struct cntr p1cntr;
	struct cntr cntr;

#ifdef HAVE_NCURSES_H
	int stdinfd=fileno(stdin);
	actg=act; // So that the sighandler can call endwin().
#else
	if(act==ACTION_STATUS)
	{
		printf("To use the live status monitor, you need to recompile with ncurses support.\n");
		return -1;
	}
#endif

	setup_signals();

	/* NULL == ::1 or 127.0.0.1 */
	if((fd=init_client_socket(NULL, conf->status_port))<0)
		return -1;
	set_non_blocking(fd);

#ifdef HAVE_NCURSES_H
	if(actg==ACTION_STATUS)
	{
		initscr();
		start_color();
		init_pair(1, COLOR_WHITE, COLOR_BLACK);
		init_pair(2, COLOR_WHITE, COLOR_BLACK);
		init_pair(3, COLOR_WHITE, COLOR_BLACK);
		raw();
		keypad(stdscr, TRUE);
		noecho();
		curs_set(0);
		halfdelay(3);
		//nodelay(stdscr, TRUE);
	}
#endif
#ifdef DBFP
	dbfp=fopen("/tmp/dbfp", "w");
#endif

	while(!ret)
	{
		int l;
		int mfd=-1;
		fd_set fsr;
		fd_set fse;
		struct timeval tval;

		// Failsafe to prevent the snapshot ever getting permanently
		// stuck.
		//if(act==ACTION_STATUS_SNAPSHOT && loop++>10000)
		//	break;

		if(sclient && !client)
		{
			client=strdup(sclient);
			details=1;
		}

		if((enterpressed || need_status()) && !reqdone)
		{
			char *req=NULL;
			if(details && client) req=client;
			if(request_status(fd, req, conf))
			{
				ret=-1;
				break;
			}
			enterpressed=0;
			if(act==ACTION_STATUS_SNAPSHOT)
				reqdone++;
		}

		FD_ZERO(&fsr);
		FD_ZERO(&fse);

		tval.tv_sec=1;
		tval.tv_usec=0;

		add_fd_to_sets(fd, &fsr, NULL, &fse, &mfd);
#ifdef HAVE_NCURSES_H
		if(actg==ACTION_STATUS)
			add_fd_to_sets(stdinfd, &fsr, NULL, &fse, &mfd);
#endif

		if(select(mfd+1, &fsr, NULL, &fse, &tval)<0)
		{
			if(errno!=EAGAIN && errno!=EINTR)
			{
				logp("select error: %s\n",
					strerror(errno));
				ret=-1;
				break;
			}
			continue;
		}

		if(FD_ISSET(fd, &fse))
		{
			ret=-1;
			break;
		}

#ifdef HAVE_NCURSES_H
		if(actg==ACTION_STATUS)
		{
			if(FD_ISSET(stdinfd, &fse))
			{
				ret=-1;
				break;
			}
			if(FD_ISSET(stdinfd, &fsr))
			{
				int quit=0;
				
				switch(getch())
				{
					case 'q':
					case 'Q':
						quit++;
						break;
					case KEY_UP:
					case 'k':
					case 'K':
						if(details) break;
						sel--;
						break;
					case KEY_DOWN:
					case 'j':
					case 'J':
						if(details) break;
						sel++;
						break;
					case KEY_ENTER:
					case '\n':
					case ' ':
						if(details) details=0;
						else details++;
						enterpressed++;
						break;
					case KEY_LEFT:
					case 'h':
					case 'H':
						details=0;
						break;
					case KEY_RIGHT:
					case 'l':
					case 'L':
						details++;
						break;
					case KEY_NPAGE:
					{
						int row=0, col=0;
						getmaxyx(stdscr, row, col);
						sel+=row-TOP_SPACE;
						break;
					}
					case KEY_PPAGE:
					{
						int row=0, col=0;
						getmaxyx(stdscr, row, col);
						sel-=row-TOP_SPACE;
						break;
					}
				}
				if(quit) break;

				if(sel>=count) sel=count-1;
				if(sel<0) sel=0;

				// Attempt to print stuff to the screen right
				// now, to give the impression of key strokes
				// being responsive.
				if(!details && !sclient)
				{
				  if((srbr=show_rbuf(last_rbuf,
					conf, sel, &client,
					&count, details, sclient,
					&p1cntr, &cntr))<0)
				  {
					ret=-1;
					break;
				  }
				  if(!details) print_star(sel);
				
				  refresh();
				}
			}
		}
#endif

		if(FD_ISSET(fd, &fsr))
		{
			// ready to read.
			if((l=read(fd, buf, sizeof(buf)-1))>0)
			{
				size_t r=0;
				buf[l]='\0';
				if(rbuf) r=strlen(rbuf);
				rbuf=(char *)realloc(rbuf, r+l+1);
				if(!r) *rbuf='\0';
				strcat(rbuf+r, buf);
			}
			else
				break;

			if(act==ACTION_STATUS_SNAPSHOT)
			{
				if(rbuf)
				{
					if(!strcmp(rbuf, "\n"))
					{
						// This happens when there are
						// no backup clients.
						break;
					}
					if(strstr(rbuf, "\n-list end-\n"))
					{
						printf("%s", rbuf);
						break;
					}
				}
				continue;
			}

			//if(rbuf) printf("rbuf: %s\n", rbuf);
/*
			if(l<0)
			{
				ret=-1;
				break;
			}
*/
		}

		if((srbr=show_rbuf(rbuf, conf,
			sel, &client, &count, details, sclient,
			&p1cntr, &cntr))<0)
		{
			ret=-1;
			break;
		}
		else if(srbr)
		{
			// Remember it, so that we can present the detailed
			// screen without delay, above.
			if(last_rbuf) free(last_rbuf);
			last_rbuf=rbuf;
			rbuf=NULL;
		}

		if(sclient) details++;

		usleep(20000);
#ifdef HAVE_NCURSES_H
		if(actg==ACTION_STATUS)
		{
			flushinp();
			continue;
		}
#endif
		if(count)
		{
			printf("\n");
			break;
		}
	}
#ifdef HAVE_NCURSES_H
	if(actg==ACTION_STATUS) endwin();
#endif
	close_fd(&fd);
	if(last_rbuf) free(last_rbuf);
	if(rbuf) free(rbuf);
#ifdef DBFP
	if(dbfp) fclose(dbfp);
#endif
	return ret;
}
