/*
    Copyright (C) 2015       Bastien Nocera <hadess@hadess.net>
    Copyright (C) 2011, 2013 Lennart Poettering•
    Copyright (C) 2015       RedHat inc.

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

#include <acl/libacl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
#include <dirent.h>

#include "internal_libreport.h"

#define IN_SET(x, y, ...)                                               \
        ({                                                              \
                const typeof(y) _y = (y);                               \
                const typeof(_y) _x = (x);                              \
                unsigned _i;                                            \
                bool _found = false;                                    \
                for (_i = 0; _i < 1 + sizeof((const typeof(_x)[]) { __VA_ARGS__ })/sizeof(const typeof(_x)); _i++) \
                        if (((const typeof(_x)[]) { _y, __VA_ARGS__ })[_i] == _x) { \
                                _found = true;                          \
                                break;                                  \
                        }                                               \
                _found;                                                 \
        })


int calc_acl_mask_if_needed(acl_t *acl_p) {
        acl_entry_t i;
        int r;

        assert(acl_p);

        for (r = acl_get_entry(*acl_p, ACL_FIRST_ENTRY, &i);
             r > 0;
             r = acl_get_entry(*acl_p, ACL_NEXT_ENTRY, &i)) {
                acl_tag_t tag;

                if (acl_get_tag_type(i, &tag) < 0)
                        return -errno;

                if (tag == ACL_MASK)
                        return 0;
                if (IN_SET(tag, ACL_USER, ACL_GROUP))
                        goto calc;
        }
        if (r < 0)
                return -errno;
        return 0;

calc:
        if (acl_calc_mask(acl_p) < 0)
                return -errno;
        return 1;
}

int add_group_acl(int fd, gid_t gid)
{
        acl_t acl = NULL;
        acl_entry_t entry;
        acl_permset_t permset;

        assert(fd >= 0);

        acl = acl_get_fd(fd);
        if (!acl)
        {
                perror_msg("Failed to get ACL: %s", strerror(errno));
                return -errno;
        }

        if (acl_create_entry(&acl, &entry) < 0 ||
            acl_set_tag_type(entry, ACL_GROUP) < 0 ||
            acl_set_qualifier(entry, &gid) < 0) {
                perror_msg("Failed to patch ACL: %s", strerror(errno));
                acl_free(&acl);
                return -errno;
        }

        if (acl_get_permset(entry, &permset) < 0 ||
            acl_add_perm(permset, ACL_READ) < 0 ||
            acl_add_perm(permset, ACL_WRITE) < 0 ||
            calc_acl_mask_if_needed(&acl) < 0) {
                perror_msg("Failed to patch ACL: %s", strerror(errno));
                acl_free(&acl);
                return -errno;
        }

        if (acl_set_fd(fd, acl) < 0) {
                acl_free(&acl);
                perror_msg("Failed to apply ACL: %s", strerror(errno));
                return -errno;
        }

        acl_free(&acl);
        return 0;
}
