/* dline_conf.h  -- lets muse over dlines, shall we?
 *
 */

#ifndef INCLUDED_dline_conf_h
#define INCLUDED_dline_conf_h

#include <sys/types.h>

struct Client;
struct ConfItem;

extern void clear_Dline_table(void);
extern void zap_Dlines(void);
extern void add_Dline(struct ConfItem *);
extern void add_ip_Kline(struct ConfItem *);
extern void add_ip_Eline(struct ConfItem *);

extern size_t count_dlines(void);
extern size_t count_ip_klines(void);

extern void add_dline(struct ConfItem *);

extern struct ConfItem *match_Dline(unsigned long);
struct ConfItem *find_Dline_exception(unsigned long);
extern struct ConfItem *match_ip_Kline(unsigned long, const char *);

extern void report_dlines(struct Client *);
extern void report_ip_Klines(struct Client *);
extern void report_ip_Ilines(struct Client *);

#endif /* INCLUDED_dline_conf_h */


