/*
 *  
 */

#ifndef INCLUDED_restart_h
#define INCLUDED_restart_h

#include "config.h"

void restart(const char *) noreturn_attribute;
void server_reboot(void) noreturn_attribute;

#endif
