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
#include "lib_report_window.h"
#include "lib_report_window_gresource.h"
#include "internal_libreport_gtk.h"
#include "search_item.h"
#define gtk_button_set_image(x, y) while (0)

#define LIB_REPORT_WINDOW_GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE((o), TYPE_LIB_REPORT_WINDOW, LibReportWindowPrivate))

#define EMERGENCY_ANALYSIS_EVENT_NAME "report_EmergencyAnalysis"
#define FORBIDDEN_WORDS_BLACKLLIST "forbidden_words.conf"
#define FORBIDDEN_WORDS_WHITELIST "ignored_words.conf"

#define PRIVATE_TICKET_CB "private_ticket_cb"
#define SENSITIVE_DATA_WARN "sensitive_data_warning"
#define SENSITIVE_LIST "ls_sensitive_words"

#define LIB_REPORT_WINDOW_UI_FILE_NAME "lib-report-window.glade"
#define DEFAULT_WIDTH   800
#define DEFAULT_HEIGHT  500

static PangoFontDescription *g_monospace_font;
static GdkCursor *g_hand_cursor;
static GdkCursor *g_regular_cursor;
/* TODO: */ static gboolean hovering_over_link = FALSE;

/* THE PAGE FLOW
 * page_0: introduction/summary
 * page_1: user comments
 * page_2: event selection
 * page_3: backtrace editor
 * page_4: summary
 * page_5: reporting progress
 * page_6: finished
 */
enum {
    PAGENO_SUMMARY,        // 0
    PAGENO_EVENT_SELECTOR, // 1
    PAGENO_EDIT_COMMENT,   // 2
    PAGENO_EDIT_ELEMENTS,  // 3
    PAGENO_REVIEW_DATA,    // 4
    PAGENO_EVENT_PROGRESS, // 5
    PAGENO_EVENT_DONE,     // 6
    PAGENO_NOT_SHOWN,      // 7
    NUM_PAGES              // 8
};

/* Use of arrays (instead of, say, #defines to C strings)
 * allows cheaper page_obj_t->name == PAGE_FOO comparisons
 * instead of strcmp.
 */
static const gchar PAGE_SUMMARY[]        = "page_0";
static const gchar PAGE_EVENT_SELECTOR[] = "page_1";
static const gchar PAGE_EDIT_COMMENT[]   = "page_2";
static const gchar PAGE_EDIT_ELEMENTS[]  = "page_3";
static const gchar PAGE_REVIEW_DATA[]    = "page_4";
static const gchar PAGE_EVENT_PROGRESS[] = "page_5";
static const gchar PAGE_EVENT_DONE[]     = "page_6";
static const gchar PAGE_NOT_SHOWN[]      = "page_7";

static const gchar *const page_names[] =
{
    PAGE_SUMMARY,
    PAGE_EVENT_SELECTOR,
    PAGE_EDIT_COMMENT,
    PAGE_EDIT_ELEMENTS,
    PAGE_REVIEW_DATA,
    PAGE_EVENT_PROGRESS,
    PAGE_EVENT_DONE,
    PAGE_NOT_SHOWN,
    NULL
};

typedef struct
{
    const gchar *name;
    const gchar *title;
    GtkWidget *page_widget;
    int page_no;
} page_obj_t;

typedef struct
{
    char *event_name;
    GtkToggleButton *toggle_button;
} event_gui_data_t;

struct cd_stats {
    LibReportWindow *window;
    off_t filesize;
    unsigned filecount;
};

enum
{
    DETAIL_COLUMN_CHECKBOX,
    DETAIL_COLUMN_NAME,
    DETAIL_COLUMN_VALUE,
    DETAIL_NUM_COLUMNS,
};

enum
{
    /*
     * the selected event is updated to a first event wich can be applied on
     * the current problem directory
     */
    UPDATE_SELECTED_EVENT = 1 << 0,
};

enum {
    TERMINATE_NOFLAGS    = 0,
    TERMINATE_WITH_RERUN = 1 << 0,
};

typedef struct {
    GtkBuilder *gtk_builder;

    page_obj_t pages[NUM_PAGES];

    GtkNotebook *assistant;
    GtkBox      *box_assistant;

    GtkWidget   *btn_stop;
    GtkWidget   *btn_close;
    GtkWidget   *btn_next;
    GtkWidget   *btn_onfail;
    GtkWidget   *btn_repeat;
    GtkWidget   *btn_detail;
    GtkBox      *box_warning_labels;
    GtkBox      *box_events;
    GtkBox      *box_workflows;
    GtkLabel    *lbl_event_log;
    GtkTextView *tv_event_log;
    GtkContainer *container_details1;
    GtkContainer *container_details2;
    GtkLabel    *lbl_cd_reason;
    GtkTextView *tv_comment;
    GtkEventBox *eb_comment;
    GtkCheckButton *cb_no_comment;
    GtkWidget   *widget_warnings_area;
    GtkToggleButton *tb_approve_bt;
    GtkButton   *btn_add_file;
    GtkLabel    *lbl_size;
    GtkTreeView *tv_details;
    GtkCellRenderer *tv_details_renderer_value;
    GtkTreeViewColumn *tv_details_col_checkbox;
    GtkListStore *ls_details;
    GtkBox       *box_buttons; //TODO: needs not be global
    GtkNotebook  *notebook;
    GtkListStore *ls_sensitive_list;
    GtkTreeView  *tv_sensitive_list;
    GtkTreeSelection *tv_sensitive_sel;
    GtkRadioButton *rb_forbidden_words;
    GtkRadioButton *rb_custom_search;
    GtkExpander  *exp_search;
    GtkSpinner   *spinner_event_log;
    GtkImage     *img_process_fail;
    GtkButton    *btn_startcast;
    GtkExpander  *exp_report_log;
    GtkWidget    *top_most_window;
    GtkToggleButton *tbtn_private_ticket;
    GtkSearchEntry *search_entry_bt;
} LibReportWindowPrivateBuilder;

struct LibReportWindowPrivate {
    LibReportWindowPrivateBuilder *builder;

    char *dump_dir_name;
    problem_data_t *problem_data;
    GList *auto_event_list;
    char *event_selected;
    pid_t event_child_pid;
    guint event_source_id;

    GHashTable *loaded_texts;
    bool expert_mode;

    gulong tv_sensitive_sel_hndlr;
    gboolean warning_issued;
    GList *list_events;

    guint timeout;
    const gchar *search_text;
    search_item_t *current_highlighted_word;
};

enum
{
    SEARCH_COLUMN_FILE,
    SEARCH_COLUMN_TEXT,
    SEARCH_COLUMN_ITEM,
};


G_DEFINE_TYPE(LibReportWindow, lib_report_window, GTK_TYPE_APPLICATION_WINDOW)

static void start_event_run(LibReportWindow *self, const char *event_name);
static void update_gui_state_from_problem_data(LibReportWindow *self, int flags);
static gboolean highlight_forbidden(LibReportWindow *self);
static void on_page_prepare(GtkNotebook *assistant, GtkWidget *page, guint page_no, gpointer user_data);
static gint select_next_page_no(LibReportWindow *self, gint current_page_no, gpointer data);
static void update_ls_details_checkboxes(LibReportWindow *self, const char *event_name);
static void clear_warnings(LibReportWindow *self);

static void wrap_fixer(GtkWidget *widget, gpointer data_unused)
{
    if (GTK_IS_CONTAINER(widget))
    {
        gtk_container_foreach((GtkContainer*)widget, wrap_fixer, NULL);
        return;
    }
    if (GTK_IS_LABEL(widget))
    {
        GtkLabel *label = (GtkLabel*)widget;
        //const char *txt = gtk_label_get_label(label);
#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 13) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 13 && GTK_MICRO_VERSION < 5))
        GtkMisc *misc = (GtkMisc*)widget;
        gfloat yalign; // = 111;
        gint ypad; // = 111;
        if (gtk_label_get_line_wrap(label)
         && (gtk_misc_get_alignment(misc, NULL, &yalign), yalign == 0)
         && (gtk_misc_get_padding(misc, NULL, &ypad), ypad == 0)
#else
        if (gtk_label_get_line_wrap(label)
         && (gtk_widget_get_halign(widget) == GTK_ALIGN_START)
         && (gtk_widget_get_margin_top(widget) == 0)
         && (gtk_widget_get_margin_bottom(widget) == 0)
#endif
        ) {
            //log("label '%s' set to autowrap", txt);
            make_label_autowrap_on_resize(label);
            return;
        }
        //log("label '%s' not set to autowrap %g %d", txt, yalign, ypad);
    }
}

static void fix_all_wrapped_labels(GtkWidget *widget)
{
    wrap_fixer(widget, NULL);
}

static void lib_report_window_builder_init_pages(LibReportWindowPrivateBuilder *builder)
{
#define INIT_PAGE(builder, page_no, page_title) \
    builder->pages[page_no].name = page_names[page_no]; \
    builder->pages[page_no].title = page_title;

    INIT_PAGE(builder, PAGENO_SUMMARY        , _("Problem description"))
    INIT_PAGE(builder, PAGENO_EVENT_SELECTOR , _("Select how to report this problem"))
    INIT_PAGE(builder, PAGENO_EDIT_COMMENT   , _("Provide additional information"))
    INIT_PAGE(builder, PAGENO_EDIT_ELEMENTS  , _("Review the data"))
    INIT_PAGE(builder, PAGENO_REVIEW_DATA    , _("Confirm data to report"))
    INIT_PAGE(builder, PAGENO_EVENT_PROGRESS , _("Processing"))
    INIT_PAGE(builder, PAGENO_EVENT_DONE     , _("Processing done"))
//do we still need this?
    INIT_PAGE(builder, PAGENO_NOT_SHOWN      , "")
}

static GtkBuilder *
make_gtk_builder()
{
    GError *error = NULL;
    GtkBuilder *gtk_builder = gtk_builder_new();
    gtk_builder_set_translation_domain(gtk_builder, GETTEXT_PACKAGE);

    //gtk_builder_add_from_file(gtk_builder, LIBREPORT_GTK_UI_DIR "/" LIB_REPORT_WINDOW_UI_FILE_NAME, &error);
    gtk_builder_add_from_resource(gtk_builder, "/org/freedesktop/libreport-gtk/lib-report-window.glade", &error);
    if(error != NULL)
    {
        g_warning("Failed to load '%s': %s", "/org/freedesktop/libreport-gtk/lib-report-window.glade", error->message);
        g_error_free(error);
        error = NULL;

        gtk_builder_add_from_file(gtk_builder, LIB_REPORT_WINDOW_UI_FILE_NAME, &error);
        if(error != NULL)
        {
            g_warning("Failed to load '%s': %s", LIB_REPORT_WINDOW_UI_FILE_NAME, error->message);
            g_error_free(error);
            g_object_unref(gtk_builder);
            return NULL;
        }
    }

    return gtk_builder;
}

static LibReportWindowPrivateBuilder *
lib_report_window_builder_new()
{
    LibReportWindowPrivateBuilder *inst = xmalloc(sizeof(*inst));

    lib_report_window_builder_init_pages(inst);

    inst->gtk_builder = make_gtk_builder();
    if (inst->gtk_builder == NULL)
        return NULL;

    inst->assistant = GTK_NOTEBOOK(gtk_notebook_new());

    inst->btn_close = gtk_button_new_with_mnemonic(_("_Close"));
    gtk_button_set_image(GTK_BUTTON(inst->btn_close),
            gtk_image_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON));

    inst->btn_stop = gtk_button_new_with_mnemonic(_("_Stop"));
    gtk_button_set_image(GTK_BUTTON(inst->btn_stop),
            gtk_image_new_from_icon_name("process-close-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_no_show_all(inst->btn_stop, true); /* else gtk_widget_hide won't work */

    inst->btn_onfail = gtk_button_new_with_label(_("Upload for analysis"));
    gtk_button_set_image(GTK_BUTTON(inst->btn_onfail),
            gtk_image_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_no_show_all(inst->btn_onfail, true); /* else gtk_widget_hide won't work */

    inst->btn_repeat = gtk_button_new_with_label(_("Repeat"));
    gtk_widget_set_no_show_all(inst->btn_repeat, true); /* else gtk_widget_hide won't work */

    inst->btn_next = gtk_button_new_with_mnemonic(_("_Forward"));
    gtk_button_set_image(GTK_BUTTON(inst->btn_next),
            gtk_image_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_no_show_all(inst->btn_next, true); /* else gtk_widget_hide won't work */

    inst->btn_detail = gtk_button_new_with_mnemonic(_("Details"));
    gtk_widget_set_no_show_all(inst->btn_detail, true); /* else gtk_widget_hide won't work */

    inst->box_buttons = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));

    gtk_box_pack_start(inst->box_buttons, inst->btn_close, false, false, 5);
    gtk_box_pack_start(inst->box_buttons, inst->btn_stop, false, false, 5);
    gtk_box_pack_start(inst->box_buttons, inst->btn_onfail, false, false, 5);
    gtk_box_pack_start(inst->box_buttons, inst->btn_repeat, false, false, 5);
    /* Btns above are to the left, the rest are to the right: */
#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 13) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 13 && GTK_MICRO_VERSION < 5))
    GtkWidget *w = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
    gtk_box_pack_start(inst->box_buttons, w, true, true, 5);
    gtk_box_pack_start(inst->box_buttons, inst->btn_detail, false, false, 5);
    gtk_box_pack_start(inst->box_buttons, inst->btn_next, false, false, 5);
#else
    gtk_widget_set_valign(GTK_WIDGET(inst->btn_next), GTK_ALIGN_END);
    gtk_box_pack_end(inst->box_buttons, inst->btn_next, false, false, 5);
    gtk_box_pack_end(inst->box_buttons, inst->btn_detail, false, false, 5);
#endif

    {   /* Warnings area widget definition start */
        inst->box_warning_labels = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
        gtk_widget_set_visible(GTK_WIDGET(inst->box_warning_labels), TRUE);

        GtkBox *vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
        gtk_widget_set_visible(GTK_WIDGET(vbox), TRUE);
        gtk_box_pack_start(vbox, GTK_WIDGET(inst->box_warning_labels), false, false, 5);

        GtkWidget *image = gtk_image_new_from_icon_name("dialog-warning-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_widget_set_visible(image, TRUE);

        inst->widget_warnings_area = GTK_WIDGET(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
        gtk_widget_set_visible(inst->widget_warnings_area, FALSE);
        gtk_widget_set_no_show_all(inst->widget_warnings_area, TRUE);

#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 13) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 13 && GTK_MICRO_VERSION < 5))
        GtkWidget *alignment_left = gtk_alignment_new(0.5,0.5,1,1);
        gtk_widget_set_visible(alignment_left, TRUE);
        gtk_box_pack_start(GTK_BOX(inst->widget_warnings_area), alignment_left, true, false, 0);
#else
        gtk_widget_set_valign(GTK_WIDGET(image), GTK_ALIGN_CENTER);
        gtk_widget_set_valign(GTK_WIDGET(vbox), GTK_ALIGN_CENTER);
#endif

        gtk_box_pack_start(GTK_BOX(inst->widget_warnings_area), image, false, false, 5);
        gtk_box_pack_start(GTK_BOX(inst->widget_warnings_area), GTK_WIDGET(vbox), false, false, 0);

#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 13) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 13 && GTK_MICRO_VERSION < 5))
        GtkWidget *alignment_right = gtk_alignment_new(0.5,0.5,1,1);
        gtk_widget_set_visible(alignment_right, TRUE);
        gtk_box_pack_start(GTK_BOX(inst->widget_warnings_area), alignment_right, true, false, 0);
#endif
    }   /* Warnings area widget definition end */

    inst->box_assistant = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_box_pack_start(inst->box_assistant, GTK_WIDGET(inst->assistant), true, true, 0);

    gtk_box_pack_start(inst->box_assistant, GTK_WIDGET(inst->widget_warnings_area), false, false, 0);
    gtk_box_pack_start(inst->box_assistant, GTK_WIDGET(inst->box_buttons), false, false, 5);

    gtk_widget_show_all(GTK_WIDGET(inst->box_buttons));
    gtk_widget_hide(inst->btn_stop);
    gtk_widget_hide(inst->btn_onfail);
    gtk_widget_hide(inst->btn_repeat);
    gtk_widget_show(inst->btn_next);

    for (int i = 0; page_names[i] != NULL; i++)
    {
        GtkWidget *page = GTK_WIDGET(gtk_builder_get_object(inst->gtk_builder, page_names[i]));
        inst->pages[i].page_widget = page;
        inst->pages[i].page_no = i;

        GtkWidget *page_parent = gtk_widget_get_parent(page);
        if (page_parent)
            gtk_container_remove(GTK_CONTAINER(page_parent), page);

        gtk_notebook_append_page(inst->assistant, page, gtk_label_new(inst->pages[i].title));
        log_notice("added page: %s", page_names[i]);
    }

    /* Set pointers to objects we might need to work with */
    inst->lbl_cd_reason        = GTK_LABEL(        gtk_builder_get_object(inst->gtk_builder, "lbl_cd_reason"));
    inst->box_events           = GTK_BOX(          gtk_builder_get_object(inst->gtk_builder, "vb_events"));
    inst->box_workflows        = GTK_BOX(          gtk_builder_get_object(inst->gtk_builder, "vb_workflows"));
    inst->lbl_event_log        = GTK_LABEL(        gtk_builder_get_object(inst->gtk_builder, "lbl_event_log"));
    inst->tv_event_log         = GTK_TEXT_VIEW(    gtk_builder_get_object(inst->gtk_builder, "tv_event_log"));
    inst->tv_comment           = GTK_TEXT_VIEW(    gtk_builder_get_object(inst->gtk_builder, "tv_comment"));
    inst->eb_comment           = GTK_EVENT_BOX(    gtk_builder_get_object(inst->gtk_builder, "eb_comment"));
    inst->cb_no_comment        = GTK_CHECK_BUTTON( gtk_builder_get_object(inst->gtk_builder, "cb_no_comment"));
    inst->tv_details           = GTK_TREE_VIEW(    gtk_builder_get_object(inst->gtk_builder, "tv_details"));
    inst->tb_approve_bt        = GTK_TOGGLE_BUTTON(gtk_builder_get_object(inst->gtk_builder, "cb_approve_bt"));
    inst->search_entry_bt      = GTK_SEARCH_ENTRY( gtk_builder_get_object(inst->gtk_builder, "entry_search_bt"));
    inst->container_details1   = GTK_CONTAINER(    gtk_builder_get_object(inst->gtk_builder, "container_details1"));
    inst->container_details2   = GTK_CONTAINER(    gtk_builder_get_object(inst->gtk_builder, "container_details2"));
    inst->btn_add_file         = GTK_BUTTON(       gtk_builder_get_object(inst->gtk_builder, "btn_add_file"));
    inst->lbl_size             = GTK_LABEL(        gtk_builder_get_object(inst->gtk_builder, "lbl_size"));
    inst->notebook             = GTK_NOTEBOOK(     gtk_builder_get_object(inst->gtk_builder, "notebook_edit"));
    inst->ls_sensitive_list    = GTK_LIST_STORE(   gtk_builder_get_object(inst->gtk_builder, "ls_sensitive_words"));
    inst->tv_sensitive_list    = GTK_TREE_VIEW(    gtk_builder_get_object(inst->gtk_builder, "tv_sensitive_words"));
    inst->tv_sensitive_sel     = GTK_TREE_SELECTION( gtk_builder_get_object(inst->gtk_builder, "tv_sensitive_words_selection"));
    inst->rb_forbidden_words   = GTK_RADIO_BUTTON( gtk_builder_get_object(inst->gtk_builder, "rb_forbidden_words"));
    inst->rb_custom_search     = GTK_RADIO_BUTTON( gtk_builder_get_object(inst->gtk_builder, "rb_custom_search"));
    inst->exp_search           = GTK_EXPANDER(     gtk_builder_get_object(inst->gtk_builder, "expander_search"));
    inst->spinner_event_log    = GTK_SPINNER(      gtk_builder_get_object(inst->gtk_builder, "spinner_event_log"));
    inst->img_process_fail     = GTK_IMAGE(      gtk_builder_get_object(inst->gtk_builder, "img_process_fail"));
    inst->btn_startcast        = GTK_BUTTON(    gtk_builder_get_object(inst->gtk_builder, "btn_startcast"));
    inst->exp_report_log       = GTK_EXPANDER(     gtk_builder_get_object(inst->gtk_builder, "expand_report"));
    inst->tbtn_private_ticket  = GTK_TOGGLE_BUTTON(gtk_builder_get_object(inst->gtk_builder, "private_ticket_cb"));

    gtk_widget_set_no_show_all(GTK_WIDGET(inst->spinner_event_log), true);

    fix_all_wrapped_labels(GTK_WIDGET(inst->assistant));

    gtk_widget_override_font(GTK_WIDGET(inst->tv_event_log), g_monospace_font);

    /* Set color of the comment evenbox */
    GdkRGBA color;
    gdk_rgba_parse(&color, "#CC3333");
    gtk_widget_override_color(GTK_WIDGET(inst->eb_comment), GTK_STATE_FLAG_NORMAL, &color);

    return inst;
}

static void update_window_title(LibReportWindow *self)
{
    /* prgname can be null according to gtk documentation */
    const char *prgname = g_get_prgname();
    const char *reason = problem_data_get_content_or_NULL(self->priv->problem_data, FILENAME_REASON);
    char *title = xasprintf("%s - %s", (reason ? reason : self->priv->dump_dir_name),
            (prgname ? prgname : "report"));
    gtk_window_set_title(GTK_WINDOW(self), title);
    free(title);
}

static bool ask_continue_before_steal(const char *base_dir, const char *dump_dir, LibReportWindow *self)
{
    char *msg = xasprintf(_("Need writable directory, but '%s' is not writable."
                            " Move it to '%s' and operate on the moved data?"),
                            dump_dir, base_dir);
    const bool response = run_ask_yes_no_yesforever_dialog("ask_steal_dir", msg, GTK_WINDOW(self));
    free(msg);
    return response;
}

struct dump_dir *wizard_open_directory_for_writing(LibReportWindow *self)
{
    struct dump_dir *dd = open_directory_for_writing_ext(self->priv->dump_dir_name,
                     (bool (*)(const char *, const char *, void *))ask_continue_before_steal, self);

    if (dd && strcmp(self->priv->dump_dir_name, dd->dd_dirname) != 0)
    {
        char *old_name = self->priv->dump_dir_name;
        self->priv->dump_dir_name = xstrdup(dd->dd_dirname);
        update_window_title(self);
        free(old_name);
    }

    return dd;
}

static struct problem_item *get_current_problem_item_or_NULL(LibReportWindow *self, GtkTreeView *tree_view, gchar **pp_item_name)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(tree_view);

    if (selection == NULL)
        return NULL;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return NULL;

    *pp_item_name = NULL;
    gtk_tree_model_get(model, &iter,
                DETAIL_COLUMN_NAME, pp_item_name,
                -1);
    if (!*pp_item_name) /* paranoia, should never happen */
        return NULL;
    struct problem_item *item = problem_data_get_item_or_NULL(self->priv->problem_data, *pp_item_name);
    return item;
}

static void load_text_to_text_view(LibReportWindow *self, GtkTextView *tv, const char *name)
{
    /* Add to set of loaded files */
    /* a key_destroy_func() is provided therefore if the key for name already exists */
    /* a result of xstrdup() is freed */
    g_hash_table_insert(self->priv->loaded_texts, (gpointer)xstrdup(name), (gpointer)1);

    const char *str = self->priv->problem_data ? problem_data_get_content_or_NULL(self->priv->problem_data, name) : NULL;
    /* Bad: will choke at any text with non-Unicode parts: */
    /* gtk_text_buffer_set_text(tb, (str ? str : ""), -1);*/
    /* Start torturing ourself instead: */

    reload_text_to_text_view(tv, str);
}

static gchar *get_malloced_string_from_text_view(GtkTextView *tv)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(tv);
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void save_text_if_changed(LibReportWindow *self, const char *name, const char *new_value)
{
    /* a text value can't be change if the file is not loaded */
    /* returns NULL if the name is not found; otherwise nonzero */
    if (!g_hash_table_lookup(self->priv->loaded_texts, name))
        return;

    const char *old_value = self->priv->problem_data ? problem_data_get_content_or_NULL(self->priv->problem_data, name) : "";
    if (!old_value)
        old_value = "";
    if (strcmp(new_value, old_value) != 0)
    {
        struct dump_dir *dd = wizard_open_directory_for_writing(self);
        if (dd)
            dd_save_text(dd, name, new_value);

//FIXME: else: what to do with still-unsaved data in the widget??
        dd_close(dd);
        lib_report_window_reload_problem_data(self);
        update_gui_state_from_problem_data(self, /* don't update selected event */ 0);
    }
}

static void save_text_from_text_view(LibReportWindow *self, GtkTextView *tv, const char *name)
{
    gchar *new_str = get_malloced_string_from_text_view(tv);
    save_text_if_changed(self, name, new_str);
    free(new_str);
}

static void on_tv_details_row_activated(
                        GtkTreeView       *tree_view,
                        GtkTreePath       *tree_path_UNUSED,
                        GtkTreeViewColumn *column,
                        gpointer           user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);

    gchar *item_name;
    struct problem_item *item = get_current_problem_item_or_NULL(self, tree_view, &item_name);
    if (!item || !(item->flags & CD_FLAG_TXT))
        goto ret;
    if (!strchr(item->content, '\n')) /* one line? */
        goto ret; /* yes */

    gint exitcode;
    gchar *arg[3];
    arg[0] = (char *) "xdg-open";
    arg[1] = concat_path_file(self->priv->dump_dir_name, item_name);
    arg[2] = NULL;

    const gboolean spawn_ret = g_spawn_sync(NULL, arg, NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                                 NULL, NULL, NULL, NULL, &exitcode, NULL);

    if (spawn_ret == FALSE || exitcode != EXIT_SUCCESS)
    {
        GtkWidget *dialog = gtk_dialog_new_with_buttons(_("View/edit a text file"),
            GTK_WINDOW(self),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            NULL, NULL);
        GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
        GtkWidget *textview = gtk_text_view_new();

        gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Save"), GTK_RESPONSE_OK);
        gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);

        gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
        gtk_widget_set_size_request(scrolled, 640, 480);
        gtk_widget_show(scrolled);

#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 7) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 7 && GTK_MICRO_VERSION < 8))
        /* http://developer.gnome.org/gtk3/unstable/GtkScrolledWindow.html#gtk-scrolled-window-add-with-viewport */
        /* gtk_scrolled_window_add_with_viewport has been deprecated since version 3.8 and should not be used in newly-written code. */
        gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled), textview);
#else
        /* gtk_container_add() will now automatically add a GtkViewport if the child doesn't implement GtkScrollable. */
        gtk_container_add(GTK_CONTAINER(scrolled), textview);
#endif

        gtk_widget_show(textview);

        load_text_to_text_view(self, GTK_TEXT_VIEW(textview), item_name);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
            save_text_from_text_view(self, GTK_TEXT_VIEW(textview), item_name);

        gtk_widget_destroy(textview);
        gtk_widget_destroy(scrolled);
        gtk_widget_destroy(dialog);
    }

    free(arg[1]);
 ret:
    g_free(item_name);
}

/* static gboolean tv_details_select_cursor_row(
                        GtkTreeView *tree_view,
                        gboolean arg1,
                        gpointer user_data) {...} */

static void on_tv_details_cursor_changed(
                        GtkTreeView *tree_view,
                        gpointer     user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);

    /* I see this being called on window "destroy" signal when the tree_view is
       not a tree view anymore (or destroyed?) causing this error msg:
       (abrt:12804): Gtk-CRITICAL **: gtk_tree_selection_get_selected: assertion `GTK_IS_TREE_SELECTION (selection)' failed
       (abrt:12804): GLib-GObject-WARNING **: invalid uninstantiatable type `(null)' in cast to `GObject'
       (abrt:12804): GLib-GObject-CRITICAL **: g_object_set: assertion `G_IS_OBJECT (object)' failed
    */
    if (!GTK_IS_TREE_VIEW(tree_view))
        return;

    gchar *item_name = NULL;
    struct problem_item *item = get_current_problem_item_or_NULL(self, tree_view, &item_name);
    g_free(item_name);

    /* happens when closing the wizard by clicking 'X' */
    if (!item)
        return;

    gboolean editable = (item
                /* With this, copying of non-editable fields are more difficult */
                //&& (item->flags & CD_FLAG_ISEDITABLE)
                && (item->flags & CD_FLAG_TXT)
                && !strchr(item->content, '\n')
    );

    /* Allow user to select the text with mouse.
     * Has undesirable side-effect of allowing user to "edit" the text,
     * but changes aren't saved (the old text reappears as soon as user
     * leaves the field). Need to disable editing somehow.
     */
    g_object_set(G_OBJECT(self->priv->builder->tv_details_renderer_value),
                "editable", editable,
                NULL);
}

static void on_tv_details_checkbox_toggled(
                        GtkCellRendererToggle *cell_renderer_UNUSED,
                        gchar    *tree_path,
                        gpointer  user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);

    //log("%s: path:'%s'", __func__, tree_path);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(self->priv->builder->ls_details), &iter, tree_path))
        return;

    gchar *item_name = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(self->priv->builder->ls_details), &iter,
                DETAIL_COLUMN_NAME, &item_name,
                -1);
    if (!item_name) /* paranoia, should never happen */
        return;
    struct problem_item *item = problem_data_get_item_or_NULL(self->priv->problem_data, item_name);
    g_free(item_name);
    if (!item) /* paranoia */
        return;

    int cur_value;
    if (item->selected_by_user == 0)
        cur_value = item->default_by_reporter;
    else
        cur_value = !!(item->selected_by_user + 1); /* map -1,1 to 0,1 */
    //log("%s: allowed:%d reqd:%d def:%d user:%d cur:%d", __func__,
    //            item->allowed_by_reporter,
    //            item->required_by_reporter,
    //            item->default_by_reporter,
    //            item->selected_by_user,
    //            cur_value
    //);
    if (item->allowed_by_reporter && !item->required_by_reporter)
    {
        cur_value = !cur_value;
        item->selected_by_user = cur_value * 2 - 1; /* map 0,1 to -1,1 */
        //log("%s: now ->selected_by_user=%d", __func__, item->selected_by_user);
        gtk_list_store_set(self->priv->builder->ls_details, &iter,
                DETAIL_COLUMN_CHECKBOX, cur_value,
                -1);
    }
}

static void save_edited_one_liner(GtkCellRendererText *renderer,
                gchar *tree_path,
                gchar *new_text,
                gpointer user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);
    //log("path:'%s' new_text:'%s'", tree_path, new_text);

    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(self->priv->builder->ls_details), &iter, tree_path))
        return;
    gchar *item_name = NULL;
    gtk_tree_model_get(GTK_TREE_MODEL(self->priv->builder->ls_details), &iter,
                DETAIL_COLUMN_NAME, &item_name,
                -1);
    if (!item_name) /* paranoia, should never happen */
        return;
    struct problem_item *item = problem_data_get_item_or_NULL(self->priv->problem_data, item_name);
    if (item && (item->flags & CD_FLAG_ISEDITABLE))
    {
        struct dump_dir *dd = wizard_open_directory_for_writing(self);
        if (dd)
        {
            dd_save_text(dd, item_name, new_text);
            free(item->content);
            item->content = xstrdup(new_text);
            gtk_list_store_set(self->priv->builder->ls_details, &iter,
                    DETAIL_COLUMN_VALUE, new_text,
                    -1);
        }
        dd_close(dd);
    }
}

static void append_to_textview(GtkTextView *tv, const char *str)
{
    GtkTextBuffer *tb = gtk_text_view_get_buffer(tv);

    /* Ensure we insert text at the end */
    GtkTextIter text_iter;
    gtk_text_buffer_get_end_iter(tb, &text_iter);
    gtk_text_buffer_place_cursor(tb, &text_iter);

    /* Deal with possible broken Unicode */
    const gchar *end;
    while (!g_utf8_validate(str, -1, &end))
    {
        gtk_text_buffer_insert_at_cursor(tb, str, end - str);
        char buf[8];
        unsigned len = snprintf(buf, sizeof(buf), "<%02X>", (unsigned char)*end);
        gtk_text_buffer_insert_at_cursor(tb, buf, len);
        str = end + 1;
    }

    gtk_text_buffer_get_end_iter(tb, &text_iter);

    const char *last = str;
    GList *urls = find_url_tokens(str);
    for (GList *u = urls; u; u = g_list_next(u))
    {
        const struct url_token *const t = (struct url_token *)u->data;
        if (last < t->start)
            gtk_text_buffer_insert(tb, &text_iter, last, t->start - last);

        GtkTextTag *tag;
        tag = gtk_text_buffer_create_tag (tb, NULL, "foreground", "blue",
                                          "underline", PANGO_UNDERLINE_SINGLE, NULL);
        char *url = xstrndup(t->start, t->len);
        g_object_set_data (G_OBJECT (tag), "url", url);

        gtk_text_buffer_insert_with_tags(tb, &text_iter, url, -1, tag, NULL);

        last = t->start + t->len;
    }

    g_list_free_full(urls, g_free);

    if (last[0] != '\0')
        gtk_text_buffer_insert(tb, &text_iter, last, strlen(last));

    /* Scroll so that the end of the log is visible */
    gtk_text_view_scroll_to_iter(tv, &text_iter,
                /*within_margin:*/ 0.0, /*use_align:*/ FALSE, /*xalign:*/ 0, /*yalign:*/ 0);
}

static void save_items_from_notepad(LibReportWindow *self)
{
    gint n_pages = gtk_notebook_get_n_pages(self->priv->builder->notebook);
    int i = 0;

    GtkWidget *notebook_child;
    GtkTextView *tev;
    GtkWidget *tab_lbl;
    const char *item_name;

    for (i = 0; i < n_pages; i++)
    {
        //notebook_page->scrolled_window->text_view
        notebook_child = gtk_notebook_get_nth_page(self->priv->builder->notebook, i);
        tev = GTK_TEXT_VIEW(gtk_bin_get_child(GTK_BIN(notebook_child)));
        tab_lbl = gtk_notebook_get_tab_label(self->priv->builder->notebook, notebook_child);
        item_name = gtk_label_get_text(GTK_LABEL(tab_lbl));
        log_notice("saving: '%s'", item_name);

        save_text_from_text_view(self, tev, item_name);
    }
}

static void remove_tabs_from_notebook(LibReportWindow *self)
{
    GtkNotebook *notebook = self->priv->builder->notebook;
    gint n_pages = gtk_notebook_get_n_pages(notebook);
    int ii;

    for (ii = 0; ii < n_pages; ii++)
    {
        /* removing a page changes the indices, so we always need to remove
         * page 0
        */
        gtk_notebook_remove_page(notebook, 0); //we need to always the page 0
    }

    /* Turn off the changed callback during the update */
    g_signal_handler_block(self->priv->builder->tv_sensitive_sel, self->priv->tv_sensitive_sel_hndlr);

    self->priv->current_highlighted_word = NULL;

    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(self->priv->builder->ls_sensitive_list), &iter);
    while (valid)
    {
        char *text = NULL;
        search_item_t *word = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(self->priv->builder->ls_sensitive_list), &iter,
                SEARCH_COLUMN_TEXT, &text,
                SEARCH_COLUMN_ITEM, &word,
                -1);

        free(text);
        free(word);

        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(self->priv->builder->ls_sensitive_list), &iter);
    }
    gtk_list_store_clear(self->priv->builder->ls_sensitive_list);
    g_signal_handler_unblock(self->priv->builder->tv_sensitive_sel, self->priv->tv_sensitive_sel_hndlr);
}

static void append_item_to_ls_details(gpointer name, gpointer value, gpointer data)
{
    problem_item *item = (problem_item*)value;
    struct cd_stats *stats = data;
    GtkTreeIter iter;

    gtk_list_store_append(stats->window->priv->builder->ls_details, &iter);
    stats->filecount++;

    //FIXME: use the human-readable problem_item_format(item) instead of item->content.
    if (item->flags & CD_FLAG_TXT)
    {
        if (item->flags & CD_FLAG_ISEDITABLE)
        {
            GtkWidget *tab_lbl = gtk_label_new((char *)name);
            GtkWidget *tev = gtk_text_view_new();

            if (strcmp(name, FILENAME_COMMENT) == 0 || strcmp(name, FILENAME_REASON) == 0)
                gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tev), GTK_WRAP_WORD);

            gtk_widget_override_font(GTK_WIDGET(tev), g_monospace_font);
            load_text_to_text_view(stats->window, GTK_TEXT_VIEW(tev), (char *)name);
            /* init searching */
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tev));
            /* found items background */
            gtk_text_buffer_create_tag(buf, "search_result_bg", "background", "red", NULL);
            gtk_text_buffer_create_tag(buf, "current_result_bg", "background", "green", NULL);
            GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
            gtk_container_add(GTK_CONTAINER(sw), tev);
            gtk_notebook_append_page(stats->window->priv->builder->notebook, sw, tab_lbl);
        }
        stats->filesize += strlen(item->content);
        /* If not multiline... */
        if (!strchr(item->content, '\n'))
        {
            gtk_list_store_set(stats->window->priv->builder->ls_details, &iter,
                              DETAIL_COLUMN_NAME, (char *)name,
                              DETAIL_COLUMN_VALUE, item->content,
                              -1);
        }
        else
        {
            gtk_list_store_set(stats->window->priv->builder->ls_details, &iter,
                              DETAIL_COLUMN_NAME, (char *)name,
                              DETAIL_COLUMN_VALUE, _("(click here to view/edit)"),
                              -1);
        }
    }
    else if (item->flags & CD_FLAG_BIN)
    {
        struct stat statbuf;
        statbuf.st_size = 0;
        if (stat(item->content, &statbuf) == 0)
        {
            stats->filesize += statbuf.st_size;
            char *msg = xasprintf(_("(binary file, %llu bytes)"), (long long)statbuf.st_size);
            gtk_list_store_set(stats->window->priv->builder->ls_details, &iter,
                                  DETAIL_COLUMN_NAME, (char *)name,
                                  DETAIL_COLUMN_VALUE, msg,
                                  -1);
            free(msg);
        }
    }

    int cur_value;
    if (item->selected_by_user == 0)
        cur_value = item->default_by_reporter;
    else
        cur_value = !!(item->selected_by_user + 1); /* map -1,1 to 0,1 */

    gtk_list_store_set(stats->window->priv->builder->ls_details, &iter,
            DETAIL_COLUMN_CHECKBOX, cur_value,
            -1);
}

static void remove_child_widget(GtkWidget *widget, gpointer unused)
{
    /* Destroy will safely remove it and free the memory
     * if there are no refs left
     */
    gtk_widget_destroy(widget);
}

static void add_workflow_buttons(LibReportWindow *self, GCallback func)
{
    gtk_container_foreach(GTK_CONTAINER(self->priv->builder->box_workflows), &remove_child_widget, NULL);

    GHashTable *workflow_table = load_workflow_config_data_from_list(
                        list_possible_events_glist(self->priv->dump_dir_name, "workflow"),
                        WORKFLOWS_DIR);

    GList *wf_list = g_hash_table_get_values(workflow_table);
    wf_list = g_list_sort(wf_list, (GCompareFunc)wf_priority_compare);

    for (GList *wf_iter = wf_list; wf_iter; wf_iter = g_list_next(wf_iter))
    {
        workflow_t *w = (workflow_t *)wf_iter->data;
        char *btn_label = xasprintf("<b>%s</b>\n%s", wf_get_screen_name(w), wf_get_description(w));
        GtkWidget *button = gtk_button_new_with_label(btn_label);
        GList *children = gtk_container_get_children(GTK_CONTAINER(button));
        GtkWidget *label = (GtkWidget *)children->data;
        gtk_label_set_use_markup(GTK_LABEL(label), true);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_top(label, 10);
#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 11) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 11 && GTK_MICRO_VERSION < 2))
        gtk_widget_set_margin_left(label, 40);
#else
        gtk_widget_set_margin_start(label, 40);
#endif
        gtk_widget_set_margin_bottom(label, 10);
        free(btn_label);
        g_object_set_data(G_OBJECT(button), "workflow", w);
        g_signal_connect(button, "clicked", func, self);
        gtk_box_pack_start(self->priv->builder->box_workflows, button, true, false, 2);
    }

    g_list_free(wf_list);
}

static void hide_next_step_button(LibReportWindow *self)
{
    /* replace 'Forward' with 'Close' button */
    /* 1. hide next button */
    gtk_widget_hide(self->priv->builder->btn_next);
    /* 2. move close button to the last position */
    gtk_box_set_child_packing(self->priv->builder->box_buttons,
            self->priv->builder->btn_close, false, false, 5, GTK_PACK_END);
}

static void show_next_step_button(LibReportWindow *self)
{
    gtk_box_set_child_packing(self->priv->builder->box_buttons,
            self->priv->builder->btn_close, false, false, 5, GTK_PACK_START);

    gtk_widget_show(self->priv->builder->btn_next);
}

static void terminate_event_chain(LibReportWindow *self, int flags)
{
    if (self->priv->expert_mode)
        return;

    hide_next_step_button(self);
    if ((flags & TERMINATE_WITH_RERUN))
        return;

    free(self->priv->event_selected);
    self->priv->event_selected = NULL;

    g_list_free_full(self->priv->auto_event_list, free);
    self->priv->auto_event_list = NULL;
}

static void cancel_processing(LibReportWindow *self, const char *message, int terminate_flags)
{
    gtk_label_set_text(self->priv->builder->lbl_event_log, message ? message : _("Processing was canceled"));
    terminate_event_chain(self, terminate_flags);
}

struct analyze_event_data
{
    LibReportWindow *window;
    struct run_event_state *run_state;
    char *event_name;
    GList *env_list;
    GIOChannel *channel;
    struct strbuf *event_log;
    int event_log_state;
    int fd;
    /*guint event_source_id;*/
};
enum {
    LOGSTATE_FIRSTLINE = 0,
    LOGSTATE_BEGLINE,
    LOGSTATE_ERRLINE,
    LOGSTATE_MIDLINE,
};

static void set_excluded_envvar(GtkListStore *ls_details)
{
    struct strbuf *item_list = strbuf_new();
    const char *fmt = "%s";

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(ls_details), &iter))
    {
        do {
            gchar *item_name = NULL;
            gboolean checked = 0;
            gtk_tree_model_get(GTK_TREE_MODEL(ls_details), &iter,
                    DETAIL_COLUMN_NAME, &item_name,
                    DETAIL_COLUMN_CHECKBOX, &checked,
                    -1);
            if (!item_name) /* paranoia, should never happen */
                continue;
            if (!checked)
            {
                strbuf_append_strf(item_list, fmt, item_name);
                fmt = ",%s";
            }
            g_free(item_name);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(ls_details), &iter));
    }
    char *var = strbuf_free_nobuf(item_list);
    //log("EXCLUDE_FROM_REPORT='%s'", var);
    if (var)
    {
        xsetenv("EXCLUDE_FROM_REPORT", var);
        free(var);
    }
    else
        unsetenv("EXCLUDE_FROM_REPORT");
}

static int spawn_next_command_in_evd(struct analyze_event_data *evd)
{
    evd->env_list = export_event_config(evd->event_name);
    int r = spawn_next_command(evd->run_state, evd->window->priv->dump_dir_name, evd->event_name, EXECFLG_SETPGID);
    if (r >= 0)
    {
        evd->window->priv->event_child_pid = evd->run_state->command_pid;
    }
    else
    {
        unexport_event_config(evd->env_list);
        evd->env_list = NULL;
    }
    return r;
}

static void save_to_event_log(struct analyze_event_data *evd, const char *str)
{
    static const char delim[] = {
        [LOGSTATE_FIRSTLINE] = '>',
        [LOGSTATE_BEGLINE] = ' ',
        [LOGSTATE_ERRLINE] = '*',
    };

    while (str[0])
    {
        char *end = strchrnul(str, '\n');
        char end_char = *end;
        if (end_char == '\n')
            end++;
        switch (evd->event_log_state)
        {
            case LOGSTATE_FIRSTLINE:
            case LOGSTATE_BEGLINE:
            case LOGSTATE_ERRLINE:
                /* skip empty lines */
                if (str[0] == '\n')
                    goto next;
                strbuf_append_strf(evd->event_log, "%s%c %.*s",
                        iso_date_string(NULL),
                        delim[evd->event_log_state],
                        (int)(end - str), str
                );
                break;
            case LOGSTATE_MIDLINE:
                strbuf_append_strf(evd->event_log, "%.*s", (int)(end - str), str);
                break;
        }
        evd->event_log_state = LOGSTATE_MIDLINE;
        if (end_char != '\n')
            break;
        evd->event_log_state = LOGSTATE_BEGLINE;
 next:
        str = end;
    }
}

static void update_event_log_on_disk(const char *dump_dir_name, const char *str)
{
    /* Load existing log */
    struct dump_dir *dd = dd_opendir(dump_dir_name, 0);
    if (!dd)
        return;
    char *event_log = dd_load_text_ext(dd, FILENAME_EVENT_LOG, DD_FAIL_QUIETLY_ENOENT);

    /* Append new log part to existing log */
    unsigned len = strlen(event_log);
    if (len != 0 && event_log[len - 1] != '\n')
        event_log = append_to_malloced_string(event_log, "\n");
    event_log = append_to_malloced_string(event_log, str);

    /* Trim log according to size watermarks */
    len = strlen(event_log);
    char *new_log = event_log;
    if (len > EVENT_LOG_HIGH_WATERMARK)
    {
        new_log += len - EVENT_LOG_LOW_WATERMARK;
        new_log = strchrnul(new_log, '\n');
        if (new_log[0])
            new_log++;
    }

    /* Save */
    dd_save_text(dd, FILENAME_EVENT_LOG, new_log);
    free(event_log);
    dd_close(dd);
}

static bool cancel_event_run(LibReportWindow *self)
{
    if (self->priv->event_child_pid <= 0)
        return false;

    kill(- self->priv->event_child_pid, SIGTERM);
    return true;
}

static void on_btn_next_clicked(GtkButton *button, LibReportWindow *self)
{
    gint current_page_no = gtk_notebook_get_current_page(self->priv->builder->assistant);
    gint next_page_no = select_next_page_no(self, current_page_no, NULL);

    /* if pageno is not change 'switch-page' signal is not emitted */
    if (current_page_no == next_page_no)
        on_page_prepare(self->priv->builder->assistant, gtk_notebook_get_nth_page(self->priv->builder->assistant, next_page_no), next_page_no, self);
    else
        gtk_notebook_set_current_page(self->priv->builder->assistant, next_page_no);
}

static void on_btn_repeat_clicked(GtkButton *button, LibReportWindow *self)
{
    self->priv->auto_event_list = g_list_prepend(self->priv->auto_event_list, self->priv->event_selected);
    self->priv->event_selected = NULL;

    show_next_step_button(self);
    clear_warnings(self);

    const gint current_page_no = gtk_notebook_get_current_page(self->priv->builder->assistant);
    const int next_page_no = select_next_page_no(self, self->priv->builder->pages[PAGENO_SUMMARY].page_no, NULL);
    if (current_page_no == next_page_no)
    {
        on_page_prepare(self->priv->builder->assistant,
                gtk_notebook_get_nth_page(self->priv->builder->assistant, next_page_no), next_page_no, self);
    }
    else
        gtk_notebook_set_current_page(self->priv->builder->assistant, next_page_no);
}

static bool is_processing_finished(LibReportWindow *self)
{
    return !self->priv->expert_mode && !self->priv->auto_event_list;
}

static void update_command_run_log(const char* message, struct analyze_event_data *evd)
{
    const bool it_is_a_dot = (message[0] == '.' && message[1] == '\0');

    if (!it_is_a_dot)
        gtk_label_set_text(evd->window->priv->builder->lbl_event_log, message);

    /* Don't append new line behind single dot */
    const char *log_msg = it_is_a_dot ? message : xasprintf("%s\n", message);
    append_to_textview(evd->window->priv->builder->tv_event_log, log_msg);
    save_to_event_log(evd, log_msg);

    /* Because of single dot, see lines above */
    if (log_msg != message)
        free((void *)log_msg);
}

static void run_event_gtk_error(const char *error_line, void *param)
{
    update_command_run_log(error_line, (struct analyze_event_data *)param);
}

static char *run_event_gtk_logging(char *log_line, void *param)
{
    struct analyze_event_data *evd = (struct analyze_event_data *)param;
    update_command_run_log(log_line, evd);
    return log_line;
}

static void log_request_response_communication(const char *request, const char *response, struct analyze_event_data *evd)
{
    char *message = xasprintf(response ? "%s '%s'" : "%s", request, response);
    update_command_run_log(message, evd);
    free(message);
}

static void run_event_gtk_alert(const char *msg, void *param)
{
    struct analyze_event_data *evd = (struct analyze_event_data *)param;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(evd->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_WARNING,
            GTK_BUTTONS_CLOSE,
            "%s", msg);
    char *tagged_msg = tag_url(msg, "\n");
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);
    free(tagged_msg);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    log_request_response_communication(msg, NULL, evd);
}

static void gtk_entry_emit_dialog_response_ok(GtkEntry *entry, GtkDialog *dialog)
{
    /* Don't close the dialogue if the entry is empty */
    if (gtk_entry_get_text_length(entry) > 0)
        gtk_dialog_response(dialog, GTK_RESPONSE_OK);
}

static char *ask_helper(const char *msg, void *param, bool password)
{
    struct analyze_event_data *evd = (struct analyze_event_data *)param;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(evd->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_OK_CANCEL,
            "%s", msg);
    char *tagged_msg = tag_url(msg, "\n");
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);
    free(tagged_msg);

    GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *textbox = gtk_entry_new();
    /* gtk_entry_set_editable(GTK_ENTRY(textbox), TRUE);
     * is not available in gtk3, so please use the highlevel
     * g_object_set
     */
    g_object_set(G_OBJECT(textbox), "editable", TRUE, NULL);
    g_signal_connect(textbox, "activate", G_CALLBACK(gtk_entry_emit_dialog_response_ok), dialog);

    if (password)
        gtk_entry_set_visibility(GTK_ENTRY(textbox), FALSE);

    gtk_box_pack_start(GTK_BOX(vbox), textbox, TRUE, TRUE, 0);
    gtk_widget_show(textbox);

    char *response = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
    {
        const char *text = gtk_entry_get_text(GTK_ENTRY(textbox));
        response = xstrdup(text);
    }

    gtk_widget_destroy(textbox);
    gtk_widget_destroy(dialog);

    const char *log_response = "";
    if (response)
        log_response = password ? "********" : response;

    log_request_response_communication(msg, log_response, evd);
    return response ? response : xstrdup("");
}

static char *run_event_gtk_ask(const char *msg, void *args)
{
    return ask_helper(msg, args, false);
}

static int run_event_gtk_ask_yes_no(const char *msg, void *param)
{
    struct analyze_event_data *evd = (struct analyze_event_data *)param;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(evd->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "%s", msg);
    char *tagged_msg = tag_url(msg, "\n");
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), tagged_msg);
    free(tagged_msg);

    /* Esc -> No, Enter -> Yes */
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    const int ret = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;

    gtk_widget_destroy(dialog);

    log_request_response_communication(msg, ret ? "YES" : "NO", evd);
    return ret;
}

static int run_event_gtk_ask_yes_no_yesforever(const char *key, const char *msg, void *param)
{
    struct analyze_event_data *evd = (struct analyze_event_data *)param;

    const int ret = run_ask_yes_no_yesforever_dialog(key, msg, GTK_WINDOW(evd->window));
    log_request_response_communication(msg, ret ? "YES" : "NO", evd);
    return ret;
}

static char *run_event_gtk_ask_password(const char *msg, void *args)
{
    return ask_helper(msg, args, true);
}

static bool event_need_review(const char *event_name)
{
    event_config_t *event_cfg = get_event_config(event_name);
    return !event_cfg || !event_cfg->ec_skip_review;
}

static void on_btn_failed_clicked(GtkButton *button, LibReportWindow *self)
{
    /* Since the Repeat button has been introduced, the event chain isn't
     * terminated upon a failure in order to be able to continue in processing
     * in the retry action.
     *
     * Now, user decided to run the emergency analysis instead of trying to
     * reconfigure libreport, so we have to terminate the event chain.
     */
    gtk_widget_hide(self->priv->builder->btn_repeat);
    terminate_event_chain(self, TERMINATE_NOFLAGS);

    /* Show detailed log */
    gtk_expander_set_expanded(self->priv->builder->exp_report_log, TRUE);

    clear_warnings(self);
    update_ls_details_checkboxes(self, EMERGENCY_ANALYSIS_EVENT_NAME);
    start_event_run(self, EMERGENCY_ANALYSIS_EVENT_NAME);

    /* single shot button -> hide after click */
    gtk_widget_hide(GTK_WIDGET(button));
}

/*
 * the widget is added as a child of the VBox in the warning area
 *
 */
static void add_widget_to_warning_area(LibReportWindow *self, GtkWidget *widget)
{
    self->priv->warning_issued = true;
    gtk_box_pack_start(self->priv->builder->box_warning_labels, widget, false, false, 0);
    gtk_widget_show_all(widget);
}

static void add_warning(LibReportWindow *self, const char *warning)
{
    char *label_str = xasprintf("• %s", warning);
    GtkWidget *warning_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(warning_lbl), label_str);
    /* should be safe to free it, gtk calls strdup() to copy it */
    free(label_str);

#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 13) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 13 && GTK_MICRO_VERSION < 5))
    gtk_misc_set_alignment(GTK_MISC(warning_lbl), 0.0, 0.0);
#else
    gtk_widget_set_halign (warning_lbl, GTK_ALIGN_START);
    gtk_widget_set_valign (warning_lbl, GTK_ALIGN_END);
#endif
    gtk_label_set_justify(GTK_LABEL(warning_lbl), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(warning_lbl), TRUE);

    add_widget_to_warning_area(self, warning_lbl);
}

static void on_sensitive_ticket_clicked_cb(GtkWidget *button, gpointer user_data)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
    {
        xsetenv(CREATE_PRIVATE_TICKET, "1");
    }
    else
    {
        safe_unsetenv(CREATE_PRIVATE_TICKET);
    }
}

static void add_sensitive_data_warning(LibReportWindow *self)
{
    GtkBuilder *builder = make_gtk_builder();

    GtkWidget *sens_data_warn = GTK_WIDGET(gtk_builder_get_object(builder, SENSITIVE_DATA_WARN));
    GtkButton *sens_ticket_cb = GTK_BUTTON(gtk_builder_get_object(builder, PRIVATE_TICKET_CB));

    g_signal_connect(sens_ticket_cb, "toggled", G_CALLBACK(on_sensitive_ticket_clicked_cb), self);
    add_widget_to_warning_area(self, GTK_WIDGET(sens_data_warn));

    g_object_unref(builder);
}

static void show_warnings(LibReportWindow *self)
{
    if (self->priv->warning_issued)
        gtk_widget_show(self->priv->builder->widget_warnings_area);
}


static void clear_warnings(LibReportWindow *self)
{
    /* erase all warnings */
    if (!self->priv->warning_issued)
        return;

    gtk_widget_hide(self->priv->builder->widget_warnings_area);
    gtk_container_foreach(GTK_CONTAINER(self->priv->builder->box_warning_labels), &remove_child_widget, NULL);
    self->priv->warning_issued = false;
}

/* TODO : this function should not set a warning directly, it makes the function unusable for add_event_buttons(); */
static bool check_minimal_bt_rating(LibReportWindow *self, const char *event_name)
{
    bool acceptable_rating = true;
    event_config_t *event_cfg = NULL;

    if (!event_name)
        error_msg_and_die(_("Cannot check backtrace rating because of invalid event name"));
    else if (prefixcmp(event_name, "report") != 0)
    {
        log_info("No checks for bactrace rating because event '%s' doesn't report.", event_name);
        return acceptable_rating;
    }
    else
        event_cfg = get_event_config(event_name);

    char *description = NULL;
    acceptable_rating = check_problem_rating_usability(event_cfg, self->priv->problem_data, &description, NULL);
    if (description)
    {
        add_warning(self, description);
        free(description);
    }

    return acceptable_rating;
}

static char *get_next_processed_event(LibReportWindow *self)
{
    if (!self->priv->auto_event_list)
        return NULL;

    char *event_name = (char *)self->priv->auto_event_list->data;
    const size_t event_len = strlen(event_name);

    /* pop the current event */
    self->priv->auto_event_list = g_list_delete_link(self->priv->auto_event_list, self->priv->auto_event_list);

    if (event_name[event_len - 1] == '*')
    {
        log_info("Expanding event '%s'", event_name);

        struct dump_dir *dd = dd_opendir(self->priv->dump_dir_name, DD_OPEN_READONLY);
        if (!dd)
            error_msg_and_die("Can't open directory '%s'", self->priv->dump_dir_name);

        /* Erase '*' */
        event_name[event_len - 1] = '\0';

        /* get 'event1\nevent2\nevent3\n' or '' if no event is possible */
        char *expanded_events = list_possible_events(dd, self->priv->dump_dir_name, event_name);

        dd_close(dd);
        free(event_name);

        GList *expanded_list = NULL;
        /* add expanded events from event having trailing '*' */
        char *next = event_name = expanded_events;
        while ((next = strchr(event_name, '\n')))
        {
            /* 'event1\0event2\nevent3\n' */
            next[0] = '\0';

            /* 'event1' */
            event_name = xstrdup(event_name);
            log_debug("Adding a new expanded event '%s' to the processed list", event_name);

            /* the last event is not added to the expanded list */
            ++next;
            if (next[0] == '\0')
                break;

            expanded_list = g_list_prepend(expanded_list, event_name);

            /* 'event2\nevent3\n' */
            event_name = next;
        }

        free(expanded_events);

        /* It's OK we can safely compare address even if them were previously freed */
        if (event_name != expanded_events)
            /* the last expanded event name is stored in event_name */
            self->priv->auto_event_list = g_list_concat(expanded_list, self->priv->auto_event_list);
        else
        {
            log_info("No event was expanded, will continue with the next one.");
            /* no expanded event try the next event */
            return get_next_processed_event(self);
        }
    }

    clear_warnings(self);
    const bool acceptable = check_minimal_bt_rating(self, event_name);
    show_warnings(self);

    if (!acceptable)
    {
        /* a node for this event was already removed */
        free(event_name);

        g_list_free_full(self->priv->auto_event_list, free);
        self->priv->auto_event_list = NULL;
        return NULL;
    }

    return event_name;
}

static char *setup_next_processed_event(LibReportWindow *self)
{
    free(self->priv->event_selected);
    self->priv->event_selected = NULL;

    char *event = get_next_processed_event(self);
    if (!event)
    {
        /* No next event, go to progress page and finish */
        gtk_label_set_text(self->priv->builder->lbl_event_log, _("Processing finished."));
        /* we don't know the result of the previous event here
         * so at least hide the spinner, because we're obviously finished
        */
        gtk_widget_hide(GTK_WIDGET(self->priv->builder->spinner_event_log));
        hide_next_step_button(self);
        return NULL;
    }

    log_notice("selected -e EVENT:%s", event);
    return event;
}

static bool get_sensitive_data_permission(LibReportWindow *self, const char *event_name)
{
    event_config_t *event_cfg = get_event_config(event_name);

    if (!event_cfg || !event_cfg->ec_sending_sensitive_data)
        return true;

    char *msg = xasprintf(_("Event '%s' requires permission to send possibly sensitive data."
                            "\nDo you want to continue?"),
                            ec_get_screen_name(event_cfg) ? ec_get_screen_name(event_cfg) : event_name);
    const bool response = run_ask_yes_no_yesforever_dialog("ask_send_sensitive_data", msg, GTK_WINDOW(self));
    free(msg);

    return response;
}

static void check_event_config(LibReportWindow *self, const char *event_name)
{
    GHashTable *errors = validate_event(event_name);
    if (errors != NULL)
    {
        g_hash_table_unref(errors);
        show_event_config_dialog(event_name, GTK_WINDOW(self));
    }
}

static gint find_by_button(gconstpointer a, gconstpointer button)
{
    const event_gui_data_t *evdata = a;
    return (evdata->toggle_button != button);
}

static void on_event_rb_toggled(GtkButton *button, gpointer user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);

    /* Note: called both when item is selected and _unselected_,
     * use gtk_toggle_button_get_active() to determine state.
     */
    GList *found = g_list_find_custom(self->priv->list_events, button, find_by_button);
    if (found)
    {
        event_gui_data_t *evdata = found->data;
        if (gtk_toggle_button_get_active(evdata->toggle_button))
        {
            free(self->priv->event_selected);
            self->priv->event_selected = xstrdup(evdata->event_name);
            check_event_config(self, evdata->event_name);

            clear_warnings(self);
            const bool good_rating = check_minimal_bt_rating(self, self->priv->event_selected);
            show_warnings(self);

            gtk_widget_set_sensitive(self->priv->builder->btn_next, good_rating);
        }
    }
}

static gint select_next_page_no(LibReportWindow *self, gint current_page_no, gpointer data)
{
    GtkWidget *page;
    page_obj_t *pages = self->priv->builder->pages;

 again:
    log_notice("%s: current_page_no:%d", __func__, current_page_no);
    current_page_no++;
    page = gtk_notebook_get_nth_page(self->priv->builder->assistant, current_page_no);

    if (pages[PAGENO_EVENT_SELECTOR].page_widget == page)
    {
        if (!self->priv->expert_mode && (self->priv->auto_event_list == NULL))
        {
            return current_page_no; //stay here and let user select the workflow
        }

        if (!self->priv->expert_mode)
        {
            /* (note: this frees and sets to NULL self->priv->event_selected) */
            char *event = setup_next_processed_event(self);
            if (!event)
            {
                current_page_no = pages[PAGENO_EVENT_PROGRESS].page_no - 1;
                goto again;
            }

            if (!get_sensitive_data_permission(self, event))
            {
                free(event);

                cancel_processing(self, /* default message */ NULL, TERMINATE_NOFLAGS);
                current_page_no = pages[PAGENO_EVENT_PROGRESS].page_no - 1;
                goto again;
            }

            if (problem_data_get_content_or_NULL(self->priv->problem_data, FILENAME_NOT_REPORTABLE))
            {
                free(event);

                char *msg = xasprintf(_("This problem should not be reported "
                                "(it is likely a known problem). %s"),
                                problem_data_get_content_or_NULL(self->priv->problem_data, FILENAME_NOT_REPORTABLE)
                );
                cancel_processing(self, msg, TERMINATE_NOFLAGS);
                free(msg);
                current_page_no = pages[PAGENO_EVENT_PROGRESS].page_no - 1;
                goto again;
            }

            self->priv->event_selected = event;

            /* Notify a user that some configuration options miss values, but */
            /* don't force him to provide them. */
            check_event_config(self, self->priv->event_selected);

            /* >>> and this but this is clearer
             * because it does exactly the same thing
             * but I'm pretty scared to touch it */
            current_page_no = pages[PAGENO_EVENT_SELECTOR].page_no + 1;
            goto event_was_selected;
        }
    }

    if (pages[PAGENO_EVENT_SELECTOR + 1].page_widget == page)
    {
 event_was_selected:
        if (!self->priv->event_selected)
        {
            /* Go back to selectors */
            current_page_no = pages[PAGENO_EVENT_SELECTOR].page_no - 1;
            goto again;
        }

        if (!event_need_review(self->priv->event_selected))
        {
            current_page_no = pages[PAGENO_EVENT_PROGRESS].page_no - 1;
            goto again;
        }
    }

#if 0
    if (pages[PAGENO_EDIT_COMMENT].page_widget == page)
    {
        if (problem_data_get_content_or_NULL(self->priv->problem_data, FILENAME_COMMENT))
            goto again; /* no comment, skip this page */
    }
#endif

    if (pages[PAGENO_EVENT_DONE].page_widget == page)
    {
        if (self->priv->auto_event_list)
        {
            /* Go back to selectors */
            current_page_no = pages[PAGENO_SUMMARY].page_no;
        }
        goto again;
    }

    if (pages[PAGENO_NOT_SHOWN].page_widget == page)
    {
        if (!self->priv->expert_mode)
            exit(0);
        /* No! this would SEGV (infinitely recurse into select_next_page_no) */
        /*gtk_assistant_commit(self->priv->assistant);*/
        current_page_no = pages[PAGENO_EVENT_SELECTOR].page_no - 1;
        goto again;
    }

    log_notice("%s: selected page #%d", __func__, current_page_no);
    return current_page_no;
}

/* Based on selected reporter, update item checkboxes */
static void update_ls_details_checkboxes(LibReportWindow *self, const char *event_name)
{
    event_config_t *cfg = get_event_config(event_name);
    //log("%s: event:'%s', cfg:'%p'", __func__, self->priv->event_selected, cfg);
    GHashTableIter iter;
    char *name;
    struct problem_item *item;
    g_hash_table_iter_init(&iter, self->priv->problem_data);
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&item))
    {
        /* Decide whether item is allowed, required, and what's the default */
        item->allowed_by_reporter = 1;
        if (cfg)
        {
            if (is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_exclude_items_always))
                item->allowed_by_reporter = 0;
            if ((item->flags & CD_FLAG_BIN) && cfg->ec_exclude_binary_items)
                item->allowed_by_reporter = 0;
        }

        item->default_by_reporter = item->allowed_by_reporter;
        if (cfg)
        {
            if (is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_exclude_items_by_default))
                item->default_by_reporter = 0;
            if (is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_include_items_by_default))
                item->allowed_by_reporter = item->default_by_reporter = 1;
        }

        item->required_by_reporter = 0;
        if (cfg)
        {
            if (is_in_comma_separated_list_of_glob_patterns(name, cfg->ec_requires_items))
                item->default_by_reporter = item->allowed_by_reporter = item->required_by_reporter = 1;
        }

        int cur_value;
        if (item->selected_by_user == 0)
            cur_value = item->default_by_reporter;
        else
            cur_value = !!(item->selected_by_user + 1); /* map -1,1 to 0,1 */

        //log("%s: '%s' allowed:%d reqd:%d def:%d user:%d", __func__, name,
        //    item->allowed_by_reporter,
        //    item->required_by_reporter,
        //    item->default_by_reporter,
        //    item->selected_by_user
        //);

        /* Find corresponding line and update checkbox */
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(self->priv->builder->ls_details), &iter))
        {
            do {
                gchar *item_name = NULL;
                gtk_tree_model_get(GTK_TREE_MODEL(self->priv->builder->ls_details), &iter,
                            DETAIL_COLUMN_NAME, &item_name,
                            -1);
                if (!item_name) /* paranoia, should never happen */
                    continue;
                int differ = strcmp(name, item_name);
                g_free(item_name);
                if (differ)
                    continue;
                gtk_list_store_set(self->priv->builder->ls_details, &iter,
                        DETAIL_COLUMN_CHECKBOX, cur_value,
                        -1);
                //log("%s: changed gtk_list_store_set to %d", __func__, (item->allowed_by_reporter && item->selected_by_user >= 0));
                break;
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(self->priv->builder->ls_details), &iter));
        }
    }
}



//static void event_rb_was_toggled(GtkButton *button, gpointer user_data)
static void set_auto_event_chain(GtkButton *button, gpointer user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);

    //event is selected, so make sure the expert mode is disabled
    lib_report_window_set_expert_mode(self, false);

    workflow_t *w = (workflow_t *)g_object_get_data(G_OBJECT(button), "workflow");

    config_item_info_t *info = workflow_get_config_info(w);
    log_notice("selected workflow '%s'", ci_get_screen_name(info));

    GList *event_list = NULL;
    GList *wf_event_list = wf_get_event_list(w);
    while(wf_event_list)
    {
        event_list = g_list_append(event_list, xstrdup(ec_get_name(wf_event_list->data)));
        load_single_event_config_data_from_user_storage((event_config_t *)wf_event_list->data);

        wf_event_list = g_list_next(wf_event_list);
    }

    lib_report_window_set_event_list(self, event_list);
}

static event_gui_data_t *new_event_gui_data_t(void)
{
    return xzalloc(sizeof(event_gui_data_t));
}

static void free_event_gui_data_t(event_gui_data_t *evdata, void *unused)
{
    if (evdata)
    {
        free(evdata->event_name);
        free(evdata);
    }
}

/* event_name contains "EVENT1\nEVENT2\nEVENT3\n".
 * Add new radio buttons to GtkBox for each EVENTn.
 * Remember them in GList **p_event_list (list of event_gui_data_t's).
 * Set "toggled" callback on each button to given GCallback if it's not NULL.
 * Return active button (or NULL if none created).
 */
/* helper */
static char *missing_items_in_comma_list(problem_data_t *pd, const char *input_item_list)
{
    if (!input_item_list)
        return NULL;

    char *item_list = xstrdup(input_item_list);
    char *result = item_list;
    char *dst = item_list;

    while (item_list[0])
    {
        char *end = strchr(item_list, ',');
        if (end) *end = '\0';
        if (!problem_data_get_item_or_NULL(pd, item_list))
        {
            if (dst != result)
                *dst++ = ',';
            dst = stpcpy(dst, item_list);
        }
        if (!end)
            break;
        *end = ',';
        item_list = end + 1;
    }
    if (result == dst)
    {
        free(result);
        result = NULL;
    }
    return result;
}

static void label_wrapper(GtkWidget *widget, gpointer data_unused)
{
    if (GTK_IS_CONTAINER(widget))
    {
        gtk_container_foreach((GtkContainer*)widget, label_wrapper, NULL);
        return;
    }
    if (GTK_IS_LABEL(widget))
    {
        GtkLabel *label = (GtkLabel*)widget;
        gtk_label_set_line_wrap(label, 1);
        //const char *txt = gtk_label_get_label(label);
        //log("label '%s' set to wrap", txt);
    }
}

static void wrap_all_labels(GtkWidget *widget)
{
    label_wrapper(widget, NULL);
}

static event_gui_data_t *add_event_buttons(LibReportWindow *self)
{
    //log_info("removing all buttons from box %p", box);
    gtk_container_foreach(GTK_CONTAINER(self->priv->builder->box_events), &remove_child_widget, NULL);
    g_list_foreach(self->priv->list_events, (GFunc)free_event_gui_data_t, NULL);
    g_list_free(self->priv->list_events);
    self->priv->list_events = NULL;

    struct dump_dir *dd = dd_opendir(self->priv->dump_dir_name, DD_OPEN_READONLY);
    if (!dd)
    {
        error_msg("Can't open directory '%s'", self->priv->dump_dir_name);
        return NULL;
    }

    char *event_name = list_possible_events(dd, NULL, "");
    dd_close(dd);

    if (!event_name || !event_name[0])
    {
        GtkWidget *lbl = gtk_label_new(_("No reporting targets are defined for this problem. Check configuration in /etc/libreport/*"));
#if ((GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION < 13) || (GTK_MAJOR_VERSION == 3 && GTK_MINOR_VERSION == 13 && GTK_MICRO_VERSION < 5))
        gtk_misc_set_alignment(GTK_MISC(lbl), /*x*/ 0.0, /*y*/ 0.0);
#else
        gtk_widget_set_halign (lbl, GTK_ALIGN_START);
        gtk_widget_set_valign (lbl, GTK_ALIGN_END);
#endif
        make_label_autowrap_on_resize(GTK_LABEL(lbl));
        gtk_box_pack_start(self->priv->builder->box_events, lbl, /*expand*/ true, /*fill*/ false, /*padding*/ 0);
        return NULL;
    }

    event_gui_data_t *first_button = NULL;
    event_gui_data_t *active_button = NULL;
    while (1)
    {
        if (!event_name || !event_name[0])
            break;

        char *event_name_end = strchr(event_name, '\n');
        *event_name_end = '\0';

        event_config_t *cfg = get_event_config(event_name);

        /* Form a pretty text representation of event */
        /* By default, use event name: */
        const char *event_screen_name = event_name;
        const char *event_description = NULL;
        char *tmp_description = NULL;
        bool red_choice = false;
        bool green_choice = false;
        if (cfg)
        {
            /* .xml has (presumably) prettier description, use it: */
            if (ec_get_screen_name(cfg))
                event_screen_name = ec_get_screen_name(cfg);
            event_description = ec_get_description(cfg);

            char *missing = missing_items_in_comma_list(self->priv->problem_data, cfg->ec_requires_items);
            if (missing)
            {
                red_choice = true;
                event_description = tmp_description = xasprintf(_("(requires: %s)"), missing);
                free(missing);
            }
            else
            if (cfg->ec_creates_items)
            {
                if (problem_data_get_item_or_NULL(self->priv->problem_data, cfg->ec_creates_items))
                {
                    char *missing = missing_items_in_comma_list(self->priv->problem_data, cfg->ec_creates_items);
                    if (missing)
                        free(missing);
                    else
                    {
                        green_choice = true;
                        event_description = tmp_description = xasprintf(_("(not needed, data already exist: %s)"), cfg->ec_creates_items);
                    }
                }
            }
        }

        //log_info("adding button '%s' to box %p", event_name, box);
        char *event_label = xasprintf("%s%s%s",
                        event_screen_name,
                        (event_description ? " - " : ""),
                        event_description ? event_description : ""
        );
        free(tmp_description);

        GtkWidget *button = gtk_radio_button_new_with_label_from_widget(
                        (first_button ? GTK_RADIO_BUTTON(first_button->toggle_button) : NULL),
                        event_label
                );
        free(event_label);

        if (green_choice || red_choice)
        {
            GtkWidget *child = gtk_bin_get_child(GTK_BIN(button));
            if (child)
            {
                static const GdkRGBA red = {
                    .red   = 1.0,
                    .green = 0.0,
                    .blue  = 0.0,
                    .alpha = 1.0,
                };
                static const GdkRGBA green = {
                    .red   = 0.0,
                    .green = 0.5,
                    .blue  = 0.0,
                    .alpha = 1.0,
                };
                const GdkRGBA *color = (green_choice ? &green : &red);
                //gtk_widget_modify_text(button, GTK_STATE_NORMAL, color);
                gtk_widget_override_color(child, GTK_STATE_FLAG_NORMAL, color);
            }
        }

        g_signal_connect(G_OBJECT(button), "toggled", G_CALLBACK(on_event_rb_toggled), self);

        if (cfg && ec_get_long_desc(cfg))
            gtk_widget_set_tooltip_text(button, ec_get_long_desc(cfg));

        event_gui_data_t *event_gui_data = new_event_gui_data_t();
        event_gui_data->event_name = xstrdup(event_name);
        event_gui_data->toggle_button = GTK_TOGGLE_BUTTON(button);
        self->priv->list_events = g_list_append(self->priv->list_events, event_gui_data);

        if (!first_button)
            first_button = event_gui_data;

        if (!green_choice && !red_choice && !active_button)
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), true);
            active_button = event_gui_data;
        }

        *event_name_end = '\n';
        event_name = event_name_end + 1;

        gtk_box_pack_start(self->priv->builder->box_events, button, /*expand*/ false, /*fill*/ false, /*padding*/ 0);
        gtk_widget_show_all(GTK_WIDGET(button));
        wrap_all_labels(button);
        /* Disabled - seems that above is enough... */
        /*fix_all_wrapped_labels(button);*/
    }
    gtk_widget_show_all(GTK_WIDGET(self->priv->builder->box_events));

    return active_button;
}

static void update_gui_state_from_problem_data(LibReportWindow *self, int flags)
{
    update_window_title(self);
    remove_tabs_from_notebook(self);

    const char *reason = problem_data_get_content_or_NULL(self->priv->problem_data, FILENAME_REASON);
    const char *not_reportable = problem_data_get_content_or_NULL(self->priv->problem_data,
                                                                  FILENAME_NOT_REPORTABLE);

    char *t = xasprintf("%s%s%s",
                        not_reportable ? : "",
                        not_reportable ? " " : "",
                        reason ? : _("(no description)"));

    gtk_label_set_text(self->priv->builder->lbl_cd_reason, t);
    free(t);

    gtk_list_store_clear(self->priv->builder->ls_details);
    struct cd_stats stats = { self };
    g_hash_table_foreach(self->priv->problem_data, append_item_to_ls_details, &stats);
    char *msg = xasprintf(_("%llu bytes, %u files"), (long long)stats.filesize, stats.filecount);
    gtk_label_set_text(self->priv->builder->lbl_size, msg);
    free(msg);

    load_text_to_text_view(self, self->priv->builder->tv_comment, FILENAME_COMMENT);

    add_workflow_buttons(self, G_CALLBACK(set_auto_event_chain));

    /* Update event radio buttons
     * show them only in expert mode
    */
    event_gui_data_t *active_button = NULL;
    if (self->priv->expert_mode)
    {
        //this widget doesn't react to show_all, so we need to "force" it
        gtk_widget_show(GTK_WIDGET(self->priv->builder->box_events));
        active_button = add_event_buttons(self);
    }

    if (flags & UPDATE_SELECTED_EVENT && self->priv->expert_mode)
    {
        /* Update the value of currently selected event */
        free(self->priv->event_selected);
        self->priv->event_selected = NULL;
        if (active_button)
        {
            self->priv->event_selected = xstrdup(active_button->event_name);
        }
        log_info("event_selected='%s'", self->priv->event_selected);
    }
    /* We can't just do gtk_widget_show_all once in main:
     * We created new widgets (buttons). Need to make them visible.
     */
    gtk_widget_show_all(GTK_WIDGET(self));
}

static void create_details_treeview(LibReportWindow *self)
{
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_toggle_new();
    column = gtk_tree_view_column_new_with_attributes(
                _("Include"), renderer,
                /* which "attr" of renderer to set from which COLUMN? (can be repeated) */
                "active", DETAIL_COLUMN_CHECKBOX,
                NULL);
    self->priv->builder->tv_details_col_checkbox = column;
    gtk_tree_view_append_column(self->priv->builder->tv_details, column);
    /* This column has a handler */
    g_signal_connect(renderer, "toggled", G_CALLBACK(on_tv_details_checkbox_toggled), self);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
                _("Name"), renderer,
                "text", DETAIL_COLUMN_NAME,
                NULL);
    gtk_tree_view_append_column(self->priv->builder->tv_details, column);
    /* This column has a clickable header for sorting */
    gtk_tree_view_column_set_sort_column_id(column, DETAIL_COLUMN_NAME);

    self->priv->builder->tv_details_renderer_value = renderer = gtk_cell_renderer_text_new();
    g_signal_connect(renderer, "edited", G_CALLBACK(save_edited_one_liner), self);
    column = gtk_tree_view_column_new_with_attributes(
                _("Value"), renderer,
                "text", DETAIL_COLUMN_VALUE,
                NULL);
    gtk_tree_view_append_column(self->priv->builder->tv_details, column);
    /* This column has a clickable header for sorting */
    gtk_tree_view_column_set_sort_column_id(column, DETAIL_COLUMN_VALUE);

    /*
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(
                _("Path"), renderer,
                "text", DETAIL_COLUMN_PATH,
                NULL);
    gtk_tree_view_append_column(self->priv->builder->tv_details, column);
    */

    self->priv->builder->ls_details = gtk_list_store_new(DETAIL_NUM_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model(self->priv->builder->tv_details, GTK_TREE_MODEL(self->priv->builder->ls_details));

    g_signal_connect(self->priv->builder->tv_details, "row-activated", G_CALLBACK(on_tv_details_row_activated), self);
    g_signal_connect(self->priv->builder->tv_details, "cursor-changed", G_CALLBACK(on_tv_details_cursor_changed), self);
    /* [Enter] on a row:
     * g_signal_connect(self->priv->builder->tv_details, "select-cursor-row", G_CALLBACK(tv_details_select_cursor_row), NULL);
     */
}

static int ask_replace_old_private_group_name(GtkWindow *parent)
{
    char *message = xasprintf(_("Private ticket is requested but the group name 'private' has been deprecated. "
                                "We kindly ask you to use 'fedora_contrib_private' group name. "
                                "Click Yes button or update the configuration manually. Or click No button, if you really want to use 'private' group.\n\n"
                                "If you are not sure what this dialogue means, please trust us and click Yes button.\n\n"
                                "Read more about the private bug reports at:\n"
                                "https://github.com/abrt/abrt/wiki/FAQ#creating-private-bugzilla-tickets\n"
                                "https://bugzilla.redhat.com/show_bug.cgi?id=1044653\n"));

    char *markup_message = xasprintf(_("Private ticket is requested but the group name <i>private</i> has been deprecated. "
                                "We kindly ask you to use <i>fedora_contrib_private</i> group name. "
                                "Click Yes button or update the configuration manually. Or click No button, if you really want to use <i>private</i> group.\n\n"
                                "If you are not sure what this dialogue means, please trust us and click Yes button.\n\n"
                                "Read more about the private bug reports at:\n"
                                "<a href=\"https://github.com/abrt/abrt/wiki/FAQ#creating-private-bugzilla-tickets\">"
                                "https://github.com/abrt/abrt/wiki/FAQ#creating-private-bugzilla-tickets</a>\n"
                                "<a href=\"https://bugzilla.redhat.com/show_bug.cgi?id=1044653\">https://bugzilla.redhat.com/show_bug.cgi?id=1044653</a>\n"));

    GtkWidget *old_private_group = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_YES_NO,
        message);

    gtk_window_set_transient_for(GTK_WINDOW(old_private_group), parent);
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(old_private_group),
                                    markup_message);
    free(message);
    free(markup_message);

    /* Esc -> No, Enter -> Yes */
    gtk_dialog_set_default_response(GTK_DIALOG(old_private_group), GTK_RESPONSE_YES);

    gint result = gtk_dialog_run(GTK_DIALOG(old_private_group));
    gtk_widget_destroy(old_private_group);

    return result == GTK_RESPONSE_YES;
}

static void on_bt_approve_toggled(GtkToggleButton *togglebutton, LibReportWindow *self)
{
    gtk_widget_set_sensitive(self->priv->builder->btn_next, gtk_toggle_button_get_active(togglebutton));
}

static void toggle_eb_comment(LibReportWindow *self)
{
    /* The page doesn't exist with report-only option */
    if (self->priv->builder->pages[PAGENO_EDIT_COMMENT].page_widget == NULL)
        return;

    bool good =
        gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(self->priv->builder->tv_comment)) >= 10
        || gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->priv->builder->cb_no_comment));

    /* Allow next page only when the comment has at least 10 chars */
    gtk_widget_set_sensitive(self->priv->builder->btn_next, good);

    /* And show the eventbox with label */
    if (good)
        gtk_widget_hide(GTK_WIDGET(self->priv->builder->eb_comment));
    else
        gtk_widget_show(GTK_WIDGET(self->priv->builder->eb_comment));
}

static void on_log_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);
    gtk_widget_show(GTK_WIDGET(self->priv->builder->exp_report_log));
}

static GList *find_words_in_text_buffer(int page,
                                        GtkTextView *tev,
                                        GList *words,
                                        GList *ignore_sitem_list,
                                        GtkTextIter start_find,
                                        GtkTextIter end_find,
                                        bool case_insensitive
                                        )
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(tev);
    gtk_text_buffer_set_modified(buffer, FALSE);

    GList *found_words = NULL;
    GtkTextIter start_match;
    GtkTextIter end_match;

    for (GList *w = words; w; w = g_list_next(w))
    {
        gtk_text_buffer_get_start_iter(buffer, &start_find);

        const char *search_word = w->data;
        while (search_word && search_word[0] && gtk_text_iter_forward_search(&start_find, search_word,
                    GTK_TEXT_SEARCH_TEXT_ONLY | (case_insensitive ? GTK_TEXT_SEARCH_CASE_INSENSITIVE : 0),
                    &start_match,
                    &end_match, NULL))
        {
            search_item_t *found_word = sitem_new(
                    page,
                    buffer,
                    tev,
                    start_match,
                    end_match
                );
            int offset = gtk_text_iter_get_offset(&end_match);
            gtk_text_buffer_get_iter_at_offset(buffer, &start_find, offset);

            if (sitem_is_in_sitemlist(found_word, ignore_sitem_list))
            {
                sitem_free(found_word);
                // don't count the word if it's part of some of the ignored words
                continue;
            }

            found_words = g_list_prepend(found_words, found_word);
        }
    }

    return found_words;
}

static void search_item_to_list_store_item(GtkListStore *store, GtkTreeIter *new_row,
        const gchar *file_name, search_item_t *word)
{
    GtkTextIter *beg = gtk_text_iter_copy(&(word->start));
    gtk_text_iter_backward_line(beg);

    GtkTextIter *end = gtk_text_iter_copy(&(word->end));
    /* the first call moves end variable at the end of the current line */
    if (gtk_text_iter_forward_line(end))
    {
        /* the second call moves end variable at the end of the next line */
        gtk_text_iter_forward_line(end);

        /* don't include the last new which causes an empty line in the GUI list */
        gtk_text_iter_backward_char(end);
    }

    gchar *tmp = gtk_text_buffer_get_text(word->buffer, beg, &(word->start),
            /*don't include hidden chars*/FALSE);
    gchar *prefix = g_markup_escape_text(tmp, /*NULL terminated string*/-1);
    g_free(tmp);

    tmp = gtk_text_buffer_get_text(word->buffer, &(word->start), &(word->end),
            /*don't include hidden chars*/FALSE);
    gchar *text = g_markup_escape_text(tmp, /*NULL terminated string*/-1);
    g_free(tmp);

    tmp = gtk_text_buffer_get_text(word->buffer, &(word->end), end,
            /*don't include hidden chars*/FALSE);
    gchar *suffix = g_markup_escape_text(tmp, /*NULL terminated string*/-1);
    g_free(tmp);

    char *content = xasprintf("%s<span foreground=\"red\">%s</span>%s", prefix, text, suffix);

    g_free(suffix);
    g_free(text);
    g_free(prefix);

    gtk_text_iter_free(end);
    gtk_text_iter_free(beg);

    gtk_list_store_set(store, new_row,
            SEARCH_COLUMN_FILE, file_name,
            SEARCH_COLUMN_TEXT, content,
            SEARCH_COLUMN_ITEM, word,
            -1);
}

static bool highligh_words_in_textview(LibReportWindow *self, int page, GtkTextView *tev, GList *words, GList *ignored_words, bool case_insensitive)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(tev);
    gtk_text_buffer_set_modified(buffer, FALSE);

    GtkWidget *notebook_child = gtk_notebook_get_nth_page(self->priv->builder->notebook, page);
    GtkWidget *tab_lbl = gtk_notebook_get_tab_label(self->priv->builder->notebook, notebook_child);

    /* Remove old results */
    bool buffer_removing = false;

    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(self->priv->builder->ls_sensitive_list), &iter);

    /* Turn off the changed callback during the update */
    g_signal_handler_block(self->priv->builder->tv_sensitive_sel, self->priv->tv_sensitive_sel_hndlr);

    while (valid)
    {
        char *text = NULL;
        search_item_t *word = NULL;

        gtk_tree_model_get(GTK_TREE_MODEL(self->priv->builder->ls_sensitive_list), &iter,
                SEARCH_COLUMN_TEXT, &text,
                SEARCH_COLUMN_ITEM, &word,
                -1);

        if (word->buffer == buffer)
        {
            buffer_removing = true;

            valid = gtk_list_store_remove(self->priv->builder->ls_sensitive_list, &iter);

            free(text);

            if (word == self->priv->current_highlighted_word)
                self->priv->current_highlighted_word = NULL;

            free(word);
        }
        else
        {
            if(buffer_removing)
                break;

            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(self->priv->builder->ls_sensitive_list), &iter);
        }
    }

    /* Turn on the changed callback after the update */
    g_signal_handler_unblock(self->priv->builder->tv_sensitive_sel, self->priv->tv_sensitive_sel_hndlr);

    GtkTextIter start_find;
    gtk_text_buffer_get_start_iter(buffer, &start_find);
    GtkTextIter end_find;
    gtk_text_buffer_get_end_iter(buffer, &end_find);

    gtk_text_buffer_remove_tag_by_name(buffer, "search_result_bg", &start_find, &end_find);
    gtk_text_buffer_remove_tag_by_name(buffer, "current_result_bg", &start_find, &end_find);

    PangoAttrList *attrs = gtk_label_get_attributes(GTK_LABEL(tab_lbl));
    gtk_label_set_attributes(GTK_LABEL(tab_lbl), NULL);
    pango_attr_list_unref(attrs);

    GList *result = NULL;
    GList *ignored_words_in_buffer = NULL;

    ignored_words_in_buffer = find_words_in_text_buffer(page,
                                                        tev,
                                                        ignored_words,
                                                        NULL,
                                                        start_find,
                                                        end_find,
                                                        /*case sensitive*/false);


    result = find_words_in_text_buffer(page,
                                       tev,
                                       words,
                                       ignored_words_in_buffer,
                                       start_find,
                                       end_find,
                                       case_insensitive
                                        );

    for (GList *w = result; w; w = g_list_next(w))
    {
        search_item_t *item = (search_item_t *)w->data;
        gtk_text_buffer_apply_tag_by_name(buffer, "search_result_bg",
                                          sitem_get_start_iter(item),
                                          sitem_get_end_iter(item));
    }

    if (result)
    {
        PangoAttrList *attrs = pango_attr_list_new();
        PangoAttribute *foreground_attr = pango_attr_foreground_new(65535, 0, 0);
        pango_attr_list_insert(attrs, foreground_attr);
        PangoAttribute *underline_attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
        pango_attr_list_insert(attrs, underline_attr);
        gtk_label_set_attributes(GTK_LABEL(tab_lbl), attrs);

        /* The current order of the found words is defined by order of words in the
         * passed list. We have to order the words according to their occurrence in
         * the buffer.
         */
        result = g_list_sort(result, (GCompareFunc)sitem_compare);

        GList *search_result = result;
        for ( ; search_result != NULL; search_result = g_list_next(search_result))
        {
            search_item_t *word = (search_item_t *)search_result->data;

            const gchar *file_name = gtk_label_get_text(GTK_LABEL(tab_lbl));

            /* Create a new row */
            GtkTreeIter new_row;
            if (valid)
                /* iter variable is valid GtkTreeIter and it means that the results */
                /* need to be inserted before this iterator, in this case iter points */
                /* to the first word of another GtkTextView */
                gtk_list_store_insert_before(self->priv->builder->ls_sensitive_list, &new_row, &iter);
            else
                /* the GtkTextView is the last one or the only one, insert the results */
                /* at the end of the list store */
                gtk_list_store_append(self->priv->builder->ls_sensitive_list, &new_row);

            /* Assign values to the new row */
            search_item_to_list_store_item(self->priv->builder->ls_sensitive_list, &new_row, file_name, word);
        }
    }

    g_list_free_full(ignored_words_in_buffer, free);
    g_list_free(result);

    return result != NULL;
}

static gboolean highligh_words_in_tabs(LibReportWindow *self, GList *forbidden_words,  GList *allowed_words, bool case_insensitive)
{
    gboolean found = false;

    gint n_pages = gtk_notebook_get_n_pages(self->priv->builder->notebook);
    int page = 0;
    for (page = 0; page < n_pages; page++)
    {
        //notebook_page->scrolled_window->text_view
        GtkWidget *notebook_child = gtk_notebook_get_nth_page(self->priv->builder->notebook, page);
        GtkWidget *tab_lbl = gtk_notebook_get_tab_label(self->priv->builder->notebook, notebook_child);

        const char *const lbl_txt = gtk_label_get_text(GTK_LABEL(tab_lbl));
        if (strncmp(lbl_txt, "page 1", 5) == 0 || strcmp(FILENAME_COMMENT, lbl_txt) == 0)
            continue;

        GtkTextView *tev = GTK_TEXT_VIEW(gtk_bin_get_child(GTK_BIN(notebook_child)));
        found |= highligh_words_in_textview(self, page, tev, forbidden_words, allowed_words, case_insensitive);
    }

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(self->priv->builder->ls_sensitive_list), &iter))
        gtk_tree_selection_select_iter(self->priv->builder->tv_sensitive_sel, &iter);

    return found;
}

static gboolean highlight_forbidden(LibReportWindow *self)
{
    GList *forbidden_words = load_words_from_file(FORBIDDEN_WORDS_BLACKLLIST);
    GList *allowed_words = load_words_from_file(FORBIDDEN_WORDS_WHITELIST);

    const gboolean result = highligh_words_in_tabs(self, forbidden_words, allowed_words, /*case sensitive*/false);

    list_free_with_free(forbidden_words);
    list_free_with_free(allowed_words);

    return result;
}

static void rehighlight_forbidden_words(LibReportWindow *self, int page, GtkTextView *tev)
{
    GList *forbidden_words = load_words_from_file(FORBIDDEN_WORDS_BLACKLLIST);
    GList *allowed_words = load_words_from_file(FORBIDDEN_WORDS_WHITELIST);
    highligh_words_in_textview(self, page, tev, forbidden_words, allowed_words, /*case sensitive*/false);
    list_free_with_free(forbidden_words);
    list_free_with_free(allowed_words);
}

static void on_sensitive_word_selection_changed(GtkTreeSelection *sel, gpointer user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);
    search_item_t *old_word = self->priv->current_highlighted_word;
    self->priv->current_highlighted_word = NULL;

    if (old_word && FALSE == gtk_text_buffer_get_modified(old_word->buffer))
        gtk_text_buffer_remove_tag_by_name(old_word->buffer, "current_result_bg", &(old_word->start), &(old_word->end));

    GtkTreeModel *model;
    GtkTreeIter iter;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter))
        return;

    search_item_t *new_word;
    gtk_tree_model_get(model, &iter,
            SEARCH_COLUMN_ITEM, &new_word,
            -1);

    if (gtk_text_buffer_get_modified(new_word->buffer))
    {
        if (self->priv->search_text == NULL)
            rehighlight_forbidden_words(self, new_word->page, new_word->tev);
        else
        {
            log_notice("searching again: '%s'", self->priv->search_text);
            GList *searched_words = g_list_append(NULL, (gpointer)self->priv->search_text);
            highligh_words_in_textview(self, new_word->page, new_word->tev, searched_words, NULL, /*case insensitive*/true);
            g_list_free(searched_words);
        }

        return;
    }

    self->priv->current_highlighted_word = new_word;

    gtk_notebook_set_current_page(self->priv->builder->notebook, new_word->page);
    gtk_text_buffer_apply_tag_by_name(new_word->buffer, "current_result_bg", &(new_word->start), &(new_word->end));
    gtk_text_buffer_place_cursor(new_word->buffer, &(new_word->start));
    gtk_text_view_scroll_to_iter(new_word->tev, &(new_word->start), 0.0, false, 0, 0);
}

static void highlight_search(LibReportWindow *self)
{
    self->priv->search_text = gtk_entry_get_text(GTK_ENTRY(self->priv->builder->search_entry_bt));

    log_notice("searching: '%s'", self->priv->search_text);
    GList *words = g_list_append(NULL, (gpointer)self->priv->search_text);
    highligh_words_in_tabs(self, words, NULL, /*case insensitive*/true);
    g_list_free(words);
}

static void on_forbidden_words_toggled(GtkToggleButton *btn, gpointer user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);
    self->priv->search_text = NULL;
    log_notice("nothing to search for, highlighting forbidden words instead");
    highlight_forbidden(self);
}

static void on_custom_search_toggled(GtkToggleButton *btn, gpointer user_data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);
    const gboolean custom_search = gtk_toggle_button_get_active(btn);
    gtk_widget_set_sensitive(GTK_WIDGET(self->priv->builder->search_entry_bt), custom_search);

    if (custom_search)
        highlight_search(self);
}

static void on_btn_add_file_clicked(GtkButton *button, LibReportWindow *self)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
            "Attach File",
            GTK_WINDOW(self),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_Open"), GTK_RESPONSE_ACCEPT,
            NULL
    );
    char *filename = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    gtk_widget_destroy(dialog);

    if (filename)
    {
        char *basename = strrchr(filename, '/');
        if (!basename)  /* wtf? (never happens) */
            goto free_and_ret;
        basename++;

        /* TODO: ask for the name to save it as? For now, just use basename */

        char *message = NULL;

        struct stat statbuf;
        if (stat(filename, &statbuf) != 0 || !S_ISREG(statbuf.st_mode))
        {
            message = xasprintf(_("'%s' is not an ordinary file"), filename);
            goto show_msgbox;
        }

        struct problem_item *item = problem_data_get_item_or_NULL(self->priv->problem_data, basename);
        if (!item || (item->flags & CD_FLAG_ISEDITABLE))
        {
            struct dump_dir *dd = wizard_open_directory_for_writing(self);
            if (dd)
            {
                dd_close(dd);
                char *new_name = concat_path_file(self->priv->dump_dir_name, basename);
                if (strcmp(filename, new_name) == 0)
                {
                    message = xstrdup(_("You are trying to copy a file onto itself"));
                }
                else
                {
                    off_t r = copy_file(filename, new_name, 0666);
                    if (r < 0)
                    {
                        message = xasprintf(_("Can't copy '%s': %s"), filename, strerror(errno));
                        unlink(new_name);
                    }
                    if (!message)
                    {
                        lib_report_window_reload_problem_data(self);
                        update_gui_state_from_problem_data(self, /* don't update selected event */ 0);
                        /* Set flags for the new item */
                        update_ls_details_checkboxes(self, self->priv->event_selected);
                    }
                }
                free(new_name);
            }
        }
        else
            message = xasprintf(_("Item '%s' already exists and is not modifiable"), basename);

        if (message)
        {
 show_msgbox: ;
            GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(self),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_WARNING,
                GTK_BUTTONS_CLOSE,
                message);
            free(message);
            gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(self));
            gtk_dialog_run(GTK_DIALOG(dlg));
            gtk_widget_destroy(dlg);
        }
 free_and_ret:
        g_free(filename);
    }
}

static void on_btn_detail_clicked(GtkButton *button, LibReportWindow *self)
{
    GtkWidget *pdd = problem_details_dialog_new(self->priv->problem_data, GTK_WINDOW(self));
    gtk_dialog_run(GTK_DIALOG(pdd));
}

/* [Del] key handling in item list */
static void delete_item(LibReportWindow *self, GtkTreeView *treeview)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(treeview);
    if (selection)
    {
        GtkTreeIter iter;
        GtkTreeModel *store = gtk_tree_view_get_model(treeview);
        if (gtk_tree_selection_get_selected(selection, &store, &iter) == TRUE)
        {
            GValue d_item_name = { 0 };
            gtk_tree_model_get_value(store, &iter, DETAIL_COLUMN_NAME, &d_item_name);
            const char *item_name = g_value_get_string(&d_item_name);
            if (item_name)
            {
                struct problem_item *item = problem_data_get_item_or_NULL(self->priv->problem_data, item_name);
                if (item->flags & CD_FLAG_ISEDITABLE)
                {
//                  GtkTreePath *old_path = gtk_tree_model_get_path(store, &iter);

                    struct dump_dir *dd = wizard_open_directory_for_writing(self);
                    if (dd)
                    {
                        char *filename = concat_path_file(self->priv->dump_dir_name, item_name);
                        unlink(filename);
                        free(filename);
                        dd_close(dd);
                        g_hash_table_remove(self->priv->problem_data, item_name);
                        gtk_list_store_remove(self->priv->builder->ls_details, &iter);
                    }

//                  /* Try to retain the same cursor position */
//                  sanitize_cursor(old_path);
//                  gtk_tree_path_free(old_path);
                }
            }
        }
    }
}

static gint on_key_press_event_in_item_list(GtkTreeView *treeview, GdkEventKey *key, gpointer data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(data);

    int k = key->keyval;

    if (k == GDK_KEY_Delete || k == GDK_KEY_KP_Delete)
    {
        delete_item(self, treeview);
        return TRUE;
    }
    return FALSE;
}

static void on_page_prepare(GtkNotebook *assistant, GtkWidget *page, guint page_no, gpointer user_data)
{
    //int page_no = gtk_assistant_get_current_page(g_assistant);
    //log_ready_state();

    /* This suppresses [Last] button: assistant thinks that
     * we never have this page ready unless we are on it
     * -> therefore there is at least one non-ready page
     * -> therefore it won't show [Last]
     */
    // Doesn't work: if Completeness:[++++++-+++],
    // then [Last] btn will still be shown.
    //gtk_assistant_set_page_complete(g_assistant,
    //            pages[PAGENO_REVIEW_DATA].page_widget,
    //            pages[PAGENO_REVIEW_DATA].page_widget == page
    //);

    /* If processing is finished and if it was terminated because of an error
     * the event progress page is selected. So, it does not make sense to show
     * the next step button and we MUST NOT clear warnings.
     */
    LibReportWindow *self = LIB_REPORT_WINDOW(user_data);

    if (!is_processing_finished(self))
    {
        /* some pages hide it, so restore it to it's default */
        show_next_step_button(self);
        clear_warnings(self);
    }

    gtk_widget_hide(self->priv->builder->btn_detail);
    gtk_widget_hide(self->priv->builder->btn_onfail);
    if (!self->priv->expert_mode)
        gtk_widget_hide(self->priv->builder->btn_repeat);
    /* Save text fields if changed */
    /* Must be called before any GUI operation because the following two
     * functions causes recreating of the text items tabs, thus all updates to
     * these tabs will be lost */
    save_items_from_notepad(self);
    save_text_from_text_view(self, self->priv->builder->tv_comment, FILENAME_COMMENT);

    page_obj_t *pages = self->priv->builder->pages;

    if (pages[PAGENO_EDIT_ELEMENTS].page_widget == page)
    {
        if (highlight_forbidden(self))
        {
            add_sensitive_data_warning(self);
            show_warnings(self);
            gtk_expander_set_expanded(self->priv->builder->exp_search, TRUE);
        }
        else
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->priv->builder->rb_custom_search), TRUE);

        show_warnings(self);
    }

    if (pages[PAGENO_REVIEW_DATA].page_widget == page)
    {
        update_ls_details_checkboxes(self, self->priv->event_selected);
        gtk_widget_set_sensitive(self->priv->builder->btn_next, gtk_toggle_button_get_active(self->priv->builder->tb_approve_bt));
    }

    if (pages[PAGENO_EDIT_COMMENT].page_widget == page)
    {
        gtk_widget_show(self->priv->builder->btn_detail);
        gtk_widget_set_sensitive(self->priv->builder->btn_next, false);
        toggle_eb_comment(self);
    }
    //log_ready_state();

    if (pages[PAGENO_EVENT_PROGRESS].page_widget == page)
    {
        log_info("event_selected:'%s'", self->priv->event_selected);
        if (self->priv->event_selected
         && self->priv->event_selected[0]
        ) {
            clear_warnings(self);
            start_event_run(self, self->priv->event_selected);
        }
    }

    if(pages[PAGENO_EVENT_SELECTOR].page_widget == page)
    {
        if (!self->priv->expert_mode && !self->priv->auto_event_list)
            hide_next_step_button(self);
    }
}

static void on_failed_event(LibReportWindow *self, const char *event_name)
{
    /* Don't show the 'on failure' button if the processed event
     * was started by that button. (avoid infinite loop)
     */
    if (strcmp(event_name, EMERGENCY_ANALYSIS_EVENT_NAME) == 0)
        return;

   add_warning(self,
_("Processing of the problem failed. This can have many reasons but there are three most common:\n"\
"\t▫ <b>network connection problems</b>\n"\
"\t▫ <b>corrupted problem data</b>\n"\
"\t▫ <b>invalid configuration</b>"));

    if (!self->priv->expert_mode)
    {
        add_warning(self,
_("If you want to update the configuration and try to report again, please open <b>Preferences</b> item\n"
"in the application menu and after applying the configuration changes click <b>Repeat</b> button."));
        gtk_widget_show(self->priv->builder->btn_repeat);
    }

    add_warning(self,
_("If you are sure that this problem is not caused by network problems neither by invalid configuration\n"
"and want to help us, please click on the upload button and provide all problem data for a deep analysis.\n"\
"<i>Before you do that, please consider the security risks. Problem data may contain sensitive information like\n"\
"passwords. The uploaded data are stored in a protected storage and only a limited number of persons can read them.</i>"));

    show_warnings(self);

    gtk_widget_show(self->priv->builder->btn_onfail);
}


static gboolean consume_cmd_output(GIOChannel *source, GIOCondition condition, gpointer data)
{
    struct analyze_event_data *evd = data;
    struct run_event_state *run_state = evd->run_state;

    bool stop_requested = false;
    int retval = consume_event_command_output(run_state, evd->window->priv->dump_dir_name);

    if (retval < 0 && errno == EAGAIN)
        /* We got all buffered data, but fd is still open. Done for now */
        return TRUE; /* "please don't remove this event (yet)" */

    /* EOF/error */

    if (WIFEXITED(run_state->process_status)
     && WEXITSTATUS(run_state->process_status) == EXIT_STOP_EVENT_RUN
    ) {
        retval = 0;
        run_state->process_status = 0;
        stop_requested = true;
        terminate_event_chain(evd->window, TERMINATE_NOFLAGS);
    }

    unexport_event_config(evd->env_list);
    evd->env_list = NULL;

    /* Make sure "Cancel" button won't send anything (process is gone) */
    evd->window->priv->event_child_pid = -1;
    evd->run_state->command_pid = -1; /* just for consistency */

    /* Write a final message to the log */
    if (evd->event_log->len != 0 && evd->event_log->buf[evd->event_log->len - 1] != '\n')
        save_to_event_log(evd, "\n");

    /* If program failed, or if it finished successfully without saying anything... */
    if (retval != 0 || evd->event_log_state == LOGSTATE_FIRSTLINE)
    {
        char *msg = exit_status_as_string(evd->event_name, run_state->process_status);
        if (retval != 0)
        {
            /* If program failed, emit *error* line */
            evd->event_log_state = LOGSTATE_ERRLINE;
        }
        append_to_textview(evd->window->priv->builder->tv_event_log, msg);
        save_to_event_log(evd, msg);
        free(msg);
    }

    /* Append log to FILENAME_EVENT_LOG */
    update_event_log_on_disk(evd->window->priv->dump_dir_name, evd->event_log->buf);
    strbuf_clear(evd->event_log);
    evd->event_log_state = LOGSTATE_FIRSTLINE;

    struct dump_dir *dd = NULL;
    if (geteuid() == 0)
    {
        /* Reset mode/uig/gid to correct values for all files created by event run */
        dd = dd_opendir(evd->window->priv->dump_dir_name, 0);
        if (dd)
            dd_sanitize_mode_and_owner(dd);
    }

    if (retval == 0 && !evd->window->priv->expert_mode)
    {
        /* Check whether NOT_REPORTABLE element appeared. If it did, we'll stop
         * even if exit code is "success".
         */
        if (!dd) /* why? because dd may be already open by the code above */
            dd = dd_opendir(evd->window->priv->dump_dir_name, DD_OPEN_READONLY | DD_FAIL_QUIETLY_EACCES);
        if (!dd)
            xfunc_die();
        char *not_reportable = dd_load_text_ext(dd, FILENAME_NOT_REPORTABLE, 0
                                            | DD_LOAD_TEXT_RETURN_NULL_ON_FAILURE
                                            | DD_FAIL_QUIETLY_ENOENT
                                            | DD_FAIL_QUIETLY_EACCES);
        if (not_reportable)
            retval = 256;
        free(not_reportable);
    }
    if (dd)
        dd_close(dd);

    /* Stop if exit code is not 0, or no more commands */
    if (stop_requested
     || retval != 0
     || spawn_next_command_in_evd(evd) < 0
    ) {
        log_notice("done running event on '%s': %d", evd->window->priv->dump_dir_name, retval);
        append_to_textview(evd->window->priv->builder->tv_event_log, "\n");

        /* Hide spinner and stop btn */
        gtk_widget_hide(GTK_WIDGET(evd->window->priv->builder->spinner_event_log));
        gtk_widget_hide(evd->window->priv->builder->btn_stop);
        /* Enable (un-gray out) navigation buttons */
        gtk_widget_set_sensitive(evd->window->priv->builder->btn_close, true);
        gtk_widget_set_sensitive(evd->window->priv->builder->btn_next, true);

        lib_report_window_reload_problem_data(evd->window);
        update_gui_state_from_problem_data(evd->window, UPDATE_SELECTED_EVENT);

        if (retval != 0)
        {
            gtk_widget_show(GTK_WIDGET(evd->window->priv->builder->img_process_fail));
            /* 256 means NOT_REPORTABLE */
            if (retval == 256)
                cancel_processing(evd->window, _("Processing was interrupted because the problem is not reportable."), TERMINATE_NOFLAGS);
            else
            {
                /* We use SIGTERM to stop event processing on user's request.
                 * So SIGTERM is not a failure.
                 */
                if (retval == EXIT_CANCEL_BY_USER || WTERMSIG(run_state->process_status) == SIGTERM)
                    cancel_processing(evd->window, /* default message */ NULL, TERMINATE_NOFLAGS);
                else
                {
                    cancel_processing(evd->window, _("Processing failed."), TERMINATE_WITH_RERUN);
                    on_failed_event(evd->window, evd->event_name);
                }
            }
        }
        else
        {
            gtk_label_set_text(evd->window->priv->builder->lbl_event_log,
                    is_processing_finished(evd->window)
                    ? _("Processing finished.") : _("Processing finished, please proceed to the next step."));
        }

        g_source_remove(evd->window->priv->event_source_id);
        evd->window->priv->event_source_id = 0;
        close(evd->fd);
        g_io_channel_unref(evd->channel);
        free_run_event_state(evd->run_state);
        strbuf_free(evd->event_log);
        free(evd->event_name);

        /* Inform abrt-gui that it is a good idea to rescan the directory */
        kill(getppid(), SIGCHLD);

        if (is_processing_finished(evd->window))
            hide_next_step_button(evd->window);
        else if (retval == 0 && !g_verbose && !evd->window->priv->expert_mode)
            on_btn_next_clicked(GTK_BUTTON(evd->window->priv->builder->btn_next), evd->window);

        free(evd);

        return FALSE; /* "please remove this event" */
    }

    /* New command was started. Continue waiting for input */

    /* Transplant cmd's output fd onto old one, so that main loop
     * is none the wiser that fd it waits on has changed
     */
    xmove_fd(evd->run_state->command_out_fd, evd->fd);
    evd->run_state->command_out_fd = evd->fd; /* just to keep it consistent */
    ndelay_on(evd->fd);

    /* Revive "Cancel" button */
    evd->window->priv->event_child_pid = evd->run_state->command_pid;

    return TRUE; /* "please don't remove this event (yet)" */
}

/*
 * https://bugzilla.redhat.com/show_bug.cgi?id=1044653
 */
static void
correct_bz_private_goup_name(LibReportWindow *self, const char *event_name)
{
    if (strcmp("report_Bugzilla", event_name) == 0 &&
        gtk_toggle_button_get_active(self->priv->builder->tbtn_private_ticket))
    {
        event_config_t *cfg = get_event_config(event_name);
        if (NULL != cfg)
        {
            GList *item = cfg->options;
            for ( ; item != NULL; item = g_list_next(item))
            {
                event_option_t *opt = item->data;
                if (strcmp("Bugzilla_PrivateGroups", opt->eo_name) == 0
                    && opt->eo_value
                    && strcmp(opt->eo_value, "private") == 0
                    && ask_replace_old_private_group_name(GTK_WINDOW(self)))
                {
                    free(opt->eo_value);
                    opt->eo_value = xstrdup("fedora_contrib_private");
                }
            }
        }
    }
}

static void start_event_run(LibReportWindow *self, const char *event_name)
{
    /* Start event asynchronously on the dump dir
     * (synchronous run would freeze GUI until completion)
     */

    /* https://bugzilla.redhat.com/show_bug.cgi?id=1044653 */
    correct_bz_private_goup_name(self, event_name);

    struct run_event_state *state = new_run_event_state();
    state->logging_callback = run_event_gtk_logging;
    state->error_callback = run_event_gtk_error;
    state->alert_callback = run_event_gtk_alert;
    state->ask_callback = run_event_gtk_ask;
    state->ask_yes_no_callback = run_event_gtk_ask_yes_no;
    state->ask_yes_no_yesforever_callback = run_event_gtk_ask_yes_no_yesforever;
    state->ask_password_callback = run_event_gtk_ask_password;

    if (prepare_commands(state, self->priv->dump_dir_name, event_name) == 0)
    {
 no_cmds:
        /* No commands needed?! (This is untypical) */
        free_run_event_state(state);
//TODO: better msg?
        char *msg = xasprintf(_("No processing for event '%s' is defined"), event_name);
        append_to_textview(self->priv->builder->tv_event_log, msg);
        free(msg);
        cancel_processing(self, _("Processing failed."), TERMINATE_NOFLAGS);
        return;
    }

    struct dump_dir *dd = wizard_open_directory_for_writing(self);
    dd_close(dd);
    if (!dd)
    {
        free_run_event_state(state);
        if (!self->priv->expert_mode)
        {
            cancel_processing(self, _("Processing interrupted: can't continue without writable directory."), TERMINATE_NOFLAGS);
        }
        return; /* user refused to steal, or write error, etc... */
    }

    set_excluded_envvar(self->priv->builder->ls_details);
    GList *env_list = export_event_config(event_name);

    if (spawn_next_command(state, self->priv->dump_dir_name, event_name, EXECFLG_SETPGID) < 0)
    {
        unexport_event_config(env_list);
        goto no_cmds;
    }
    self->priv->event_child_pid = state->command_pid;

    /* At least one command is needed, and we started first one.
     * Hook its output fd to the main loop.
     */
    struct analyze_event_data *evd = xzalloc(sizeof(*evd));
    evd->window = self;
    evd->run_state = state;
    evd->event_name = xstrdup(event_name);
    evd->env_list = env_list;
    evd->event_log = strbuf_new();
    evd->fd = state->command_out_fd;

    state->logging_param = evd;
    state->error_param = evd;
    state->interaction_param = evd;

    ndelay_on(evd->fd);
    evd->channel = g_io_channel_unix_new(evd->fd);
    self->priv->event_source_id = g_io_add_watch(evd->channel,
            G_IO_IN | G_IO_ERR | G_IO_HUP, /* need HUP to detect EOF w/o any data */
            consume_cmd_output,
            evd
    );

    gtk_label_set_text(self->priv->builder->lbl_event_log, _("Processing..."));
    log_notice("running event '%s' on '%s'", event_name, self->priv->dump_dir_name);
    char *msg = xasprintf("--- Running %s ---\n", event_name);
    append_to_textview(self->priv->builder->tv_event_log, msg);
    free(msg);

    /* don't bother testing if they are visible, this is faster */
    gtk_widget_hide(GTK_WIDGET(self->priv->builder->img_process_fail));

    gtk_widget_show(GTK_WIDGET(self->priv->builder->spinner_event_log));
    gtk_widget_show(self->priv->builder->btn_stop);
    /* Disable (gray out) navigation buttons */
    gtk_widget_set_sensitive(self->priv->builder->btn_close, false);
    gtk_widget_set_sensitive(self->priv->builder->btn_next, false);
}

static void on_btn_close_clicked(void *obj, void *data)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(data);

    gtk_widget_destroy(GTK_WIDGET(self));
}

static void on_btn_startcast_clicked(GtkWidget *btn, LibReportWindow *self)
{
    const char *args[15];
    args[0] = (char *) "fros";
    args[1] = NULL;

    pid_t castapp = 0;
    castapp = fork_execv_on_steroids(
                EXECFLG_QUIET,
                (char **)args,
                NULL,
                /*env_vec:*/ NULL,
                self->priv->dump_dir_name,
                /*uid (ignored):*/ 0
    );
    gtk_widget_hide(GTK_WIDGET(self));
    /*flush all gui events before we start waitpid
     * otherwise the main window wouldn't hide
     */
    while (gtk_events_pending())
        gtk_main_iteration_do(false);

    int status;
    safe_waitpid(castapp, &status, 0);
    lib_report_window_reload_problem_data(self);
    update_gui_state_from_problem_data(self, 0 /* don't update the selected event */);
    gtk_widget_show(GTK_WIDGET(self));
}

static bool is_screencast_available(const char *dump_dir_name)
{
    const char *args[3];
    args[0] = (char *) "fros";
    args[1] = "--is-available";
    args[2] = NULL;

    pid_t castapp = 0;
    castapp = fork_execv_on_steroids(
                EXECFLG_QUIET,
                (char **)args,
                NULL,
                /*env_vec:*/ NULL,
                dump_dir_name,
                /*uid (ignored):*/ 0
    );

    int status;
    safe_waitpid(castapp, &status, 0);

    /* 0 means that it's available */
    return status == 0;
}

/* Looks at all tags covering the position of iter in the text view,
 * and if one of them is a link, follow it by showing the page identified
 * by the data attached to it.
 */
static void open_browse_if_link(GtkWidget *text_view, GtkTextIter *iter)
{
    GSList *tags = NULL, *tagp = NULL;

    tags = gtk_text_iter_get_tags (iter);
    for (tagp = tags;  tagp != NULL;  tagp = tagp->next)
    {
        GtkTextTag *tag = tagp->data;
        const char *url = g_object_get_data (G_OBJECT (tag), "url");

        if (url != 0)
        {
            /* http://techbase.kde.org/KDE_System_Administration/Environment_Variables#KDE_FULL_SESSION */
            if (getenv("KDE_FULL_SESSION") != NULL)
            {
                gint exitcode;
                gchar *arg[3];
                /* kde-open is from kdebase-runtime, it should be there. */
                arg[0] = (char *) "kde-open";
                arg[1] = (char *) url;
                arg[2] = NULL;

                const gboolean spawn_ret = g_spawn_sync(NULL, arg, NULL,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                                 NULL, NULL, NULL, NULL, &exitcode, NULL);

                if (spawn_ret)
                    break;
            }

            GError *error = NULL;
            if (!gtk_show_uri(/* use default screen */ NULL, url, GDK_CURRENT_TIME, &error))
                error_msg("Can't open url '%s': %s", url, error->message);

            break;
        }
    }

    if (tags)
        g_slist_free (tags);
}

/* Links can be activated by pressing Enter.
 */
static gboolean key_press_event(GtkWidget *text_view, GdkEventKey *event)
{
    GtkTextIter iter;
    GtkTextBuffer *buffer;

    switch (event->keyval)
    {
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW (text_view));
            gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                    gtk_text_buffer_get_insert(buffer));
            open_browse_if_link(text_view, &iter);
            break;

        default:
            break;
    }

    return FALSE;
}

/* Links can also be activated by clicking.
 */
static gboolean event_after(GtkWidget *text_view, GdkEvent *ev)
{
    GtkTextIter start, end, iter;
    GtkTextBuffer *buffer;
    GdkEventButton *event;
    gint x, y;

    if (ev->type != GDK_BUTTON_RELEASE)
        return FALSE;

    event = (GdkEventButton *)ev;

    if (event->button != GDK_BUTTON_PRIMARY)
        return FALSE;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

    /* we shouldn't follow a link if the user has selected something */
    gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
    if (gtk_text_iter_get_offset(&start) != gtk_text_iter_get_offset(&end))
        return FALSE;

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW (text_view),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          event->x, event->y, &x, &y);

    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW (text_view), &iter, x, y);

    open_browse_if_link(text_view, &iter);

    return FALSE;
}

/* Looks at all tags covering the position (x, y) in the text view,
 * and if one of them is a link, change the cursor to the "hands" cursor
 * typically used by web browsers.
 */
static void set_cursor_if_appropriate(GtkTextView *text_view,
                                      gint x,
                                      gint y)
{
    GSList *tags = NULL, *tagp = NULL;
    GtkTextIter iter;
    gboolean hovering = FALSE;

    gtk_text_view_get_iter_at_location(text_view, &iter, x, y);

    tags = gtk_text_iter_get_tags(&iter);
    for (tagp = tags; tagp != NULL; tagp = tagp->next)
    {
        GtkTextTag *tag = tagp->data;
        gpointer url = g_object_get_data(G_OBJECT (tag), "url");

        if (url != 0)
        {
            hovering = TRUE;
            break;
        }
    }

    if (hovering != hovering_over_link)
    {
        hovering_over_link = hovering;

        if (hovering_over_link)
            gdk_window_set_cursor(gtk_text_view_get_window(text_view, GTK_TEXT_WINDOW_TEXT), g_hand_cursor);
        else
            gdk_window_set_cursor(gtk_text_view_get_window(text_view, GTK_TEXT_WINDOW_TEXT), g_regular_cursor);
    }

    if (tags)
        g_slist_free (tags);
}


/* Update the cursor image if the pointer moved.
 */
static gboolean motion_notify_event(GtkWidget *text_view, GdkEventMotion *event)
{
    gint x, y;

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          event->x, event->y, &x, &y);

    set_cursor_if_appropriate(GTK_TEXT_VIEW(text_view), x, y);
    return FALSE;
}

/* Also update the cursor image if the window becomes visible
 * (e.g. when a window covering it got iconified).
 */
static gboolean visibility_notify_event(GtkWidget *text_view, GdkEventVisibility *event)
{
    gint wx, wy, bx, by;

    GdkWindow *win = gtk_text_view_get_window(GTK_TEXT_VIEW(text_view), GTK_TEXT_WINDOW_TEXT);
    GdkDeviceManager *device_manager = gdk_display_get_device_manager(gdk_window_get_display (win));
    GdkDevice *pointer = gdk_device_manager_get_client_pointer(device_manager);
    gdk_window_get_device_position(gtk_widget_get_window(text_view), pointer, &wx, &wy, NULL);

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(text_view),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          wx, wy, &bx, &by);

    set_cursor_if_appropriate(GTK_TEXT_VIEW (text_view), bx, by);

    return FALSE;
}

static void lib_report_window_finalize(GObject *object);

static void
lib_report_window_class_init(LibReportWindowClass *klass)
{
    lib_report_window_gresource_register_resource();

    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = lib_report_window_finalize;

    g_type_class_add_private(klass, sizeof(LibReportWindowPrivate));

    g_monospace_font = pango_font_description_from_string("monospace");
    g_hand_cursor = gdk_cursor_new (GDK_HAND2);
    g_regular_cursor = gdk_cursor_new (GDK_XTERM);
}

static void
lib_report_window_finalize(GObject *object)
{
    LibReportWindow *self = LIB_REPORT_WINDOW(object);

    problem_data_free(self->priv->problem_data);
    free(self->priv->dump_dir_name);
    g_list_free_full(self->priv->auto_event_list, free);

    g_object_unref(self->priv->builder->gtk_builder);

    /* TODO : free */
    free(self->priv->builder);

    /* Suppress execution of consume_cmd_output() */
    if (self->priv->event_source_id != 0)
    {
        g_source_remove(self->priv->event_source_id);
        self->priv->event_source_id = 0;
    }

    cancel_event_run(self);

    if (self->priv->loaded_texts)
    {
        g_hash_table_destroy(self->priv->loaded_texts);
        self->priv->loaded_texts = NULL;
    }
}

static void
lib_report_window_init(LibReportWindow *self)
{
    self->priv = LIB_REPORT_WINDOW_GET_PRIVATE(self);
    self->priv->loaded_texts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    self->priv->builder = lib_report_window_builder_new();
    if (self->priv->builder == NULL)
        error_msg_and_die(_("Could not initialize GUI"));

    gtk_container_add(GTK_CONTAINER(self), GTK_WIDGET(self->priv->builder->box_assistant));

    gtk_window_set_default_size(GTK_WINDOW(self), DEFAULT_WIDTH, DEFAULT_HEIGHT);

    g_signal_connect_swapped(self->priv->builder->cb_no_comment, "toggled", G_CALLBACK(toggle_eb_comment), self);
    g_signal_connect(self->priv->builder->rb_forbidden_words, "toggled", G_CALLBACK(on_forbidden_words_toggled), self);
    g_signal_connect(self->priv->builder->rb_custom_search, "toggled", G_CALLBACK(on_custom_search_toggled), self);
    g_signal_connect(self->priv->builder->tv_details, "key-press-event", G_CALLBACK(on_key_press_event_in_item_list), self);
    self->priv->tv_sensitive_sel_hndlr = g_signal_connect(self->priv->builder->tv_sensitive_sel,
            "changed", G_CALLBACK(on_sensitive_word_selection_changed), self);

    create_details_treeview(self);

    g_signal_connect(self->priv->builder->btn_close, "clicked", G_CALLBACK(on_btn_close_clicked), self);
    g_signal_connect_swapped(self->priv->builder->btn_stop, "clicked", G_CALLBACK(cancel_event_run), self);
    g_signal_connect(self->priv->builder->btn_onfail, "clicked", G_CALLBACK(on_btn_failed_clicked), self);
    g_signal_connect(self->priv->builder->btn_repeat, "clicked", G_CALLBACK(on_btn_repeat_clicked), self);
    g_signal_connect(self->priv->builder->btn_next, "clicked", G_CALLBACK(on_btn_next_clicked), self);

    g_signal_connect(self->priv->builder->assistant, "switch-page", G_CALLBACK(on_page_prepare), self);

    g_signal_connect(self->priv->builder->tb_approve_bt, "toggled", G_CALLBACK(on_bt_approve_toggled), self);
    g_signal_connect_swapped(gtk_text_view_get_buffer(self->priv->builder->tv_comment), "changed", G_CALLBACK(toggle_eb_comment), self);

    g_signal_connect(self->priv->builder->btn_add_file, "clicked", G_CALLBACK(on_btn_add_file_clicked), self);
    g_signal_connect(self->priv->builder->btn_detail, "clicked", G_CALLBACK(on_btn_detail_clicked), self);

    if (is_screencast_available(self->priv->dump_dir_name)) {
        /* we need to override the activate-link handler, because we use
         * the link button instead of normal button and if we wouldn't override it
         * gtk would try to run it's defualt action and open the associated URI
         * but since the URI is empty it would complain about it...
         */
        g_signal_connect(self->priv->builder->btn_startcast, "activate-link", G_CALLBACK(on_btn_startcast_clicked), self);
    }
    else {
        gtk_widget_set_sensitive(GTK_WIDGET(self->priv->builder->btn_startcast), false);
        gtk_widget_set_tooltip_markup(GTK_WIDGET(self->priv->builder->btn_startcast),
          _("In order to enable the built-in screencasting "
            "functionality the package fros-recordmydesktop has to be installed. "
            "Please run the following command if you want to install it."
            "\n\n"
            "<b>su -c \"yum install fros-recordmydesktop\"</b>"
            ));
    }

    g_signal_connect_swapped(self->priv->builder->search_entry_bt, "search-changed", G_CALLBACK(highlight_search), self);

    g_signal_connect(self->priv->builder->tv_event_log, "key-press-event", G_CALLBACK (key_press_event), self);
    g_signal_connect(self->priv->builder->tv_event_log, "event-after", G_CALLBACK (event_after), self);
    g_signal_connect(self->priv->builder->tv_event_log, "motion-notify-event", G_CALLBACK (motion_notify_event), self);
    g_signal_connect(self->priv->builder->tv_event_log, "visibility-notify-event", G_CALLBACK (visibility_notify_event), self);
    g_signal_connect(gtk_text_view_get_buffer(self->priv->builder->tv_event_log), "changed", G_CALLBACK (on_log_changed), self);

    /* switch to right starting page */
#if 0
    if (!self->priv->expert_mode)
    {
        /* Skip intro screen */
        int n = select_next_page_no(self, self->priv->builder->pages[PAGENO_SUMMARY].page_no, NULL);
        log_info("switching to page_no:%d", n);
        gtk_notebook_set_current_page(self->priv->builder->assistant, n);
    }
    else
#endif
        on_page_prepare(self->priv->builder->assistant, gtk_notebook_get_nth_page(self->priv->builder->assistant, 0), 0, self);
}

LibReportWindow *
lib_report_window_new_for_dir(GtkApplication *app, const char *dump_dir_name)
{
    INITIALIZE_LIBREPORT();

    GObject *object = g_object_new(TYPE_LIB_REPORT_WINDOW, NULL);
    LibReportWindow *self = LIB_REPORT_WINDOW(object);

    self->priv->dump_dir_name = xstrdup(dump_dir_name);

    lib_report_window_reload_problem_data(self);

    ProblemDetailsWidget *details = problem_details_widget_new(self->priv->problem_data);
    gtk_container_add(GTK_CONTAINER(self->priv->builder->container_details1), GTK_WIDGET(details));

    update_gui_state_from_problem_data(self, UPDATE_SELECTED_EVENT);

    gtk_widget_show_all(GTK_WIDGET(self));

    return self;
}

void
lib_report_window_reload_problem_data(LibReportWindow *self)
{
    /* TODO : free(g_events); */

    struct dump_dir *dd = dd_opendir(self->priv->dump_dir_name, DD_OPEN_READONLY);
    if (!dd)
        xfunc_die(); /* dd_opendir already logged error msg */

    problem_data_t *new_cd = create_problem_data_from_dump_dir(dd);
    problem_data_add_text_noteditable(new_cd, CD_DUMPDIR, self->priv->dump_dir_name);

    /* TODO : g_events = list_possible_events(dd, NULL, ""); */
    dd_close(dd);

    /* Copy "selected for reporting" flags */
    GHashTableIter iter;
    char *name;
    struct problem_item *new_item;
    g_hash_table_iter_init(&iter, new_cd);
    while (g_hash_table_iter_next(&iter, (void**)&name, (void**)&new_item))
    {
        struct problem_item *old_item = self->priv->problem_data ? problem_data_get_item_or_NULL(self->priv->problem_data, name) : NULL;
        if (old_item)
        {
            new_item->selected_by_user = old_item->selected_by_user;
            new_item->allowed_by_reporter = old_item->allowed_by_reporter;
            new_item->default_by_reporter = old_item->default_by_reporter;
            new_item->required_by_reporter = old_item->required_by_reporter;
        }
        else
        {
            new_item->selected_by_user = 0;
            new_item->allowed_by_reporter = 0;
            new_item->default_by_reporter = 0;
            new_item->required_by_reporter = 0;
        }
        //log("%s: was ->selected_by_user=%d", __func__, new_item->selected_by_user);
    }

    problem_data_free(self->priv->problem_data);
    self->priv->problem_data = new_cd;
}

void
lib_report_window_set_expert_mode(LibReportWindow *self, gboolean expert_mode)
{
    self->priv->expert_mode = expert_mode;

    /* Show tabs only in verbose expert mode
     *
     * It is safe to let users randomly switch tabs only in expert mode because
     * in all other modes a data for the selected page may not be ready and it
     * will probably cause unexpected behaviour like crash.
     */
    gtk_notebook_set_show_tabs(self->priv->builder->assistant, (g_verbose != 0 && expert_mode));

    add_event_buttons(self);
}

void
lib_report_window_set_event_list(LibReportWindow *self, GList *event_list)
{
    g_list_free_full(self->priv->auto_event_list, free);
    self->priv->auto_event_list = event_list;

    gint current_page_no = gtk_notebook_get_current_page(self->priv->builder->assistant);
    gint next_page_no = select_next_page_no(self, current_page_no, NULL);

    /* if pageno is not change 'switch-page' signal is not emitted */
    if (current_page_no == next_page_no)
        on_page_prepare(self->priv->builder->assistant, gtk_notebook_get_nth_page(self->priv->builder->assistant, next_page_no), next_page_no, self);
    else
        gtk_notebook_set_current_page(self->priv->builder->assistant, next_page_no);

    /* Show Next Step button which was hidden on Selector page in non-expert
     * mode. Next Step button must be hidden because Selector page shows only
     * workflow buttons in non-expert mode.
     */
    show_next_step_button(self);
}
