/*
 * Copyright (c) 2005 Atheme Development Group
 * Rights to this code are as documented in doc/LICENSE.
 *
 * IRCServices's weird password encryption thingy, taken from Anope 1.6.3.
 *
 * $Id: ircservices.c 5107 2006-04-17 17:48:00Z gxti $
 */
/* Include file for high-level encryption routines.
 *
 * (C) 2003 Anope Team
 * Contact us at info@anope.org
 *
 * Please read COPYING and README for furhter details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church. 
 * 
 * $\Id: myencrypt.c 5 2004-03-29 01:29:50Z dane $ 
 *
 */

#include "atheme.h"

/* necessary anope defines */
#define PASSMAX 32
#define ENCRYPT_MD5

/*************************************************************************/

/******** Our own high-level routines. ********/


#define XTOI(c) ((c)>9 ? (c)-'A'+10 : (c)-'0')
/* Result of this:
 * c in [-128,9]  =>  [-183,-46]
 * c in [10,127]  =>  [-38,79]
 */

/* Encrypt `src' of length `len' and store the result in `dest'.  If the
 * resulting string would be longer than `size', return -1 and leave `dest'
 * unchanged; else return 0.
 */
static int myencrypt(const char *src, int len, char *dest, int size)
{

#ifdef ENCRYPT_MD5

    MD5_CTX context;
    char digest[33];
    char dest2[16];
    int i;

    if (size < 32)
        return -1;

    memset(&context, 0, sizeof(context));
    memset(&digest, 0, sizeof(digest));

    MD5Init(&context);
    MD5Update(&context, (unsigned const char *) src, (size_t) len);
    MD5Final((unsigned char *) digest, &context);

    for (i = 0; i < 32; i += 2)
        dest2[i / 2] = XTOI(digest[i]) << 4 | XTOI(digest[i + 1]);

    /* convert to hex, skipping last 8 bytes (constant) -- jilles */
    strcpy(dest, "$ircservices$");
    for (i = 0; i <= 7; i++)
	sprintf(dest + 13 + 2 * i, "%02x", 255 & (int)dest2[i]);
    return 0;

#endif

    return -1;                  /* unknown encryption algorithm */

}

#if 0 /* unused */
/* Shortcut for encrypting a null-terminated string in place. */
static int encrypt_in_place(char *buf, int size)
{
    return myencrypt(buf, strlen(buf), buf, size);
}
#endif

/* Compare a plaintext string against an encrypted password.  Return 1 if
 * they match, 0 if not, and -1 if something went wrong. */

static int check_password(const char *plaintext, const char *password)
{
    char buf[BUFSIZE];

    if (myencrypt(plaintext, strlen(plaintext), buf, sizeof(buf)) < 0)
        return -1;
#ifdef ENCRYPT_MD5
    if (strcmp(buf, password) == 0)
#else
    if (0)
#endif
        return 1;
    else
        return 0;
}

/*************************************************************************/

DECLARE_MODULE_V1
(
	"crypto/ircservices", FALSE, _modinit, _moddeinit,
	"$Id: ircservices.c 5107 2006-04-17 17:48:00Z gxti $",
	"Jilles Tjoelker <jilles@stack.nl>"
);

static char *ircservices_crypt_string(char *key, char *salt)
{
	static char output[PASSMAX];
	if (salt[0] == '$' && salt[1] == '1') /* this is a new pw XXX */
	{
		myencrypt(key, strlen(key), output, PASSMAX);
		return output;
	}
	else
	{
		if (check_password(key, salt))
			return salt;
		else
			return "";
	}
}

void _modinit(module_t *m)
{
	crypt_string = &ircservices_crypt_string;

	crypto_module_loaded = TRUE;
}

void _moddeinit(void)
{
	crypt_string = &generic_crypt_string;

	crypto_module_loaded = FALSE;
}
