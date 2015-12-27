/* gtd-provider-selector.c
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

#include "gtd-application.h"
#include "gtd-manager.h"
#include "interfaces/gtd-provider.h"
#include "gtd-provider-row.h"
#include "gtd-provider-selector.h"

#include <glib/gi18n.h>

typedef struct
{
  GtkWidget                 *listbox;
  GtkWidget                 *local_check;

  /* stub rows */
  GtkWidget                 *exchange_stub_row;
  GtkWidget                 *google_stub_row;
  GtkWidget                 *owncloud_stub_row;
  GtkWidget                 *local_row;

  GtdManager                *manager;

  gint                      select_default : 1;
  gint                      show_local_provider : 1;
  gint                      show_stub_rows : 1;
} GtdProviderSelectorPrivate;

struct _GtdProviderSelector
{
  GtkBox                     parent;

  /*< private >*/
  GtdProviderSelectorPrivate *priv;
};

G_DEFINE_TYPE_WITH_PRIVATE (GtdProviderSelector, gtd_provider_selector, GTK_TYPE_BOX)

enum {
  PROP_0,
  PROP_MANAGER,
  PROP_SELECT_DEFAULT,
  PROP_SHOW_LOCAL,
  PROP_SHOW_STUB_ROWS,
  LAST_PROP
};

enum {
  PROVIDER_SELECTED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void
spawn (const gchar *action,
       const gchar *arg)
{
  const gchar *command[] = {"gnome-control-center", "online-accounts", action, arg, NULL};
  g_spawn_async (NULL, (gchar **) command, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

/**
 * display_header_func:
 *
 * Shows a separator before each row.
 */
static void
display_header_func (GtkListBoxRow *row,
                     GtkListBoxRow *before,
                     gpointer       user_data)
{
  if (before != NULL)
    {
      GtkWidget *header;

      header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);

      gtk_list_box_row_set_header (row, header);
      gtk_widget_show (header);
    }
}

static void
gtd_provider_selector__default_provider_changed (GtdProviderSelector *selector,
                                               GtdProvider         *current,
                                               GtdProvider         *previous)
{
  GtdProviderSelectorPrivate *priv;
  GList *children;
  GList *l;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));
  g_return_if_fail (GTD_IS_PROVIDER (previous));
  g_return_if_fail (GTD_IS_PROVIDER (current));

  priv = selector->priv;

  if (!priv->select_default)
    return;

  children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      GtdProvider *provider;

      if (!GTD_IS_PROVIDER_ROW (l->data))
        continue;

      provider = gtd_provider_row_get_provider (l->data);

      gtd_provider_row_set_selected (l->data, provider == current);
    }

  g_list_free (children);

  g_signal_emit (selector, signals[PROVIDER_SELECTED], 0, current);
}

static void
gtd_provider_selector__listbox_row_activated (GtdProviderSelector *selector,
                                             GtkWidget          *row)
{
  GtdProviderSelectorPrivate *priv;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));

  priv = selector->priv;

  /* The row is either one of the stub rows, or a GtdGoaRow */
  if (row == priv->google_stub_row)
    {
      spawn ("add", "google");
    }
  else if (row == priv->owncloud_stub_row)
    {
      spawn ("add", "owncloud");
    }
  else if (row == priv->exchange_stub_row)
    {
      spawn ("add", "exchange");
    }
  else
    {
      GtdProvider *provider;
      GList *children;
      GList *l;

      children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));
      provider = gtd_provider_row_get_provider (GTD_PROVIDER_ROW (row));

      for (l = children; l != NULL; l = l->next)
        {
          if (GTD_IS_PROVIDER_ROW (l->data))
            gtd_provider_row_set_selected (l->data, FALSE);
        }

      /*
       * If the account has it's calendars disabled, we cannot let it
       * be a default provider location. Instead, open the Control Center
       * to give the user the ability to change it.
       */
      if (gtd_provider_get_enabled (provider))
        {
          gtd_provider_row_set_selected (GTD_PROVIDER_ROW (row), TRUE);
          g_signal_emit (selector, signals[PROVIDER_SELECTED], 0, provider);
        }
      else
        {
          spawn ((gchar*) gtd_provider_get_id (provider), NULL);
        }

      g_list_free (children);
    }
}

static void
gtd_provider_selector__check_toggled (GtdProviderSelector *selector,
                                     GtkToggleButton    *check)
{
  GtdProviderSelectorPrivate *priv;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));

  priv = selector->priv;

  /*
   * Unset the currently selected provider location row when the check button is
   * activated. No need to do this when deactivated, since we already did.
   */

  if (gtk_toggle_button_get_active (check))
    {
      GtdProvider *local_provider;
      GList *children;
      GList *l;

      children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));
      local_provider = gtd_provider_row_get_provider (GTD_PROVIDER_ROW (priv->local_row));

      for (l = children; l != NULL; l = l->next)
        {
          if (GTD_IS_PROVIDER_ROW (l->data))
            gtd_provider_row_set_selected (l->data, FALSE);
        }

      g_list_free (children);

      /*
       * Sets the provider location to "local", and don't unset it if the
       * check gets deactivated.
       */
      g_signal_emit (selector, signals[PROVIDER_SELECTED], 0, local_provider);
    }
  else
    {
      g_signal_emit (selector, signals[PROVIDER_SELECTED], 0, NULL);
    }
}

static void
gtd_provider_selector__remove_provider (GtdProviderSelector *selector,
                                        GtdProvider         *provider)
{
  GtdProviderSelectorPrivate *priv;
  GList *children;
  GList *l;
  gint exchange;
  gint google;
  gint owncloud;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));
  g_return_if_fail (GTD_IS_PROVIDER (provider));

  priv = selector->priv;
  children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));
  exchange = google = owncloud = 0;

  for (l = children; l != NULL; l = l->next)
    {
      GtdProvider *row_provider;
      const gchar *provider_id;

      if (!GTD_IS_PROVIDER_ROW (l->data))
        continue;

      row_provider = gtd_provider_row_get_provider (l->data);
      provider_id = gtd_provider_get_id (row_provider);

      if (row_provider == provider)
        {
          gtk_widget_destroy (l->data);
        }
      else
        {
          if (g_strcmp0 (provider_id, "exchange") == 0)
            exchange++;
          else if (g_strcmp0 (provider_id, "google") == 0)
            google++;
          else if (g_strcmp0 (provider_id, "owncloud") == 0)
            owncloud++;
        }
    }

  gtk_widget_set_visible (priv->exchange_stub_row, exchange == 0);
  gtk_widget_set_visible (priv->google_stub_row, google == 0);
  gtk_widget_set_visible (priv->owncloud_stub_row, owncloud == 0);

  g_list_free (children);
}

static void
gtd_provider_selector__add_provider (GtdProviderSelector *selector,
                                     GtdProvider         *provider)
{
  GtdProviderSelectorPrivate *priv;
  GtkWidget *row;
  const gchar *provider_id;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));
  g_return_if_fail (GTD_IS_PROVIDER (provider));

  priv = selector->priv;

  row = gtd_provider_row_new (provider);
  provider_id = gtd_provider_get_id (provider);

  gtk_container_add (GTK_CONTAINER (priv->listbox), row);

  /* track the local provider row */
  if (g_strcmp0 (provider_id, "local") == 0)
    {
      gtk_widget_set_visible (row, priv->show_local_provider);
      priv->local_row = row;
    }

  /* Auto selects the default provider row when needed */
  if (priv->select_default &&
      //gtd_provider_get_is_default (provider) &&
      !gtd_provider_selector_get_selected_provider (selector))
    {
      gtd_provider_selector_set_selected_provider (selector, provider);
    }

  /* hide the related stub row */
  if (g_strcmp0 (provider_id, "exchange") == 0)
    gtk_widget_hide (priv->exchange_stub_row);
  else if (g_strcmp0 (provider_id, "google") == 0)
    gtk_widget_hide (priv->google_stub_row);
  else if (g_strcmp0 (provider_id, "owncloud") == 0)
    gtk_widget_hide (priv->owncloud_stub_row);
}

static void
gtd_provider_selector__fill_accounts (GtdProviderSelector *selector)
{
  GtdProviderSelectorPrivate *priv;
  GList *providers;
  GList *l;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));

  priv = selector->priv;

  /* load accounts */
  providers = gtd_manager_get_providers (priv->manager);

  for (l = providers; l != NULL; l = l->next)
    gtd_provider_selector__add_provider (selector, l->data);

  g_list_free (providers);
}

static gint
sort_func (GtkListBoxRow *row1,
           GtkListBoxRow *row2,
           gpointer       user_data)
{
  GtdProvider *provider1;
  GtdProvider *provider2;

  if (!GTD_IS_PROVIDER_ROW (row1))
    return 1;
  else if (!GTD_IS_PROVIDER_ROW (row2))
    return -1;

  provider1 = gtd_provider_row_get_provider (GTD_PROVIDER_ROW (row1));
  provider2 = gtd_provider_row_get_provider (GTD_PROVIDER_ROW (row2));

  return provider2 != provider1;//gtd_provider_compare (provider1, provider2);
}

static void
gtd_provider_selector_constructed (GObject *object)
{
  GtdProviderSelectorPrivate *priv;

  G_OBJECT_CLASS (gtd_provider_selector_parent_class)->constructed (object);

  priv = GTD_PROVIDER_SELECTOR (object)->priv;

  gtk_list_box_set_header_func (GTK_LIST_BOX (priv->listbox),
                                display_header_func,
                                NULL,
                                NULL);
  gtk_list_box_set_sort_func (GTK_LIST_BOX (priv->listbox),
                              (GtkListBoxSortFunc) sort_func,
                              NULL,
                              NULL);
}

static void
gtd_provider_selector_finalize (GObject *object)
{
  G_OBJECT_CLASS (gtd_provider_selector_parent_class)->finalize (object);
}

static void
gtd_provider_selector_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  GtdProviderSelector *self = GTD_PROVIDER_SELECTOR (object);

  switch (prop_id)
    {
    case PROP_MANAGER:
      g_value_set_object (value, self->priv->manager);
      break;

    case PROP_SELECT_DEFAULT:
      g_value_set_boolean (value, self->priv->select_default);
      break;

    case PROP_SHOW_LOCAL:
      g_value_set_boolean (value, self->priv->show_local_provider);
      break;

    case PROP_SHOW_STUB_ROWS:
      g_value_set_boolean (value, self->priv->show_stub_rows);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_selector_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  GtdProviderSelector *self = GTD_PROVIDER_SELECTOR (object);

  switch (prop_id)
    {
    case PROP_MANAGER:
      self->priv->manager = g_value_get_object (value);

      if (!self->priv->manager)
        break;

      gtd_provider_selector__fill_accounts (self);

      g_signal_connect_swapped (self->priv->manager,
                                "default-provider-changed",
                                G_CALLBACK (gtd_provider_selector__default_provider_changed),
                                object);

      g_signal_connect_swapped (self->priv->manager,
                                "provider-added",
                                G_CALLBACK (gtd_provider_selector__add_provider),
                                object);

      g_signal_connect_swapped (self->priv->manager,
                                "provider-removed",
                                G_CALLBACK (gtd_provider_selector__remove_provider),
                                object);

      g_object_notify (object, "manager");
      break;

    case PROP_SELECT_DEFAULT:
      gtd_provider_selector_set_select_default (self, g_value_get_boolean (value));
      break;

    case PROP_SHOW_LOCAL:
      gtd_provider_selector_show_local (self, g_value_get_boolean (value));
      break;

    case PROP_SHOW_STUB_ROWS:
      gtd_provider_selector_set_show_stub_rows (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gtd_provider_selector_class_init (GtdProviderSelectorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = gtd_provider_selector_finalize;
  object_class->constructed = gtd_provider_selector_constructed;
  object_class->get_property = gtd_provider_selector_get_property;
  object_class->set_property = gtd_provider_selector_set_property;

  /**
   * GtdProviderSelector::location-selected:
   *
   * Emitted when a provider location is selected.
   */
  signals[PROVIDER_SELECTED] = g_signal_new ("provider-selected",
                                             GTD_TYPE_PROVIDER_SELECTOR,
                                             G_SIGNAL_RUN_LAST,
                                             0,
                                             NULL,
                                             NULL,
                                             NULL,
                                             G_TYPE_NONE,
                                             1,
                                             GTD_TYPE_PROVIDER);

  /**
   * GtdProviderSelector::manager:
   *
   * A weak reference to the application's #GtdManager instance.
   */
  g_object_class_install_property (
        object_class,
        PROP_MANAGER,
        g_param_spec_object ("manager",
                             "Manager of this window's application",
                             "The manager of the window's application",
                             GTD_TYPE_MANAGER,
                             G_PARAM_READWRITE));

  /**
   * GtdProviderSelector::show-local-provider:
   *
   * Whether it should show a row for the local provider.
   */
  g_object_class_install_property (
        object_class,
        PROP_SHOW_LOCAL,
        g_param_spec_boolean ("show-local",
                              "Show local provider row",
                              "Whether should show a local provider row instead of a checkbox",
                              FALSE,
                              G_PARAM_READWRITE));

  /**
   * GtdProviderSelector::show-stub-rows:
   *
   * Whether it should show stub rows for non-added accounts.
   */
  g_object_class_install_property (
        object_class,
        PROP_SHOW_STUB_ROWS,
        g_param_spec_boolean ("show-stub-rows",
                              "Show stub rows",
                              "Whether should show stub rows for non-added accounts",
                              TRUE,
                              G_PARAM_READWRITE));

  /**
   * GtdProviderSelector::select-default:
   *
   * Whether it should auto selects the default provider location row.
   */
  g_object_class_install_property (
        object_class,
        PROP_SELECT_DEFAULT,
        g_param_spec_boolean ("select-default",
                              "Selects default provider row",
                              "Whether should select the default provider row",
                              FALSE,
                              G_PARAM_READWRITE));

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/todo/ui/provider-selector.ui");

  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderSelector, exchange_stub_row);
  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderSelector, google_stub_row);
  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderSelector, listbox);
  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderSelector, local_check);
  gtk_widget_class_bind_template_child_private (widget_class, GtdProviderSelector, owncloud_stub_row);

  gtk_widget_class_bind_template_callback (widget_class, gtd_provider_selector__check_toggled);
  gtk_widget_class_bind_template_callback (widget_class, gtd_provider_selector__listbox_row_activated);
}

static void
gtd_provider_selector_init (GtdProviderSelector *self)
{
  self->priv = gtd_provider_selector_get_instance_private (self);
  self->priv->show_stub_rows = TRUE;

  gtk_widget_init_template (GTK_WIDGET (self));
}

/**
 * gtd_provider_selector_new:
 *
 * Creates a new #GtdProviderSelector.
 *
 * Returns: (transfer full): a new #GtdProviderSelector
 */
GtkWidget*
gtd_provider_selector_new (void)
{
  return g_object_new (GTD_TYPE_PROVIDER_SELECTOR, NULL);
}

/**
 * gtd_provider_selector_show_local:
 *
 * Shows a row for local provider item.
 *
 * Returns:
 */
void
gtd_provider_selector_show_local (GtdProviderSelector *selector,
                                 gboolean            show)
{
  GtdProviderSelectorPrivate *priv;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));

  priv = selector->priv;

  if (priv->show_local_provider != show)
    {
      priv->show_local_provider = show;

      gtk_widget_set_visible (priv->local_check, !show);

      if (priv->local_row)
        gtk_widget_set_visible (priv->local_row, show);

      g_object_notify (G_OBJECT (selector), "show-local");
    }
}

/**
 * gtd_provider_selector_get_select_default:
 * @selector: a #GtdProviderSelector
 *
 * Whether the default provider location is selected by default.
 *
 * Returns: %TRUE if the default provider location is selected automatically,
 * %FALSE otherwise.
 */
gboolean
gtd_provider_selector_get_select_default (GtdProviderSelector *selector)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_SELECTOR (selector), FALSE);

  return selector->priv->select_default;
}

/**
 * gtd_provider_selector_set_select_default:
 * @selector: a #GtdProviderSelector
 * @select_default: %TRUE to auto select the default provider location.
 *
 * Whether @selector should select the default provider location by default.
 *
 * Returns:
 */
void
gtd_provider_selector_set_select_default (GtdProviderSelector *selector,
                                         gboolean            select_default)
{
  GtdProviderSelectorPrivate *priv;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));

  priv = selector->priv;

  if (priv->select_default != select_default)
    {
      priv->select_default = select_default;

      if (select_default)
        {
          GList *children;
          GList *l;

          /* Select the appropriate row */
          children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));

          for (l = children; l != NULL; l = l->next)
            {
              if (GTD_IS_PROVIDER_ROW (l->data))
                {
                  GtdProvider *provider = gtd_provider_row_get_provider (l->data);

                  if (FALSE)//gtd_provider_get_is_default (provider))
                    {
                      gtd_provider_row_set_selected (l->data, TRUE);
                      g_signal_emit (selector, signals[PROVIDER_SELECTED], 0, provider);
                    }
                }
            }

          g_list_free (children);
        }

      g_object_notify (G_OBJECT (selector), "select-default");
    }
}

/**
 * gtd_provider_selector_get_selected_provider:
 * @selector: a #GtdProviderSelector
 *
 * Retrieves the currently selected #GtdProvider, or %NULL if
 * none is selected.
 *
 * Returns: (transfer none): the selected #GtdProvider
 */
GtdProvider*
gtd_provider_selector_get_selected_provider (GtdProviderSelector *selector)
{
  GtdProviderSelectorPrivate *priv;
  GtdProvider *provider;
  GList *children;
  GList *l;

  g_return_val_if_fail (GTD_IS_PROVIDER_SELECTOR (selector), NULL);

  priv = selector->priv;
  provider = NULL;
  children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      if (GTD_IS_PROVIDER_ROW (l->data) && gtd_provider_row_get_selected (l->data))
        {
          provider = gtd_provider_row_get_provider (l->data);
          break;
        }
    }

  g_list_free (children);

  return provider;
}

/**
 * gtd_provider_selector_set_selected_provider:
 * @selector: a #GtdProviderSelector
 * @provider: a #GtdProvider
 *
 * Selects @provider in the given #GtdProviderSelector.
 *
 * Returns:
 */
void
gtd_provider_selector_set_selected_provider (GtdProviderSelector *selector,
                                           GtdProvider         *provider)
{
  GtdProviderSelectorPrivate *priv;
  GList *children;
  GList *l;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));

  priv = selector->priv;
  children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));

  for (l = children; l != NULL; l = l->next)
    {
      if (GTD_IS_PROVIDER_ROW (l->data))
        {
          gtd_provider_row_set_selected (l->data, gtd_provider_row_get_provider (l->data) == provider);
          g_signal_emit (selector, signals[PROVIDER_SELECTED], 0, provider);
        }
    }

  g_list_free (children);
}

/**
 * gtd_provider_selector_get_show_stub_rows:
 * @selector: a #GtdProviderSelector
 *
 * Retrieves the ::show-stub-rows property.
 *
 * Returns: %TRUE if it shows stub rows, %FALSE if it hides them.
 */
gboolean
gtd_provider_selector_get_show_stub_rows (GtdProviderSelector *selector)
{
  g_return_val_if_fail (GTD_IS_PROVIDER_SELECTOR (selector), FALSE);

  return selector->priv->show_stub_rows;
}

/**
 * gtd_provider_selector_set_show_stub_rows:
 * @selector: a #GtdProviderSelector
 * @show_stub_rows: %TRUE to show stub rows, %FALSE to hide them.
 *
 * Sets the #GtdProviderSelector::show-stub-rows property.
 *
 * Returns:
 */
void
gtd_provider_selector_set_show_stub_rows (GtdProviderSelector *selector,
                                         gboolean            show_stub_rows)
{
  GtdProviderSelectorPrivate *priv;

  g_return_if_fail (GTD_IS_PROVIDER_SELECTOR (selector));

  priv = selector->priv;

  if (priv->show_stub_rows != show_stub_rows)
    {
      priv->show_stub_rows = show_stub_rows;

      /*
       * If we're showing the stub rows, it must check which ones should be shown.
       * We don't want to show stub rows for
       */
      if (show_stub_rows)
        {
          GList *children;
          GList *l;
          gint google_counter;
          gint exchange_counter;
          gint owncloud_counter;

          children = gtk_container_get_children (GTK_CONTAINER (priv->listbox));
          google_counter = 0;
          exchange_counter = 0;
          owncloud_counter = 0;

          for (l = children; l != NULL; l = l->next)
            {
              if (GTD_IS_PROVIDER_ROW (l->data))
                {
                  GtdProvider *provider = gtd_provider_row_get_provider (l->data);
                  const gchar *type;

                  type = gtd_provider_get_id (provider);

                  if (g_strcmp0 (type, "google") == 0)
                    google_counter++;
                  else if (g_strcmp0 (type, "exchange") == 0)
                    exchange_counter++;
                  else if (g_strcmp0 (type, "owncloud") == 0)
                    owncloud_counter++;
                }
            }

          gtk_widget_set_visible (priv->google_stub_row, google_counter == 0);
          gtk_widget_set_visible (priv->exchange_stub_row, exchange_counter == 0);
          gtk_widget_set_visible (priv->owncloud_stub_row, owncloud_counter == 0);

          g_list_free (children);
        }
      else
        {
          gtk_widget_hide (priv->exchange_stub_row);
          gtk_widget_hide (priv->google_stub_row);
          gtk_widget_hide (priv->owncloud_stub_row);
        }

      g_object_notify (G_OBJECT (selector), "show-stub-rows");
    }
}