#include "burp.h"
#include "prog.h"
#include "msg.h"
#include "rs_buf.h"
#include "lock.h"
#include "handy.h"
#include "asyncio.h"
#include "counter.h"
#include "extrameta.h"
#include "backup_phase1_client.h"

static char filesymbol=CMD_FILE;
static char metasymbol=CMD_METADATA;

static int maybe_send_extrameta(const char *path, char cmd, const char *attribs, struct cntr *p1cntr)
{
	if(has_extrameta(path, cmd))
	{
		if(async_write_str(CMD_STAT, attribs)
		  || async_write_str(metasymbol, path))
			return -1;
		do_filecounter(p1cntr, metasymbol, 1);
	}
	return 0;
}

int send_file(FF_PKT *ff, bool top_level, struct config *conf, struct cntr *p1cntr)
{
   char msg[128]="";
   char attribs[MAXSTRING];

   if(!file_is_included(conf->incexcdir, conf->iecount,
	conf->incext, conf->incount,
	conf->excext, conf->excount,
	conf->increg, conf->ircount,
	conf->excreg, conf->ercount,
	ff->fname, top_level)) return 0;
#ifdef HAVE_WIN32
// Useful Windows attributes debug
/*
	printf("\n%llu", ff->winattr);
	printf("\n%s\n", ff->fname);
if(ff->winattr & FILE_ATTRIBUTE_READONLY) printf("readonly\n");
if(ff->winattr & FILE_ATTRIBUTE_HIDDEN) printf("hidden\n");
if(ff->winattr & FILE_ATTRIBUTE_SYSTEM) printf("system\n");
if(ff->winattr & FILE_ATTRIBUTE_DIRECTORY) printf("directory\n");
if(ff->winattr & FILE_ATTRIBUTE_ARCHIVE) printf("archive\n");
if(ff->winattr & FILE_ATTRIBUTE_DEVICE) printf("device\n");
if(ff->winattr & FILE_ATTRIBUTE_NORMAL) printf("normal\n");
if(ff->winattr & FILE_ATTRIBUTE_TEMPORARY) printf("temporary\n");
if(ff->winattr & FILE_ATTRIBUTE_SPARSE_FILE) printf("sparse\n");
if(ff->winattr & FILE_ATTRIBUTE_REPARSE_POINT) printf("reparse\n");
if(ff->winattr & FILE_ATTRIBUTE_COMPRESSED) printf("compressed\n");
if(ff->winattr & FILE_ATTRIBUTE_OFFLINE) printf("offline\n");
if(ff->winattr & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) printf("notcont\n");
if(ff->winattr & FILE_ATTRIBUTE_ENCRYPTED) printf("encrypted\n");
if(ff->winattr & FILE_ATTRIBUTE_VIRTUAL) printf("virtual\n");
*/
	if(ff->winattr & FILE_ATTRIBUTE_ENCRYPTED)
	{
//		if(ff->type!=FT_DIREND)
//			logw(p1cntr, "EFS not yet supported: %s", ff->fname);
//		return 0;

		if(ff->type==FT_REGE
		  || ff->type==FT_REG
		  || ff->type==FT_DIRBEGIN)
		{
			encode_stat(attribs,
				&ff->statp, ff->winattr, conf->compression);
			if(async_write_str(CMD_STAT, attribs)
			  || async_write_str(CMD_EFS_FILE, ff->fname))
				return -1;
			do_filecounter(p1cntr, CMD_EFS_FILE, 1);
			if(ff->type==FT_REG)
				do_filecounter_bytes(p1cntr,
					(unsigned long long)ff->statp.st_size);
			return 0;
		}
		else if(ff->type==FT_DIREND)
			return 0;
		else
		{
			// Hopefully, here is never reached.
			logw(p1cntr, "EFS type %d not yet supported: %s",
				ff->type,
				ff->fname);
			return 0;
		}
	}
#endif

   //logp("%d: %s\n", ff->type, ff->fname);

   switch (ff->type) {
   case FT_LNKSAVED:
        //printf("Lnka: %s -> %s\n", ff->fname, ff->link);
   	encode_stat(attribs, &ff->statp, ff->winattr, conf->compression);
	if(async_write_str(CMD_STAT, attribs)
	  || async_write_str(CMD_HARD_LINK, ff->fname)
	  || async_write_str(CMD_HARD_LINK, ff->link))
		return -1;
	do_filecounter(p1cntr, CMD_HARD_LINK, 1);
	// At least FreeBSD 8.2 can have different xattrs on hard links.
	if(maybe_send_extrameta(ff->fname, CMD_HARD_LINK, attribs, p1cntr))
		return -1;
      break;
   case FT_RAW:
   case FT_FIFO:
   case FT_REGE:
   case FT_REG:
      encode_stat(attribs, &ff->statp, ff->winattr,
		in_exclude_comp(conf->excom, conf->excmcount,
			ff->fname, conf->compression));
      if(async_write_str(CMD_STAT, attribs)
	|| async_write_str(filesymbol, ff->fname))
		return -1;
      do_filecounter(p1cntr, filesymbol, 1);
      if(ff->type==FT_REG)
	do_filecounter_bytes(p1cntr, (unsigned long long)ff->statp.st_size);
      if(maybe_send_extrameta(ff->fname, filesymbol, attribs, p1cntr))
		return -1;
      break;
   case FT_LNK:
	//printf("link: %s -> %s\n", ff->fname, ff->link);
   	encode_stat(attribs, &ff->statp, ff->winattr, conf->compression);
	if(async_write_str(CMD_STAT, attribs)
	  || async_write_str(CMD_SOFT_LINK, ff->fname)
	  || async_write_str(CMD_SOFT_LINK, ff->link))
		return -1;
	do_filecounter(p1cntr, CMD_SOFT_LINK, 1);
        if(maybe_send_extrameta(ff->fname, CMD_SOFT_LINK, attribs, p1cntr))
		return -1;
      break;
   case FT_DIREND:
      return 0;
   case FT_NOFSCHG:
   case FT_DIRBEGIN:
   case FT_REPARSE:
   case FT_JUNCTION:
	{
         char errmsg[100] = "";
         if (ff->type == FT_NOFSCHG)
            snprintf(errmsg, sizeof(errmsg), _("\t[will not descend: file system change not allowed]"));
	 if(*errmsg)
	 {
		snprintf(msg, sizeof(msg),
			"%s%s%s\n", "Dir: ", ff->fname, errmsg);
		logw(p1cntr, "%s", msg);
	 }
	 else
	 {
		encode_stat(attribs,
			&ff->statp, ff->winattr, conf->compression);
	      	if(async_write_str(CMD_STAT, attribs)) return -1;
#if defined(WIN32_VSS)
		if(async_write_str(filesymbol, ff->fname)) return -1;
		do_filecounter(p1cntr, filesymbol, 1);
#else
		if(async_write_str(CMD_DIRECTORY, ff->fname)) return -1;
		do_filecounter(p1cntr, CMD_DIRECTORY, 1);
        	if(maybe_send_extrameta(ff->fname, CMD_DIRECTORY,
			attribs, p1cntr)) return -1;
#endif
	 }
	}
      break;
   case FT_SPEC: // special file - fifo, socket, device node...
      encode_stat(attribs, &ff->statp, ff->winattr, conf->compression);
      if(async_write_str(CMD_STAT, attribs)
	  || async_write_str(CMD_SPECIAL, ff->fname))
		return -1;
      do_filecounter(p1cntr, CMD_SPECIAL, 1);
      if(maybe_send_extrameta(ff->fname, CMD_SPECIAL, attribs, p1cntr))
		return -1;
      break;
   case FT_NOACCESS:
      logw(p1cntr, _("Err: Could not access %s: %s"), ff->fname, strerror(errno));
      break;
   case FT_NOFOLLOW:
      logw(p1cntr, _("Err: Could not follow ff->link %s: %s"), ff->fname, strerror(errno));
      break;
   case FT_NOSTAT:
      logw(p1cntr, _("Err: Could not stat %s: %s"), ff->fname, strerror(errno));
      break;
   case FT_NOCHG:
      logw(p1cntr, _("Skip: File not saved. No change. %s"), ff->fname);
      break;
   case FT_ISARCH:
      logw(p1cntr, _("Err: Attempt to backup archive. Not saved. %s"), ff->fname);
      break;
   case FT_NOOPEN:
      logw(p1cntr, _("Err: Could not open directory %s: %s"), ff->fname, strerror(errno));
      break;
   default:
      logw(p1cntr, _("Err: Unknown file ff->type %d: %s"), ff->type, ff->fname);
      break;
   }
   return 0;
}

int backup_phase1_client(struct config *conf, int estimate, struct cntr *p1cntr, struct cntr *cntr)
{
	int sd=0;
	int ret=0;
	FF_PKT *ff=NULL;

	// First, tell the server about everything that needs to be backed up.

	logp("Phase 1 begin (file system scan)\n");

	if(conf->encryption_password)
	{
		filesymbol=CMD_ENC_FILE;
		metasymbol=CMD_ENC_METADATA;
	}

	ff=init_find_files();
	for(; sd < conf->sdcount; sd++)
	{
		if(conf->startdir[sd]->flag)
		{
			if((ret=find_files_begin(ff, conf,
				conf->startdir[sd]->path, p1cntr)))
					break;
		}
	}
	term_find_files(ff);

	print_endcounter(p1cntr);
	//print_filecounters(p1cntr, cntr, ACTION_BACKUP);
	if(ret) logp("Error in phase 1\n");
	logp("Phase 1 end (file system scan)\n");

	return ret;
}
