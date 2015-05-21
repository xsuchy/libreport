/*
    Copyright (C) 2010  ABRT team
    Copyright (C) 2010  RedHat Inc

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#include <sys/un.h>
#include "internal_libreport.h"

#define SOCKET_FILE  VAR_RUN"/abrt/abrt.socket"

static int connect_to_abrtd_and_call_DeleteDebugDump(const char *dump_dir_name)
{
    int result = 1; /* error so far */

    int socketfd = xsocket(AF_UNIX, SOCK_STREAM, 0);
    /*close_on_exec_on(socketfd); - not needed, we are closing it soon */
    struct sockaddr_un local;
    memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, SOCKET_FILE);
    int r = connect(socketfd, (struct sockaddr*)&local, sizeof(local));
    if (r == 0)
    {
        full_write(socketfd, "DELETE ", strlen("DELETE "));
        full_write(socketfd, dump_dir_name, strlen(dump_dir_name));
        full_write(socketfd, " HTTP/1.1\r\n\r\n", strlen(" HTTP/1.1\r\n\r\n"));
        shutdown(socketfd, SHUT_WR);

        char response[64];
        r = full_read(socketfd, response, sizeof(response) - 1);
        if (r >= 0)
        {
            VERB1 log("Response via socket:'%.*s'", r, response);
            /*  0123456789...  */
            /* "HTTP/1.1 200 " */
            response[5] = '1';
            response[7] = '1';
            if (strncmp(response, "HTTP/1.1 ", strlen("HTTP/1.1 ")) == 0
                    && isdigit(response[9])
                    && isdigit(response[10])
                    && isdigit(response[11])
                    && response[12] == ' ')
            {
                result = (response[9] - '0') * 100 + (response[10] - '0') * 10 + (response[11] - '0');
            }
        }
    }
    else
    {
        VERB1 perror_msg("Can't connect to '%s'", SOCKET_FILE);
    }
    close(socketfd);

    return result;
}

int delete_dump_dir_possibly_using_abrtd(const char *dump_dir_name)
{
    /* The old schema "abrt:user" causes ABRT to move dump directories to
     * user's home, so in the old schema we try to delete it ourselves first
     * and then we try to use abrtd. */
#if DUMP_DIR_OWNED_BY_USER == 0
    /* Try to delete it ourselves */
    struct dump_dir *dd = dd_opendir(dump_dir_name, DD_OPEN_READONLY);
    if (dd)
    {
        if (dd->locked) /* it is not readonly */
            return dd_delete(dd);
        dd_close(dd);
    }
    else
    {
        if (errno == ENOENT || errno == ENOTDIR)
            /* No such dir, no point in trying to talk over socket */
            return 1;
    }

    VERB1 log("Deleting '%s' via abrtd", dump_dir_name);
    const int res = connect_to_abrtd_and_call_DeleteDebugDump(dump_dir_name);
    if (res != 0)
        error_msg(_("Can't delete: '%s'"), dump_dir_name);

    return res;
#else
    VERB2 log("Deleting '%s' via abrtd", dump_dir_name);
    const int res = connect_to_abrtd_and_call_DeleteDebugDump(dump_dir_name);
    if (res == 200)
    {
        /*
         * Deleted
         */
        return 0;
    }

    /*
     * An error occurred but we can still try to delete it directly
     */

    /* Using NULL in order to easily detect a buggy error message */
    const char *error_reason = NULL;
    /* Used only for error messages */
    char num_buf[sizeof(int)*3 + 1];

    if (res < 0 || res == 400)
    {
        /*  -1 : an error in communication
         * 400 : bad request or abrtd refused to delete the directory outside of the dump location
         *
         * Try to delete it ourselves
         */
        struct dump_dir *dd = dd_opendir(dump_dir_name, DD_OPEN_READONLY);
        if (dd)
        {
            if (dd->locked) /* it is not readonly */
                return dd_delete(dd);

            error_reason = _("locked by another process");
            dd_close(dd);
        }
    }
    else
    {
        switch (res)
        {
            case 403:
                error_reason = _("permission denied");
                break;
            case 404:
                error_reason = _("not a problem directory");
                break;
            default:
                snprintf(num_buf, sizeof(num_buf), "%d", res);
                error_reason = num_buf;
                break;
        }
    }

    error_msg(_("Can't delete '%s': %s"), dump_dir_name, error_reason);
    return 1;
#endif
}
