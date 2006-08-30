/*
 * Change these if you want to allow for larger/smaller cut-off periods,
 * or to change the default expiry time and exit reason.
 */
#define HURT_CUTOFF 		(10) 		/* protocol messages. */
#define HURT_DEFAULT_EXPIRE 	(7 * 24 * 60)	/* minutes. */
#define HURT_EXIT_REASON	"Hurt: Failed to identify to services"

#define SNO(c) snomask_modes[(c)]

enum {
	HEAL_NICK = 0,
	HEAL_IP
};

typedef struct _hurt_state {
	time_t start_time;
	uint32_t n_hurts;
	dlink_list hurt_clients;
	uint16_t cutoff;
	time_t default_expire;
	const char *exit_reason;
} hurt_state_t;

typedef struct _hurt {
	const char *ip;
	struct sockaddr *saddr;
	int saddr_bits;
	const char *reason;
	time_t expire;
} hurt_t;

/*
 * vim: ts=8 sw=8 noet fdm=marker tw=80
 */
