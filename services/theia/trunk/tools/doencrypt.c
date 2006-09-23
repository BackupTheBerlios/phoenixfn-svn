#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

/*
doencrypt()
 Encrypt a plaintext string. Returns a pointer to encrypted
string
*/

extern char *libshadow_md5_crypt(const char *, const char *);

char *
doencrypt(char *plaintext)

{
  char *hash, salt[12] = "";
  static char saltChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";

  salt[0] = saltChars[random() % 64];
  salt[1] = saltChars[random() % 64];
  salt[2] = saltChars[random() % 64];
  salt[3] = saltChars[random() % 64];
  salt[4] = saltChars[random() % 64];
  salt[5] = saltChars[random() % 64];
  salt[6] = saltChars[random() % 64];
  salt[7] = saltChars[random() % 64];
  salt[8] = 0;
  hash = libshadow_md5_crypt(plaintext, salt);

  return hash;
} /* doencrypt() */
