/*
    Copyright (C) 2014  ABRT Team
    Copyright (C) 2014  RedHat inc.

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
#ifndef _LIB_REPORT_WINDOW_H
#define _LIB_REPORT_WINDOW_H

#include <gtk/gtk.h>
#include "problem_data.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

G_BEGIN_DECLS

#define TYPE_LIB_REPORT_WINDOW            (lib_report_window_get_type())
#define LIB_REPORT_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_LIB_REPORT_WINDOW, LibReportWindow))
#define LIB_REPORT_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_LIB_REPORT_WINDOW, LibReportWindowClass))
#define IS_LIB_REPORT_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_LIB_REPORT_WINDOW))
#define IS_LIB_REPORT_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_LIB_REPORT_WINDOW))
#define LIB_REPORT_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_LIB_REPORT_WINDOW, LibReportWindowClass))

typedef struct _LibReportWindow        LibReportWindow;
typedef struct _LibReportWindowClass   LibReportWindowClass;
typedef struct LibReportWindowPrivate  LibReportWindowPrivate;

struct _LibReportWindow {
   GtkApplicationWindow    parent_instance;
   LibReportWindowPrivate *priv;
};

struct _LibReportWindowClass {
   GtkApplicationWindowClass parent_class;
};

GType lib_report_window_get_type (void) G_GNUC_CONST;

LibReportWindow *lib_report_window_new_for_dir(GtkApplication *app, const char *dump_dir_name);

void lib_report_window_reload_problem_data(LibReportWindow *self);
void lib_report_window_set_expert_mode(LibReportWindow *self, gboolean expert_mode);
gboolean lib_report_window_get_expert_mode(LibReportWindow *self);

G_END_DECLS

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LIB_REPORT_WINDOW_H */

