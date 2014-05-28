/*
**  SMTP routines for mailsend - a simple mail sender via SMTP
**
**  Limitations and Comments:
**    SMTP RF[C] RFC-821
**    Also look at: TCP/IP Illustraged Vol 1 by Richard Stevens
**
**    Written mainly for NT, Unix has this kind of tools in hundreds.
**
**  Development History:
**      who                  when           why
**      muquit@muquit.com    Mar-26-2001    first cut
*/

#include "mailsend.h"

static char buf[BUFSIZ];
static int  bufsz = sizeof(buf) - 1;
static int  break_out=0;

static char smtp_line[BUFSIZ];
static char smtp_errbuf[BUFSIZ];
static int  smtp_code;
static int  smtp_sep;
static int  s_esmtp=0;

#ifdef WINNT
static BOOL 
    WINAPI CntrlHandler(DWORD CtrlEvent);
#else
#define closesocket close
#endif /* WINNT */

void smtpDisconnect(void)
{
    msock_close();
}
/* connect to SMTP server and returns the socket fd */
static SOCKET smtpConnect(char *smtp_server,int port)
{
    SOCKET
        sfd;

    if (g_use_protocol == MSOCK_USE_IPV4)
    {
        showVerbose("Forcing to use IPv4 address of SMTP server\n");
    }
    else if (g_use_protocol == MSOCK_USE_IPV6)
    {
        showVerbose("Forcing to use IPv6 address of SMTP server\n");
    }
    else
    {
        showVerbose("Will detect IPv4 or IPv6 automatically\n");
    }
    
    sfd=clientSocket(g_use_protocol, smtp_server,port, g_connect_timeout);
    if (sfd == INVALID_SOCKET)
    {
        errorMsg("Could not connect to SMTP server \"%s\" at port %d",
                smtp_server,port);
        return (INVALID_SOCKET);
    }

    /* set the socket to msock lib's static place, not thread safe*/
    msock_set_socket(sfd);

    return (sfd);
}

int read_smtp_multi_lines(void)
{
    int
        rc,
        lcnt = 0;
    if (smtp_sep == A_DASH)
    {
        for (;;)
        {
            rc=read_smtp_line();
            if (rc < 0)
                break;
            lcnt++;
            if (lcnt >= 100)
            {
                errorMsg("Too many lines from server\n");
                rc=(-1);
                goto ExitProcessing;
            }
            if (smtp_sep != A_DASH)
                break;
        }
    }
    smtp_sep = A_SPACE;
    return(0);
ExitProcessing:
    return(1);
}

/*
 * sfd   socket 
 *
 * return 0 on success, -1 on failure
 *
 * populates globals: smtp_code,smtp_line and smtp_errbuf on error
 *
 */
int read_smtp_line(void)
{
    int
        rc=(-1),
        n;

    char
        tbuf[BUFSIZ],
        lbuf[BUFSIZ];

    memset(smtp_line,0,sizeof(smtp_line));
    memset(smtp_errbuf,0,sizeof(smtp_errbuf));
    smtp_code=(-1);

    memset(lbuf,0,sizeof(lbuf));
    /* read a line */
    n=msock_gets(lbuf,sizeof(lbuf)-1);
    if (n < 3 )
    {
        /*errorMsg("Error reading SMTP line, read %d bytes",n);*/
        return(-1);
    }
    showVerbose("[S] %s\n",lbuf);
    if (n >= 5)
    {
        memset(tbuf,0,sizeof(tbuf));
        memcpy(tbuf,lbuf,3);
        smtp_code=atoi(tbuf);

        smtp_sep=lbuf[3];

        (void) snprintf(smtp_line,sizeof(smtp_line)-1,"%s",lbuf + 4);
        rc=0;
/*
        (void) fprintf(stderr," Line: \"%s\"\n",lbuf);
        (void) fprintf(stderr," Code: \"%d\"\n",smtp_code);
        (void) fprintf(stderr," Sep: '%c'\n",smtp_sep);
        (void) fprintf(stderr," line: \"%s\"\n",smtp_line);
*/
    }
    else
    {
        (void) snprintf(smtp_errbuf,sizeof(smtp_errbuf)-1,"%s",lbuf);
    }
    

    return(rc);
}

int smtp_start_tls(int sfd)
{
    int
        rc=(-1);
#ifdef HAVE_OPENSSL
    SSL
        *ssl=NULL;
#endif /* HAVE_OPENSSL */
    memset(buf,0,sizeof(buf));
    (void) snprintf(buf,sizeof(buf)-1,"%s\r\n","STARTTLS");
    showVerbose("[C] %s",buf);
    msock_puts(buf);
    rc=read_smtp_line();
    if (smtp_code != 220)
    {
        errorMsg("Unknown STARTTLS response code %d\n",smtp_code);
        return(-1);
    }
#ifdef HAVE_OPENSSL
    ssl=msock_get_ssl();
    if (ssl)
    {
        if (!SSL_set_fd(ssl,sfd))
        {
            errorMsg("failed to set socket to SSL\n");
            return(-1);
        }
        /* must set back to msock's static */
        msock_set_ssl(ssl);
        rc=SSL_connect(ssl);
        if (rc < 1)
        {
            errorMsg("SSL connection failed\n");
            ERR_print_errors_fp(stderr);
            return(-1);
        }
        print_cert_info(ssl);

        if(g_verify_certificate) {
            rc = SSL_get_verify_result(ssl);
            if(rc != X509_V_OK) {
                errorMsg("Certificate verification error: %ld\n", rc);
                ERR_print_errors_fp(stderr);
                return(-1);
            } else {
                (void) fprintf(stdout,"Certificate verified!\n");
            }
        }

        /* tell msock everything is ssl after that */
        msock_turn_ssl_on();
        rc=0;
    }
    else
    {
        errorMsg("Could not start STARTTLS, SSL not initialized properly");
        rc=(-1);
    }
#else
    errorMsg("Not Compiled with OpenSSL, will not try STARTTLS");
    rc=(-1);
#endif /*HAVE_OPENSSL */
    return(rc);
}

/* SMTP: HELO */
static int say_helo(char *helo_domain)
{
    int
        cnt=0,
        rc;

    (void) snprintf(buf,sizeof(buf)-1,"%s %s\r\n",
                    s_esmtp ? "EHLO" : "HELO",helo_domain);
    showVerbose("[C] %s",buf);

    /* send */
    msock_puts(buf);
    rc=read_smtp_line();
    if (smtp_code != 250)
    {
        errorMsg("%s failed", s_esmtp ? "EHLO" : "HELO");
        return(-1);
    }
    /* read all the capabilities if separator is - */
    if (smtp_sep == A_DASH)
    {
        for (;;)
        {
            rc=read_smtp_line();
            if (rc == 0)
                add_server_cap_to_list(smtp_line);
            cnt++;
            if (cnt >= 1000)
                break;
            if (rc < 0 || smtp_sep != A_DASH)
                break;
        }
    }
    smtp_sep = A_SPACE;

    return(rc);
}


/* SMTP: MAIL FROM */
static int smtp_MAIL_FROM(char *from)
{
    memset(buf,0,sizeof(buf));
    (void) snprintf(buf,sizeof(buf)-1,"MAIL FROM: <%s>\r\n",from);
    showVerbose("[C] %s",buf);

    msock_puts(buf);
    read_smtp_line();
    if (smtp_code != 250)
    {
        errorMsg("MAIL FROM failed: '%d %s'",smtp_code,smtp_line);
        return(-1);
    }
    return(0);
}

/* SMTP: quit */
static int smtp_QUIT(void)
{
    showVerbose("[C] QUIT\r\n");
    msock_puts("QUIT\r\n");
    read_smtp_line();
    /*
    ** google does not seem to write anything back in response to QUIT
    ** command. I'll ignore it anyway
    */
    return(0);
}

/* SMTP: RSET */
/* aborts current mail transaction and cause both ends to reset */
static int smtp_RSET()
{
    msock_puts("RSET\r\n");
    return(read_smtp_line());
}



/* SMTP: RCPT TO */
static int smtp_RCPT_TO(void)
{
    Sll
        *l,
        *al;

    Address
        *a;

    char
        *x;

    int
        rc;

    al=getAddressList();
    
    for (l=al; l; l=l->next)
    {
        a=(Address *) l->data;
        if (! a)
            return(-1);
        if (! a->address)
            return(-1);

        memset(buf,0,sizeof(buf));
        x=getenv("NOTIFY_RCPT");
        if (x != NULL)
        {
            /* MS Exchange has it */
            showVerbose("NOTIFY_RCPT=%s\n",x);
            (void) snprintf(buf,sizeof(buf)-1,"RCPT TO: %s %s\r\n",
                            a->address,x);
        }
        else
        {
            (void) snprintf(buf,sizeof(buf)-1,"RCPT TO: <%s>\r\n",a->address);
        }

        showVerbose("[C] %s",buf);
        
        msock_puts(buf);
        rc=read_smtp_line();
        if (rc == 0)
        {
            if (smtp_code != 250)
            {
                errorMsg("RCPT TO: <%s> failed '%d:%s'\n",
                        a->address,smtp_code,smtp_line);
                smtp_RSET();
                return(-1);
            }
        }
    }
    return (0);

}

/* SMTP: DATA */
static int smtp_DATA(void)
{
    int
        rc;

    msock_puts("DATA\r\n");
    showVerbose("[C] DATA\r\n");

    rc=read_smtp_line();
    if (rc == 0)
    {
        if (smtp_code != 354)
        {
            errorMsg("DATA failed: '%d %s'\n",smtp_code,smtp_line);
            return(-1);
        }
    }
    return(0);
}

/* SMTP: EOM */
/* return 0 on success, -1 on failure */
int smtpEom(int sfd)
{
    int
        rc;

    msock_puts("\r\n.\r\n");

    showVerbose("\r\n[C] .\r\n");

    /*
    ** Bug# 1 
    ** we want to see smtp code 250 now
    ** if mail is too big, it can mail with 552 message too large
    */
    rc = read_smtp_line();
    if (smtp_code != 250)
    {
        read_smtp_multi_lines();
        errorMsg("Expected smtp code 250, got %d\n",smtp_code);
        rc = (-1);
    }

    return(rc);
}

void doCleanup(void)
{
    smtpDisconnect();
}

#ifdef WINNT
/*
** Handle Ctrl+C 
*/
static BOOL WINAPI CntrlHandler(DWORD CtrlEvent)
{
    break_out=0;

    switch (CtrlEvent)
    {
        case CTRL_C_EVENT:
        {
            break_out=1;
            (void) fprintf(stderr,"\nNot sending mail. Exiting.......\n");
            exit_error();
            break;
        }
    }

    return (TRUE);
}
#endif /* WINNT */

int write_to_socket(char *str)
{
    int
        n;
    if (str == NULL)
    {
        return (-1);
    }
    n = msock_puts(str);
    /*showVerbose("%d bytes: %s\n",n, str);*/
    return(n);
}

/*
** return 0 on success
*/
int encode2base64andwrite2socket(const char *str)
{
    FILE
        *tfp1 = NULL,
        *tfp2 = NULL;
    char
        mbuf[1000],
        oneline_tempfile1[MUTILS_PATH_MAX],
        oneline_tempfile2[MUTILS_PATH_MAX];

    memset(oneline_tempfile1, 0, sizeof(oneline_tempfile1));
    /* write the text to a tmp file */
    tfp1 = mutils_get_tempfileFP(oneline_tempfile1,
            sizeof(oneline_tempfile1)-1);
    if (tfp1 == NULL)
    {
        errorMsg("%s (%d) - Could not open temp file1 for writing (%s)",
                MFL,
                ERR_STR);
        return (-1);
    }
    (void) fprintf(tfp1,"%s",str);
    (void) fclose(tfp1);

    /* open another tmp file to write the base64 of the first tmp file to */
    memset(oneline_tempfile2, 0, sizeof(oneline_tempfile2));
    tfp2 = mutils_get_tempfileFP(oneline_tempfile2,
        sizeof(oneline_tempfile2)-1);
    showVerbose("Oneline temp file2: * %s\n",oneline_tempfile2);
    if (tfp2 == NULL)
    {
        errorMsg("%s (%d) - Could not open temp file2 for writing (%s)",
            MFL,
            ERR_STR);
        return (-1);
    }

    tfp1 = fopen(oneline_tempfile1,"rb");
    if (tfp1 == NULL)
    {
        errorMsg("%s (%d) - Could not open temp file for reading (%s)",
            MFL,
            ERR_STR);
        return(-1);
    }

    mutilsBase64Encode(tfp1,tfp2);
    (void) fclose(tfp1);
    (void) fclose(tfp2);

    /* open the file with base64 and write the content to socket */
    tfp2 = fopen(oneline_tempfile2,"r");
    if (tfp2 == NULL)
    {
        errorMsg("%s (%d) - Could not open temp file for reading (%s)",
            MFL,
            ERR_STR);
        return(-1);
    }
    while(fgets(mbuf, sizeof(mbuf)-1, tfp2))
    {
        write_to_socket(mbuf);
        if (g_show_attachment_in_log)
        {
            showVerbose("[C] %s",mbuf);
        }
    }
    (void) fclose(tfp2);
    unlink(oneline_tempfile1);
    unlink(oneline_tempfile2);
 
    return(0);
}

/*
** send one line messages, each one is an inline attachment
** return 0 if mail is sent, -1 otherwise
*/
int process_oneline_messages(const char *boundary)
{
    Attachment
        *a = NULL;
    Sll
        *l,
        *oneline_attachment_list;

    oneline_attachment_list = get_oneline_attachment_list();
    if (oneline_attachment_list == NULL)
    {
        return(0);
    }
    print_oneline_attachment_list();

    for (l = oneline_attachment_list; l; l = l->next)
    {
        a = (Attachment *) l->data;
        (void) snprintf(buf, bufsz, "\r\n--%s\r\n",boundary);
        write_to_socket(buf);

        if (strcmp(a->charset,"none") != 0)
        {
            (void) snprintf(buf, bufsz, "Content-Type: %s; charset=%s\r\n",
                a->mime_type,
                a->charset);
        }
        else
        {
            (void) snprintf(buf, bufsz, "Content-Type: %s\r\n",a->mime_type);
        }
        write_to_socket(buf);

        (void) strcpy(buf,"Content-Disposition: inline\r\n");
        write_to_socket(buf);

        /* add encoding type if needed */
        if (strncmp(a->content_transfer_encoding,"none",4) != 0)
        {
            (void) snprintf(buf, bufsz, "Content-Transfer-Encoding: %s\r\n\r\n",
                    a->content_transfer_encoding);
            write_to_socket(buf);
        }

        
        if (strncmp(a->content_transfer_encoding,"base64",6) == 0)
        {
            /* encode the mssage to base 64 and write to socket */
            encode2base64andwrite2socket(a->oneline_msg);
        }
        else
        {
            /*
            ** no encoding type, so last \r\n was no added, we must
            ** add it or some mailer will thing the mail is invalid
            */
            if (strncmp(a->content_transfer_encoding,"none",4) == 0)
            {
                write_to_socket("\r\n");
            }
            write_to_socket(a->oneline_msg);
            if (g_show_attachment_in_log)
            {
                showVerbose("[C] %s\n",a->oneline_msg);

            }
        }
        write_to_socket("\r\n");
    }
    return(0);
}

/*
** include the content of the file as body of the message
** No encoding will be done.
*/
int include_msg_body(void)
{
    Sll
        *l,
        *msg_body_attachment_head;

    FILE
        *fp = NULL;

    Attachment
        *a;

    msg_body_attachment_head = get_msg_body_attachment_list();
    if (msg_body_attachment_head == NULL)
    {
        return(-1);
    }

    l = msg_body_attachment_head;
    a = (Attachment *) l->data;

    (void) snprintf(buf,bufsz,"Mime-version: 1.0\r\n");
    write_to_socket(buf);

    if (strcmp(a->charset,"none") != 0)
    {
        (void) snprintf(buf,bufsz,"Content-Type: %s; charset=%s\r\n\r\n",
                a->mime_type,
                a->charset);
    }
    else
    {
        (void) snprintf(buf,bufsz,"Content-Type: %s\r\n\r\n",a->mime_type);
    }
    write_to_socket(buf);

    fp=fopen(a->file_path,"r");
    if (fp == (FILE *) NULL)
    {
        errorMsg("Could not open message body file: %s",a->file_path);
        return (-1);
    } 

    while (fgets(buf,bufsz,fp))
    {
        write_to_socket(buf);
        if (g_show_attachment_in_log)
        {
            showVerbose("[C] %s",buf); 
        }
    }
    (void) fclose(fp);

    (void) snprintf(buf,bufsz,"\r\n\r\n");
    msock_puts(buf);
    showVerbose(buf);
    return(0);
}

int include_image(void)
{
    return(0);
}

static void cleanup_attachment(Attachment *a)
{
    if (a == NULL)
        return;
    if (a->fp_read)
    {
        (void) fclose(a->fp_read);
    }
    if (*a->mime_tmpfile != '\0')
    {
        unlink(a->mime_tmpfile);
    }
}


/*
** return the attachment with fp_read member open.
** If encoding type requires some kind of encoding,
** mime_tmpfile will contain the file which holds the
** encoded content. The caller must close the file pointer
** and unlik the tmp_filename
** return NULL on error
*/
static Attachment *get_encoded_attachment(Attachment *a)
{
    FILE
        *fp_read = NULL,
        *tfp_write = NULL;
    int
        tempfile_opened = 0;

    if (strncmp(a->content_transfer_encoding, "base64", 6) == 0)
    {
        tfp_write = mutils_get_tempfileFP(a->mime_tmpfile,sizeof(a->mime_tmpfile)-1);
        if (tfp_write == NULL)
        {
            errorMsg("%s (%d) - Could not create temp file for MIME (%s)",
                    MFL,
                    ERR_STR);
            return(NULL);
        }
        tempfile_opened = 1;
        showVerbose("%s (%d) - MIME temp file: %s created successfully, FILE pointer=%x\n",
                MFL,
                a->mime_tmpfile,
                tfp_write);

        /* open the file to attach */
        fp_read = fopen(a->file_path,"rb");
        if (fp_read == (FILE *) NULL)
        {
            errorMsg("%s (%d) - Could not open file for %s reading (%s)",
                    MFL,
                    a->file_path,
                    ERR_STR);
            goto ExitProcessing;
        }

        showVerbose("%s (%d) - Writing Content to FILE pointer: %x\n",
                MFL,
                tfp_write);
        /* write base64 content to tmp file */
        mutilsBase64Encode(fp_read,tfp_write);
        (void) fclose(fp_read);
        fp_read = NULL;

        (void) fclose(tfp_write);
        tfp_write = NULL;
        a->fp_read = fopen(a->mime_tmpfile, "r");
        if (a->fp_read == (FILE *) NULL)
        {
            errorMsg("%s (%d) - Could not open file for %s reading (%s)",
                    MFL,
                    a->file_path,
                    ERR_STR);
            goto ExitProcessing;
        }
        if (tempfile_opened)
        {
            unlink(a->mime_tmpfile);
        }
        return(a);
    }
    /* no tmp file */
    memset(a->mime_tmpfile, 0, sizeof(a->mime_tmpfile));
    
    /* open the attachment. caller must close it */
    a->fp_read = fopen(a->file_path, "r");
    if (a->fp_read == (FILE *) NULL)
    {
        errorMsg("%s (%d) - Could not open file for %s reading (%s)",
                MFL,
                a->file_path,
                ERR_STR);
        return (NULL);
    }
    return(a);

ExitProcessing:
    if (tempfile_opened)
    {
        unlink(a->mime_tmpfile);
    }
    return(NULL);
}

/*
** write MIME headers, encode attachment if needed
** and write to socket
** returns 0 on success -1 on failure
*/
int send_attachment(Attachment *a, const char *boundary)
{
    Attachment
        *encoded_attachment;

    encoded_attachment = get_encoded_attachment(a);
    if (encoded_attachment == NULL)
    {
        errorMsg("%s (%d) - Could not encode attachment %s",
                MFL,
                a->file_path);
        return(-1);
    }

    (void) snprintf(buf, bufsz,"--%s\r\n",boundary);
    write_to_socket(buf);

    if (a->charset && strncmp(a->charset,"none", 4) != 0)
    {
        (void) snprintf(buf, bufsz, "Content-Type: %s; charset=%s\r\n",
                a->mime_type,
                a->charset);
    }
    else
    {
        if (a->attachment_name)
        {
            (void) snprintf(buf, bufsz, "Content-Type: %s; name=\"%s\"\r\n",
                    a->mime_type,
                    a->attachment_name);
        }
        else
        {
            (void) snprintf(buf, bufsz, "Content-Type: %s\r\n",a->mime_type);
        }
    }
    write_to_socket(buf);
    
    if (a->content_id == NULL)
    {
        if (strncmp(a->content_disposition, "inline", 6) == 0)
        {
            (void) snprintf(buf, bufsz, "Content-Disposition: %s\r\n",a->content_disposition);
        }
        else
        {
            if (a->attachment_name)
            {
                (void) snprintf(buf, bufsz, "Content-Disposition: %s; filename=\"%s\"\r\n",
                    a->content_disposition,
                    a->attachment_name);

            }
            else if (a->file_name)
            {
                (void) snprintf(buf, bufsz, "Content-Disposition: %s; filename=\"%s\"\r\n",
                    a->content_disposition,
                    a->file_name);
            }
            else
            {
                (void) snprintf(buf, bufsz, "Content-Disposition: %s\r\n",a->content_disposition);
            }
        }
        write_to_socket(buf);
    }

    if (a->content_id)
    {
        (void) snprintf(buf, bufsz, "Content-ID: <%s>\r\n",a->content_id);
        write_to_socket(buf);
        (void) snprintf(buf, bufsz, "X-Attachment-Id: %s\r\n",a->content_id);
        write_to_socket(buf);
    }

    if (strncmp(a->content_transfer_encoding,"none",4) != 0)
    {
        (void) snprintf(buf, bufsz, "Content-Transfer-Encoding: %s\r\n\r\n",
                a->content_transfer_encoding);
        write_to_socket(buf);
    }
    else
    {
        write_to_socket("\r\n");
    }

    /* our FILE pointer is already open and read to read from */
    while (fgets(buf, bufsz, a->fp_read))
    {
        write_to_socket(buf);
    }
    /* close file, remove tmp file if needed */
    cleanup_attachment(a);
    return(0);
}

int process_attachments(const char *boundary)
{
    Attachment
        *a;

    Sll
        *attachment_list,
        *al;

    int
        rc;

    attachment_list = get_attachment_list();
    if (attachment_list == NULL)
    {
        return(0);
    }

    for (al=attachment_list; al; al=al->next)
    {
        a=(Attachment *) al->data;
        if (a == NULL)
            continue;

        rc = send_attachment(a, boundary);
        if (rc == -1)
        {
            errorMsg("%s (%d) - failed to send attachment %s\n",
                    MFL,
                    a->file_path);
            return(-1);
        }
    }
    
    (void) snprintf(buf,sizeof(buf)-1,"--%s--\r\n",boundary);
    msock_puts(buf);
    showVerbose(buf);

    return(0);
}

/*
** Print multipart/mixed or multipart/related header
** if mixed and embedded images are specified, make
** to use multipart/alternative for the embedded images.
*/
int print_content_type_header(const char *boundary)
{
    Sll
        *oneline_attachment_list,
        *attachment_list,
        *embed_image_list;

    (void) snprintf(buf, bufsz,"Mime-version: 1.0\r\n");
    write_to_socket(buf);

    if (*g_content_type!='\0')
    {
      (void) snprintf(buf,sizeof(buf)-1,"Content-type: %s; boundary=\"%s\"\r\n",
              g_content_type,
              boundary);
      write_to_socket(buf);
      return(0);
    }

    oneline_attachment_list = get_oneline_attachment_list();
    attachment_list = get_attachment_list();
    embed_image_list = get_embed_image_attachment_list();
    if (oneline_attachment_list || attachment_list)
    {
        (void) snprintf(buf,sizeof(buf)-1,
                "Content-type: multipart/mixed; boundary=\"%s\"\r\n", boundary);
        write_to_socket(buf);
        return(0);
    }
    if (embed_image_list)
    {
        (void) snprintf(buf,sizeof(buf)-1,
                "Content-type: multipart/related; boundary=\"%s\"\r\n", boundary);
        write_to_socket(buf);
        return(0);
    }
    write_to_socket("\r\n");
    return(0);
}

int process_embeded_images(const char *boundary)
{
    char
        *ib,
        *b,
        related[17],
        cid[17],
        tbuf[24],
        alternative[17];

    Attachment
        *a;

    Sll
        *il,
        *oneline_attachment_list,
        *attachment_list,
        *embed_image_list;

    int
        rc,
        ic = 1;

    oneline_attachment_list = get_oneline_attachment_list();
    attachment_list = get_attachment_list();

    embed_image_list = get_embed_image_attachment_list();
    if (embed_image_list == NULL)
    {
        return(0);
    }
    memset(related, 0, sizeof(related));
    memset(alternative, 0, sizeof(alternative));

    mutilsGenerateMIMEBoundary(related,sizeof(related));
    mutilsGenerateMIMEBoundary(alternative,sizeof(alternative));
    b = boundary;
    ib = boundary;

    (void) snprintf(buf, bufsz, "--%s\r\n",boundary);
    write_to_socket(buf);

    if (attachment_list || oneline_attachment_list)
    {
        b = related;
        ib = b;
        (void) snprintf(buf, bufsz, "Content-Type: multipart/related; boundary=%s\r\n",b);
        write_to_socket(buf);

        (void) snprintf(buf, bufsz, "Content-Type: multipart/alternative; boundary=%s\r\n\r\n",
                alternative);
        write_to_socket(buf);

        (void) snprintf(buf, bufsz, "--%s\r\n",b);
        write_to_socket(buf);
    }
    else
    {
        ib = alternative;
        (void) snprintf(buf, bufsz, "Content-Type: multipart/alternative; boundary=%s\r\n\r\n",
                alternative);
        write_to_socket(buf);

        (void) snprintf(buf, bufsz, "--%s\r\n",alternative);
        write_to_socket(buf);
    }

    write_to_socket("Content-Type: text/html; charset=ISO-8859-1\r\n\r\n");

    /* write the img tags with cid */
    embed_image_list = get_embed_image_attachment_list();
    for (il = embed_image_list; il; il = il->next)
    {
        a = (Attachment *) il->data;
        mutilsGenerateMIMEBoundary(cid,sizeof(cid));
        (void) snprintf(tbuf,sizeof(tbuf)-1,"ii%d_%s",ic,cid);
        a->content_id = xStrdup(tbuf);
        (void) snprintf(buf, bufsz, "<img src=\"cid:%s\" alt=\"inline image %d\"><br>\n",
                tbuf,
                ic);
        write_to_socket(buf);
        ic++;
    }
    write_to_socket("\r\n");
    (void) snprintf(buf, bufsz, "--%s--\r\n",ib);
    write_to_socket(buf);

    for (il = embed_image_list; il; il = il->next)
    {
        a = (Attachment *) il->data;
        if (a == NULL)
            continue;
        rc = send_attachment(a,b);
        RETURN_IF_NOT_ZERO(rc);
    }
    (void) snprintf(buf, bufsz, "--%s--\r\n",b);
    write_to_socket(buf);
    return(0);
}

static void print_end_boundary(const char *boundary)
{
    if (get_attachment_list() == NULL)
    {
        (void) snprintf(buf,sizeof(buf)-1,"--%s--\r\n",boundary);
        msock_puts(buf);
        showVerbose(buf);
        return;
    }
    /* process_attachments(boundary) already printed the end bounday */
}

/* SMTP: mail */
static int smtpMail(int sfd,char *to,char *cc,char *bcc,char *from,char *rrr,char *rt,
                    char *subject,char *attach_file,char *msg_body_file,
                    char *the_msg,int is_mime,int add_dateh)
{
    char
        *os="Unix",
        boundary[17],
        related[17],
        alternative[17],
        mbuf[1024];

    int
        newline_before;

    Sll
        *oneline_attachment_list,
        *attachment_list,
        *embed_image_list;

#ifdef WINNT
    os="Windows";
#else
    os="Unix";
#endif /* WINNT */

    memset(boundary, 0, sizeof(boundary));
    memset(related, 0, sizeof(related));
    memset(alternative, 0, sizeof(alternative));

    attachment_list=get_attachment_list();
    embed_image_list = get_embed_image_attachment_list();
    oneline_attachment_list = get_oneline_attachment_list();
    if (attachment_list || embed_image_list || oneline_attachment_list)
    {
        is_mime=1;
    }

    if (subject)
    {
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"Subject: %s\r\n",subject);

        msock_puts(buf);

        showVerbose(buf);
    }

    /* headers */
    if (from)
    {
        memset(buf,0,sizeof(buf));
        if (*g_from_name != '\0')
        {
            /* Name in From: */

            memset(buf,0,sizeof(buf));
            (void) snprintf(buf,sizeof(buf)-1,"From: %s <%s>\r\n",
                            g_from_name,from);
        }
        else
        {
            (void) snprintf(buf,sizeof(buf)-1,"From: %s\r\n",from);
        }
        msock_puts(buf);

        showVerbose(buf);
    }

    if (add_dateh)
    {
        /* add Date: header */
        char
            datebuf[65];

        memset(datebuf,0,sizeof(datebuf));
        if (rfc822_date(time(NULL),datebuf,sizeof(datebuf)-1) == 0)
        {
            memset(buf,0,sizeof(buf));
            (void) snprintf(buf,sizeof(buf)-1,"Date: %s\r\n",datebuf);
            msock_puts(buf);

            showVerbose(buf);
        }
    }
    
    if (to)
    {
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"To: %s\r\n",to);
        msock_puts(buf);

        showVerbose(buf);

    }

    if (cc)
    {
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"Cc: %s\r\n",cc);
        msock_puts(buf);
        showVerbose(buf);
    }

    /*
    if (bcc)
    {
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"Bcc: %s\r\n",bcc);
        msock_puts(buf);

        showVerbose(buf);
    }
    */

    if (rt != NULL)
    {
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"Reply-To: %s\r\n",rt);
        msock_puts(buf);
        showVerbose(buf);

    }
    if (rrr != NULL)
    {
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"Disposition-Notification-To: %s\r\n",rrr);
        msock_puts(buf);
        showVerbose(buf);
    }

    /* add custom headers if any. No verification is done */
    {
        Sll 
            *l,
            *custom_header_list;
        custom_header_list = get_custom_header_list();
        if (custom_header_list)
        {
            for (l = custom_header_list; l; l = l->next)
            {
                if (l->data)
                {
                    msock_puts((char *) l->data);
                    msock_puts("\r\n");
                    showVerbose((char *) l->data);
                    showVerbose("\r\n");
                }
            }
        }
    }


    memset(buf,0,sizeof(buf));
    (void) snprintf(buf,sizeof(buf)-1,"X-Mailer: %s (%s)\r\n",MAILSEND_VERSION,os);
    msock_puts(buf);
    showVerbose(buf);

    memset(buf,0,sizeof(buf));
    (void) snprintf(buf,sizeof(buf)-1,"X-Copyright: %s\r\n",NO_SPAM_STATEMENT);
    msock_puts(buf);
    showVerbose(buf);

    if (is_mime)
    {
        int
            rc;
        srand(time(NULL));
        memset(boundary,0,sizeof(boundary));
        mutilsGenerateMIMEBoundary(boundary,sizeof(boundary));

        /* if msg body file is specified, include and return */
        if (msg_body_file)
        {
            return (include_msg_body());
        }

        rc = print_content_type_header(boundary);
        RETURN_IF_NOT_ZERO(rc);

        rc = process_oneline_messages(boundary);
        RETURN_IF_NOT_ZERO(rc);

        rc = process_embeded_images(boundary);
        RETURN_IF_NOT_ZERO(rc);

        rc = process_attachments(boundary);
        RETURN_IF_NOT_ZERO(rc);

        /*
        ** if there were other kind of MIME types but no attachments,
        ** we have to print the last boundary 
        */
        print_end_boundary(boundary);

        /* handle MIME attachments ends */
        goto done;
    } /* is_mime */

    /* mail body */
    if (attach_file == NULL && the_msg == NULL) /* read from stdin */
    {

        /* if stdin is a terminal, print the instruction */
        if (isInConsole(_fileno(stdin)))
        {
            (void) printf("=========================================================================\n");
            (void) printf("Type . in a new line and press Enter to end the message, CTRL+C to abort\n");
            (void) printf("=========================================================================\n");
        }

#ifdef WINNT
        SetConsoleCtrlHandler(CntrlHandler,TRUE);
#endif /* WINNT */

        newline_before=1;
        msock_puts("\r\n"); /* RFC822 sec 3.1 */
        showVerbose("\r\n");
        while (fgets(mbuf,sizeof(mbuf)-1,stdin) && (break_out == 0))
        {
            if (newline_before && *mbuf == '.')
            {
                break;
            }
            else
            {
                int
                    len;
                /* vinicio qmail fix */
                len=strlen(mbuf);
                if (mbuf[len-1] != '\n')
                    strcat(mbuf,"\r\n");
                else
                {
                   mbuf[--len]='\0';
                   strcat(mbuf,"\r\n");
                }
                /* vinicio qmail fix */
                msock_puts(mbuf);
                showVerbose("[C] %s",mbuf);
            }
            newline_before=(*mbuf != '\0' && mbuf[strlen(mbuf)-1] == '\n');
            if (break_out == 1)
            {
                (void) fprintf(stderr," Breaking out\n");
                return (0);
            }
        }
    }
done:

    return (0);
}

/* greeting can be multi line */
static int read_greetings(void)
{
    int
        lcnt=0,
        rc=(-1);

    rc=read_smtp_line();
    if (rc < 0)
        goto cleanup;

    s_esmtp=g_esmtp; /* if forced with -ehlo */

    if (smtp_code != 220)
    {
        read_smtp_multi_lines();
        errorMsg("Expected smtp code 220, got %d\n",smtp_code);
        rc=(-1);
        goto cleanup;
    }
    if (mutilsStristr(smtp_line,"ESMTP"))
    {
        s_esmtp=1;
    }
    /* greeting can be multi-line */
    if (smtp_sep == A_DASH)
    {
        for (;;)
        {
            rc=read_smtp_line();
            if (rc < 0)
                break;
            lcnt++;
            if (lcnt >= 100)
            {
                errorMsg("Too many greeting lines\n");
                rc=(-1);
                goto cleanup;
            }
            if (mutilsStristr(smtp_line,"ESMTP"))
            {
                s_esmtp=1;
            }
            if (smtp_sep != A_DASH)
                break;
        }
    }
cleanup:
    smtp_sep = A_SPACE;
    return(rc);
}

static int turn_on_raw_ssl(SOCKET sfd)
{
#ifdef HAVE_OPENSSL
    int
        rc=(-1);

    SSL
        *ssl;

    ssl=msock_get_ssl();
    if (ssl)
    {
        if (!SSL_set_fd(ssl,sfd))
        {
            errorMsg("turn_on_raw_ssl: failed to set socket %d to SSL\n",sfd);
            return(-1);
        }
        /* must set back to msock's static */
        msock_set_ssl(ssl);
        rc=SSL_connect(ssl);
        if (rc < 1)
        {
            errorMsg("turn_on_raw_ssl: SSL connection failed\n");
            ERR_print_errors_fp(stderr);
            return(-1);
        }
        print_cert_info(ssl);

        (void) fprintf(stdout,"Should verify certificate: %ld\n",g_verify_certificate);
        if(g_verify_certificate) {
            rc = SSL_get_verify_result(ssl);
            (void) fprintf(stdout,"Verify certificate result: %ld\n",rc);
            if(rc != X509_V_OK) {
                errorMsg("Certificate verification error: %ld\n", rc);
                ERR_print_errors_fp(stderr);
                return(-1);
            }
        }

        /* tell msock everything is ssl after that */
        msock_turn_ssl_on();
        rc=0;
        return(0);
    }
#endif /* HAVE_OPENSSL */
    return(-1);
}

/* It's stupid, I need to change all the args to a struct, one of those 
 * days! I'll do it
 */

/* returns 0 on success, -1 on failure */
int send_the_mail(char *from,char *to,char *cc,char *bcc,char *sub,
             char *smtp_server,int smtp_port,char *helo_domain,
             char *attach_file,char *txt_msg_file,char *the_msg,int is_mime,char *rrr,char *rt,
             int add_dateh)
{
    SOCKET
        sfd;

    TheMail
        *mail;


    Sll
        *al;

    int
        rc=(-1);

    char
        *mech=NULL,
        *auth=NULL;

    /*
    unsigned char
		*b64=NULL;
    */
    char
        *b64 = NULL;

    int
        authenticated=0;
 
    /*
    (void) fprintf(stderr,"From: %s\n",from);
    (void) fprintf(stderr,"To: %s\n",to);
    (void) fprintf(stderr,"Cc: %s\n",cc);
    (void) fprintf(stderr,"Cc: %s\n",bcc);
    (void) fprintf(stderr,"Sub: %s\n",sub);
    (void) fprintf(stderr,"smtp: %s\n",smtp_server);
    (void) fprintf(stderr,"smtp port: %d\n",smtp_port);
    (void) fprintf(stderr,"domain: %s\n",helo_domain);
    (void) fprintf(stderr,"attach file: %s\n",attach_file);
    (void) fprintf(stderr,"txt_msg_file: %s\n",txt_msg_file);
    (void) fprintf(stderr,"the_msg: %s\n",the_msg);
    (void) fprintf(stderr,"is_mime: %d\n",is_mime);
    */

    al=getAddressList();

    if (al == (Sll *) NULL)
    {
        errorMsg("No To address/es specified");
        return (-1);
    }

    if (from == (char *) NULL)
    {
        errorMsg("No From address specified");
        return (-1);
    }

    if (smtp_server == (char *) NULL)
        smtp_server="127.0.0.1";

    if (smtp_port == -1)
        smtp_port=MAILSEND_SMTP_PORT;

    if (sub == (char *) NULL)
        sub=MAILSEND_DEF_SUB;

    if (helo_domain == (char *) NULL)
    {
        errorMsg("No domain specified");
        return (-1);
    }

    mail=newTheMail();
    if (mail == (TheMail *) NULL)
    {
        errorMsg("Error: malloc failed in createTheMail()\n");
        return (-1);
    }

    showVerbose("Connecting to %s:%d\n",smtp_server,smtp_port);
    /* open the network connection */
    sfd=smtpConnect(smtp_server,smtp_port);
    if (sfd == INVALID_SOCKET)
    {
        rc=(-1);
        goto cleanup;
    }

    if (g_do_ssl) /* smtp.gmail:465 supports it for example */
    {
        rc = turn_on_raw_ssl(sfd);
        if(rc < 0)
            goto cleanup;
    }

    /* read greeting */
    rc=read_greetings();
    if (rc < 0)
        goto cleanup;

    /* say HELO/EHLO */
    say_helo(helo_domain);

    /* check if the server supports STARTTLS or TLS */
    if (g_do_starttls)
    {
        if (check_server_cap("STARTTLS") ||
            check_server_cap("TLS"))
        {
            rc=smtp_start_tls(sfd);
            if (rc == 0)
            {
               /* send HELO again */
                say_helo(helo_domain);
            }

            if(rc < 0)
                goto cleanup;
        }
    }

    if (g_do_auth || g_auth_cram_md5 || g_auth_login || g_auth_plain)
    {
        auth=check_server_cap("AUTH");
    }
    if (!auth)
        goto MailFrom;
    /*
    (void) fprintf(stderr,"auth=%s\n",auth);
    (void) fprintf(stderr," g_auth_cram_md5=%d; g_auth_login=%d; g_auth_plain=%d\n", g_auth_cram_md5, g_auth_login, g_auth_plain);
    */

    /*
    if (auth && g_do_auth)
    {
        g_auth_cram_md5=1;
        g_auth_login=1;
        g_auth_plain=1;
    }
    */
    /* Try CRAM-MD5 first */
    mech="CRAM-MD5";
    if (g_auth_cram_md5 && check_server_cap(mech))
    {
        char
            *cmd5 = NULL;

        CHECK_USERNAME(mech);
        CHECK_USERPASS(mech);

#ifndef HAVE_OPENSSL
        errorMsg("Must be compiled with OpenSSL in order to get CRAM-MD5 support\n");
        goto cleanup;
#endif /* !HAVE_OPENSSL */
        showVerbose("Using AUTH %s\n",mech);
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"AUTH %s\r\n",mech);
        showVerbose("[C] %s",buf);
        msock_puts(buf);

        read_smtp_line();
        if (smtp_code != 334)
        {
            errorMsg("AUTH CRAM-MD5 failed: '%d %s'",
                    smtp_code,
                    smtp_line);
            rc=(-1);
            goto cleanup;
        }
        cmd5 = encode_cram_md5(smtp_line,g_username,g_userpass);
        if (cmd5 == NULL)
        {
            errorMsg("Could not encode CRAM-MD5");
            rc = (-1);
            goto cleanup;
        }
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"%s\r\n",cmd5);
        showVerbose("[C] %s",buf);
        msock_puts(buf);
        read_smtp_line();
        if (smtp_code != 235)
        {
            errorMsg("AUTH CRAM-MD5 failed: '%d %s'",
                    smtp_code,
                    smtp_line);
            rc=(-1);
            goto cleanup;
        }

        showVerbose("%s Authentication succeeded\n",mech);
        authenticated++;
        if (cmd5)
        {
            (void) free((char *) cmd5);
        }
    }
    else
    {
        if (g_auth_cram_md5)
            showVerbose("Server does not support AUTH CRAM-MD5\n");
    }
    if (authenticated)
        goto MailFrom;

    mech="LOGIN";
    if (g_auth_login && check_server_cap(mech))
    {
        CHECK_USERNAME(mech);
        CHECK_USERPASS(mech);

        showVerbose("Using AUTH %s\n",mech);
        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"AUTH %s\r\n",mech);
        showVerbose("[C] %s",buf);
        msock_puts(buf);

        read_smtp_line();
        if (smtp_code != 334)
        {
            errorMsg("AUTH LOGIN failed: '%d %s'",
                    smtp_code,
                    smtp_line);
            rc=(-1);
            goto cleanup;
        }
        /*
        b64=mutils_encode_base64(g_username,strlen(g_username),&b64len);
        b64[b64len-2]='\0';
        */
        b64=mutils_encode_base64_noformat(g_username,strlen(g_username));
        if (b64 == NULL)
        {
            errorMsg("Could not base64 encode user: %s",g_username);
            rc=(-1);
            goto cleanup;
        }

        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"%s\r\n",b64);
        showVerbose("[C] %s",buf);
        msock_puts(buf);
        read_smtp_line();
        if (smtp_code != 334)
        {
            errorMsg("AUTH LOGIN failed: '%d %s'",
                    smtp_code,
                    smtp_line);
            rc=(-1);
            goto cleanup;
        }

        /*
        b64=mutils_encode_base64(g_userpass,strlen(g_userpass),&b64len);
        b64[b64len-2]='\0';
        */
        b64=mutils_encode_base64_noformat(g_userpass,strlen(g_userpass));
        if (b64 == NULL)
        {
            errorMsg("Could not base64 encode passworf of user: %s",g_username);
            rc=(-1);
            goto cleanup;
        }

        memset(buf,0,sizeof(buf));
        (void) snprintf(buf,sizeof(buf)-1,"%s\r\n",b64);
        showVerbose("[C] %s",buf);
        msock_puts(buf);
        read_smtp_line();
        if (smtp_code != 235)
        {
            errorMsg("AUTH LOGIN failed: '%d %s'",
                    smtp_code,
                    smtp_line);
            rc=(-1);
            goto cleanup;
        }
        authenticated++;
    }
    else
    {
        if (g_auth_login)
            showVerbose("Server does not support AUTH LOGIN\n");
    }


    if (authenticated)
        goto MailFrom;

    mech="PLAIN";
    if (g_auth_plain && check_server_cap(mech))
    {
        int
            len,
            ulen,
            plen;

        unsigned char
            *b64=NULL;

        CHECK_USERNAME(mech);
        CHECK_USERPASS(mech);

        showVerbose("Using AUTH %s\n",mech);
        memset(buf,0,sizeof(buf));
        /*
        ** authzid\0authid\0pass
        ** authzid can be skipped if both are the same
        */

        ulen=strlen(g_username);
        memcpy(buf + 1,g_username,ulen);
        plen=strlen(g_userpass);

        memcpy(buf + ulen + 2,g_userpass,plen);
        len=ulen + plen + 2;
#if 0
        b64=mutils_encode_base64(buf,len,&b64len);
        /* mutils_encode_base64 adds CRLF */
        b64[b64len-2]='\0';
#endif
        b64=mutils_encode_base64_noformat(buf,len);
        if (b64 == NULL)
        {
            errorMsg("Could not base64 for AUTH-PLAIN for user: %s",g_username);
            rc=(-1);
            goto cleanup;
        }

        (void) snprintf(buf,sizeof(buf)-1,"AUTH PLAIN %s\r\n",(char *) b64);

        showVerbose("[C] %s",buf);
        msock_puts(buf);

        read_smtp_line();
        if (smtp_code != 235)
        {
            errorMsg("AUTH PLAIN failed: '%d %s'",
                    smtp_code,
                    smtp_line);
            rc=(-1);
            goto cleanup;
        }
        showVerbose("PLAIN Authentication succeeded\n");
        authenticated++;
    }
    else
    {
        if (g_auth_plain)
            showVerbose("Server does not support AUTH PLAIN\n");
    }

    if (authenticated)
        goto MailFrom;

    if (auth && !g_quiet)
    {
        if (!g_auth_cram_md5)
        {
            if (check_server_cap("CRAM-MD5"))
            {
                (void) fprintf(stderr,
" * Server supports AUTH CRAM-MD5.");
            }
        }
        if (!g_auth_login)
        {
            if (check_server_cap("LOGIN"))
            {
                (void) fprintf(stderr,
" * Server supports AUTH LOGIN.\n");
            }
        }

        if (!g_auth_plain)
        {
            if (check_server_cap("PLAIN"))
            {
                (void) fprintf(stderr,
" * Server supports AUTH PLAIN.\n");
            }
        }
        if (!authenticated)
        {
            (void) fprintf(stderr,
" Use -auth or specify a mechanism that server supports. exiting.\n");
            exit_error();
        }
    }

MailFrom:
    rc=smtp_MAIL_FROM(from);
    if (rc != 0)
        goto cleanup;

    rc=smtp_RCPT_TO();
    if (rc != 0)
        goto cleanup;

    rc=smtp_DATA();
    if (rc != 0)
        goto cleanup;

    rc=smtpMail(sfd,to,cc,bcc,from,rrr,rt,sub,attach_file,txt_msg_file,the_msg,is_mime,add_dateh);
    RETURN_IF_NOT_ZERO(rc);

    rc=smtpEom(sfd);
    RETURN_IF_NOT_ZERO(rc);
    smtp_QUIT();

cleanup:
    return (rc);
}

/*
** print info about SMTP server on stdout
** return 0 on success -1 on error
*/
int show_smtp_info(char *smtp_server,int port,char *domain)
{
    int
        fd,
        rc = (-1);
#ifdef HAVE_OPENSSL
    SSL
        *ssl=NULL;
#endif /* HAVE_OPENSSL */

    g_verbose=1;
    print_info("Connecting to SMTP server: %s at Port: %d\n",smtp_server,port);
    print_info("Connection timeout: %d secs\n",g_connect_timeout);
    /* connect */
    fd=smtpConnect(smtp_server,port);
    if (fd == INVALID_SOCKET)
    {
        rc = (-1);
        goto ExitProcessing;
    }

#ifdef HAVE_OPENSSL
    if (g_do_ssl)
    {
        ssl=msock_get_ssl();
        if (ssl)
        {
            if (!SSL_set_fd(ssl,fd))
            {
                errorMsg("failed to set socket to SSL\n");
                goto ExitProcessing;
            }
            /* must set back to msock's static */
            msock_set_ssl(ssl);
            rc=SSL_connect(ssl);
            if (rc < 1)
            {
                errorMsg("SSL connection failed\n");
                ERR_print_errors_fp(stderr);
                goto ExitProcessing;
            }
            print_cert_info(ssl);
            /* tell msock everything is ssl after that */
            msock_turn_ssl_on();
            rc=0;
        }
    }
#endif /* HAVE_OPENSSL */

    /* read greeting */
    rc=read_greetings();
    if (rc < 0)
    {
        errorMsg("Could not read greetings\n");
        goto ExitProcessing;
    }

    /* say HELO/EHLO */
    say_helo(domain);

    /* check if server supports STARTTLS */
    if (check_server_cap("STARTTLS") ||
        check_server_cap("TLS"))
    {
        rc=smtp_start_tls(fd);
        if (rc == 0)
            say_helo(domain);
        else
            goto ExitProcessing;
    }
    smtp_QUIT();
    smtpDisconnect();

ExitProcessing:
    return (rc);
}

