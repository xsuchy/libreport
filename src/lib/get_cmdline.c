/*
    Copyright (C) 2009  RedHat inc.

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
#include "internal_libreport.h"

/* If s is a string with only printable ASCII chars
 * and has no spaces, ", ', and \, copy it verbatim.
 * Else, encapsulate it in single quotes, and
 * encode ', " and \ with \c escapes.
 * Control chars are encoded as \r, \n, \t, or \xNN.
 * In all cases, terminating NUL is added
 * and the pointer to it is returned.
 */
static char *append_escaped(char *start, const char *s)
{
    char *dst = start;
    const unsigned char *p = (unsigned char *)s;

    while (1)
    {
        const unsigned char *old_p = p;
        while (*p > ' ' && *p <= 0x7e && *p != '\"' && *p != '\'' && *p != '\\')
            p++;
        if (dst == start)
        {
            if (p != (unsigned char *)s && *p == '\0')
            {
                /* entire word does not need escaping and quoting */
                strcpy(dst, s);
                dst += strlen(s);
                return dst;
            }
            *dst++ = '\'';
        }

        strncpy(dst, (char *)old_p, (p - old_p));
        dst += (p - old_p);

        if (*p == '\0')
        {
            *dst++ = '\'';
            *dst = '\0';
            return dst;
        }

        char hex_char_buf[5];
        const char *a;
        switch (*p)
        {
        case '\r': a = "\\r"; break;
        case '\n': a = "\\n"; break;
        case '\t': a = "\\t"; break;
        case '\'': a = "\\\'"; break;
        case '\"': a = "\\\""; break;
        case '\\': a = "\\\\"; break;
        case ' ': a = " "; break;
        default:
            /* Build \xNN string */
            hex_char_buf[0] = '\\';
            hex_char_buf[1] = 'x';
            hex_char_buf[2] = "0123456789abcdef"[*p >> 4];
            hex_char_buf[3] = "0123456789abcdef"[*p & 0xf];
            hex_char_buf[4] = '\0';
            a = hex_char_buf;
        }
        strcpy(dst, a);
        dst += strlen(a);
        p++;
    }
}

static char* get_escaped(const char *path, char separator)
{
    char *escaped = NULL;

    int fd = open(path, O_RDONLY);
    if (fd >= 0)
    {
        char *dst = NULL;
        unsigned total_esc_len = 0;
        while (total_esc_len < 1024 * 1024) /* paranoia check */
        {
            /* read and escape one block */
            char buffer[4 * 1024 + 1];
            int len = read(fd, buffer, sizeof(buffer) - 1);
            if (len <= 0)
                break;
            buffer[len] = '\0';

            /* string CC can expand into '\xNN\xNN' and thus needs len*4 + 3 bytes,
             * including terminating NUL.
             * We add +1 for possible \n added at the very end.
             */
            escaped = xrealloc(escaped, total_esc_len + len*4 + 4);
            char *src = buffer;
            dst = escaped + total_esc_len;
            while (1)
            {
                /* escape till next NUL char */
                char *d = append_escaped(dst, src);
                total_esc_len += (d - dst);
                dst = d;
                src += strlen(src) + 1;
                if ((src - buffer) >= len)
                    break;
                *dst++ = separator;
            }

        }

        if (dst)
        {
            if (separator == '\n')
                *dst++ = separator;
            *dst = '\0';
        }

        close(fd);
    }

    return escaped;
}

char* get_cmdline(pid_t pid)
{
    char path[sizeof("/proc/%lu/cmdline") + sizeof(long)*3];
    snprintf(path, sizeof(path), "/proc/%lu/cmdline", (long)pid);
    return get_escaped(path, ' ');
}

char* get_environ(pid_t pid)
{
    char path[sizeof("/proc/%lu/environ") + sizeof(long)*3];
    snprintf(path, sizeof(path), "/proc/%lu/environ", (long)pid);
    return get_escaped(path, '\n');
}

int get_ns_ids(pid_t pid, struct ns_ids *ids)
{
    int r = 0;
    static char ns_dir_path[sizeof("/proc/%lu/ns") + sizeof(long)*3];
    sprintf(ns_dir_path, "/proc/%lu/ns", (long)pid);

    DIR *ns_dir_fd = opendir(ns_dir_path);
    if (ns_dir_fd == NULL) {
        return -errno;
    }

    for (size_t i = 0; i < ARRAY_SIZE(libreport_proc_namespaces); ++i) {
        struct stat stbuf;
        if (fstatat(dirfd(ns_dir_fd), libreport_proc_namespaces[i], &stbuf, /* flags */0) != 0) {
            if (errno != ENOENT) {
                r = (i + 1);
                goto get_ns_ids_cleanup;
            }

            ids->nsi_ids[i] = PROC_NS_UNSUPPORTED;
            continue;
        }

        ids->nsi_ids[i] = stbuf.st_ino;
    }

get_ns_ids_cleanup:
    closedir(ns_dir_fd);

    return r;
}

int dump_namespace_diff(const char *dest_filename, pid_t base_pid, pid_t tested_pid)
{
    struct ns_ids base_ids;
    struct ns_ids tested_ids;

    if (get_ns_ids(base_pid, &base_ids) != 0)
    {
        log_notice("Failed to get base namesapce IDs");
        return -1;
    }

    if (get_ns_ids(tested_pid, &tested_ids) != 0)
    {
        log_notice("Failed to get tested namesapce IDs");
        return -2;
    }

    FILE *fout = fopen(dest_filename, "w");
    if (fout == NULL)
    {
        log_notice("Failed to create %s", dest_filename);
        return -3;
    }

    for (size_t i = 0; i < ARRAY_SIZE(libreport_proc_namespaces); ++i) {
        const char *status = "unknown";

        if (base_ids.nsi_ids[i] != PROC_NS_UNSUPPORTED)
            status = base_ids.nsi_ids[i] == tested_ids.nsi_ids[i] ? "default" : "own";

        fprintf(fout, "%s : %s\n", libreport_proc_namespaces[i], status);
    }
    return 0;
}

void mountinfo_destroy(struct mountinfo *mntnf)
{
    for (size_t i = 0; i < ARRAY_SIZE(mntnf->mntnf_items); ++i)
        free(mntnf->mntnf_items[i]);
}

int get_mountinfo_for_mount_point(FILE *fin, struct mountinfo *mntnf, const char *mnt_point)
{
    int r = 0;

    memset(mntnf->mntnf_items, 0, sizeof(mntnf->mntnf_items));

    long pos_bck = 0;
    int c = 0;
    int pre_c;
    unsigned fn;
    while (1)
    {
        pos_bck = ftell(fin);
        fn = 0;
        pre_c = c;
        while ((c = fgetc(fin)) != EOF)
        {
            fn += (c == '\n' || (pre_c != '\\' && c == ' '));
            if (fn >= 4)
                break;
        }

        if (c == EOF)
        {
            if (pre_c != '\n')
            {
                log_notice("Mountinfo line does not have enough fields %d\n", fn);
                r = 1;
            }
            goto get_mount_info_cleanup;
        }

        const char *ptr = mnt_point;
        while (((c = fgetc(fin)) != EOF) && (*ptr != '\0') && (c == *ptr))
            ++ptr;

        if (c == EOF)
        {
            log_notice("Mountinfo line does not have root field\n");
            r = 1;
            goto get_mount_info_cleanup;
        }

        if (*ptr == '\0' && c == ' ')
            break;

        /* this is the mount point we are searching for */
        while (((c = fgetc(fin)) != EOF) && c != '\n')
            ;

        if (c == EOF)
        {
            r = -1;
            goto get_mount_info_cleanup;
        }
    }

    fseek(fin, pos_bck, SEEK_SET);
    for (fn = 0; fn < sizeof(mntnf->mntnf_items)/sizeof(mntnf->mntnf_items[0]); )
    {
        pos_bck = ftell(fin);

        while ((c = fgetc(fin)) != EOF && (pre_c == '\\' || c != ' ') && c != '\n')
            ;

        if (c == EOF && fn != (sizeof(mntnf->mntnf_items)/sizeof(mntnf->mntnf_items[0]) - 1))
        {
            fprintf(stderr, "Unexpected end of file\n");
            r = 1;
            goto get_mount_info_cleanup;
        }

        /* we are standing on ' ', so len is +1 longer than the string we want to copy*/
        size_t len = (ftell(fin) - pos_bck);
        mntnf->mntnf_items[fn] = malloc(sizeof(char) * (len));
        if (mntnf->mntnf_items[fn] == NULL)
        {
            perror("malloc");
            r = 1;
            goto get_mount_info_cleanup;
        }

        --len; /* we are standing on ' ' */

        fseek(fin, pos_bck, SEEK_SET);
        if (fread(mntnf->mntnf_items[fn], sizeof(char), len, fin) != len)
        {
            log_warning("Failed to read from file");
            goto get_mount_info_cleanup;
        }

        mntnf->mntnf_items[fn][len] = '\0';

        fseek(fin, 1, SEEK_CUR);

        /* ignore optional fields
           'shared:X' 'master:X' 'propagate_from:X' 'unbindable'
         */
        if (   strncmp("shared:", mntnf->mntnf_items[fn], strlen("shared:")) == 0
            || strncmp("master:", mntnf->mntnf_items[fn], strlen("master:")) == 0
            || strncmp("propagate_from:", mntnf->mntnf_items[fn], strlen("propagate_from:")) == 0
            || strncmp("unbindable", mntnf->mntnf_items[fn], strlen("unbindable")) == 0)
        {
            free(mntnf->mntnf_items[fn]);
            mntnf->mntnf_items[fn] = NULL;
            continue;
        }

        ++fn;
    }

get_mount_info_cleanup:
    if (r)
        mountinfo_destroy(mntnf);

    return r;
}

static int proc_ns_eq(struct ns_ids *lhs_ids, struct ns_ids *rhs_ids, int neg)
{
    for (size_t i = 0; i < ARRAY_SIZE(lhs_ids->nsi_ids); ++i)
        if (    lhs_ids->nsi_ids[i] != PROC_NS_UNSUPPORTED
             && (neg ? lhs_ids->nsi_ids[i] == rhs_ids->nsi_ids[i]
                     : lhs_ids->nsi_ids[i] != rhs_ids->nsi_ids[i]))
            return 1;

    return 0;
}

static int get_process_ppid(pid_t pid, pid_t *ppid)
{
    int r = 0;
    static char stat_path[sizeof("/proc/%lu/stat") + sizeof(long)*3];
    sprintf(stat_path, "/proc/%lu/stat", (long)pid);

    FILE *stat_file = fopen(stat_path, "re");
    if (stat_file == NULL) {
        perror("Failed to open stat file");
        r = -1;
        goto get_process_ppid_cleanup;
    }

    int p = fscanf(stat_file, "%*d %*s %*c %d", ppid);
    if (p != 1) {
        fprintf(stderr, "Failed to parse stat line %d\n", p);
        r = -2;
        goto get_process_ppid_cleanup;
    }

get_process_ppid_cleanup:
    if (stat_file != NULL) {
        fclose(stat_file);
    }

    return r;
}

int process_is_in_container(pid_t pid)
{
    struct ns_ids pid_ids;
    struct ns_ids init_ids;

    if (get_ns_ids(pid, &pid_ids) != 0)
    {
        log_notice("Failed to get process's IDs");
        return -1;
    }

    if (get_ns_ids(1, &init_ids) != 0)
    {
        log_notice("Failed to get PID1's IDs");
        return -2;
    }

    /* If any pid's NS equals init's NS, then pid is not running in a container. */
    return proc_ns_eq(&init_ids, &pid_ids, /*neg*/1) == 0;
}

int get_pid_of_init(pid_t pid, pid_t *init_pid)
{
    pid_t cpid = pid;
    pid_t ppid = 0;

    struct ns_ids pid_ids;
    if (get_ns_ids(pid, &pid_ids) != 0)
    {
        log_notice("Failed to get process's IDs");
        return -1;
    }

    while (1)
    {
        if (get_process_ppid(cpid, &ppid) != 0)
            return -1;

        if (ppid == 1)
            break;

        struct ns_ids ppid_ids;
        if (get_ns_ids(ppid, &ppid_ids) != 0)
        {
            log_notice("Failed to get parent's IDs");
            return -2;
        }

        /* If any pid's  NS differs from parent's NS, then parent is pid's init. */
        if (proc_ns_eq(&pid_ids, &ppid_ids, 0) != 0)
            break;

        cpid = ppid;
    }

    *init_pid = ppid;
    return 0;
}

