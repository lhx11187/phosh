/*
 * Copyright (C) 2018 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on  gnome-shell's shell-keyring-prompt.c
 * Author: Stef Walter <stefw@gnome.org>
 */

#define G_LOG_DOMAIN "phosh-system-prompt"

#include "config.h"

#include "system-prompt.h"

#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

#include <glib/gi18n.h>

/**
 * SECTION:phosh-system-prompt
 * @short_description: A modal system prompt
 * @Title: PhoshSystemPrompt
 *
 * The #PhoshSystemPrompt is used to ask for PINs and passwords
 */

enum {
  PROP_0,

  /* GcrPromptIface */
  PROP_TITLE,
  PROP_MESSAGE,
  PROP_DESCRIPTION,
  PROP_WARNING,
  PROP_CHOICE_LABEL,
  PROP_CHOICE_CHOSEN,
  PROP_PASSWORD_NEW,
  PROP_PASSWORD_STRENGTH,
  PROP_CALLER_WINDOW,
  PROP_CONTINUE_LABEL,
  PROP_CANCEL_LABEL,

  /* our own */
  PROP_PASSWORD_VISIBLE,
  PROP_CONFIRM_VISIBLE,
  PROP_WARNING_VISIBLE,
  PROP_CHOICE_VISIBLE,
  PROP_LAST_PROP,
};

typedef enum
{
  PROMPTING_NONE,
  PROMPTING_FOR_CONFIRM,
  PROMPTING_FOR_PASSWORD
} PromptingMode;


typedef struct
{
  gchar *title;
  gchar *message;
  gchar *description;
  gchar *warning;
  gchar *choice_label;
  gboolean choice_chosen;
  gboolean password_new;
  guint password_strength;
  gchar *continue_label;
  gchar *cancel_label;

  GtkWidget *grid;
  GtkWidget *btn_continue;
  GtkEntryBuffer *password_buffer;
  GtkEntryBuffer *confirm_buffer;

  GTask *task;
  GcrPromptReply last_reply;
  PromptingMode mode;
  gboolean shown;
} PhoshSystemPromptPrivate;


struct _PhoshSystemPrompt
{
  PhoshLayerSurface parent;
};


static void phosh_system_prompt_iface_init (GcrPromptIface *iface);
G_DEFINE_TYPE_WITH_CODE(PhoshSystemPrompt, phosh_system_prompt, PHOSH_TYPE_LAYER_SURFACE,
                        G_IMPLEMENT_INTERFACE (GCR_TYPE_PROMPT,
                                               phosh_system_prompt_iface_init)
                        G_ADD_PRIVATE (PhoshSystemPrompt));


static void
phosh_system_prompt_set_property (GObject      *obj,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PhoshSystemPrompt *self = PHOSH_SYSTEM_PROMPT (obj);
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);

  switch (prop_id) {
  case PROP_TITLE:
    g_free (priv->title);
    priv->title = g_value_dup_string (value);
    g_object_notify (obj, "title");
    break;
  case PROP_MESSAGE:
    g_free (priv->message);
    priv->message = g_value_dup_string (value);
    g_object_notify (obj, "message");
    break;
  case PROP_DESCRIPTION:
    g_free (priv->description);
    priv->description = g_value_dup_string (value);
    g_object_notify (obj, "description");
    break;
  case PROP_WARNING:
    g_free (priv->warning);
    priv->warning = g_value_dup_string (value);
    g_object_notify (obj, "warning");
    g_object_notify (obj, "warning-visible");
    break;
  case PROP_CHOICE_LABEL:
    g_free (priv->choice_label);
    priv->choice_label = g_value_dup_string (value);
    g_object_notify (obj, "choice-label");
    g_object_notify (obj, "choice-visible");
    break;
  case PROP_CHOICE_CHOSEN:
    priv->choice_chosen = g_value_get_boolean (value);
    g_object_notify (obj, "choice-chosen");
    break;
  case PROP_PASSWORD_NEW:
    priv->password_new = g_value_get_boolean (value);
    g_object_notify (obj, "password-new");
    g_object_notify (obj, "confirm-visible");
    break;
  case PROP_CALLER_WINDOW:
    /* ignored */
    break;
  case PROP_CONTINUE_LABEL:
    g_free (priv->continue_label);
    priv->continue_label = g_value_dup_string (value);
    g_object_notify (obj, "continue-label");
    break;
  case PROP_CANCEL_LABEL:
    g_free (priv->cancel_label);
    priv->cancel_label = g_value_dup_string (value);
    g_object_notify (obj, "cancel-label");
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    break;
  }
}


static void
phosh_system_prompt_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  PhoshSystemPrompt *self = PHOSH_SYSTEM_PROMPT (obj);
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);

  switch (prop_id) {
  case PROP_TITLE:
    g_value_set_string (value, priv->title ? priv->title : "");
    break;
  case PROP_MESSAGE:
    g_value_set_string (value, priv->message ? priv->message : "");
    break;
  case PROP_DESCRIPTION:
    g_value_set_string (value, priv->description ? priv->description : "");
    break;
  case PROP_WARNING:
    g_value_set_string (value, priv->warning ? priv->warning : "");
    break;
  case PROP_CHOICE_LABEL:
    g_value_set_string (value, priv->choice_label ? priv->choice_label : "");
    break;
  case PROP_CHOICE_CHOSEN:
    g_value_set_boolean (value, priv->choice_chosen);
    break;
  case PROP_PASSWORD_NEW:
    g_value_set_boolean (value, priv->password_new);
    break;
  case PROP_PASSWORD_STRENGTH:
    g_value_set_int (value, priv->password_strength);
    break;
  case PROP_CALLER_WINDOW:
    g_value_set_string (value, "");
    break;
  case PROP_CONTINUE_LABEL:
    g_value_set_string (value, priv->continue_label);
    break;
  case PROP_CANCEL_LABEL:
    g_value_set_string (value, priv->cancel_label);
    break;
  case PROP_PASSWORD_VISIBLE:
    g_value_set_boolean (value, priv->mode == PROMPTING_FOR_PASSWORD);
    break;
  case PROP_CONFIRM_VISIBLE:
    g_value_set_boolean (value, priv->password_new &&
                         priv->mode == PROMPTING_FOR_CONFIRM);
    break;
  case PROP_WARNING_VISIBLE:
    g_value_set_boolean (value, priv->warning && priv->warning[0]);
    break;
  case PROP_CHOICE_VISIBLE:
    g_value_set_boolean (value, priv->choice_label && priv->choice_label[0]);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    break;
  }
}



static void
phosh_system_prompt_password_async (GcrPrompt *prompt,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
  PhoshSystemPrompt *self = PHOSH_SYSTEM_PROMPT (prompt);
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);
  GObject *obj;

  g_debug ("Starting system password prompt: %s", __func__);
  if (priv->task != NULL) {
    g_warning ("this prompt can only show one prompt at a time");
    return;
  }
  priv->mode = PROMPTING_FOR_PASSWORD;
  priv->task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (priv->task, phosh_system_prompt_password_async);

  gtk_widget_set_sensitive (priv->btn_continue, TRUE);
  gtk_widget_set_sensitive (priv->grid, TRUE);

  obj = G_OBJECT (self);
  g_object_notify (obj, "password-visible");
  g_object_notify (obj, "confirm-visible");
  g_object_notify (obj, "warning-visible");
  g_object_notify (obj, "choice-visible");
  priv->shown = TRUE;
}


static const gchar *
phosh_system_prompt_password_finish (GcrPrompt *prompt,
                                     GAsyncResult *result,
                                     GError **error)
{
  g_return_val_if_fail (g_task_get_source_object (G_TASK (result)) == prompt, NULL);
  g_return_val_if_fail (g_async_result_is_tagged (result,
                                                  phosh_system_prompt_password_async), NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}


static void
phosh_system_prompt_close (GcrPrompt *prompt)
{
  PhoshSystemPrompt *self = PHOSH_SYSTEM_PROMPT (prompt);
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);

  if (!priv->shown)
    return;

  priv->shown = FALSE;
  gtk_widget_destroy (GTK_WIDGET (self));
}


static void
phosh_system_prompt_confirm_async (GcrPrompt *prompt,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
  PhoshSystemPrompt *self = PHOSH_SYSTEM_PROMPT (prompt);
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);
  GObject *obj;

  g_debug ("Starting system confirmation prompt: %s", __func__);
  if (priv->task != NULL) {
    g_warning ("this prompt can only show one prompt at a time");
    return;
  }
  priv->mode = PROMPTING_FOR_CONFIRM;
  priv->task = g_task_new (self, NULL, callback, user_data);
  g_task_set_source_tag (priv->task, phosh_system_prompt_confirm_async);

  gtk_widget_set_sensitive (priv->btn_continue, TRUE);
  gtk_widget_set_sensitive (priv->grid, TRUE);

  obj = G_OBJECT (self);
  g_object_notify (obj, "password-visible");
  g_object_notify (obj, "confirm-visible");
  g_object_notify (obj, "warning-visible");
  g_object_notify (obj, "choice-visible");
  priv->shown = TRUE;
}


static GcrPromptReply
phosh_system_prompt_confirm_finish (GcrPrompt *prompt,
                                    GAsyncResult *result,
                                    GError **error)
{
  GTask *task = G_TASK (result);
  gssize res;

  g_debug ("Finishing system confirmation prompt: %s", __func__);
  g_return_val_if_fail (g_task_get_source_object (task) == prompt,
                        GCR_PROMPT_REPLY_CANCEL);
  g_return_val_if_fail (g_async_result_is_tagged (result,
                                                  phosh_system_prompt_confirm_async), GCR_PROMPT_REPLY_CANCEL);

  res = g_task_propagate_int (task, error);
  return res == -1 ? GCR_PROMPT_REPLY_CANCEL : (GcrPromptReply)res;
}


static gboolean
prompt_complete (PhoshSystemPrompt *self)
{
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);
  GTask *res;
  PromptingMode mode;
  const gchar *password;
  const gchar *confirm;
  const gchar *env;

  g_return_val_if_fail (PHOSH_IS_SYSTEM_PROMPT (self), FALSE);
  g_return_val_if_fail (priv->mode != PROMPTING_NONE, FALSE);
  g_return_val_if_fail (priv->task != NULL, FALSE);

  password = gtk_entry_buffer_get_text (priv->password_buffer);

  if (priv->mode == PROMPTING_FOR_CONFIRM) {
    /* Is it a new password? */
    if (priv->password_new) {
      confirm = gtk_entry_buffer_get_text (priv->confirm_buffer);
      /* Do the passwords match? */
      if (!g_str_equal (password, confirm)) {
        gcr_prompt_set_warning (GCR_PROMPT (self), _("Passwords do not match."));
        return FALSE;
      }

      /* Don't allow blank passwords if in paranoid mode */
      env = g_getenv ("GNOME_KEYRING_PARANOID");
      if (env && *env) {
        gcr_prompt_set_warning (GCR_PROMPT (self), _("Password cannot be blank"));
        return FALSE;
      }
    }
  }

  res = priv->task;
  mode = priv->mode;
  priv->task = NULL;
  priv->mode = PROMPTING_NONE;

  if (mode == PROMPTING_FOR_CONFIRM)
    g_task_return_int (res, (gssize)GCR_PROMPT_REPLY_CONTINUE);
  else
    g_task_return_pointer (res, (gpointer)password, NULL);
  g_object_unref (res);

  gtk_widget_set_sensitive (priv->btn_continue, FALSE);
  gtk_widget_set_sensitive (priv->grid, FALSE);

  return TRUE;
}


static void
prompt_cancel (PhoshSystemPrompt *self)
{
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);
  GTask *res;
  PromptingMode mode;

  g_debug ("Canceling system password prompt: %s", __func__);
  g_return_if_fail (PHOSH_IS_SYSTEM_PROMPT (self));
  /*
   * If cancelled while not prompting, we should just close the prompt,
   * the user wants it to go away.
   */
  if (priv->mode == PROMPTING_NONE) {
    if (priv->shown)
      gcr_prompt_close (GCR_PROMPT (self));
    return;
  }

  g_return_if_fail (priv->task != NULL);

  res = priv->task;
  mode = priv->mode;
  priv->task = NULL;
  priv->mode = PROMPTING_NONE;

  if (mode == PROMPTING_FOR_CONFIRM)
    g_task_return_int (res, (gssize) GCR_PROMPT_REPLY_CANCEL);
  else
    g_task_return_pointer (res, NULL, NULL);
  g_object_unref (res);
}


static void
on_password_changed (GtkEditable *editable,
                     gpointer user_data)
{
  int upper, lower, digit, misc;
  const char *password;
  gdouble pwstrength;
  int length, i;

  password = gtk_entry_get_text (GTK_ENTRY (editable));

  /*
   * This code is based on the Master Password dialog in Firefox
   * (pref-masterpass.js)
   * Original code triple-licensed under the MPL, GPL, and LGPL
   * so is license-compatible with this file
   */

  length = strlen (password);
  upper = 0;
  lower = 0;
  digit = 0;
  misc = 0;

  for ( i = 0; i < length ; i++) {
    if (g_ascii_isdigit (password[i]))
      digit++;
    else if (g_ascii_islower (password[i]))
      lower++;
    else if (g_ascii_isupper (password[i]))
      upper++;
    else
      misc++;
  }

  if (length > 5)
    length = 5;
  if (digit > 3)
    digit = 3;
  if (upper > 3)
    upper = 3;
  if (misc > 3)
    misc = 3;

  pwstrength = ((length * 0.1) - 0.2) +
    (digit * 0.1) +
    (misc * 0.15) +
    (upper * 0.1);

  if (pwstrength < 0.0)
    pwstrength = 0.0;
  if (pwstrength > 1.0)
    pwstrength = 1.0;

  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (user_data), pwstrength);
}


static void
btn_continue_clicked_cb (PhoshSystemPrompt *self, GtkButton *btn)
{
  prompt_complete (self);
}


static void
btn_cancel_clicked_cb (PhoshSystemPrompt *self, GtkButton *btn)
{
  prompt_cancel (self);
}

static void
phosh_system_prompt_iface_init (GcrPromptIface *iface)
{
  iface->prompt_confirm_async = phosh_system_prompt_confirm_async;
  iface->prompt_confirm_finish = phosh_system_prompt_confirm_finish;
  iface->prompt_password_async = phosh_system_prompt_password_async;
  iface->prompt_password_finish = phosh_system_prompt_password_finish;
  iface->prompt_close = phosh_system_prompt_close;
}

static gboolean
draw_cb (GtkWidget *widget, cairo_t *cr, gpointer unused)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  GdkRGBA c;

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_style_context_get_background_color (context, GTK_STATE_FLAG_NORMAL, &c);
  G_GNUC_END_IGNORE_DEPRECATIONS
    cairo_set_source_rgba (cr, c.red, c.green, c.blue, 0.7);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);
  return FALSE;
}


static void
phosh_system_prompt_dispose (GObject *obj)
{
  PhoshSystemPrompt *self = PHOSH_SYSTEM_PROMPT (obj);
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);

  if (priv->shown)
    gcr_prompt_close (GCR_PROMPT (self));

  if (priv->task)
    prompt_cancel (self);

  g_assert (priv->task == NULL);
  G_OBJECT_CLASS (phosh_system_prompt_parent_class)->dispose (obj);
}


static void
phosh_system_prompt_finalize (GObject *obj)
{
  PhoshSystemPrompt *self = PHOSH_SYSTEM_PROMPT (obj);
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);

  g_free (priv->title);
  g_free (priv->message);
  g_free (priv->description);
  g_free (priv->warning);
  g_free (priv->choice_label);
  g_free (priv->continue_label);
  g_free (priv->cancel_label);

  G_OBJECT_CLASS (phosh_system_prompt_parent_class)->finalize (obj);
}


static void
phosh_system_prompt_constructed (GObject *object)
{
  PhoshSystemPrompt *self = PHOSH_SYSTEM_PROMPT (object);
  PhoshSystemPromptPrivate *priv = phosh_system_prompt_get_instance_private (self);
  GtkBuilder *builder;
  GtkWidget *widget;
  GtkEntry *entry;

  G_OBJECT_CLASS (phosh_system_prompt_parent_class)->constructed (object);

  /*
   * We use gtk_buidler_* instead of the nicer
   * gtk_widget_class_set_template_from_resource since
   * PhoshLayerSuface isn't supported as type in glade yet.
   */
  builder = gtk_builder_new_from_resource ("/sm/puri/phosh/ui/system-prompt.ui");
  widget = GTK_WIDGET (gtk_builder_get_object (builder, "grid_system_prompt"));
  gtk_container_add (GTK_CONTAINER (self), widget);
  gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);
  gtk_widget_set_halign (widget, GTK_ALIGN_CENTER);
  priv->grid = widget;

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "lbl_message"));
  g_object_bind_property (self, "message", widget, "label", G_BINDING_DEFAULT);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "lbl_description"));
  g_object_bind_property (self, "description", widget, "label", G_BINDING_DEFAULT);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "lbl_password"));
  g_object_bind_property (self, "password-visible", widget, "visible", G_BINDING_DEFAULT);

  priv->password_buffer = gcr_secure_entry_buffer_new ();
  entry =
    GTK_ENTRY (gtk_builder_get_object (builder, "entry_password"));
  gtk_entry_set_buffer (GTK_ENTRY (entry), GTK_ENTRY_BUFFER (priv->password_buffer));
  g_object_bind_property (self, "password-visible", entry, "visible", G_BINDING_DEFAULT);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "lbl_confirm"));
  g_object_bind_property (self, "confirm-visible", widget, "visible", G_BINDING_DEFAULT);

  priv->confirm_buffer = gcr_secure_entry_buffer_new ();
  widget =
    GTK_WIDGET (gtk_builder_get_object (builder, "entry_confirm"));
  gtk_entry_set_buffer (GTK_ENTRY (widget), GTK_ENTRY_BUFFER (priv->confirm_buffer));
  g_object_bind_property (self, "confirm-visible", widget, "visible", G_BINDING_DEFAULT);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "pbar_quality"));
  g_object_bind_property (self, "confirm-visible", widget, "visible", G_BINDING_DEFAULT);
  g_signal_connect (entry, "changed", G_CALLBACK (on_password_changed), widget);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "lbl_warning"));
  g_object_bind_property (self, "warning", widget, "label", G_BINDING_DEFAULT);
  g_object_bind_property (self, "warning-visible", widget, "visible", G_BINDING_DEFAULT);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "checkbtn_choice"));
  g_object_bind_property (self, "choice-label", widget, "label", G_BINDING_DEFAULT);
  g_object_bind_property (self, "choice-visible", widget, "visible", G_BINDING_DEFAULT);
  g_object_bind_property (self, "choice-chosen", widget, "active", G_BINDING_BIDIRECTIONAL);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "btn_cancel"));
  g_object_bind_property (self, "cancel-label", widget, "label", G_BINDING_DEFAULT);
  g_signal_connect_object (widget,
                           "clicked",
                           G_CALLBACK (btn_cancel_clicked_cb),
                           self,
                           G_CONNECT_SWAPPED);

  widget = GTK_WIDGET (gtk_builder_get_object (builder, "btn_continue"));
  g_object_bind_property (self, "continue-label", widget, "label", G_BINDING_DEFAULT);
  g_signal_connect_object (widget,
                           "clicked",
                           G_CALLBACK (btn_continue_clicked_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_grab_default (widget);
  priv->btn_continue = widget;

  gtk_style_context_add_class (
    gtk_widget_get_style_context (GTK_WIDGET (self)),
    "phosh-system-prompt");

  gtk_widget_set_app_paintable(GTK_WIDGET (self), TRUE);
  g_signal_connect (G_OBJECT(self),
                    "draw",
                    G_CALLBACK(draw_cb),
                    NULL);
}


static void
phosh_system_prompt_class_init (PhoshSystemPromptClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->get_property = phosh_system_prompt_get_property;
  object_class->set_property = phosh_system_prompt_set_property;
  object_class->constructed = phosh_system_prompt_constructed;
  object_class->dispose = phosh_system_prompt_dispose;
  object_class->finalize = phosh_system_prompt_finalize;

  g_object_class_override_property (object_class, PROP_TITLE, "title");
  g_object_class_override_property (object_class, PROP_MESSAGE, "message");
  g_object_class_override_property (object_class, PROP_DESCRIPTION, "description");
  g_object_class_override_property (object_class, PROP_WARNING, "warning");
  g_object_class_override_property (object_class, PROP_PASSWORD_NEW, "password-new");
  g_object_class_override_property (object_class, PROP_PASSWORD_STRENGTH, "password-strength");
  g_object_class_override_property (object_class, PROP_CHOICE_LABEL, "choice-label");
  g_object_class_override_property (object_class, PROP_CHOICE_CHOSEN, "choice-chosen");
  g_object_class_override_property (object_class, PROP_CALLER_WINDOW, "caller-window");
  g_object_class_override_property (object_class, PROP_CONTINUE_LABEL, "continue-label");
  g_object_class_override_property (object_class, PROP_CANCEL_LABEL, "cancel-label");

  /**
   * GcrPromptDialog:password-visible:
   *
   * Whether the password entry is visible or not.
   */
  g_object_class_install_property (object_class, PROP_PASSWORD_VISIBLE,
                                   g_param_spec_boolean ("password-visible", "Password visible", "Password field is visible",
                                                         FALSE, G_PARAM_READABLE));

  /**
   * GcrPromptDialog:confirm-visible:
   *
   * Whether the password confirm entry is visible or not.
   */
  g_object_class_install_property (object_class, PROP_CONFIRM_VISIBLE,
                                   g_param_spec_boolean ("confirm-visible", "Confirm visible", "Confirm field is visible",
                                                         FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * PhoshSystemPrompt:warning-visible:
   *
   * Whether the warning label is visible or not.
   */
  g_object_class_install_property (object_class, PROP_WARNING_VISIBLE,
                                   g_param_spec_boolean ("warning-visible", "Warning visible", "Warning is visible",
                                                         FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * PhoshSystemPrompt:choice-visible:
   *
   * Whether the choice check box is visible or not.
   */
  g_object_class_install_property (object_class, PROP_CHOICE_VISIBLE,
                                   g_param_spec_boolean ("choice-visible", "Choice visible", "Choice is visible",
                                                         FALSE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}


static void
phosh_system_prompt_init (PhoshSystemPrompt *self)
{
  /*
   * This is a stupid hack to help the window act like a normal object
   * with regards to reference counting and unref. (see gcr's
   * gcr_prompt_dialog_init ui/gcr-prompt-dialog.c as of
   * e0a506eeb29bc6be01a96e805e0244a03428ebf5.
   * Otherwise it gets clean up too early.
   */
  gtk_window_set_has_user_ref_count (GTK_WINDOW (self), FALSE);
}


GtkWidget *
phosh_system_prompt_new (gpointer layer_shell,
                         gpointer wl_output)
{
  return g_object_new (PHOSH_TYPE_SYSTEM_PROMPT,
                       "layer-shell", layer_shell,
                       "wl-output", wl_output,
                       "anchor", ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                       ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
                       "layer", ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                       "kbd-interactivity", TRUE,
                       "exclusive-zone", -1,
                       "namespace", "phosh prompter",
                       NULL);
}