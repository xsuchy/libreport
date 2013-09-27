# coding=UTF-8

## Copyright (C) 2015 ABRT team <abrt-devel-list@redhat.com>
## Copyright (C) 2015 Red Hat, Inc.

## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.

## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.

## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335  USA
import os
import subprocess

from reportclient import (_, RETURN_OK, RETURN_CANCEL_BY_USER, ask_yes_no)


class ZYppDebugInfoDownload(DebugInfoDownload):

    def __init__(self, cache, tmp, repo_pattern="*debug*", keep_rpms=False,
                 noninteractive=True):
        super(ZyppDebugInfoDownload, self).__init__(cache, tmp, repo_pattern, keep_rpms, noninteractive)


    def prepare(self):
        pass

    def initialize_progress(self, updater):
        pass

    def initialize_repositories(self):
        pass

    def triage(self, files):

        # jfilak: backup, for future needs
        # if not download_exact_files:
        #     ins_cmds = ('-C "debuginfo(build-id)={0}"'.format(bid)
        #                 for bid in files)
        # else:
        #    ins_cmds = files

        # insert empty line
        print("")

        print(_("Please run the following commands as root user to install"
                " all necessary debug info packages:"))

        for cmd in ins_cmds:
            print("zypper install {0}".format(cmd))

        # insert empty line
        print("")

        if ask_yes_no(_("See the log and install necessary debuginfo packages. "
                     "Once you are done answer 'Yes' or if you do not want to "
                     "download debuginfo answer 'No'")):
            # (packages to download {package name - passed to download_package
            #                        list of files to copy out of the package},
            #  files without providers,
            #  download size,
            #  install size)
            return (dict(), list(), 0, 0)

        os.exit(RETURN_CANCEL_BY_USER)

    def download_package(self, pkg):
        # (full path to rpm, error)
        return (None, "Not supported")

