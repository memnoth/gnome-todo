/* gtd-task-row.c
 *
 * Copyright (C) 2015 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gtd-provider.h"
#include "gtd-task-row.h"
#include "gtd-task.h"
#include "gtd-task-list.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

struct _GtdTaskRow
{
  GtkListBoxRow      parent;

  /*<private>*/
  GtkRevealer               *revealer;
  GtkStack                  *stack;

  /* new task widgets */
  GtkStack                  *done_check;
  GtkEntry                  *new_task_entry;
  GtkStack                  *new_task_stack;

  /* task widgets */
  GtkEntry                  *title_entry;
  GtkLabel                  *task_date_label;
  GtkLabel                  *task_list_label;
  GtkStack                  *task_stack;
  GtkSpinner                *task_loading_spinner;
  GtkLabel                  *title_label;

  /* dnd widgets */
  GtkWidget                 *dnd_box;
  GtkWidget                 *dnd_event_box;
  GtkWidget                 *dnd_icon;
  gdouble                    clicked_x;
  gdouble                    clicked_y;

  /* data */
  gboolean                   new_task_mode;
  GtdTask                   *task;

  gint                       destroy_row_timeout_id;
};

#define PRIORITY_ICON_SIZE         8
#define DND_ICON_SCALE             0.85

G_DEFINE_TYPE (GtdTaskRow, gtd_task_row, GTK_TYPE_LIST_BOX_ROW)

enum {
  ENTER,
  EXIT,
  ACTIVATED,
  CREATE_TASK,
  NUM_SIGNALS
};

enum {
  PROP_0,
  PROP_NEW_TASK_MODE,
  PROP_TASK,
  LAST_PROP
};

typedef enum
{
  CURSOR_NONE,
  CURSOR_GRAB,
  CURSOR_GRABBING
} CursorType;

static guint signals[NUM_SIGNALS] = { 0, };

static void
set_dnd_cursor (GtkWidget  *widget,
                CursorType  type)
{
  GdkDisplay *display;
  GdkCursor *cursor;

  if (!gtk_widget_get_realized (widget))
    return;

  display = gtk_widget_get_display (widget);

  switch (type)
    {
    case CURSOR_NONE:
      cursor = NULL;
      break;

    case CURSOR_GRAB:
      cursor = gdk_cursor_new_from_name (display, "grab");
      break;

    case CURSOR_GRABBING:
      cursor = gdk_cursor_new_from_name (display, "grabbing");
      break;

    default:
      cursor = NULL;
    }

  gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
  gdk_display_flush (display);

  g_clear_object (&cursor);
}

static cairo_surface_t*
get_dnd_icon (GtdTaskRow *self)
{
  cairo_surface_t *surface;
  GtkWidget *widget;
  cairo_t *cr;
  gint real_x;

  widget = GTK_WIDGET (self);
  gtk_widget_translate_coordinates (self->dnd_event_box,
                                    gtk_widget_get_parent (widget),
                                    0,
                                    0,
                                    &real_x,
                                    NULL);

  /* Make it transparent */
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        gtk_widget_get_allocated_width (widget) * DND_ICON_SCALE,
                                        gtk_widget_get_allocated_height (widget) * DND_ICON_SCALE);

  cr = cairo_create (surface);
  cairo_scale (cr, DND_ICON_SCALE, DND_ICON_SCALE);

  gtk_widget_draw (widget, cr);

  cairo_surface_set_device_offset (surface,
                                   -(self->clicked_x + real_x) * DND_ICON_SCALE,
                                   -self->clicked_y * DND_ICON_SCALE);

  return surface;
}

static gboolean
mouse_out_event (GtkWidget  *widget,
                 GdkEvent   *event,
                 GtdTaskRow *self)
{
  set_dnd_cursor (widget, CURSOR_NONE);

  return GDK_EVENT_STOP;
}

static gboolean
mouse_over_event (GtkWidget  *widget,
                  GdkEvent   *event,
                  GtdTaskRow *self)
{
  set_dnd_cursor (widget, CURSOR_GRAB);

  return GDK_EVENT_STOP;
}

static gboolean
button_press_event (GtkWidget      *widget,
                    GdkEventButton *event,
                    GtdTaskRow     *self)
{
  self->clicked_x = event->x;
  self->clicked_y = event->y;

  return GDK_EVENT_PROPAGATE;
}

static void
drag_begin_cb (GtkWidget      *widget,
               GdkDragContext *context,
               GtdTaskRow     *self)
{
  cairo_surface_t *surface;

  surface = get_dnd_icon (self);

  set_dnd_cursor (widget, CURSOR_GRABBING);

  gtk_drag_set_icon_surface (context, surface);

  gtk_widget_hide (GTK_WIDGET (self));

  g_clear_pointer (&surface, cairo_surface_destroy);
}

static void
gtd_task_row__priority_changed_cb (GtdTaskRow *row,
                                   GParamSpec *spec,
                                   GObject    *object)
{
  GtkStyleContext *context;
  gint priority;

  g_return_if_fail (GTD_IS_TASK_ROW (row));

  context = gtk_widget_get_style_context (GTK_WIDGET (row));
  priority = gtd_task_get_priority (GTD_TASK (object));

  /* remove all styles */
  gtk_style_context_remove_class (context, "priority-low");
  gtk_style_context_remove_class (context, "priority-medium");
  gtk_style_context_remove_class (context, "priority-hight");

  switch (priority)
    {
    case 1:
      gtk_style_context_add_class (context, "priority-low");
      break;

    case 2:
      gtk_style_context_add_class (context, "priority-medium");
      break;

    case 3:
      gtk_style_context_add_class (context, "priority-hight");
      break;

    default:
      break;
    }

  /* redraw background according to the new applied style */
  gtk_widget_queue_draw (GTK_WIDGET (row));
}

static void
complete_changed_cb (GtdTaskRow *self,
                     GParamSpec *pspec,
                     GtdTask    *task)
{
  GtkStyleContext *context;

  context = gtk_widget_get_style_context (GTK_WIDGET (self));

  if (gtd_task_get_complete (task))
    gtk_style_context_add_class (context, "complete");
  else
    gtk_style_context_remove_class (context, "complete");
}

static void
depth_changed_cb (GtdTaskRow *self,
                  GParamSpec *pspec,
                  GtdTask    *task)
{
  gtk_widget_set_margin_start (self->dnd_box, 32 * gtd_task_get_depth (task));
}

static gboolean
gtd_task_row__date_changed_binding (GBinding     *binding,
                                    const GValue *from_value,
                                    GValue       *to_value,
                                    gpointer      user_data)
{
  GDateTime *dt;
  gchar *new_label = NULL;

  g_return_val_if_fail (GTD_IS_TASK_ROW (user_data), FALSE);

  dt = g_value_get_boxed (from_value);

  if (dt)
    {
      GDateTime *today = g_date_time_new_now_local ();

      if (g_date_time_get_year (dt) == g_date_time_get_year (today) &&
          g_date_time_get_month (dt) == g_date_time_get_month (today))
        {
          if (g_date_time_get_day_of_month (dt) == g_date_time_get_day_of_month (today))
            {
              new_label = g_strdup (_("Today"));
            }
          else if (g_date_time_get_day_of_month (dt) == g_date_time_get_day_of_month (today) + 1)
            {
              new_label = g_strdup (_("Tomorrow"));
            }
          else if (g_date_time_get_day_of_month (dt) == g_date_time_get_day_of_month (today) - 1)
            {
              new_label = g_strdup (_("Yesterday"));
            }
          else if (g_date_time_get_day_of_year (dt) > g_date_time_get_day_of_month (today) &&
                   g_date_time_get_day_of_year (dt) < g_date_time_get_day_of_month (today) + 7)
            {
              new_label = g_date_time_format (dt, "%A");
            }
          else
            {
              new_label = g_date_time_format (dt, "%x");
            }
        }
      else
        {
          new_label = g_date_time_format (dt, "%x");
        }

      g_date_time_unref (today);

    }
  else
    {
      new_label = g_strdup (_("No date set"));
    }

  g_value_set_string (to_value, new_label);

  g_free (new_label);

  return TRUE;
}

static GtdTask*
gtd_task_row__create_task_for_name (const gchar *name)
{
  GtdTask *task = gtd_task_new (NULL);

  gtd_task_set_title (task, name);
  gtd_task_save (task);

  return task;
}

static gboolean
gtd_task_row__destroy_cb (GtkWidget *row)
{
  gtk_widget_destroy (row);

  return G_SOURCE_REMOVE;
}

GtkWidget*
gtd_task_row_new (GtdTask *task)
{
  return g_object_new (GTD_TYPE_TASK_ROW,
                       "task", task,
                       NULL);
}

static gboolean
gtd_task_row__entry_focus_out (GtkWidget     *widget,
                               GdkEventFocus *event,
                               gpointer       user_data)
{
  GtdTaskRow *self = GTD_TASK_ROW (user_data);

  g_return_val_if_fail (GTD_IS_TASK_ROW (user_data), FALSE);

  if (self->new_task_mode)
    gtk_stack_set_visible_child_name (self->new_task_stack, "label");
  else
    gtk_stack_set_visible_child_name (self->task_stack, "label");

  return FALSE;
}

static gboolean
gtd_task_row__focus_in (GtkWidget *widget,
                        GdkEventFocus *event)
{
  GtdTaskRow *self = GTD_TASK_ROW (widget);

  g_return_val_if_fail (GTD_IS_TASK_ROW (widget), FALSE);

  if (self->new_task_mode)
    {
      gtk_stack_set_visible_child_name (self->new_task_stack, "entry");
      gtk_widget_grab_focus (GTK_WIDGET (self->new_task_entry));
    }
  else
    {
      gtk_stack_set_visible_child_name (self->task_stack, "title");
      gtk_widget_grab_focus (GTK_WIDGET (self->title_entry));
    }

  return FALSE;
}

static gboolean
gtd_task_row__key_press_event (GtkWidget   *row,
                               GdkEventKey *event)
{
  GtdTaskRow *self = GTD_TASK_ROW (row);

  if (event->keyval == GDK_KEY_Escape && // Esc is pressed
      !(event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK))) // No modifiers together
    {
      if (self->new_task_mode)
        {
          gtk_stack_set_visible_child_name (self->new_task_stack, "label");
          gtk_entry_set_text (self->new_task_entry, "");
          return TRUE;
        }
      else
        {
          g_signal_emit (row, signals[EXIT], 0);
        }
    }

  return FALSE;
}

static void
gtd_task_row__entry_activated (GtkEntry *entry,
                               gpointer  user_data)
{
  GtdTaskRow *self = GTD_TASK_ROW (user_data);

  g_return_if_fail (GTD_IS_TASK_ROW (user_data));
  g_return_if_fail (GTK_IS_ENTRY (entry));

  if (entry == self->new_task_entry)
    {
      GtdTask *new_task;

      /* Cannot create empty tasks */
      if (gtk_entry_get_text_length (self->new_task_entry) == 0)
        return;

      new_task = gtd_task_row__create_task_for_name (gtk_entry_get_text (self->new_task_entry));

      g_signal_emit (user_data, signals[CREATE_TASK], 0, new_task);

      gtk_entry_set_text (self->new_task_entry, "");
    }
}

static void
gtd_task_row_finalize (GObject *object)
{
  GtdTaskRow *self = GTD_TASK_ROW (object);

  g_clear_object (&self->task);

  G_OBJECT_CLASS (gtd_task_row_parent_class)->finalize (object);
}

static void
gtd_task_row_dispose (GObject *object)
{
  GtdTaskRow *self;
  GtdTask *task;

  self = GTD_TASK_ROW (object);
  task = self->task;

  if (task)
    {
      g_signal_handlers_disconnect_by_func (task,
                                        depth_changed_cb,
                                        self);

      g_signal_handlers_disconnect_by_func (task,
                                            complete_changed_cb,
                                            self);

      g_signal_handlers_disconnect_by_func (task,
                                            gtd_task_row__priority_changed_cb,
                                            self);
    }

  G_OBJECT_CLASS (gtd_task_row_parent_class)->dispose (object);
}

static void
gtd_task_row_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  GtdTaskRow *self = GTD_TASK_ROW (object);

  switch (prop_id)
    {
    case PROP_NEW_TASK_MODE:
      g_value_set_boolean (value, self->new_task_mode);
      break;

    case PROP_TASK:
      g_value_set_object (value, self->task);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_row_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  GtdTaskRow *self = GTD_TASK_ROW (object);

  switch (prop_id)
    {
    case PROP_NEW_TASK_MODE:
      gtd_task_row_set_new_task_mode (self, g_value_get_boolean (value));
      break;

    case PROP_TASK:
      gtd_task_row_set_task (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_task_row_activate (GtkListBoxRow *row)
{
  GTK_LIST_BOX_ROW_CLASS (gtd_task_row_parent_class)->activate (row);

  g_signal_emit (row, signals[ENTER], 0);
}

static void
gtd_task_row_class_init (GtdTaskRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkListBoxRowClass *row_class = GTK_LIST_BOX_ROW_CLASS (klass);

  object_class->dispose = gtd_task_row_dispose;
  object_class->finalize = gtd_task_row_finalize;
  object_class->get_property = gtd_task_row_get_property;
  object_class->set_property = gtd_task_row_set_property;

  widget_class->focus_in_event = gtd_task_row__focus_in;
  widget_class->key_press_event = gtd_task_row__key_press_event;

  row_class->activate = gtd_task_row_activate;

  /**
   * GtdTaskRow::new-task-mode:
   *
   * If the row is used to add new tasks.
   */
  g_object_class_install_property (
          object_class,
          PROP_NEW_TASK_MODE,
          g_param_spec_boolean ("new-task-mode",
                                "If the row is used to add a new task",
                                "Whether the row is used to add a new task",
                                FALSE,
                                G_PARAM_READWRITE));

  /**
   * GtdTaskRow::task:
   *
   * The task that this row represents, or %NULL.
   */
  g_object_class_install_property (
          object_class,
          PROP_TASK,
          g_param_spec_object ("task",
                               "Task of the row",
                               "The task that this row represents",
                               GTD_TYPE_TASK,
                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GtdTaskRow::enter:
   *
   * Emitted when the row is focused and in the editing state.
   */
  signals[ENTER] = g_signal_new ("enter",
                                 GTD_TYPE_TASK_ROW,
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL,
                                 NULL,
                                 NULL,
                                 G_TYPE_NONE,
                                 0);

  /**
   * GtdTaskRow::exit:
   *
   * Emitted when the row is unfocused and leaves the editing state.
   */
  signals[EXIT] = g_signal_new ("exit",
                                GTD_TYPE_TASK_ROW,
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL,
                                NULL,
                                NULL,
                                G_TYPE_NONE,
                                0);

  /**
   * GtdTaskRow::activate:
   *
   * Emitted when the row wants to apply the changes.
   */
  signals[ACTIVATED] = g_signal_new ("activated",
                                     GTD_TYPE_TASK_ROW,
                                     G_SIGNAL_RUN_LAST,
                                     0,
                                     NULL,
                                     NULL,
                                     NULL,
                                     G_TYPE_NONE,
                                     0);

  /**
   * GtdTaskRow::create-task:
   *
   * Emitted when the row wants the parent widget to create a new task.
   */
  signals[CREATE_TASK] = g_signal_new ("create-task",
                                       GTD_TYPE_TASK_ROW,
                                       G_SIGNAL_RUN_LAST,
                                       0,
                                       NULL,
                                       NULL,
                                       NULL,
                                       G_TYPE_NONE,
                                       1,
                                       GTD_TYPE_TASK);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/task-row.ui");

  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_event_box);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, dnd_icon);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, done_check);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, stack);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, new_task_entry);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, new_task_stack);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, revealer);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_date_label);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_list_label);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_stack);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, task_loading_spinner);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, title_entry);
  gtk_widget_class_bind_template_child (widget_class, GtdTaskRow, title_label);

  gtk_widget_class_bind_template_callback (widget_class, button_press_event);
  gtk_widget_class_bind_template_callback (widget_class, drag_begin_cb);
  gtk_widget_class_bind_template_callback (widget_class, mouse_out_event);
  gtk_widget_class_bind_template_callback (widget_class, mouse_over_event);
  gtk_widget_class_bind_template_callback (widget_class, gtd_task_row__entry_activated);
  gtk_widget_class_bind_template_callback (widget_class, gtd_task_row__entry_focus_out);

  gtk_widget_class_set_css_name (widget_class, "taskrow");
}

static void
gtd_task_row_init (GtdTaskRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  /* The source of DnD is the drag icon */
  gtk_drag_source_set (self->dnd_event_box,
                       GDK_BUTTON1_MASK,
                       NULL,
                       0,
                       GDK_ACTION_COPY);
}

/**
 * gtd_task_row_get_new_task_mode:
 * @row: a #GtdTaskRow
 *
 * Whether @row is in new task mode.
 *
 * Returns: %TRUE if @row is in new task mode, %FALSE otherwise
 */
gboolean
gtd_task_row_get_new_task_mode (GtdTaskRow *row)
{
  g_return_val_if_fail (GTD_IS_TASK_ROW (row), FALSE);

  return row->new_task_mode;
}

/**
 * gtd_task_row_set_new_task_mode:
 * @row: a #GtdTaskRow
 * @new_task_mode: %TRUE to set new task mode, %FALSE otherwise
 *
 * Sets @row new task mode to @new_task_mode. It is up to the caller
 * to set GtdTaskRow::task to %NULL.
 *
 * Returns:
 */
void
gtd_task_row_set_new_task_mode (GtdTaskRow *row,
                                gboolean    new_task_mode)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  if (row->new_task_mode != new_task_mode)
    {
      row->new_task_mode = new_task_mode;

      if (new_task_mode)
        {
          gtk_stack_set_visible_child_name (GTK_STACK (row->stack), "new");
          gtd_task_row_reveal (row);
        }
      else
        {
          gtk_stack_set_visible_child_name (GTK_STACK (row->stack), "task");
        }

      g_object_notify (G_OBJECT (row), "new-task-mode");
    }
}

/**
 * gtd_task_row_get_task:
 * @row: a #GtdTaskRow
 *
 * Retrieves the #GtdTask that @row manages, or %NULL if none
 * is set.
 *
 * Returns: (transfer none): the internal task of @row
 */
GtdTask*
gtd_task_row_get_task (GtdTaskRow *row)
{
  g_return_val_if_fail (GTD_IS_TASK_ROW (row), NULL);

  return row->task;
}

/**
 * gtd_task_row_set_task:
 * @row: a #GtdTaskRow
 * @task: a #GtdTask
 *
 * Sets the internal #GtdTask of @row. The task must be set to %NULL
 * before setting GtdObject::new-task-mode to %TRUE.
 */
void
gtd_task_row_set_task (GtdTaskRow *row,
                       GtdTask    *task)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  if (g_set_object (&row->task, task))
    {
      if (task)
        {
          gtk_label_set_label (row->task_list_label, gtd_task_list_get_name (gtd_task_get_list (task)));

          g_object_bind_property (task,
                                  "title",
                                  row->title_entry,
                                  "text",
                                  G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

          g_object_bind_property (task,
                                  "title",
                                  row->title_label,
                                  "label",
                                  G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

          g_object_bind_property (task,
                                  "complete",
                                  row->done_check,
                                  "active",
                                  G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

          g_object_bind_property (task,
                                  "ready",
                                  row->task_loading_spinner,
                                  "visible",
                                  G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

          g_object_bind_property_full (task,
                                       "due-date",
                                       row->task_date_label,
                                       "label",
                                       G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
                                       gtd_task_row__date_changed_binding,
                                       NULL,
                                       row,
                                       NULL);

          /*
           * Here we generate a false callback call just to reuse the method to
           * sync the initial state of the priority icon.
           */
          gtd_task_row__priority_changed_cb (row, NULL, G_OBJECT (task));
          g_signal_connect_swapped (task,
                                    "notify::priority",
                                    G_CALLBACK (gtd_task_row__priority_changed_cb),
                                    row);

          complete_changed_cb (row, NULL, task);
          g_signal_connect_swapped (task,
                                    "notify::complete",
                                    G_CALLBACK (complete_changed_cb),
                                    row);

          depth_changed_cb (row, NULL, task);
          g_signal_connect_swapped (task,
                                    "notify::depth",
                                    G_CALLBACK (depth_changed_cb),
                                    row);
        }

      g_object_notify (G_OBJECT (row), "task");
    }
}

/**
 * gtd_task_row_set_list_name_visible:
 * @row: a #GtdTaskRow
 * @show_list_name: %TRUE to show the list name, %FALSE to hide it
 *
 * Sets @row's list name label visibility to @show_list_name.
 *
 * Returns:
 */
void
gtd_task_row_set_list_name_visible (GtdTaskRow *row,
                                    gboolean    show_list_name)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  gtk_widget_set_visible (GTK_WIDGET (row->task_list_label), show_list_name);
}

/**
 * gtd_task_row_reveal:
 * @row: a #GtdTaskRow
 *
 * Runs a nifty animation to reveal @row.
 *
 * Returns:
 */
void
gtd_task_row_reveal (GtdTaskRow *row)
{
  g_return_if_fail (GTD_IS_TASK_ROW (row));

  gtk_revealer_set_reveal_child (row->revealer, TRUE);
}

/**
 * gtd_task_row_destroy:
 * @self: a #GtdTaskRow
 *
 * Destroy @self after hiding it.
 */
void
gtd_task_row_destroy (GtdTaskRow *self)
{
  g_return_if_fail (GTD_IS_TASK_ROW (self));

  if (!gtk_revealer_get_child_revealed (self->revealer))
    {
      gtk_widget_destroy (GTK_WIDGET (self));
    }
  else if (self->destroy_row_timeout_id == 0)
    {
      guint duration;

      duration = gtk_revealer_get_transition_duration (self->revealer);

      gtk_revealer_set_reveal_child (self->revealer, FALSE);
      self->destroy_row_timeout_id = g_timeout_add (duration,
                                                    (GSourceFunc) gtd_task_row__destroy_cb,
                                                    self);
    }
}

gboolean
gtd_task_row_is_drag_valid (GtdTaskRow     *self,
                            GdkDragContext *context)
{
  GtdTaskRow *row;
  GtkWidget *source_widget;
  GtdTask *row_task, *target_task;

  source_widget = gtk_drag_get_source_widget (context);

  if (!source_widget)
    return FALSE;

  row = GTD_TASK_ROW (gtk_widget_get_ancestor (source_widget, GTD_TYPE_TASK_ROW));

  /* Do not allow dropping on itself, nor on the new task row */
  if (row == self || self->new_task_mode)
    return FALSE;

  row_task = row->task;
  target_task = self->task;

  /* Forbid adding the parent task as a subtask */
  if (gtd_task_is_subtask (row_task, target_task))
    return FALSE;

  return TRUE;
}
