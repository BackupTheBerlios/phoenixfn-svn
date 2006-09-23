/************************************************************************
 *   IRC - Internet Relay Chat, src/send.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                    University of Oulu, Computing Center
 *             (C) 2001,2002 Andrew Suffield
 *                    <asuffield@freenode.net>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "send.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "config.h"
#include "irc_string.h"
#include "ircd.h"
#include "m_commands.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_serv.h"
#include "s_zip.h"
#include "sprintf_irc.h"
#include "struct.h"
#include "s_conf.h"
#include "s_debug.h"
#include "s_log.h"
#include "flud.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

#define NEWLINE "\r\n"
#define LOG_BUFSIZE 2048

static  char    sendbuf[2048];
static  int     send_message (aClient *, char *, int);

static  void vsendto_prefix_one(register aClient *, register aClient *, const char *, va_list);
static  void vsendto_one(aClient *, const char *, va_list);
static  void vsendto_ops_flag_butflag_butone_from(aClient*, int, int, aClient*, const char *, va_list);
static  void vsendto_local_ops_flag_butflag_butone_from(aClient*, int, int, aClient*, const char *, va_list);
static  void vsendto_remote_ops_flag_butflag_butone_from(aClient*, int, int, aClient*, const char *, va_list);

static  unsigned long sentalong[MAXCONNECTIONS];
static unsigned long current_serial=0L;

/*
** dead_link
**      An error has been detected. The link *must* be closed,
**      but *cannot* call ExitClient (m_bye) from here.
**      Instead, mark it with FLAGS_DEADSOCKET. This should
**      generate ExitClient from the main loop.
**
**      If 'notice' is not NULL, it is assumed to be a format
**      for a message to local opers. I can contain only one
**      '%s', which will be replaced by the sockhost field of
**      the failing link.
**
**      Also, the notice is skipped for "uninteresting" cases,
**      like Persons and yet unknown connections...
*/

static int
dead_link(aClient *to, const char *notice)

{
  to->flags |= FLAGS_DEADSOCKET;

  /*
   * If because of BUFFERPOOL problem then clean dbuf's now so that
   * notices don't hurt operators below.
   */
  DBufClear(&to->recvQ);
  DBufClear(&to->sendQ);
  if (!IsPerson(to) && !IsUnknown(to) && !(to->flags & FLAGS_CLOSING))
    sendto_ops_flag(UMODE_SERVNOTICE, notice, get_client_name(to, MASK_IP));
  
  Debug((DEBUG_ERROR, notice, get_client_name(to, MASK_IP)));

  return (-1);
} /* dead_link() */

/*
 * flush_connections - Used to empty all output buffers for all connections. 
 * Should only be called once per scan of connections. There should be a 
 * select in here perhaps but that means either forcing a timeout or doing 
 * a poll. When flushing, all we do is empty the obuffer array for each local
 * client and try to send it. if we cant send it, it goes into the sendQ
 *      -avalon
 */
void flush_connections(struct Client* cptr)
{
#ifdef SENDQ_ALWAYS
  if (0 == cptr) {
    int i;
    for (i = highest_fd; i >= 0; --i) {
      if ((cptr = local[i]) && DBufLength(&cptr->sendQ) > 0)
        send_queued(cptr);
    }
  }
  else if (-1 < cptr->fd && DBufLength(&cptr->sendQ) > 0)
    send_queued(cptr);

#endif /* SENDQ_ALWAYS */
}


/*
 * send_message
 *      Internal utility which delivers one message buffer to the
 *      socket. Takes care of the error handling and buffering, if
 *      needed.
 *      if SENDQ_ALWAYS is defined, the message will be queued.
 *      if ZIP_LINKS is defined, the message will eventually be compressed,
 *      anything stored in the sendQ is compressed.
 *
 *      if msg is a null pointer, we are flushing connection
 */
static int
send_message(aClient *to, char *msg, int len)

#ifdef SENDQ_ALWAYS

{
  static int SQinK;

  if (to->from)
    to = to->from; /* shouldn't be necessary */

  if (IsMe(to))
    {
      sendto_ops_flag(UMODE_DEBUG, "Trying to send to myself! [%s]", msg);
      logprintf(L_ERROR, "Trying to send to myself! [%s]", msg);
      return 0;
    }

  if (to->fd < 0)
    return 0; /* Thou shalt not write to closed descriptors */

  if (IsDead(to))
    return 0; /* This socket has already been marked as dead */

  if (DBufLength(&to->sendQ) > get_sendq(to))
    {
      if (IsServer(to))
	{
	  sendto_ops_flag_butone(to, UMODE_SERVNOTICE, "Max SendQ limit exceeded for %s: %zd > %zd",
				 to->name, DBufLength(&to->sendQ), get_sendq(to));
	  logprintf(L_WARN, "Max SendQ limit exceeded for %s: %zd > %zd",
	      to->name, DBufLength(&to->sendQ), get_sendq(to));
	}

      if (IsDoingList(to))
	{
	  /* Pop the sendq for this message */
	  /*if (!IsAnOper(to))
	    sendto_realops("LIST blocked for %s", get_client_name(to, FALSE)); */
	  SetSendqPop(to);
	  return 0;
	}
      else
	{
	  if (IsClient(to))
	    to->flags |= FLAGS_SENDQEX;
	  return dead_link(to, "Max Sendq exceeded");
	}
    }
  else
    {
#ifdef ZIP_LINKS

      /*
       * data is first stored in to->zip->outbuf until
       * it's big enough to be compressed and stored in the sendq.
       * send_queued is then responsible to never let the sendQ
       * be empty and to->zip->outbuf not empty.
       */
      if (to->flags2 & FLAGS2_ZIP)
	msg = zip_buffer(to, msg, &len, 0);

      if (len && !dbuf_put(&to->sendQ, msg, len))

#else /* ZIP_LINKS */
	if (!dbuf_put(&to->sendQ, msg, len))

#endif /* ZIP_LINKS */

	  return dead_link(to, "Buffer allocation error for %s");
    }

  /*
  ** Update statistics. The following is slightly incorrect
  ** because it counts messages even if queued, but bytes
  ** only really sent. Queued bytes get updated in SendQueued.
  */
  to->sendM += 1;
  me.sendM += 1;

  /*
  ** This little bit is to stop the sendQ from growing too large when
  ** there is no need for it to. Thus we call send_queued() every time
  ** 2k has been added to the queue since the last non-fatal write.
  ** Also stops us from deliberately building a large sendQ and then
  ** trying to flood that link with data (possible during the net
  ** relinking done by servers with a large load).
  */
  /*
   * Well, let's try every 4k for clients, and immediately for servers
   *  -Taner
   */
  SQinK = DBufLength(&to->sendQ)/1024;
  if (IsServer(to))
    {
      if (SQinK > to->lastsq)
	send_queued(to);
    }
  else
    {
      if (SQinK > (to->lastsq + 4))
	send_queued(to);
    }
  return 0;
} /* send_message() */

#else /* SENDQ_ALWAYS */

{
  int rlen = 0;

  if (to->from)
    to = to->from;

  if (IsMe(to))
    {
      sendto_ops_flag(UMODE_DEBUG, "Trying to send to myself! [%s]", msg);
      logprintf(L_ERROR, "Trying to send to myself! [%s]", msg);
      return 0;
    }

  if (to->fd < 0)
    return 0; /* Thou shalt not write to closed descriptors */

  if (IsDead(to))
    return 0; /* This socket has already been marked as dead */

  /*
  ** DeliverIt can be called only if SendQ is empty...
  */
  if ((DBufLength(&to->sendQ) == 0) &&
      (rlen = deliver_it(to, msg, len)) < 0)
    {
      static char buf[1024];
      ircsnprintf(buf, "Write error to %%s (%d: %s), closing link",
		  errno, strerror(errno));
      return dead_link(to,buf);
    }
  else if (rlen < len)
    {
      /*
      ** Was unable to transfer all of the requested data. Queue
      ** up the remainder for some later time...
      */
      if (DBufLength(&to->sendQ) > get_sendq(to))
	{
	  sendto_ops_flag_butone(to, UMODE_SERVNOTICE, 
				 "Max SendQ limit exceeded for %s : %d > %d",
				 get_client_name(to, MASK_IP),
				 DBufLength(&to->sendQ), get_sendq(to));
		  
	  return dead_link(to, "Max Sendq exceeded");
	}
      else
	{
#ifdef ZIP_LINKS

	  /*
	  ** data is first stored in to->zip->outbuf until
	  ** it's big enough to be compressed and stored in the sendq.
	  ** send_queued is then responsible to never let the sendQ
	  ** be empty and to->zip->outbuf not empty.
	  */
	  if (to->flags2 & FLAGS2_ZIP)
	    msg = zip_buffer(to, msg, &len, 0);

	  if (len && !dbuf_put(&to->sendQ, msg + rlen, len - rlen))

#else /* ZIP_LINKS */

	    if (!dbuf_put(&to->sendQ,msg+rlen,len-rlen))

#endif /* ZIP_LINKS */

	      return dead_link(to,"Buffer allocation error for %s");
	}
    } /* else if (rlen < len) */

  /*
  ** Update statistics. The following is slightly incorrect
  ** because it counts messages even if queued, but bytes
  ** only really sent. Queued bytes get updated in SendQueued.
  */
  to->sendM += 1;
  me.sendM += 1;

  return 0;
} /* send_message() */

#endif /* SENDQ_ALWAYS */

/*
 * send_queued
 *      This function is called from the main select-loop (or whatever)
 *      when there is a chance the some output would be possible. This
 *      attempts to empty the send queue as far as possible...
 */
int send_queued(aClient *to)
{
  const char *msg;
#ifdef ZIP_LINKS
  int more = NO;
#endif

  /*
  ** Once socket is marked dead, we cannot start writing to it,
  ** even if the error is removed...
  */
  if (IsDead(to)) {
    /*
     * Actually, we should *NEVER* get here--something is
     * not working correct if send_queued is called for a
     * dead socket... --msa
     */
#ifndef SENDQ_ALWAYS
    return dead_link(to, "send_queued called for a DEADSOCKET:%s");
#else
    return -1;
#endif
  } /* if (IsDead(to)) */

  if (!MyConnect(to))
    {
      return dead_link(to, "send_queued called for non-local socket %s");
    }

#ifdef ZIP_LINKS
  /*
  ** Here, we must make sure than nothing will be left in to->zip->outbuf
  ** This buffer needs to be compressed and sent if all the sendQ is sent
  */
  if ((to->flags2 & FLAGS2_ZIP) && to->zip->outcount) {
    if (DBufLength(&to->sendQ) > 0)
      more = 1;
    else {
      int zlen;
      msg = zip_buffer(to, NULL, &zlen, 1);

      if (zlen == -1)
	{
	  struct ConfItem *c_conf = find_conf_name(to->confs, to->name, CONF_CONNECT_SERVER);
	  if (c_conf)
	    c_conf->flags &= ~CONF_FLAGS_ZIP_LINK;
	  return dead_link(to, "fatal error in zip_buffer()");
	}

      if (!dbuf_put(&to->sendQ, msg, zlen))
        return dead_link(to, "Buffer allocation error for %s");
    }
  } /* if ((to->flags2 & FLAGS2_ZIP) && to->zip->outcount) */
#endif /* ZIP_LINKS */

  while (DBufLength(&to->sendQ) > 0) {
    size_t len;
    int rlen;
    msg = dbuf_map(&to->sendQ, &len);

    /* Returns always len > 0 */
    if ((rlen = deliver_it(to, msg, len)) < 0)
      return dead_link(to,"Write error to %s, closing link");

    dbuf_delete(&to->sendQ, rlen);
    to->lastsq = DBufLength(&to->sendQ) / 1024;
    /* 
     * sendq is now empty.. if there a blocked list?
     */
    if (IsSendqPopped(to) && (DBufLength(&to->sendQ) == 0)) {
      char* parv[2];
      char  param[HOSTLEN + 1];
      ClearSendqPop(to);
      parv[0] = param;
      parv[1] = 0;
      strncpy_irc(param, to->name, HOSTLEN + 1);
      m_list(to, to, 1, parv);
    }
    if (rlen < (int)len) {    
      /* ..or should I continue until rlen==0? */
      /* no... rlen==0 means the send returned EWOULDBLOCK... */
      break;
    }

#ifdef ZIP_LINKS
    if (DBufLength(&to->sendQ) == 0 && more) {
      /*
      ** The sendQ is now empty, compress what's left
      ** uncompressed and try to send it too
      */
      int zlen;
      more = 0;
      msg = zip_buffer(to, NULL, &zlen, 1);

      if (zlen == -1)
	{
	  struct ConfItem *c_conf = find_conf_name(to->confs, to->name, CONF_CONNECT_SERVER);
	  if (c_conf)
	    c_conf->flags &= ~CONF_FLAGS_ZIP_LINK;
	  return dead_link(to, "fatal error in zip_buffer()");
	}

      if (!dbuf_put(&to->sendQ, msg, zlen))
        return dead_link(to, "Buffer allocation error for %s");
    } /* if (DBufLength(&to->sendQ) == 0 && more) */
#endif /* ZIP_LINKS */      
  } /* while (DBufLength(&to->sendQ) > 0) */

  return (IsDead(to)) ? -1 : 0;
}

/*
 * send message to single client
 */

void
sendto_one(aClient *to, const char *pattern, ...)

{
  va_list       args;

  va_start(args, pattern);

  if (to)
    vsendto_one(to, pattern, args);

  va_end(args);
} /* sendto_one() */

void
send_markup(struct Client *to, struct Client *from, const char *type, const char *pattern, ...)
{
  static char buf[2048];
  va_list args;
  va_start(args, pattern);

  if (to)
    {
      int len = vsnprintf_irc(buf, 2048, pattern, args);
      if (len > 510)
        {
          sendbuf[510] = '\0';
          len = 510;
        }
/*       if (to->caps & CAP_MARKUP) */
        sendto_one(to, form_str(RPL_MESSAGE), from->name, to->name, type, buf);
/*       else */
/*         sendto_one(to, form_str(RPL_MESSAGE), from->name, to->name, type, strip_markup(buf)); */
    }

  va_end(args);
}

/*
 * vsendto_one()
 * Backend for sendto_one() - send string with variable
 * arguments to client 'to'
 * -wnder
*/

static void
vsendto_one(aClient *to, const char *pattern, va_list args)

{
  int len; /* used for the length of the current message */
  
  if (to->from)
    to = to->from;
  
  if (to->fd < 0)
    {
      Debug((DEBUG_ERROR,
             "Local socket %s with negative fd... AARGH!",
             to->name));
    }
  else if (IsMe(to))
    {
      sendto_ops_flag(UMODE_DEBUG, "Trying to send to myself! [%s]", sendbuf);
      logprintf(L_ERROR, "Trying to send to myself! [%s]", sendbuf);
      return;
    }

  len = vsnprintf_irc(sendbuf, 2048, pattern, args);

  /*
   * from rfc1459
   *
   * IRC messages are always lines of characters terminated with a CR-LF
   * (Carriage Return - Line Feed) pair, and these messages shall not
   * exceed 512 characters in length, counting all characters including
   * the trailing CR-LF. Thus, there are 510 characters maximum allowed
   * for the command and its parameters.  There is no provision for
   * continuation message lines.  See section 7 for more details about
   * current implementations.
   */

  /*
   * We have to get a \r\n\0 onto sendbuf[] somehow to satisfy
   * the rfc. We must assume sendbuf[] is defined to be 513
   * bytes - a maximum of 510 characters, the CR-LF pair, and
   * a trailing \0, as stated in the rfc. Now, if len is greater
   * than the third-to-last slot in the buffer, an overflow will
   * occur if we try to add three more bytes, if it has not
   * already occured. In that case, simply set the last three
   * bytes of the buffer to \r\n\0. Otherwise, we're ok. My goal
   * is to get some sort of vsnprintf() function operational
   * for this routine, so we never again have a possibility
   * of an overflow.
   * -wnder
   */
  if (len > 510)
    {
      sendbuf[510] = '\r';
      sendbuf[511] = '\n';
      sendbuf[512] = '\0';
      len = 512;
    }
  else
    {
      sendbuf[len++] = '\r';
      sendbuf[len++] = '\n';
      sendbuf[len] = '\0';
    }

  Debug((DEBUG_SEND,"Sending [%s] to %s",sendbuf,to->name));

  (void)send_message(to, sendbuf, len);
}

void
sendto_channel_butone(aClient *one, aClient *from, aChannel *chptr, 
                      const char *pattern, ...)

{
  va_list args;
  register Link *lp;
  register aClient *acptr;
  /* index of sentalong[] to flag client as having received message */
  register int lindex;

  ++current_serial;
  
  for (lp = chptr->members; lp; lp = lp->next)
    {
      acptr = lp->value.cptr;
      
      if (acptr->from == one)
        continue;       /* ...was the one I should skip */
      
      lindex = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
	  va_start(args, pattern);
          vsendto_prefix_one(acptr, from, pattern, args);
	  va_end(args);
          sentalong[lindex] = current_serial;
        }
      else
        {
          /*
           * Now check whether a message has been sent to this
           * remote link already
           */
          if(sentalong[lindex] != current_serial)
            {
	      va_start(args, pattern);
              vsendto_prefix_one(acptr, from, pattern, args);
	      va_end(args);
              sentalong[lindex] = current_serial;
            }
        }
    }

#if 0
  for (lp = chptr->loggers; lp; lp = lp->next)
    {
      acptr = lp->value.cptr;

      if (acptr->from == one)
        continue;       /* ...was the one I should skip */
      
      lindex = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
	  va_start(args, pattern);
          vsendto_prefix_one(acptr, from, pattern, args);
	  va_end(args);
          sentalong[lindex] = current_serial;
        }
      else
        {
          /*
           * Now check whether a message has been sent to this
           * remote link already
           */
          if(sentalong[lindex] != current_serial)
            {
	      va_start(args, pattern);
              vsendto_prefix_one(acptr, from, pattern, args);
	      va_end(args);
              sentalong[lindex] = current_serial;
            }
        }
    }
#endif

  va_end(args);
}

void
sendto_channel_message_butone(aClient *one, aClient *from, aChannel *chptr, const char *cmd, const char *msg)

{
  struct SLink *lp;
  /* index of sentalong[] to flag client as having received message */
  int lindex;
  int is_ctcp = check_for_ctcp(msg);
  int cap = is_ctcp ? CAP_IDENTIFY_CTCP : CAP_IDENTIFY_MSG;

  ++current_serial;
  
  for (lp = chptr->members; lp; lp = lp->next)
    {
      struct Client *acptr = lp->value.cptr;
      
      if (acptr->from == one)
        continue;       /* ...was the one I should skip */
      
      lindex = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
          sendto_prefix_one(acptr, from, ":%s %s %s :%s%s",
                            from->name, cmd, chptr->chname,
			    !(acptr->caps & cap) ? "" :
			    (HasUmode(from, UMODE_IDENTIFIED) ? "+" : "-"),
                            msg);
          sentalong[lindex] = current_serial;
        }
      else
        {
          /*
           * Now check whether a message has been sent to this
           * remote link already
           */
          if(sentalong[lindex] != current_serial)
            {
              sendto_prefix_one(acptr, from, ":%s %s %s :%s",
                                from->name, cmd, chptr->chname, msg);
              sentalong[lindex] = current_serial;
            }
        }
    }

#if 0
  for (lp = chptr->loggers; lp; lp = lp->next)
    {
      struct Client *acptr = lp->value.cptr;

      if (acptr->from == one)
        continue;       /* ...was the one I should skip */
      
      lindex = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
          sendto_prefix_one(acptr, from, ":%s %s %s :%s%s",
                            from->name, cmd, chptr->chname,
			    !(acptr->caps & cap) ? "" :
			    (HasUmode(from, UMODE_IDENTIFIED) ? "+" : "-"),
                            msg);
          sentalong[lindex] = current_serial;
        }
      else
        {
          /*
           * Now check whether a message has been sent to this
           * remote link already
           */
          if(sentalong[lindex] != current_serial)
            {
              sendto_prefix_one(acptr, from, ":%s %s %s :%s",
                                from->name, cmd, chptr->chname, msg);
              sentalong[lindex] = current_serial;
            }
        }
    }
#endif
}

void
sendto_channel_type(aClient *one, aClient *from, aChannel *chptr,
                    int type,
                    const char *nick,
                    const char *cmd,
                    const char *message)

{
  register Link *lp;
  register aClient *acptr;
  register int i;
  char char_type;
  int is_ctcp = check_for_ctcp(message);
  int cap = is_ctcp ? CAP_IDENTIFY_CTCP : CAP_IDENTIFY_MSG;

  ++current_serial;

  if(type&MODE_CHANOP)
    char_type = '@';
  else
    char_type = '+';

  /* For opmoderate, send it as message to regular channel.
   * This assumes the other side will process it in the same way
   * -- jilles */
  if (type & MODE_OPMODERATE) 
    char_type = ' ';

  for (lp = chptr->members; lp; lp = lp->next)
    {
      if (!(lp->flags & type))
        continue;

      acptr = lp->value.cptr;
      if (acptr->from == one)
        continue;

      i = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
          sendto_prefix_one(acptr, from,
			    ":%s %s %c%s :%s%s",
			    from->name,
			    cmd,                    /* PRIVMSG or NOTICE */
			    char_type,              /* @ or + */
			    nick,
			    !(acptr->caps & cap) ? "" :
			    (HasUmode(from, UMODE_IDENTIFIED) ? "+" : "-"),
			    message);
        }
      else
        {
          /*
           * If the target's server can do CAP_CHW, only
           * one send is needed, otherwise, I do a bunch of
           * send's to each target on that server. (kludge)
           *
           * -Dianora
           */
          if(!IsCapable(acptr->from,CAP_CHW))
            {
              /* Send it individually to each opped or voiced
               * client on channel
	       *
	       * But change it to a NOTICE so chanserv won't get PRIVMSGs
	       * it will respond to -- jilles
               */
	      sendto_prefix_one(acptr, from,
				":%s NOTICE %s :%s%s",
				from->name,
				lp->value.cptr->name, /* target name */
                                !(acptr->caps & cap) ? "" :
                                (HasUmode(from, UMODE_IDENTIFIED) ? "+" : "-"),
				message);
            }
          else
            {
              /* Now check whether a message has been sent to this
               * remote link already
               */
              if (sentalong[i] != current_serial)
                {
                  sendto_prefix_one(acptr, from,
				    ":%s %s %c%s :%s",
				    from->name,
				    cmd,
				    char_type,
				    nick,
				    message);
                  sentalong[i] = current_serial;
                }
            }
        }
    } /* for (lp = chptr->members; lp; lp = lp->next) */

} /* sendto_channel_type() */


/* 
 * sendto_channel_type_notice()  - sends a message to all users on a channel who meet the
 * type criteria (chanop/voice/whatever).
 * message is also sent back to the sender if they have those privs.
 * used in knock/invite/privmsg@+/notice@+
 * -good
 */
void
sendto_channel_type_notice(aClient *from, aChannel *chptr, int type, const char *message)
{
  register Link *lp;
  register aClient *acptr;
  register int i;
  int is_ctcp = check_for_ctcp(message);
  int cap = is_ctcp ? CAP_IDENTIFY_CTCP : CAP_IDENTIFY_MSG;

  for (lp = chptr->members; lp; lp = lp->next)
    {
      if (!(lp->flags & type))
	continue;

      acptr = lp->value.cptr;

      i = acptr->from->fd;
      if (IsRegisteredUser(acptr))
	{
	  sendto_prefix_one(acptr, from, ":%s NOTICE %s :%s%s",
			    from->name, 
			    acptr->name,
			    !(acptr->caps & cap) ? "" :
			    (HasUmode(from, UMODE_IDENTIFIED) ? "+" : "-"),
                            message);
	}
    }
}


/*
 * sendto_serv_butone
 *
 * Send a message to all connected servers except the client 'one'.
 */

void
sendto_serv_butone(aClient *one, const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (one && (cptr == one->from))
        continue;

      va_start(args, pattern);
      vsendto_one(cptr, pattern, args);
      va_end(args);
    }
}

/*
 * sendto_common_channels()
 *
 * Sends a message to all people (excluding user) on local server who are
 * in same channel with user.
 */

void
sendto_common_channels(aClient *user, const char *pattern, ...)
{
  va_list args;
  register Link *channels;
  register Link *users;
  register aClient *cptr;

  ++current_serial;
  if (user->fd >= 0)
    sentalong[user->fd] = current_serial;

  if (user->user)
    {
      for (channels = user->user->channel; channels; channels = channels->next)
        {
          for(users = channels->value.chptr->members; users; users = users->next)
            {
              cptr = users->value.cptr;
              /* "dead" clients i.e. ones with fd == -1 should not be
               * looked at -db
               */
              if (!MyConnect(cptr) || (cptr->fd < 0) ||
                  (sentalong[cptr->fd] == current_serial))
                continue;
            
              sentalong[cptr->fd] = current_serial;

	      va_start(args, pattern);
              vsendto_prefix_one(cptr, user, pattern, args);
	      va_end(args);
            }
#if 0
          for (users = channels->value.chptr->loggers; users; users = users->next)
            {
              cptr = users->value.cptr;
              
              if (!MyConnect(cptr) || (cptr->fd < 0) ||
                  (sentalong[cptr->fd] == current_serial))
                continue;
            
              sentalong[cptr->fd] = current_serial;

	      va_start(args, pattern);
              vsendto_prefix_one(cptr, user, pattern, args);
	      va_end(args);
            }
#endif
        }
    }

  if (MyConnect(user))
    {
      va_start(args, pattern);
      vsendto_prefix_one(user, user, pattern, args);
      va_end(args);
    }
  va_end(args);
}

/*
 * sendto_channel_butserv
 *
 * Send a message to all members of a channel that are connected to this
 * server.
 */

void
sendto_channel_butserv(aChannel *chptr, aClient *from, 
                       const char *pattern, ...)
{
  va_list args;
  register Link *lp;
  register aClient *acptr;

  for (lp = chptr->members; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      {
	va_start(args, pattern);
        vsendto_prefix_one(acptr, from, pattern, args);
	va_end(args);
      }

#if 0
  for (lp = chptr->loggers; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      {
	va_start(args, pattern);
        vsendto_prefix_one(acptr, from, pattern, args);
	va_end(args);
      }
#endif

  va_end(args);
}

/*
 * sendto_channel_chanops_butserv
 *
 * Send a message to all ops of a channel that are connected to this
 * server.
 */

void
sendto_channel_chanops_butserv(aChannel *chptr, aClient *from, 
			       const char *pattern, ...)

{
  va_list args;
  register Link *lp;
  register aClient *acptr;

  for (lp = chptr->members; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      if(is_chan_op(acptr,chptr))
	{
	  va_start(args, pattern);
	  vsendto_prefix_one(acptr, from, pattern, args);
	  va_end(args);
	}
}

/*
 * sendto_channel_non_chanops_butserv
 *
 * Send a message to all non-ops of a channel that are connected to this
 * server.
 */

void
sendto_channel_non_chanops_butserv(aChannel *chptr, aClient *from, 
				   const char *pattern, ...)

{
  va_list args;
  register Link *lp;
  register aClient *acptr;

  for (lp = chptr->members; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      if(!is_chan_op(acptr,chptr))
	{
	  va_start(args, pattern);
	  vsendto_prefix_one(acptr, from, pattern, args);
	  va_end(args);
	}

#if 0
  for (lp = chptr->loggers; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      if(!is_chan_op(acptr,chptr))
	{
	  va_start(args, pattern);
	  vsendto_prefix_one(acptr, from, pattern, args);
	  va_end(args);
	}
#endif
}

/*
** send a msg to all ppl on servers/hosts that match a specified mask
** (used for enhanced PRIVMSGs)
**
** addition -- Armin, 8jun90 (gruner@informatik.tu-muenchen.de)
*/

static int
match_it(const aClient *one, const char *mask, int what)

{
  if(what == MATCH_HOST)
    return match(mask, one->host);
  else
    return match(mask, one->user->server);
} /* match_it() */

/*
 * sendto_match_servs
 *
 * send to all servers which match the mask at the end of a channel name
 * (if there is a mask present) or to all if no mask.
 */

void
sendto_match_servs(aChannel *chptr, aClient *from, const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;
  

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr->from == from)
        continue;

      va_start(args, pattern);
      vsendto_one(cptr, pattern, args);
      va_end(args);
    }
}

/*
 * sendto_match_cap_servs
 *
 * send to all servers which match the mask at the end of a channel name
 * (if there is a mask present) or to all if no mask, and match the capability
 */

void
sendto_match_cap_servs(aChannel *chptr, aClient *from, int caps, int nocaps, 
                       const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;


  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == from)
        continue;
      
      if(!IsCapable(cptr, caps))
        continue;
      
      if(!NotCapable(cptr, nocaps))
        continue;
      
      va_start(args, pattern);
      vsendto_one(cptr, pattern, args);
      va_end(args);
    }
}

/*
 * sendto_match_butone
 *
 * Send to all clients which match the mask in a way defined on 'what';
 * either by user hostname or user servername.
 */

void
sendto_match_butone(aClient *one, aClient *from, char *mask, 
                    int what, const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;

  /* scan the local clients */
  for(cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
    {
      if (cptr == one)  /* must skip the origin !! */
        continue;

      if (match_it(cptr, mask, what))
	{
	  va_start(args, pattern);
	  vsendto_prefix_one(cptr, from, pattern, args);
	  va_end(args);
	}
    }

  /* Now scan servers */
  for (cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == one) /* must skip the origin !! */
        continue;

      /*
       * The old code looped through every client on the
       * network for each server to check if the
       * server (cptr) has at least 1 client matching
       * the mask, using something like:
       *
       * for (acptr = GlobalClientList; acptr; acptr = acptr->next)
       *        if (IsRegisteredUser(acptr) &&
       *                        match_it(acptr, mask, what) &&
       *                        (acptr->from == cptr))
       *   vsendto_prefix_one(cptr, from, pattern, args);
       *
       * That way, we wouldn't send the message to
       * a server who didn't have a matching client.
       * However, on a network such as EFNet, that
       * code would have looped through about 50
       * servers, and in each loop, loop through
       * about 50k clients as well, calling match()
       * in each nested loop. That is a very bad
       * thing cpu wise - just send the message
       * to every connected server and let that
       * server deal with it.
       * -wnder
       */

      va_start(args, pattern);
      vsendto_prefix_one(cptr, from, pattern, args);
      va_end(args);
    }
}

/*
** sendto_wallops_butone
**      Send message to all operators.
** one - client not to send message to
** from- client which message is from *NEVER* NULL!!
*/

void
sendto_wallops_butone(aClient *one, aClient *from, const char *pattern, ...)

{
  va_list args;
  register int lindex;
  register aClient *cptr;

  ++current_serial;

  for (cptr = GlobalClientList; cptr; cptr = cptr->next)
    {
      if (!ReceiveWallops(cptr))
        continue;

      /* find connection oper is on */
      lindex = cptr->from->fd;

      if (sentalong[lindex] == current_serial)
        continue;

      if (cptr->from == one)
        continue; /* ...was the one I should skip */

      sentalong[lindex] = current_serial;

      va_start(args, pattern);
      vsendto_prefix_one(cptr->from, from, pattern, args);
      va_end(args);
    }
} /* sendto_wallops_butone() */

/*
 * send_operwall -- Send Wallop to All Opers on this server
 *
 */

void
send_operwall(aClient *from, const char *type_message, const char *message)
{
  char sender[NICKLEN + USERLEN + HOSTLEN + 5];
  aClient *acptr;
  anUser *user;
  
  if (!from || !message)
    return;

  if (!IsPerson(from))
    return;

  user = from->user;
  strncpy_irc(sender, from->name, HOSTLEN + 1);

  if (*from->username) 
    {
      strcat(sender, "!");
      strcat(sender, from->username);
    }

  if (*from->host)
    {
      strcat(sender, "@");
      strcat(sender, from->host);
    }

  for (acptr = local_cptr_list; acptr; acptr = acptr->next_local_client)
    {
      if (!ReceiveOperwall(acptr))
        continue; /* has to be oper if in this linklist */

      sendto_one(acptr, ":%s WALLOPS :%s - %s", sender, type_message, message);
    }
} /* send_operwall() */

/*
 * to - destination client
 * from - client which message is from
 *
 * NOTE: NEITHER OF THESE SHOULD *EVER* BE NULL!!
 * -avalon
 *
 */

void
sendto_prefix_one(register aClient *to, register aClient *from, 
                  const char *pattern, ...)

{
  va_list args;

  va_start(args, pattern);

  vsendto_prefix_one(to, from, pattern, args);

  va_end(args);
} /* sendto_prefix_one() */

/*
 * vsendto_prefix_one()
 * Backend to sendto_prefix_one(). stdarg.h does not work
 * well when variadic functions pass their arguments to other
 * variadic functions, so we can call this function in those
 * situations.
 *  This function must ALWAYS be passed a string of the form:
 * ":%s COMMAND <other args>"
 * 
 * -cosine
 */

static void
vsendto_prefix_one(register aClient *to, register aClient *from,
                   const char *pattern, va_list args)

{
  static char sender[HOSTLEN + NICKLEN + USERLEN + 5];
  char* par = 0;
  register int parlen, len;
  static char outbuf[1024];

  assert(0 != to);
  assert(0 != from);

  /* Optimize by checking if (from && to) before everything */
  if (!MyClient(from) && IsPerson(to) && (to->from == from->from))
    {
      if (IsServer(from))
        {
	  va_list args_copy;
	  va_copy(args_copy,args);
          vsnprintf_irc(outbuf, 1024, pattern, args_copy);
	  va_end(args_copy);
          
          sendto_ops_flag(UMODE_SERVNOTICE,
			  "Send message (%s) to %s[%s] dropped from %s(Fake Dir)",
			  outbuf, to->name, to->from->name, from->name);
          return;
        }

      sendto_serv_butone(NULL, ":%s KILL %s :%s (%s[%s@%s] Ghosted %s)",
                         me.name, to->name, me.name, to->name,
                         to->username, to->host, to->from->name);

      sendto_ops_flag(UMODE_SERVNOTICE, "Ghosted: %s[%s@%s] from %s[%s@%s] (%s)",
		      to->name, to->username, to->host,
		      from->name, from->username, from->host,
		      to->from->name);
      
      to->flags |= FLAGS_KILLED;

      exit_client(NULL, to, &me, "Ghosted client");

      if (IsPerson(from))
        sendto_one(from, form_str(ERR_GHOSTEDCLIENT),
                   me.name, from->name, to->name, to->username,
                   to->host, to->from);
      
      return;
    }

  par = va_arg(args, char *);
  if (MyClient(to) && IsPerson(from) && !irccmp(par, from->name))
    {
      strncpy_irc(sender, from->name, HOSTLEN + 1);
      
      if (*from->username)
        {
          strcat(sender, "!");
          strcat(sender, from->username);
        }

      if (*from->host)
        {
          strcat(sender, "@");
          strcat(sender, from->host);
        }
      
      par = sender;
    } /* if (user) */

  *outbuf = ':';
  strncpy_irc(outbuf + 1, par, sizeof(sendbuf) - 1);

  parlen = strlen(par) + 1;
  outbuf[parlen++] = ' ';

  len = parlen;
  len += vsnprintf_irc(outbuf + parlen, 1024 - parlen, &pattern[4], args);

  if (len > 510)
    {
      outbuf[510] = '\r';
      outbuf[511] = '\n';
      outbuf[512] = '\0';
      len = 512;
    }
  else
    {
      outbuf[len++] = '\r';
      outbuf[len++] = '\n';
      outbuf[len] = '\0';
    }

  Debug((DEBUG_SEND,"Sending [%s] to %s",outbuf,to->name));

  send_message(to, outbuf, len);
}

/*
 * ts_warn
 *      Call sendto_ops, with some flood checking (at most 5 warnings
 *      every 5 seconds)
 */
void
ts_warn(const char *pattern, ...)
{
  va_list args;
  char buf[LOG_BUFSIZE];
  static time_t last = 0;
  static int warnings = 0;
  time_t now;

  /*
  ** if we're running with TS_WARNINGS enabled and someone does
  ** something silly like (remotely) connecting a nonTS server,
  ** we'll get a ton of warnings, so we make sure we don't send
  ** more than 5 every 5 seconds.  -orabidoo
  */

  /*
   * hybrid servers always do TS_WARNINGS -Dianora
   */
  now = time(NULL);
  if (now - last < 5)
    {
      if (++warnings > 5)
        return;
    }
  else
    {
      last = now;
      warnings = 0;
    }

  va_start(args, pattern);
  vsendto_ops_flag_butflag_butone_from(NULL, UMODE_SERVNOTICE, 0, &me, pattern, args);
  va_end(args);

  va_start(args, pattern);
  vsprintf(buf, pattern, args);
  va_end(args);

  logprintf(L_CRIT, "%s", buf);
}

void
flush_server_connections()
{
  aClient *cptr;

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    if (DBufLength(&cptr->sendQ) > 0)
      (void)send_queued(cptr);
} /* flush_server_connections() */

int
sendto_slaves(aClient *one, const char *message, const char *nick, int parc, char *parv[])
{
  aClient *acptr;

  for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client)
    {
      if (one == acptr)
        continue;

      if(parc > 3)
        sendto_one(acptr,":%s %s %s %s %s :%s",
                   me.name,
                   message,
                   nick,
                   parv[1],
                   parv[2],
                   parv[3]);
      else if(parc > 2)
        sendto_one(acptr,":%s %s %s %s :%s",
                   me.name,
                   message,
                   nick,
                   parv[1],
                   parv[2]);
      else if(parc > 1)
        sendto_one(acptr,":%s %s %s :%s",
                   me.name,
                   message,
                   nick,
                   parv[1]);
    }

  return 0;
}

/*
 * sendto_ops_flag_butone
 *
 *    Send to any client with the given umode
 */

void
sendto_ops_flag(int flag, const char *pattern, ...)
{
  va_list args;

  va_start(args, pattern);

  vsendto_ops_flag_butflag_butone_from(NULL, flag, 0, &me, pattern, args);

  va_end(args);
}

void
sendto_ops_flag_butone(aClient *one, int flag, const char *pattern, ...)
{
  va_list args;

  va_start(args, pattern);

  vsendto_ops_flag_butflag_butone_from(one, flag, 0, &me, pattern, args);

  va_end(args);
}

void
sendto_ops_flag_butflag(int flag, int butflag, const char* pattern, ...)
{
  va_list args;

  va_start(args, pattern);

  vsendto_ops_flag_butflag_butone_from(NULL, flag, butflag, &me, pattern, args);

  va_end(args);
}

void
sendto_ops_flag_butflag_butone(aClient* one, int flag, int butflag, const char* pattern, ...)
{
  va_list args;

  va_start(args, pattern);

  vsendto_ops_flag_butflag_butone_from(one, flag, butflag, &me, pattern, args);

  va_end(args);
}

/*
vsendto_ops_flag_butone()
 Send the given string to all clients with the given flag, excluding the given server connection
*/

static void
vsendto_ops_flag_butflag_butone_from(aClient *one, int flag, int butflag, aClient* from, const char *pattern, va_list args1)
{
  va_list args2;
  va_copy(args2,args1);

  vsendto_local_ops_flag_butflag_butone_from(one, flag, butflag, from, pattern, args1);
  vsendto_remote_ops_flag_butflag_butone_from(one, flag, butflag, from, pattern, args2);
  va_end(args2);
}

static void
vsendto_local_ops_flag_butflag_butone_from(aClient *one, int flag, int butflag, aClient* from, const char *pattern, va_list args)
{
  register aClient *cptr;
  char nbuf[512];
  va_list args_copy;
  va_copy(args_copy,args);

  for(cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
    {
      if (IsPerson(cptr) && (!flag || TestBit(cptr->umodes,flag)) && (!butflag || !TestBit(cptr->umodes, butflag)))
	{
	  ircsnprintf(nbuf, 512, ":%s NOTICE %s :*** Notice -- ",
		     from->name, cptr->name);
	  strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
	  va_end(args);
	  va_copy(args,args_copy);
	  vsendto_one(cptr, nbuf, args);
	}
    }
  va_end(args_copy);
}

static void
vsendto_remote_ops_flag_butflag_butone_from(aClient *one, int flag, int butflag, aClient* from, const char *pattern, va_list args)
{
  register aClient *cptr;
  char nbuf[1024];
  va_list args_copy;
  va_copy(args_copy,args);

  if (from && from->user)
    from->user->last_sent = CurrentTime;

  ircsnprintf(nbuf, 1024, ":%s WALLOPS %i-%i :",
	      from->name, flag, butflag);
  strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (IsMe(cptr) || (one && (cptr == one->from)))
	continue;
      if (cptr->serv->umode_count[flag] == 0)
	continue;
      va_end(args);
      va_copy(args,args_copy);
      vsendto_one(cptr, nbuf, args);
    }
  va_end(args_copy);
}


/* This one is used in the magic WALLOPS hack to propogate server notices */

void
sendto_ops_flag_butflag_butone_from(aClient *one, int flag, int butflag, aClient* from, const char *pattern, ...)
{
  va_list args;

  va_start(args, pattern);

  vsendto_ops_flag_butflag_butone_from(one, flag, butflag, from, pattern, args);

  va_end(args);
}

/* This one is used to propogate without giving away the source (so protecting routing information) */

void
sendto_ops_flag_butflag_butone_hidefrom(aClient *one, int flag, int butflag, const char *pattern, ...)
{
  register aClient *cptr;
  char nbuf[1024];
  va_list args;

  va_start(args, pattern);
  vsendto_local_ops_flag_butflag_butone_from(one, flag, butflag, &me, pattern, args);
  va_end(args);

  ircsnprintf(nbuf, 1024, ":%s WALLOPS *%i-%i :",
	      me.name, flag, butflag);
  strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (IsMe(cptr) || (one && (cptr == one->from)))
	continue;
      if (cptr->serv->umode_count[flag] == 0)
	continue;
      va_start(args, pattern);
      vsendto_one(cptr, nbuf, args);
      va_end(args);
    }
}

void
sendto_local_ops_flag(int flag, const char* pattern, ...)
{
  va_list args;

  va_start(args, pattern);

  vsendto_local_ops_flag_butflag_butone_from(NULL, flag, 0, &me, pattern, args);

  va_end(args);
}

void
sendto_local_ops_flag_butone(aClient* one, int flag, const char* pattern, ...)
{
  va_list args;

  va_start(args, pattern);

  vsendto_local_ops_flag_butflag_butone_from(one, flag, 0, &me, pattern, args);

  va_end(args);
}

void
sendto_local_ops_flag_butone_from(aClient* one, int flag, aClient* from, const char* pattern, ...)
{
  va_list args;

  va_start(args, pattern);

  vsendto_local_ops_flag_butflag_butone_from(one, flag, 0, from, pattern, args);

  va_end(args);
}
