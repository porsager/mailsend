#ifndef MAILSEND_H
#define MAILSEND_H

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <math.h>


#include "mutils.h"
#include "msock.h"
#include "sll.h"

#ifdef UNIX
#include <signal.h>
#endif /* UNIX */

#ifdef HAVE_OPENSSL

#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#endif /* HAVE_OPENSSL */


/*
**  header for mailsend - a simple mail sender via SMTP
**  $Id: mailsend.h,v 1.3 2002/06/22 21:17:29 muquit Exp $
**
**  Development History:
**      who                  when           why
**      muquit@muquit.com    Mar-23-2001    first cut
*/

#define MFL __FILE__,__LINE__

#define MAILSEND_VERSION    "@(#) mailsend v1.19"
#define MAILSEND_PROG       "mailsend"
#define MAILSEND_AUTHOR     "muquit@muquit.com"
#define MAILSEND_URL        "http://www.muquit.com/"
#define NO_SPAM_STATEMENT   "BSD. It is illegal to use this software for Spamming"

#define MAILSEND_SMTP_PORT  25
#define MAILSEND_DEF_SUB    ""

#define A_SPACE ' '
#define A_DASH  '-'

#define EMPTY_OK        0x01
#define EMPTY_NOT_OK    0x02

#define FILE_TYPE_DOS       0x00000001
#define FILE_TYPE_UNIX      0x00000002
#define FILE_TYPE_BINARY    0x00000004

#define DEFAULT_CONNECT_TIMEOUT   5 /* seconds */
#define DEFAULT_READ_TIMEOUT      5 /* seconds */


#ifdef EXTERN
#undef EXTERN
#endif /* EXTERN */

#ifndef __MAIN__
#define EXTERN extern
#else 
#define EXTERN
#endif /* __MAIN__ */

#ifdef WINNT
#define snprintf _snprintf
#endif /* WINNT */

#define CFL __FILE__,__LINE__

#define CHECK_MALLOC(x) \
do \
{ \
    if (x == NULL) \
    { \
        (void) fprintf(stderr,"%s (%d) - Memory allocation failed\n",CFL); \
        exit(0); \
    }\
}while(0)

#define CHECK_USERNAME(mech) \
do \
{ \
    if (*g_username == '\0') \
    { \
        (void) fprintf(stderr,"\nError: No user name specified for 'AUTH %s'\n",mech); \
        (void) fprintf(stderr,"       use the flag '-user username'\n\n");\
        rc=(-1);\
        goto cleanup; \
    }\
}while(0)

#define CHECK_USERPASS(mech) \
do \
{ \
    if (*g_userpass == '\0') \
    { \
        (void) fprintf(stderr,"\nError: No password specified for user %s for 'AUTH %s'\n",g_username,mech); \
        (void) fprintf(stderr,"       user '-pass password' or env var SMTP_USER_PASS\n\n");\
        rc=(-1);\
        goto cleanup; \
    }\
}while(0)

#define RETURN_IF_NOT_ZERO(rc) \
do \
{ \
    if (rc != 0) \
    { \
        return(rc); \
    } \
}while(0)

#define CHECK_WRITE_STATUS(n) \
do \
{ \
    if (n <= 0) \
    { \
        goto ExitProcessing; \
    } \
}while(0)


#define ERR_STR strerror(errno)

#define CONTENT_DISPOSITION_INLINE     0x01
#define CONTENT_DISPOSITION_ATTACHMENT 0x02

/* only suport base64 at this time */
/* as of Jul-01-2013 */
#define ENCODE_7BIT             0x01 /* default for text/plain */
#define ENCODE_8BIT             0x02
#define ENCODE_BASE64           0x03
#define ENCODE_QUOTED_PRINTABLE 0x04
#define ENCODE_NONE             0x05

#define DEFAULT_CHARSET "utf-8"

EXTERN int  g_verbose;
EXTERN int  g_connect_timeout;
EXTERN int  g_read_timeout;
EXTERN int  g_wait_for_cr;
EXTERN int  g_do_ssl;
EXTERN int  g_do_starttls;
EXTERN int  g_verify_certificate;
EXTERN int  g_quiet;
EXTERN int  g_do_auth;
EXTERN int  g_esmtp;
EXTERN int  g_auth_plain;
EXTERN int  g_auth_cram_md5;
EXTERN int  g_auth_login;
EXTERN char g_charset[33];
EXTERN char g_username[64];
EXTERN char g_userpass[64];
EXTERN char g_from_name[64];
EXTERN char g_content_transfer_encoding[32];
EXTERN FILE *g_log_fp;
EXTERN char g_log_file[MUTILS_PATH_MAX];
EXTERN int  g_show_attachment_in_log;
EXTERN int  g_use_protocol;
EXTERN char g_content_type[64];
EXTERN char g_attach_sep[4];
EXTERN char g_attach_name[64];
EXTERN char g_content_disposition[32];
EXTERN char g_content_id[64];
EXTERN char g_mime_type[64];
EXTERN int g_force;


typedef struct _Address
{

    /*
    ** label holds strings like "To" "Cc" "Bcc". 
    ** The address is the email address.
    */

    char
        *label,     /* To: Cc: Bcc: */
        *address;   /* the email address */
}Address;

typedef struct _Attachment
{
    char
        *file_path,
        *file_name,
        *attachment_name,
	    *content_id;

    char
        *oneline_msg;

    char
        *mime_type;

    char
        *content_disposition;

    char
        *content_transfer_encoding,
        *charset;

    int
        attach_separator;

    FILE
        *fp_read;

    char
        mime_tmpfile[MUTILS_PATH_MAX];
}Attachment;

/* the mail sturct */
typedef struct _TheMail
{
    SOCKET
        fd;

    Address
        *address;

    char
        *from,
        *subject,
        *x_mailer,
        *smtp_server,
        *helo_domain,
        *msg_file;
} TheMail;


/* struct for $HOME/.mailsendrc */
typedef struct _Mailsendrc
{
    char
        *domain,
        *from,
        *smtp_server;
}Mailsendrc;

/* function prototypes */
char        *xStrdup(const char *string);
int         addAddressToList(char *a,char *label);
TheMail     *initTheMail(void);
Address     *newAddress(void);
Sll         *getAddressList(void);
void        printAddressList(void);
void        print_server_caps(void);
void        print_one_lines(void);
char        *check_server_cap(char *what);
int         read_smtp_line(void);
int         read_smtp_multi_lines(void);
int         show_smtp_info(char *smtp_server,int port,char *domain);
int         send_the_mail(char *from,char *to,char *cc,char *bcc,char *sub,
                     char *smtp_server,int smtp_port,char *helo_domain,
                     char *attach_file,char *txt_msg_file,char *the_msg,
                     int is_mime,char *rrr,char *rt,int add_dateh,char* return_path_addr);
TheMail     *newTheMail(void);
void        errorMsg(char *format,...);
void        showVerbose(char *format,...);
void        print_info(char *format,...);
void        write_log(char *format,...);
void        open_log(const char *log_file);
void        close_log(void);
void        exit_ok(void);
void        exit_error(void);
void        log_info(const char *fmt, ...);
void        log_debug(const char *fmt, ...);
void        log_error(const char *fmt, ...);
void        log_fatal(const char *fmt, ...);
int         addAddressesFromFileToList(char *adress_list_file);
int         validateMusts(char *from,char *to,char *smtp_server,
                          char *helo_domain);
char        *askFor(char *buf,int buflen,char *label,int loop);
int         isInConsole(int fd);
int         add_one_line_to_list(char *line);
int         add_msg_body_files_to_list(char *file_path);
int         add_embed_image_to_attachment_list(const char *image_file);
int         add_customer_header_to_list(char *line);
int         add_attachment_to_list(char *file_path_mime);
int         add_oneline_to_attachment_list(char *one_line_msg);
int         add_msg_body_to_attachment_list(const char *msg_body_file);
int         add_server_cap_to_list(char *capability);
Sll         *get_one_line_list(void);
Sll         *get_custom_header_list(void);
Sll         *get_attachment_list(void);
Sll         *get_oneline_attachment_list(void);
Sll         *get_msg_body_attachment_list(void);
Sll         *get_embed_image_attachment_list(void);
Sll         *get_server_caps_list(void);
Sll         *get_msg_body_files_list(void);
void        print_attachment_list(void);
void        print_oneline_attachment_list(void);
char        *fix_to(char *to);
int         isInteractive(void);
int         get_filepath_mimetype(char *str,char *filename,int fn_size,
                                  char *mype_type,int mt_size);
int         rfc822_date(time_t when,char *datebuf,int bufsiz);

void        openssl_init_init_SSLLibrary(void);
int         do_tls(int sfd);
void        initialize_openssl(char *cipher);
char        *encode_cram_md5(char *challenge,char *user,char *pass);
int         guess_file_type(char *path,unsigned int *flag);
void        generate_encrypted_password(const char *plaintext);
void        print_copyright(void);
int         get_encoding_type(const char *type);
int         get_content_disposition(const char *disposition);
Attachment  *allocate_attachment(void);
int         write_to_socket(char *str);
int         print_content_type_header(const char *boundary);
int         send_attachment(Attachment *a, const char *boundary);
int         process_attachments(const char *boundary);
int         process_oneline_messages(const char *boundary);
int         process_embeded_images(const char *boundary);
int         encode2base64andwrite2socket(const char *str);
int         include_msg_body(void);
int         include_image(void);
void        show_examples(void);
char        *get_mime_type(char *path);
#ifdef HAVE_OPENSSL
void        print_cert_info(SSL *ssl);
void        show_mime_types(void);
#endif /* HAVE_OPENSSL */

#endif /* ! MAIL_SEND_H */
