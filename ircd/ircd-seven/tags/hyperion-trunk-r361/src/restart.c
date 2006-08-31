/*
 * restart.c
 *
 */

#include "restart.h"
#include "common.h"
#include "ircd.h"
#include "send.h"
#include "struct.h"
#include "s_debug.h"
#include "s_log.h"
#include "client.h"
#include "umodes.h"
#include "paths.h"

#include <unistd.h>
#include <stdlib.h>

/* external var */
extern char** myargv;

void restart(const char *mesg)
{
  static int was_here = NO; /* redundant due to restarting flag below */

  if (was_here)
    abort();
  was_here = YES;

  logprintf(L_NOTICE, "Restarting Server because: %s, memory data limit: %zi",
      mesg, get_maxrss());

  server_reboot();
}

void server_reboot(void)
{
  int i;
  
  sendto_ops_flag(UMODE_SERVNOTICE, "Aieeeee!!!  Restarting server... memory: %zi", get_maxrss());

  logprintf(L_NOTICE, "Restarting server...");
  flush_connections(0);
#ifdef SAVE_MAXCLIENT
  write_stats();
#endif

  for (i = 0; i < MAXCONNECTIONS; ++i)
    close(i);
  execv(my_binary, myargv);

  exit(-1);
}


