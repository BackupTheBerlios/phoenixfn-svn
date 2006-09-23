## Process this file with automake to produce Makefile.in

include $(top_srcdir)/build-misc/common.mk

sbin_PROGRAMS = hyperion-ircd

AM_CPPFLAGS = -DPREFIX="\"$(prefix)\""

hyperion_ircd_SOURCES = ircd.c \
	blalloc.c \
	channel.c \
	class.c \
	client.c \
	dbuf.c \
	dline_conf.c \
	m_dump.c \
	fdlist.c \
	feature.c \
	fileio.c \
	flud.c \
	hash.c \
	irc_string.c \
	ircd_signal.c \
	list.c \
	listener.c \
	m_admin.c \
	m_away.c \
	m_burst.c \
	m_capab.c \
	m_chall.c \
	m_close.c \
	m_collide.c \
	m_connect.c \
	md5.c \
	md5crypt.c \
	m_die.c \
	m_error.c \
	m_htm.c \
	m_info.c \
	m_ison.c \
	m_help.c \
	m_kill.c \
	m_kline.c \
	m_links.c \
	m_list.c \
	m_ltrace.c \
	m_lusers.c \
	m_makepass.c \
	m_map.c \
	m_message.c \
	m_mode.c \
	m_operwall.c \
	m_oper.c \
	m_pass.c \
	m_ping.c \
	m_pong.c \
	m_quit.c \
	m_rehash.c \
	m_resp.c \
	m_restart.c \
	m_scan.c \
	m_services.c \
	m_server.c \
	m_set.c \
	m_setname.c \
	m_snick.c \
	m_spingtime.c \
	m_squit.c \
	m_stats.c \
	m_stopic.c \
	m_svinfo.c \
	m_time.c \
	m_trace.c \
	m_unkline.c \
	m_userhost.c \
	m_users.c \
	m_version.c \
	m_wallops.c \
	m_who.c \
	m_whois.c \
	match.c \
	motd.c \
	mtrie_conf.c \
	numeric.c \
	packet.c \
	paths.c \
	parse.c \
	res.c \
	restart.c \
	s_auth.c \
	s_bsd.c \
	s_cr.c \
	s_conf.c \
	s_debug.c \
	s_log.c \
	s_misc.c \
	s_serv.c \
	s_stats.c \
	s_user.c \
	s_zip.c \
	scache.c \
	send.c \
	sprintf_irc.c \
	svn_version.c \
	tsdelta.c \
	whowas.c \
	umodes.c \
	varparse.c \
	version.c

.PHONY: svn_version.c

svn_version.c:
	if [ -d .svn ] || [ ! -f svn_version.c ]; then \
		( \
		echo '/* SVN repository version.'; \
		echo ' * This auto-generated file must not be included in svn, but must be included'; \
		echo ' * in tarballs.'; \
		echo ' */'; \
		echo '#include "ircd.h"'; \
		printf 'const char* serial = "'; \
		(cd ..; svnversion -n .); \
		echo '" SERIAL_ADDENDUM;') > svn_version.c.new; \
		if cmp svn_version.c svn_version.c.new; then \
			rm svn_version.c.new; \
		else \
			mv -f svn_version.c.new svn_version.c; \
		fi; \
	fi

