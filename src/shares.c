/* shares.c
 *
 * Copyright (C) 2012 Paddubsky A.V. <pan.pav.7c5@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <glib/gprintf.h>
#include <glib/gi18n.h>
#include <string.h>

#include "shares.h"
#include "configuration.h"

/* Variables */

GreeterData greeter = {NULL, };
const gchar* const USER_GUEST           = "*guest";
const gchar* const USER_OTHER           = "*other";
const gchar* const APP_NAME             = "lightdm-another-gtk-greeter";
const gchar* const DEFAULT_USER_ICON    = "avatar-default";
const gchar* const ACTION_TEXT_LOGIN    = N_("Login");
const gchar* const ACTION_TEXT_UNLOCK   = N_("Unlock");

const WindowPosition WINDOW_POSITION_CENTER =
{
    .x_is_absolute = FALSE, .x = 50,
    .y_is_absolute = FALSE, .y = 50,
    .anchor = {.width=0, .height=0}
};

const WindowPosition WINDOW_POSITION_TOP =
{
    .x_is_absolute = FALSE, .x = 50,
    .y_is_absolute = TRUE,  .y = 0,
    .anchor = {.width=0, .height=-1}
};

const WindowPosition WINDOW_POSITION_BOTTOM =
{
    .x_is_absolute = FALSE, .x = 50,
    .y_is_absolute = FALSE, .y = 100,
    .anchor = {.width=0, .height=1}
};

#ifdef _DEBUG_
gchar* GETTEXT_PACKAGE = "lightdm-another-gtk-greeter";
gchar* LOCALE_DIR = "/usr/share/locale";
gchar* GREETER_DATA_DIR = "../../data";
gchar* CONFIG_FILE = "../../data/lightdm-another-gtk-greeter.dev.conf";
gchar* PACKAGE_VERSION = "<DEBUG>";
#endif

typedef struct
{
    GMainLoop* loop;
    gint response;
    gint cancel_id;
} MessageBoxRunInfo;

typedef struct
{
    MessageBoxRunInfo* info;
    gint id;
} MessageBoxButtonRunInfo;

typedef struct
{
    GtkWidget* owner;
    GtkWidget* label;
    GtkWidget* active;
    gint model_column;
    GtkTreeModel* model;
    GtkMenuShell* menu;
    GSList* menu_group;
    SetWidgetLabelFunc set_owner_label;
    GCallback on_changed;
} MenuBinding;

/* Static functions */

static void stop_messagebox_loop            (MessageBoxRunInfo* info,
                                             gint response);
static gboolean on_messagebox_key_press     (GtkWidget* widget,
                                             GdkEventKey* event,
                                             MessageBoxRunInfo* info);
static void on_messagebox_button_clicked    (GtkWidget* widget,
                                             MessageBoxButtonRunInfo* button_info);

/* For g_slist_find_custom */
static gint compare_menu_widget_item_path   (GObject* obj,
                                             GtkTreePath* path);
static void on_menu_widget_row_changed      (GtkTreeModel* model,
                                             GtkTreePath* path,
                                             GtkTreeIter* iter,
                                             MenuBinding* binding);
static void on_menu_widget_row_deleted      (GtkTreeModel* model,
                                             GtkTreePath* path,
                                             MenuBinding* binding);
static gboolean on_menu_widget_row_inserted (GtkTreeModel* model,
                                             GtkTreePath* path,
                                             GtkTreeIter* iter,
                                             MenuBinding* binding);
static void on_menu_widget_item_toggled     (GtkWidget* widget,
                                             MenuBinding* binding);

static const gchar* MENU_WIDGET_BINDING_PROP = "model-binding";
static const gchar* MENU_ITEM_PATH_PROP      = "model-path";

/* ---------------------------------------------------------------------------*
 * Definitions: public
 * -------------------------------------------------------------------------- */

void show_message_dialog(GtkMessageType type,
                         const gchar* title,
                         const gchar* message_format,
                         ...)
{
    gchar* message;
    va_list argptr;
    va_start(argptr, message_format);
    g_vasprintf(&message, message_format, argptr);
    va_end(argptr);

    GtkWidget* dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", message);
    g_free(message);

    const gchar* window_name = NULL;
    switch(type)
    {
        case GTK_MESSAGE_INFO: window_name = "dialog_window_info"; break;
        case GTK_MESSAGE_WARNING: window_name = "dialog_window_warning"; break;
        case GTK_MESSAGE_QUESTION: window_name = "dialog_window_question"; break;
        case GTK_MESSAGE_ERROR: window_name = "dialog_window_error"; break;
        default:
            window_name = "dialog_window";
    }
    gtk_widget_hide(greeter.ui.screen_layout);
    gtk_widget_set_name(dialog, window_name);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    set_window_position(dialog, &WINDOW_POSITION_CENTER);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    gtk_widget_show(greeter.ui.screen_layout);
    focus_main_window();
}

gint show_message(const gchar* title,
                  const gchar* message_format,
                  const gchar* icon_name,
                  const gchar* icon_stock,
                  const MessageButtonOptions* buttons,
                  gint default_id,
                  gint cancel_id,
                  ...)
{
    gchar* message;
    gulong key_press_handler;
    MessageBoxRunInfo info = (MessageBoxRunInfo)
    {
        .loop = g_main_loop_new(NULL, FALSE),
        .cancel_id = cancel_id
    };
    GSList* buttons_info = NULL;

    va_list argptr;
    va_start(argptr, cancel_id);
    g_vasprintf(&message, _(message_format), argptr);
    va_end(argptr);

    clear_container(GTK_CONTAINER(greeter.ui.messagebox_buttons));
    for(const MessageButtonOptions* button = buttons; button->id != GTK_RESPONSE_NONE; ++button)
    {
        GtkWidget* widget;
        if(button->stock)
            widget = gtk_button_new_from_stock(button->stock);
        else
        {
            widget = gtk_button_new_with_label(_(button->text));
            if(button->text_stock_icon)
                gtk_button_set_image(GTK_BUTTON(widget), gtk_image_new_from_icon_name(button->text_stock_icon, GTK_ICON_SIZE_BUTTON));
        }
        gtk_widget_show(widget);
        gtk_container_add(GTK_CONTAINER(greeter.ui.messagebox_buttons), widget);
        if(default_id == button->id)
            gtk_widget_grab_focus(widget);

        MessageBoxButtonRunInfo* button_info = g_malloc(sizeof(MessageBoxButtonRunInfo));
        buttons_info = g_slist_append(buttons_info, button_info);
        button_info->info = &info;
        button_info->id = button->id;
        g_signal_connect(widget, "clicked", G_CALLBACK(on_messagebox_button_clicked), button_info);
    }

    set_widget_text(greeter.ui.messagebox_title, _(title));
    set_widget_text(greeter.ui.messagebox_text, message);
    if(icon_name)
        gtk_image_set_from_icon_name(GTK_IMAGE(greeter.ui.messagebox_icon), icon_name, GTK_ICON_SIZE_DIALOG);
    else if(icon_stock)
        gtk_image_set_from_stock(GTK_IMAGE(greeter.ui.messagebox_icon), icon_stock, GTK_ICON_SIZE_DIALOG);

    key_press_handler = g_signal_connect(greeter.ui.messagebox_content, "key-press-event",
                                         G_CALLBACK(on_messagebox_key_press), &info);

    gtk_widget_hide(greeter.ui.main_layout);
    gtk_widget_show(greeter.ui.messagebox_layout);
    gtk_widget_set_sensitive(greeter.ui.panel_layout, FALSE);

    g_main_loop_run(info.loop);

    g_signal_handler_disconnect(greeter.ui.messagebox_content, key_press_handler);
    g_main_loop_unref(info.loop);
    g_slist_free_full(buttons_info, g_free);

    gtk_widget_hide(greeter.ui.messagebox_layout);
    gtk_widget_show(greeter.ui.main_layout);
    gtk_widget_set_sensitive(greeter.ui.panel_layout, TRUE);
    focus_main_window();
    return info.response;
}

void rearrange_grid_child(GtkGrid* grid,
                          GtkWidget* child,
                          gint row)
{
    gtk_container_remove(GTK_CONTAINER(grid), child);
    gtk_grid_attach(grid, child, 0, row, 1, 1);
}

void set_window_position(GtkWidget* window,
                         const WindowPosition* p)
{
    GdkScreen* screen = gtk_window_get_screen(GTK_WINDOW(window));
    GtkRequisition size;
    GdkRectangle geometry;
    gint dx, dy;

    gdk_screen_get_monitor_geometry(screen, gdk_screen_get_primary_monitor(screen), &geometry);
    gtk_widget_get_preferred_size(window, NULL, &size);

    dx = !p->x_is_absolute ? geometry.width*p->x/100.0  : (p->x < 0) ? geometry.width + p->x  : p->x;
    dy = !p->y_is_absolute ? geometry.height*p->y/100.0 : (p->y < 0) ? geometry.height + p->y : p->y;

    dx -= (p->anchor.width == 0) ? size.width/2 : (p->anchor.width > 0) ? size.width : 0;
    dy -= (p->anchor.height == 0) ? size.height/2 : (p->anchor.height > 0) ? size.height : 0;

    gtk_window_move(GTK_WINDOW(window), geometry.x + dx, geometry.y + dy);
}

void set_widget_text(GtkWidget* widget,
                     const gchar* text)
{
    if(GTK_IS_MENU_ITEM(widget))
        gtk_menu_item_set_label(GTK_MENU_ITEM(widget), text);
    else if(GTK_IS_BUTTON(widget))
        gtk_button_set_label(GTK_BUTTON(widget), text);
    else if(GTK_IS_LABEL(widget))
        gtk_label_set_label(GTK_LABEL(widget), text);
    else if(GTK_IS_ENTRY(widget))
        gtk_entry_set_text(GTK_ENTRY(widget), text);
    else g_return_if_reached();
}

GtkListStore* get_widget_model(GtkWidget* widget)
{
    if(GTK_IS_COMBO_BOX(widget))
        return GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(widget)));
    if(GTK_IS_TREE_VIEW(widget))
        return GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));
    if(GTK_IS_ICON_VIEW(widget))
        return GTK_LIST_STORE(gtk_icon_view_get_model(GTK_ICON_VIEW(widget)));
    if(IS_MENU_WIDGET(widget))
        return GTK_LIST_STORE(get_menu_widget_model(widget));
    g_return_val_if_reached(NULL);
}

gchar* get_widget_selection_str(GtkWidget* widget,
                                gint column,
                                const gchar* default_value)
{
    GtkTreeIter iter;
    if(!get_widget_active_iter(widget, &iter))
        return default_value ? g_strdup(default_value) : NULL;
    gchar* value;
    gtk_tree_model_get(GTK_TREE_MODEL(get_widget_model(widget)), &iter, column, &value, -1);
    return value;
}

GdkPixbuf* get_widget_selection_image(GtkWidget* widget,
                                      gint column,
                                      GdkPixbuf* default_value)
{
    GtkTreeIter iter;
    if(!get_widget_active_iter(widget, &iter))
        return default_value;
    GdkPixbuf* value;
    gtk_tree_model_get(GTK_TREE_MODEL(get_widget_model(widget)), &iter, column, &value, -1);
    return value;
}

gint get_widget_selection_int(GtkWidget* widget,
                              gint column,
                              gint default_value)
{
    GtkTreeIter iter;
    if(get_widget_active_iter(widget, &iter))
        gtk_tree_model_get(GTK_TREE_MODEL(get_widget_model(widget)), &iter, column, &default_value, -1);
    return default_value;
}

gboolean get_widget_active_iter(GtkWidget* widget,
                                GtkTreeIter* iter)
{
    if(GTK_IS_COMBO_BOX(widget))
        return gtk_combo_box_get_active_iter(GTK_COMBO_BOX(widget), iter);
    if(GTK_IS_TREE_VIEW(widget))
        return gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), NULL, iter);
    if(GTK_IS_ICON_VIEW(widget))
    {
        gboolean ok = FALSE;
        GList* selection = gtk_icon_view_get_selected_items(GTK_ICON_VIEW(widget));
        if(g_list_first(selection) != NULL)
        {
            GtkTreePath* path = (GtkTreePath*)g_list_first(selection)->data;
            ok = gtk_tree_model_get_iter(gtk_icon_view_get_model(GTK_ICON_VIEW(widget)), iter, path);
        }
        g_list_free_full(selection, (GDestroyNotify)gtk_tree_path_free);
        return ok;
    }
    if(IS_MENU_WIDGET(widget))
    {
        GtkTreePath* path = get_menu_widget_active_path(widget);
        gboolean ok = gtk_tree_model_get_iter(get_menu_widget_model(widget), iter, path);
        gtk_tree_path_free(path);
        return ok;
    }
    g_return_val_if_reached(NULL);
}

void set_widget_active_iter(GtkWidget* widget,
                            GtkTreeIter* iter)
{
    if(GTK_IS_COMBO_BOX(widget))
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(widget), iter);
    else if(GTK_IS_TREE_VIEW(widget))
    {
        GtkTreePath* path = gtk_tree_model_get_path(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)), iter);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
        gtk_tree_path_free(path);
    }
    else if(GTK_IS_ICON_VIEW(widget))
    {
        GtkTreePath* path = gtk_tree_model_get_path(gtk_icon_view_get_model(GTK_ICON_VIEW(widget)), iter);
        gtk_icon_view_set_cursor(GTK_ICON_VIEW(widget), path, NULL, FALSE);
        gtk_tree_path_free(path);
    }
    else if(IS_MENU_WIDGET(widget))
    {
        GtkTreePath* path = gtk_tree_model_get_path(get_menu_widget_model(widget), iter);
        set_menu_widget_active_path(widget, path);
        gtk_tree_path_free(path);
    }
    else
        g_return_val_if_reached(NULL);
}

void set_widget_active_first(GtkWidget* widget)
{
    if(GTK_IS_COMBO_BOX(widget))
        gtk_combo_box_set_active(GTK_COMBO_BOX(widget), 0);
    else if(GTK_IS_TREE_VIEW(widget))
    {
        GtkTreePath* path = gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
        gtk_tree_path_free(path);
    }
    else if(GTK_IS_ICON_VIEW(widget))
    {
        GtkTreePath* path = gtk_tree_path_new_first();
        gtk_icon_view_set_cursor(GTK_ICON_VIEW(widget), path, NULL, FALSE);
        gtk_tree_path_free(path);
    }
    else
        g_return_val_if_reached(NULL);
}

gboolean get_model_iter_str(GtkListStore* model,
                            int column,
                            const gchar* value,
                            GtkTreeIter* iter)
{
    if(!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), iter))
        return FALSE;
    gchar* iter_value;
    do
    {
        gtk_tree_model_get(GTK_TREE_MODEL(model), iter, column, &iter_value, -1);
        gboolean matched = g_strcmp0(iter_value, value) == 0;
        g_free(iter_value);
        if(matched)
            return TRUE;
    } while(gtk_tree_model_iter_next(GTK_TREE_MODEL(model), iter));
    return FALSE;
}

void fix_image_menu_item_if_empty(GtkImageMenuItem* widget)
{
    if(GTK_IS_IMAGE_MENU_ITEM(widget) &&
       (!gtk_menu_item_get_label(GTK_MENU_ITEM(widget)) ||
        strlen(gtk_menu_item_get_label(GTK_MENU_ITEM(widget))) == 0))
    {
        GtkWidget* image = gtk_image_menu_item_get_image(widget);
        if(!image)
            return;
        gtk_image_menu_item_set_image(widget, NULL);
        gtk_container_foreach(GTK_CONTAINER(widget), (GtkCallback)gtk_widget_destroy, NULL);
        gtk_container_add(GTK_CONTAINER(widget), image);
    }
}

gboolean get_widget_toggled(GtkWidget* widget)
{
    if(GTK_IS_TOGGLE_BUTTON(widget))
        return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    else if(GTK_IS_CHECK_MENU_ITEM(widget))
        return gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
    else
        g_return_val_if_reached(FALSE);
}

void set_widget_toggled(GtkWidget* widget,
                        gboolean state,
                        GCallback suppress_callback)
{
    if(suppress_callback)
        g_signal_handlers_block_matched(widget, G_SIGNAL_MATCH_FUNC, 0, 0, 0, suppress_callback, NULL);
    if(GTK_IS_TOGGLE_BUTTON(widget))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), state);
    else if(GTK_IS_CHECK_MENU_ITEM(widget))
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(widget), state);
    if(suppress_callback)
        g_signal_handlers_unblock_matched(widget, G_SIGNAL_MATCH_FUNC, 0, 0, 0, suppress_callback, NULL);
}

void clear_container(GtkContainer* container)
{

    GList *children = gtk_container_get_children(container);
    for(GList* iter = children; iter != NULL; iter = g_list_next(iter))
      gtk_widget_destroy(GTK_WIDGET(iter->data));
    g_list_free(children);
}

void update_main_window_layout(void)
{
    if(greeter.ui.main_layout == greeter.ui.main_content ||
       !GTK_IS_FIXED(greeter.ui.main_layout))
        return;
    const WindowPosition* p = &config.appearance.position;
    GdkScreen* screen = gtk_window_get_screen(GTK_WINDOW(greeter.ui.screen_window));
    GtkAllocation size, size_layout;
    GdkRectangle geometry;
    gint x, y;

    gtk_container_check_resize(GTK_CONTAINER(greeter.ui.screen_layout));
    gtk_widget_get_allocation(greeter.ui.main_content, &size);
    gtk_widget_get_allocation(greeter.ui.main_layout, &size_layout);
    if(config.appearance.position_is_relative)
        geometry = (GtkAllocation){.x = 0, .y = 0, .width = size_layout.width, .height = size_layout.height};
    else
        gdk_screen_get_monitor_geometry(screen, gdk_screen_get_primary_monitor(screen), &geometry);

    x = geometry.x + !p->x_is_absolute ? geometry.width*p->x/100.0  : (p->x < 0) ? geometry.width + p->x  : p->x;
    y = geometry.x + !p->y_is_absolute ? geometry.height*p->y/100.0 : (p->y < 0) ? geometry.height + p->y : p->y;

    x -= (p->anchor.width == 0) ? size.width/2 : (p->anchor.width > 0) ? size.width : 0;
    y -= (p->anchor.height == 0) ? size.height/2 : (p->anchor.height > 0) ? size.height : 0;

    if(!config.appearance.position_is_relative)
        gtk_widget_translate_coordinates(greeter.ui.screen_window, greeter.ui.main_layout,
                                         x, y, &x, &y);

    if(y + size.height > size_layout.height)
        y = size_layout.height - size.height - 1;
    if(y < 0)
        y = 0;
    gtk_fixed_move(GTK_FIXED(greeter.ui.main_layout), greeter.ui.main_content, x, y);
    gtk_container_check_resize(GTK_CONTAINER(greeter.ui.screen_layout));
}

void focus_main_window(void)
{
    GtkWidget* widget = greeter.ui.main_content;
    if(gtk_widget_is_visible(greeter.ui.prompt_entry))
        widget = greeter.ui.prompt_entry;
    else if(gtk_widget_is_visible(greeter.ui.users_widget))
        widget = greeter.ui.users_widget;
    else if(gtk_widget_is_visible(greeter.ui.login_widget))
        widget = greeter.ui.login_widget;
    gtk_widget_grab_focus(widget);
}

GtkTreeModel* get_menu_widget_model(GtkWidget* widget)
{
    MenuBinding* binding = g_object_get_data(G_OBJECT(widget), MENU_WIDGET_BINDING_PROP);
    return binding ? binding->model : NULL;
}

void bind_menu_widget_model(GtkWidget* widget,
                            GtkWidget* label,
                            GtkListStore* model,
                            gint model_column,
                            GCallback on_changed)
{
    g_return_if_fail(IS_MENU_WIDGET(widget));
    MenuBinding* binding  = g_malloc(sizeof(MenuBinding));
    binding->owner        = GTK_WIDGET(widget);
    binding->label        = label;
    binding->active       = NULL;
    binding->model        = GTK_TREE_MODEL(model);
    binding->model_column = model_column;
    binding->on_changed   = on_changed;
    binding->menu_group   = NULL;
    binding->menu = GTK_IS_MENU_ITEM(widget) ? GTK_MENU_SHELL(widget)
                                             : GTK_MENU_SHELL(gtk_menu_button_get_popup(GTK_MENU_BUTTON(widget)));
    gtk_tree_model_foreach(GTK_TREE_MODEL(model), (GtkTreeModelForeachFunc)on_menu_widget_row_inserted, binding);
    g_object_set_data(G_OBJECT(widget), MENU_WIDGET_BINDING_PROP, binding);
    g_signal_connect(model, "row-changed", G_CALLBACK(on_menu_widget_row_changed), binding);
    g_signal_connect(model, "row-deleted", G_CALLBACK(on_menu_widget_row_deleted), binding);
    g_signal_connect(model, "row-inserted", G_CALLBACK(on_menu_widget_row_inserted), binding);
}

void set_menu_widget_active_path(GtkWidget* widget,
                                 GtkTreePath* path)
{
    MenuBinding* binding = g_object_get_data(G_OBJECT(widget), MENU_WIDGET_BINDING_PROP);
    GList* items = gtk_container_get_children(GTK_CONTAINER(binding->menu));
    GList* item = g_list_find_custom(items, path, (GCompareFunc)compare_menu_widget_item_path);
    if(item)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item->data), TRUE);
    g_list_free(items);
}

GtkTreePath* get_menu_widget_active_path(GtkWidget* widget)
{
    MenuBinding* binding = g_object_get_data(G_OBJECT(widget), MENU_WIDGET_BINDING_PROP);
    GtkTreePath* path = g_object_get_data(G_OBJECT(binding->active), MENU_ITEM_PATH_PROP);
    return gtk_tree_path_copy(path);
}

/* ---------------------------------------------------------------------------*
 * Definitions: static
 * -------------------------------------------------------------------------- */

static void stop_messagebox_loop(MessageBoxRunInfo* info,
                                 gint response)
{
    if(g_main_loop_is_running(info->loop))
        g_main_loop_quit(info->loop);
    info->response = response;
}

static gboolean on_messagebox_key_press(GtkWidget* widget,
                                        GdkEventKey* event,
                                        MessageBoxRunInfo* info)
{
    if(event->keyval == GDK_KEY_Escape)
    {
        gdk_window_beep(gtk_widget_get_window(greeter.ui.screen_window));
        stop_messagebox_loop(info, info->cancel_id);
        return TRUE;
    }
    return FALSE;
}

static void on_messagebox_button_clicked(GtkWidget* widget,
                                         MessageBoxButtonRunInfo* button_info)
{
    stop_messagebox_loop(button_info->info, button_info->id);
}

static gint compare_menu_widget_item_path(GObject* obj,
                                          GtkTreePath* path)
{
    return gtk_tree_path_compare(path, g_object_get_data(obj, MENU_ITEM_PATH_PROP));
}

static void on_menu_widget_row_deleted(GtkTreeModel* tree_model,
                                       GtkTreePath* path,
                                       MenuBinding* binding)
{
    GList* items = gtk_container_get_children(GTK_CONTAINER(binding->menu));
    GList* item = g_list_find_custom(items, path, (GCompareFunc)compare_menu_widget_item_path);
    if(item)
        /* TODO: update menu_group */
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(items);
}

static void on_menu_widget_row_changed(GtkTreeModel* model,
                                       GtkTreePath* path,
                                       GtkTreeIter* iter,
                                       MenuBinding* binding)
{
    GList* items = gtk_container_get_children(GTK_CONTAINER(binding->menu));
    GList* item = g_list_find_custom(items, path, (GCompareFunc)compare_menu_widget_item_path);
    if(item)
    {
        gchar* label;
        gtk_tree_model_get(GTK_TREE_MODEL(model), iter, binding->model_column, &label);
        gtk_menu_item_set_label(GTK_MENU_ITEM(item->data), label);
        if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item->data)))
            set_widget_text(binding->label, label);
        g_free(label);
    }
    g_list_free(items);
}

static gboolean on_menu_widget_row_inserted(GtkTreeModel* model,
                                            GtkTreePath* path,
                                            GtkTreeIter* iter,
                                            MenuBinding* binding)
{
    const gint* indices = gtk_tree_path_get_indices(path);
    gchar* label;
    GtkWidget* item;
    gtk_tree_model_get(GTK_TREE_MODEL(model), iter, binding->model_column, &label, -1);
    item = gtk_radio_menu_item_new_with_label(binding->menu_group, label);
    binding->menu_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
    g_object_set_data(G_OBJECT(item), MENU_ITEM_PATH_PROP, gtk_tree_model_get_path(model, iter));
    g_signal_connect(item, "toggled", G_CALLBACK(on_menu_widget_item_toggled), binding);
    gtk_widget_show(item);
    gtk_menu_shell_insert(binding->menu, item, indices[0]);
    g_free(label);
    return FALSE;
}

static void on_menu_widget_item_toggled(GtkWidget* widget,
                                        MenuBinding* binding)
{
    if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)))
    {
        binding->active = widget;
        set_widget_text(binding->label, gtk_menu_item_get_label(GTK_MENU_ITEM(widget)));
        if(binding->on_changed)
            ((GtkCallback)binding->on_changed)(binding->owner, NULL);
    }
}
