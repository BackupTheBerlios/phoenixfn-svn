/*
 * send.h
 * Copyright (C) 1999 Patrick Alken
 *
 * 
 */

#ifndef INCLUDED_send_h
#define INCLUDED_send_h
#ifndef INCLUDED_config_h
#include "config.h"       /* HAVE_STDARG_H */
#endif

#include "umodes.h"

/*
 * struct decls
 */
struct Client;
struct Channel;

/* send.c prototypes */

extern  void send_operwall(struct Client *, const char *, const char *);
extern  void sendto_channel_type_notice(struct Client *, 
                                        struct Channel *, int, const char *);
extern  int sendto_slaves(struct Client *, const char *, const char *, int, char **);

extern  void sendto_one(struct Client *, const char *, ...)
     printf_attribute(2,3);
extern  void send_markup(struct Client *, struct Client *, const char *, const char *, ...)
     printf_attribute(4,5);
extern  void sendto_channel_message_butone(struct Client *, struct Client *, 
                                   struct Channel *, const char *, const char *);
extern  void sendto_channel_butone(struct Client *, struct Client *, 
                                   struct Channel *, const char *, ...)
     printf_attribute(4,5);
extern  void sendto_channel_message(struct Client *, struct Client *, 
				    struct Channel *, const char *, const char *);
extern  void sendto_channel_type(struct Client *,
                                 struct Client *, 
                                 struct Channel *,
                                 int type,
                                 const char *nick,
                                 const char *cmd,
                                 const char *message);
extern  void sendto_serv_butone(struct Client *, const char *, ...)
     printf_attribute(2,3);
extern  void sendto_common_channels(struct Client *, const char *, ...)
     printf_attribute(2,3);
extern  void sendto_channel_butserv(struct Channel *, struct Client *, 
                                    const char *, ...)
     printf_attribute(3,4);

extern  void sendto_channel_chanops_butserv(struct Channel *chptr,
					    struct Client *from, 
					    const char *pattern, ...)
     printf_attribute(3,4);

extern  void sendto_channel_non_chanops_butserv(struct Channel *chptr,
						struct Client *from, 
						const char *pattern, ...)
     printf_attribute(3,4);

extern  void sendto_match_servs(struct Channel *, struct Client *, 
                                const char *, ...)
     printf_attribute(3,4);
extern  void sendto_match_cap_servs(struct Channel *, struct Client *, 
                                    int, int, const char *, ...)
     printf_attribute(5,6);
extern  void sendto_match_butone(struct Client *, struct Client *, 
                                 char *, int, const char *, ...)
     printf_attribute(5,6);

extern  void sendto_wallops_butone(struct Client *, struct Client *, 
                                   const char *, ...)
     printf_attribute(3,4);
extern  void ts_warn(const char *, ...)
     printf_attribute(1,2);

extern  void sendto_prefix_one(struct Client *, struct Client *, 
                               const char *, ...)
     printf_attribute(3,4);

extern  void    flush_server_connections(void);
extern void flush_connections(struct Client* cptr);

/* used when sending to #mask or $mask */

#define MATCH_SERVER  1
#define MATCH_HOST    2

extern  void sendto_ops_flag(int, const char *, ...)
     printf_attribute(2,3);
extern  void sendto_ops_flag_butone(struct Client *, int, const char *, ...)
     printf_attribute(3,4);
extern  void sendto_ops_flag_butflag(int, int, const char *, ...)
     printf_attribute(3,4);
extern  void sendto_ops_flag_butflag_butone(struct Client *, int, int, const char *, ...)
     printf_attribute(4,5);
extern  void sendto_ops_flag_butflag_butone_from(struct Client *, int, int, struct Client*, const char *, ...)
     printf_attribute(5,6);
extern  void sendto_ops_flag_butflag_butone_hidefrom(struct Client *, int, int, const char *, ...)
     printf_attribute(4,5);
extern  void sendto_local_ops_flag(int, const char*, ...)
     printf_attribute(2,3);
extern  void sendto_local_ops_flag_butone(struct Client *, int, const char *, ...)
     printf_attribute(3,4);
extern  void sendto_local_ops_flag_butone_from(struct Client *, int, struct Client*, const char *, ...)
     printf_attribute(4,5);

#endif /* INCLUDED_send_h */
