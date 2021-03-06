/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager Connection editor -- Connection editor for NetworkManager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * (C) Copyright 2008 - 2009 Red Hat, Inc.
 */

#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <glade/glade.h>
#include <gtk/gtk.h>

#include <NetworkManager.h>
#include <nm-setting-gsm.h>
#include <nm-setting-cdma.h>
#include <nm-client.h>
#include <nm-gsm-device.h>
#include <nm-cdma-device.h>

#include "mobile-wizard.h"
#include "nmn-mobile-providers.h"
#include "utils.h"

#define DEVICE_TAG "device"
#define TYPE_TAG "setting-type"

static char *get_selected_country (MobileWizard *self, gboolean *unlisted);
static NmnMobileProvider *get_selected_provider (MobileWizard *self);
static NmnMobileAccessMethodType get_provider_unlisted_type (MobileWizard *self);
static NmnMobileAccessMethod *get_selected_method (MobileWizard *self, gboolean *manual);

struct MobileWizard {
	GtkWidget *assistant;
	MobileWizardCallback callback;
	gpointer user_data;
	GHashTable *providers;
	GHashTable *country_codes;
	NmnMobileAccessMethodType method_type;
	gboolean initial_method_type;
	gboolean will_connect_after;

	/* Intro page */
	GtkWidget *dev_combo;
	GtkTreeStore *dev_store;
	char *dev_desc;
	NMClient *client;

	/* Country page */
	guint32 country_idx;
	const char *country;
	GtkWidget *country_page;
	GtkWidget *country_view;
	GtkTreeStore *country_store;
	GtkTreeModelSort *country_sort;
	guint32 country_focus_id;

	/* Providers page */
	guint32 providers_idx;
	GtkWidget *providers_page;
	GtkWidget *providers_view;
	GtkTreeStore *providers_store;
	GtkTreeModelSort *providers_sort;
	guint32 providers_focus_id;
	GtkWidget *providers_view_radio;

	GtkWidget *provider_unlisted_radio;
	GtkWidget *provider_unlisted_entry;
	GtkWidget *provider_unlisted_type_combo;

	gboolean provider_only_cdma;

	/* Plan page */
	guint32 plan_idx;
	GtkWidget *plan_page;
	GtkWidget *plan_combo;
	GtkTreeStore *plan_store;
	guint32 plan_focus_id;

	GtkWidget *plan_unlisted_entry;

	/* Confirm page */
	GtkWidget *confirm_page;
	GtkWidget *confirm_provider;
	GtkWidget *confirm_plan;
	GtkWidget *confirm_apn;
	GtkWidget *confirm_plan_label;
	GtkWidget *confirm_device;
	GtkWidget *confirm_device_label;
	guint32 confirm_idx;
};

static void
assistant_closed (GtkButton *button, gpointer user_data)
{
	MobileWizard *self = user_data;
	NmnMobileProvider *provider;
	NmnMobileAccessMethod *method;
	MobileWizardAccessMethod *wiz_method;
	NmnMobileAccessMethodType method_type = self->method_type;

	wiz_method = g_malloc0 (sizeof (MobileWizardAccessMethod));

	provider = get_selected_provider (self);
	if (!provider) {
		if (method_type == NMN_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN)
			method_type = get_provider_unlisted_type (self);

		wiz_method->provider_name = g_strdup (gtk_entry_get_text (GTK_ENTRY (self->provider_unlisted_entry)));
		if (method_type == NMN_MOBILE_ACCESS_METHOD_TYPE_GSM)
			wiz_method->gsm_apn = g_strdup (gtk_entry_get_text (GTK_ENTRY (self->plan_unlisted_entry)));
	} else {
		gboolean manual = FALSE;

		wiz_method->provider_name = g_strdup (provider->name);
		method = get_selected_method (self, &manual);
		if (method) {
			if (method->name)
				wiz_method->plan_name = g_strdup (method->name);
			method_type = method->type;
			if (method_type == NMN_MOBILE_ACCESS_METHOD_TYPE_GSM)
				wiz_method->gsm_apn = g_strdup (method->gsm_apn);
			wiz_method->username = method->username ? g_strdup (method->username) : NULL;
			wiz_method->password = method->password ? g_strdup (method->password) : NULL;
		} else {
			if (self->provider_only_cdma)
				method_type = NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA;
			else {
				method_type = NMN_MOBILE_ACCESS_METHOD_TYPE_GSM;
				wiz_method->gsm_apn = g_strdup (gtk_entry_get_text (GTK_ENTRY (self->plan_unlisted_entry)));
			}
		}
	}

	switch (method_type) {
	case NMN_MOBILE_ACCESS_METHOD_TYPE_GSM:
		wiz_method->devtype = NM_DEVICE_TYPE_GSM;
		break;
	case NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA:
		wiz_method->devtype = NM_DEVICE_TYPE_CDMA;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	(*(self->callback)) (self, FALSE, wiz_method, self->user_data);

	g_free (wiz_method->provider_name);
	g_free (wiz_method->plan_name);
	g_free (wiz_method->username);
	g_free (wiz_method->password);
	g_free (wiz_method->gsm_apn);
	g_free (wiz_method);
}

static void
assistant_cancel (GtkButton *button, gpointer user_data)
{
	MobileWizard *self = user_data;

	(*(self->callback)) (self, TRUE, NULL, self->user_data);
}

/**********************************************************/
/* Confirm page */
/**********************************************************/

static void
confirm_setup (MobileWizard *self)
{
	GtkWidget *vbox, *label, *alignment, *pbox;

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
	label = gtk_label_new (_("Your mobile broadband connection is configured with the following settings:"));
	gtk_widget_set_size_request (label, 500, -1);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 6);

	/* Device */
	self->confirm_device_label = gtk_label_new (_("Your Device:"));
	gtk_misc_set_alignment (GTK_MISC (self->confirm_device_label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), self->confirm_device_label, FALSE, FALSE, 0);

	alignment = gtk_alignment_new (0, 0.5, 0, 0);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 12, 25, 0);
	self->confirm_device = gtk_label_new (NULL);
	gtk_container_add (GTK_CONTAINER (alignment), self->confirm_device);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

	/* Provider */
	label = gtk_label_new (_("Your Provider:"));
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	alignment = gtk_alignment_new (0, 0.5, 0, 0);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 12, 25, 0);
	self->confirm_provider = gtk_label_new (NULL);
	gtk_container_add (GTK_CONTAINER (alignment), self->confirm_provider);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

	/* Plan and APN */
	self->confirm_plan_label = gtk_label_new (_("Your Plan:"));
	gtk_misc_set_alignment (GTK_MISC (self->confirm_plan_label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), self->confirm_plan_label, FALSE, FALSE, 0);

	alignment = gtk_alignment_new (0, 0.5, 0, 0);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 0, 25, 0);
	pbox = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (alignment), pbox);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

	self->confirm_plan = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (self->confirm_plan), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (pbox), self->confirm_plan, FALSE, FALSE, 0);

	self->confirm_apn = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (self->confirm_apn), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (self->confirm_apn), 0, 6);
	gtk_box_pack_start (GTK_BOX (pbox), self->confirm_apn, FALSE, FALSE, 0);

	if (self->will_connect_after) {
		alignment = gtk_alignment_new (0, 0.5, 1, 0);
		label = gtk_label_new (_("A connection will now be made to your mobile broadband provider using the settings you selected.  If the connection fails or you cannot access network resources, double-check your settings.  To modify your mobile broadband connection settings, choose \"Network Connections\" from the System >> Preferences menu."));
		gtk_widget_set_size_request (label, 500, -1);
		gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
		gtk_misc_set_padding (GTK_MISC (label), 0, 6);
		gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
		gtk_container_add (GTK_CONTAINER (alignment), label);
		gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 6);
	}

	gtk_widget_show_all (vbox);
	self->confirm_idx = gtk_assistant_append_page (GTK_ASSISTANT (self->assistant), vbox);
	gtk_assistant_set_page_title (GTK_ASSISTANT (self->assistant),
	                              vbox, _("Confirm Mobile Broadband Settings"));

	gtk_assistant_set_page_complete (GTK_ASSISTANT (self->assistant), vbox, TRUE);
	gtk_assistant_set_page_type (GTK_ASSISTANT (self->assistant), vbox, GTK_ASSISTANT_PAGE_CONFIRM);

	self->confirm_page = vbox;
}

static void
confirm_prepare (MobileWizard *self)
{
	NmnMobileProvider *provider = NULL;
	NmnMobileAccessMethod *method = NULL;
	char *country = NULL;
	gboolean manual = FALSE;
	GString *str;

	country = get_selected_country (self, NULL);
	provider = get_selected_provider (self);
	if (provider)
		method = get_selected_method (self, &manual);

	/* Provider */
	str = g_string_new (NULL);
	if (provider)
		g_string_append (str, provider->name);
	else {
		const char *unlisted_provider;

		unlisted_provider = gtk_entry_get_text (GTK_ENTRY (self->provider_unlisted_entry));
		g_string_append (str, unlisted_provider);
	}

	if (country)
		g_string_append_printf (str, ", %s", country);
	gtk_label_set_text (GTK_LABEL (self->confirm_provider), str->str);
	g_string_free (str, TRUE);

	if (self->dev_desc)
		gtk_label_set_text (GTK_LABEL (self->confirm_device), self->dev_desc);
	else {
		gtk_widget_hide (self->confirm_device_label);
		gtk_widget_hide (self->confirm_device);
	}

	if (self->provider_only_cdma) {
		gtk_widget_hide (self->confirm_plan_label);
		gtk_widget_hide (self->confirm_plan);
		gtk_widget_hide (self->confirm_apn);
	} else {
		const char *apn = NULL;

		/* Plan */
		gtk_widget_show (self->confirm_plan_label);
		gtk_widget_show (self->confirm_plan);
		gtk_widget_show (self->confirm_apn);

		if (method) {
			gtk_label_set_text (GTK_LABEL (self->confirm_plan), method->name);
			apn = method->gsm_apn;
		} else {
			gtk_label_set_text (GTK_LABEL (self->confirm_plan), _("Unlisted"));
			apn = gtk_entry_get_text (GTK_ENTRY (self->plan_unlisted_entry));
		}

		str = g_string_new (NULL);
		g_string_append_printf (str, "<span color=\"#999999\">APN: %s</span>", apn);
		gtk_label_set_markup (GTK_LABEL (self->confirm_apn), str->str);
		g_string_free (str, TRUE);
	}
}

/**********************************************************/
/* Plan page */
/**********************************************************/

#define PLAN_COL_NAME 0
#define PLAN_COL_METHOD 1
#define PLAN_COL_MANUAL 2

static NmnMobileAccessMethod *
get_selected_method (MobileWizard *self, gboolean *manual)
{
	GtkTreeModel *model;
	NmnMobileAccessMethod *method = NULL;
	GtkTreeIter iter;
	gboolean is_manual = FALSE;

	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (self->plan_combo), &iter))
		return NULL;

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (self->plan_combo));
	if (!model)
		return NULL;

	gtk_tree_model_get (model, &iter,
	                    PLAN_COL_METHOD, &method,
	                    PLAN_COL_MANUAL, &is_manual,
	                    -1);
	if (is_manual) {
		if (manual)
			*manual = is_manual;
		if (method)
			nmn_mobile_access_method_unref (method);
		method = NULL;
	}

	return method;
}

static void
plan_update_complete (MobileWizard *self)
{
	GtkAssistant *assistant = GTK_ASSISTANT (self->assistant);
	gboolean is_manual = FALSE;
	NmnMobileAccessMethod *method;

	method = get_selected_method (self, &is_manual);
	if (method) {
		gtk_assistant_set_page_complete (assistant, self->plan_page, TRUE);
		nmn_mobile_access_method_unref (method);
	} else {
		const char *manual_apn;

		manual_apn = gtk_entry_get_text (GTK_ENTRY (self->plan_unlisted_entry));
		gtk_assistant_set_page_complete (assistant, self->plan_page,
		                                 (manual_apn && strlen (manual_apn)));
	}
}

static void
plan_combo_changed (MobileWizard *self)
{
	NmnMobileAccessMethod *method = NULL;
	gboolean is_manual = FALSE;

	method = get_selected_method (self, &is_manual);
	if (method) {
		gtk_entry_set_text (GTK_ENTRY (self->plan_unlisted_entry), method->gsm_apn);
		gtk_widget_set_sensitive (self->plan_unlisted_entry, FALSE);
	} else {
		gtk_entry_set_text (GTK_ENTRY (self->plan_unlisted_entry), "");
		gtk_widget_set_sensitive (self->plan_unlisted_entry, TRUE);
		gtk_widget_grab_focus (self->plan_unlisted_entry);
	}

	if (method)
		nmn_mobile_access_method_unref (method);

	plan_update_complete (self);
}

static gboolean
plan_row_separator_func (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	NmnMobileAccessMethod *method = NULL;
	gboolean is_manual = FALSE;
	gboolean draw_separator = FALSE;

	gtk_tree_model_get (model, iter,
	                    PLAN_COL_METHOD, &method,
	                    PLAN_COL_MANUAL, &is_manual,
	                    -1);
	if (!method && !is_manual)
		draw_separator = TRUE;
	if (method)
		nmn_mobile_access_method_unref (method);
	return draw_separator;
}

static void
plan_setup (MobileWizard *self)
{
	GtkWidget *vbox, *label, *alignment, *hbox, *image;
	GtkCellRenderer *renderer;

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	label = gtk_label_new_with_mnemonic (_("_Select your plan:"));
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	self->plan_store = gtk_tree_store_new (3, G_TYPE_STRING, NMN_TYPE_MOBILE_ACCESS_METHOD, G_TYPE_BOOLEAN);

	self->plan_combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (self->plan_store));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), self->plan_combo);
	gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (self->plan_combo),
	                                      plan_row_separator_func,
	                                      NULL,
	                                      NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->plan_combo), renderer, TRUE);
	gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (self->plan_combo), renderer, "text", PLAN_COL_NAME);

	g_signal_connect_swapped (self->plan_combo, "changed", G_CALLBACK (plan_combo_changed), self);

	alignment = gtk_alignment_new (0, 0.5, 0.5, 0);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 12, 0, 0);
	gtk_container_add (GTK_CONTAINER (alignment), self->plan_combo);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

	label = gtk_label_new_with_mnemonic (_("Selected plan _APN (Access Point Name):"));
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	self->plan_unlisted_entry = gtk_entry_new ();
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), self->plan_unlisted_entry);
	gtk_entry_set_max_length (GTK_ENTRY (self->plan_unlisted_entry), 40);
	g_signal_connect_swapped (self->plan_unlisted_entry, "changed", G_CALLBACK (plan_update_complete), self);

	alignment = gtk_alignment_new (0, 0.5, 0.5, 0);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 24, 0, 0);
	gtk_container_add (GTK_CONTAINER (alignment), self->plan_unlisted_entry);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

	hbox = gtk_hbox_new (FALSE, 6);
	image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0.0);
	gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

	label = gtk_label_new (_("Warning: Selecting an incorrect plan may result in billing issues for your broadband account or may prevent connectivity.\n\nIf you are unsure of your plan please ask your provider for your plan's APN."));
	gtk_widget_set_size_request (label, 500, -1);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

	self->plan_idx = gtk_assistant_append_page (GTK_ASSISTANT (self->assistant), vbox);
	gtk_assistant_set_page_title (GTK_ASSISTANT (self->assistant), vbox, _("Choose your Billing Plan"));
	gtk_assistant_set_page_type (GTK_ASSISTANT (self->assistant), vbox, GTK_ASSISTANT_PAGE_CONTENT);
	gtk_widget_show_all (vbox);

	self->plan_page = vbox;
}

static void
plan_prepare (MobileWizard *self)
{
	NmnMobileProvider *provider;
	GtkTreeIter method_iter;

	gtk_tree_store_clear (self->plan_store);

	provider = get_selected_provider (self);
	if (provider) {
		GSList *iter;
		guint32 count = 0;

		for (iter = provider->methods; iter; iter = g_slist_next (iter)) {
			NmnMobileAccessMethod *method = iter->data;

			if (   (self->method_type != NMN_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN)
			    && (method->type != self->method_type))
				continue;

			gtk_tree_store_append (GTK_TREE_STORE (self->plan_store), &method_iter, NULL);
			gtk_tree_store_set (GTK_TREE_STORE (self->plan_store),
			                    &method_iter,
			                    PLAN_COL_NAME,
			                    method->name,
			                    PLAN_COL_METHOD,
			                    method,
			                    -1);
			count++;
		}

		/* Draw the separator */
		if (count)
			gtk_tree_store_append (GTK_TREE_STORE (self->plan_store), &method_iter, NULL);
	}

	/* Add the "My plan is not listed..." item */
	gtk_tree_store_append (GTK_TREE_STORE (self->plan_store), &method_iter, NULL);
	gtk_tree_store_set (GTK_TREE_STORE (self->plan_store),
	                    &method_iter,
	                    PLAN_COL_NAME,
	                    _("My plan is not listed..."),
	                    PLAN_COL_MANUAL,
	                    TRUE,
	                    -1);

	/* Select the first item by default if nothing is yet selected */
	if (gtk_combo_box_get_active (GTK_COMBO_BOX (self->plan_combo)) < 0)
		gtk_combo_box_set_active (GTK_COMBO_BOX (self->plan_combo), 0);

	plan_combo_changed (self);
}

/**********************************************************/
/* Providers page */
/**********************************************************/

#define PROVIDER_COL_NAME 0
#define PROVIDER_COL_PROVIDER 1

static gboolean
providers_search_func (GtkTreeModel *model,
                       gint column,
                       const char *key,
                       GtkTreeIter *iter,
                       gpointer search_data)
{
	gboolean unmatched = TRUE;
	char *provider = NULL;

	if (!key)
		return TRUE;

	gtk_tree_model_get (model, iter, column, &provider, -1);
	if (!provider)
		return TRUE;

	unmatched = !!g_ascii_strncasecmp (provider, key, strlen (key));
	g_free (provider);
	return unmatched;
}

static NmnMobileProvider *
get_selected_provider (MobileWizard *self)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	NmnMobileProvider *provider = NULL;

	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->providers_view_radio)))
		return NULL;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->providers_view));
	g_assert (selection);

	if (!gtk_tree_selection_get_selected (GTK_TREE_SELECTION (selection), &model, &iter))
		return NULL;

	gtk_tree_model_get (model, &iter, PROVIDER_COL_PROVIDER, &provider, -1);
	return provider;
}

static void
providers_update_complete (MobileWizard *self)
{
	GtkAssistant *assistant = GTK_ASSISTANT (self->assistant);
	gboolean use_view;

	use_view = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->providers_view_radio));
	if (use_view) {
		NmnMobileProvider *provider;

		provider = get_selected_provider (self);
		gtk_assistant_set_page_complete (assistant, self->providers_page, !!provider);
		if (provider)
			nmn_mobile_provider_unref (provider);
	} else {
		const char *manual_provider;

		manual_provider = gtk_entry_get_text (GTK_ENTRY (self->provider_unlisted_entry));
		gtk_assistant_set_page_complete (assistant, self->providers_page,
		                                 (manual_provider && strlen (manual_provider)));
	}
}

static gboolean
focus_providers_view (gpointer user_data)
{
	MobileWizard *self = user_data;

	self->providers_focus_id = 0;
	gtk_widget_grab_focus (self->providers_view);
	return FALSE;
}

static gboolean
focus_provider_unlisted_entry (gpointer user_data)
{
	MobileWizard *self = user_data;

	self->providers_focus_id = 0;
	gtk_widget_grab_focus (self->provider_unlisted_entry);
	return FALSE;
}

static void
providers_radio_toggled (GtkToggleButton *button, gpointer user_data)
{
	MobileWizard *self = user_data;
	gboolean use_view;

	use_view = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->providers_view_radio));
	if (use_view) {
		if (!self->providers_focus_id)
			self->providers_focus_id = g_idle_add (focus_providers_view, self);
		gtk_widget_set_sensitive (self->providers_view, TRUE);
		gtk_widget_set_sensitive (self->provider_unlisted_entry, FALSE);
		gtk_widget_set_sensitive (self->provider_unlisted_type_combo, FALSE);
	} else {
		if (!self->providers_focus_id)
			self->providers_focus_id = g_idle_add (focus_provider_unlisted_entry, self);
		gtk_widget_set_sensitive (self->providers_view, FALSE);
		gtk_widget_set_sensitive (self->provider_unlisted_entry, TRUE);
		gtk_widget_set_sensitive (self->provider_unlisted_type_combo, TRUE);
	}

	providers_update_complete (self);
}

static NmnMobileAccessMethodType
get_provider_unlisted_type (MobileWizard *self)
{
	switch (gtk_combo_box_get_active (GTK_COMBO_BOX (self->provider_unlisted_type_combo))) {
	case 0:
		return NMN_MOBILE_ACCESS_METHOD_TYPE_GSM;
	case 1:
		return NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA;
	default:
		return NMN_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN;
	}
}

static void
providers_setup (MobileWizard *self)
{
	GtkWidget *vbox, *scroll, *alignment, *unlisted_table, *label;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	self->providers_view_radio = gtk_radio_button_new_with_mnemonic (NULL, _("Select your provider from a _list:"));
	g_signal_connect (self->providers_view_radio, "toggled", G_CALLBACK (providers_radio_toggled), self);
	gtk_box_pack_start (GTK_BOX (vbox), self->providers_view_radio, FALSE, TRUE, 0);

	self->providers_store = gtk_tree_store_new (2, G_TYPE_STRING, NMN_TYPE_MOBILE_PROVIDER);

	self->providers_sort = GTK_TREE_MODEL_SORT (gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (self->providers_store)));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (self->providers_sort),
	                                      PROVIDER_COL_NAME, GTK_SORT_ASCENDING);

	self->providers_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (self->providers_sort));

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Provider"),
	                                                   renderer,
	                                                   "text", PROVIDER_COL_NAME,
	                                                   NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (self->providers_view), column);
	gtk_tree_view_column_set_clickable (column, TRUE);

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->providers_view));
	g_assert (selection);
	g_signal_connect_swapped (selection, "changed", G_CALLBACK (providers_update_complete), self);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
	                                GTK_POLICY_NEVER,
	                                GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request (scroll, -1, 140);
	gtk_container_add (GTK_CONTAINER (scroll), self->providers_view);

	alignment = gtk_alignment_new (0, 0, 1, 1);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 12, 25, 0);
	gtk_container_add (GTK_CONTAINER (alignment), scroll);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, TRUE, TRUE, 0);

	self->provider_unlisted_radio = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON (self->providers_view_radio),
	                                            _("I can't find my provider and I wish to enter it _manually:"));
	g_signal_connect (self->providers_view_radio, "toggled", G_CALLBACK (providers_radio_toggled), self);
	gtk_box_pack_start (GTK_BOX (vbox), self->provider_unlisted_radio, FALSE, TRUE, 0);

	alignment = gtk_alignment_new (0, 0, 0, 0);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 0, 15, 0);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

	unlisted_table = gtk_table_new (2, 2, FALSE);
	gtk_container_add (GTK_CONTAINER (alignment), unlisted_table);

	label = gtk_label_new (_("Provider:"));
	gtk_misc_set_alignment (GTK_MISC (label), 1, 0.5);
	gtk_table_attach (GTK_TABLE (unlisted_table), label, 0, 1, 0, 1, 0, 0, 6, 6);

	self->provider_unlisted_entry = gtk_entry_new ();
	gtk_entry_set_width_chars (GTK_ENTRY (self->provider_unlisted_entry), 40);
	g_signal_connect_swapped (self->provider_unlisted_entry, "changed", G_CALLBACK (providers_update_complete), self);

	alignment = gtk_alignment_new (0, 0.5, 0.66, 0);
	gtk_container_add (GTK_CONTAINER (alignment), self->provider_unlisted_entry);
	gtk_table_attach (GTK_TABLE (unlisted_table), alignment,
	                  1, 2, 0, 1, GTK_EXPAND | GTK_FILL, 0, 6, 6);

	self->provider_unlisted_type_combo = gtk_combo_box_new_text ();
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), self->provider_unlisted_type_combo);
	gtk_combo_box_append_text (GTK_COMBO_BOX (self->provider_unlisted_type_combo),
	                           _("My provider uses GSM technology (GPRS, EDGE, UMTS, HSPA)"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (self->provider_unlisted_type_combo),
	                           _("My provider uses CDMA technology (1xRTT, EVDO)"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (self->provider_unlisted_type_combo), 0);

	gtk_table_attach (GTK_TABLE (unlisted_table), self->provider_unlisted_type_combo,
	                  1, 2, 1, 2, 0, 0, 6, 6);

	/* Only show the CDMA/GSM combo if we don't know the device type */
	if (self->method_type != NMN_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN)
		gtk_widget_hide (self->provider_unlisted_type_combo);

	self->providers_idx = gtk_assistant_append_page (GTK_ASSISTANT (self->assistant), vbox);
	gtk_assistant_set_page_title (GTK_ASSISTANT (self->assistant), vbox, _("Choose your Provider"));
	gtk_assistant_set_page_type (GTK_ASSISTANT (self->assistant), vbox, GTK_ASSISTANT_PAGE_CONTENT);
	gtk_widget_show_all (vbox);

	self->providers_page = vbox;
}

static void
providers_prepare (MobileWizard *self)
{
	GtkTreeSelection *selection;
	GSList *providers, *piter;
	char *country;

	gtk_tree_store_clear (self->providers_store);

	country = get_selected_country (self, NULL);
	if (!country) {
		/* Unlisted country */
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->provider_unlisted_radio), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (self->providers_view_radio), FALSE);
		goto done;
	}

	providers = g_hash_table_lookup (self->providers, country);
	g_free (country);

	for (piter = providers; piter; piter = g_slist_next (piter)) {
		NmnMobileProvider *provider = piter->data;
		GtkTreeIter provider_iter;

		/* Ignore providers that don't match the current device type */
		if (self->method_type != NMN_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN) {
			GSList *miter;
			guint32 count = 0;

			for (miter = provider->methods; miter; miter = g_slist_next (miter)) {
				NmnMobileAccessMethod *method = miter->data;

				if (self->method_type == method->type)
					count++;
			}

			if (!count)
				continue;
		}

		gtk_tree_store_append (GTK_TREE_STORE (self->providers_store), &provider_iter, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (self->providers_store),
		                    &provider_iter,
		                    PROVIDER_COL_NAME,
		                    provider->name,
		                    PROVIDER_COL_PROVIDER,
		                    provider,
		                    -1);
	}

	g_object_set (G_OBJECT (self->providers_view), "enable-search", TRUE, NULL);

	gtk_tree_view_set_search_column (GTK_TREE_VIEW (self->providers_view), PROVIDER_COL_NAME);
	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (self->providers_view),
	                                     providers_search_func, self, NULL);

	/* If no row has focus yet, focus the first row so that the user can start
	 * incremental search without clicking.
	 */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->providers_view));
	g_assert (selection);
	if (!gtk_tree_selection_count_selected_rows (selection)) {
		GtkTreeIter first_iter;
		GtkTreePath *first_path;

		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->providers_sort), &first_iter)) {
			first_path = gtk_tree_model_get_path (GTK_TREE_MODEL (self->providers_sort), &first_iter);
			if (first_path) {
				gtk_tree_selection_select_path (selection, first_path);
				gtk_tree_path_free (first_path);
			}
		}
	}

done:
	providers_radio_toggled (NULL, self);

	/* Initial completeness state */
	providers_update_complete (self);

	/* If there's already a selected device, hide the GSM/CDMA radios */
	if (self->method_type != NMN_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN)
		gtk_widget_hide (self->provider_unlisted_type_combo);
	else
		gtk_widget_show (self->provider_unlisted_type_combo);
}

/**********************************************************/
/* Country page */
/**********************************************************/

static gboolean
country_search_func (GtkTreeModel *model,
                     gint column,
                     const char *key,
                     GtkTreeIter *iter,
                     gpointer search_data)
{
	gboolean unmatched = TRUE;
	char *country = NULL;

	if (!key)
		return TRUE;

	gtk_tree_model_get (model, iter, column, &country, -1);
	if (!country)
		return TRUE;

	unmatched = !!g_ascii_strncasecmp (country, key, strlen (key));
	g_free (country);
	return unmatched;
}

static void
add_one_country (gpointer key, gpointer value, gpointer user_data)
{
	MobileWizard *self = user_data;
	GtkTreeIter country_iter;
	GtkTreePath *country_path, *country_view_path;

	g_assert (key);

	gtk_tree_store_append (GTK_TREE_STORE (self->country_store), &country_iter, NULL);
	gtk_tree_store_set (GTK_TREE_STORE (self->country_store), &country_iter, 0, key, -1);

	/* If this country is the same country as the user's current locale,
	 * select it by default.
	 */
	if (!self->country || g_ascii_strcasecmp (self->country, key))
		return;

	country_path = gtk_tree_model_get_path (GTK_TREE_MODEL (self->country_store), &country_iter);
	if (!country_path)
		return;

	country_view_path = gtk_tree_model_sort_convert_child_path_to_path (self->country_sort, country_path);
	if (country_view_path) {
		GtkTreeSelection *selection;

		gtk_tree_view_expand_row (GTK_TREE_VIEW (self->country_view), country_view_path, TRUE);

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->country_view));
		g_assert (selection);
		gtk_tree_selection_select_path (selection, country_view_path);
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (self->country_view),
		                              country_view_path, NULL, TRUE, 0, 0);
		gtk_tree_path_free (country_view_path);
	}
	gtk_tree_path_free (country_path);
}

static char *
get_selected_country (MobileWizard *self, gboolean *out_unlisted)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	char *country = NULL;
	gboolean unlisted = FALSE;

	if (!self->country_codes)
		return NULL;

	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->country_view));
	g_assert (selection);

	if (gtk_tree_selection_get_selected (GTK_TREE_SELECTION (selection), &model, &iter)) {
		gtk_tree_model_get (model, &iter, 0, &country, 1, &unlisted, -1);
		if (unlisted) {
			g_free (country);
			country = NULL;
			if (out_unlisted)
				*out_unlisted = unlisted;
		}
	}

	return country;
}

static void
country_update_complete (MobileWizard *self)
{
	char *country = NULL;
	gboolean unlisted = FALSE;

	country = get_selected_country (self, &unlisted);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (self->assistant),
	                                 self->country_page,
	                                 (!!country || unlisted));
	g_free (country);
}

static gint
country_sort_func (GtkTreeModel *model,
                   GtkTreeIter *a,
                   GtkTreeIter *b,
                   gpointer user_data)
{
	char *a_str = NULL, *b_str = NULL;
	gboolean a_def = FALSE, b_def = FALSE;
	gint ret = 0;

	gtk_tree_model_get (model, a, 0, &a_str, 1, &a_def, -1);
	gtk_tree_model_get (model, b, 0, &b_str, 1, &b_def, -1);

	if (a_def) {
		ret = -1;
		goto out;
	} else if (b_def) {
		ret = 1;
		goto out;
	}

	if (a_str && !b_str)
		ret = -1;
	else if (!a_str && b_str)
		ret = 1;
	else if (!a_str && !b_str)
		ret = 0;
	else
		ret = g_utf8_collate (a_str, b_str);

out:
	g_free (a_str);
	g_free (b_str);
	return ret;
}

static void
country_setup (MobileWizard *self)
{
	GtkWidget *vbox, *label, *scroll, *alignment;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkTreeIter unlisted_iter;

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
	label = gtk_label_new (_("Country List:"));
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);

	self->country_store = gtk_tree_store_new (2, G_TYPE_STRING, G_TYPE_BOOLEAN);

	self->country_sort = GTK_TREE_MODEL_SORT (gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (self->country_store)));
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (self->country_sort), 0, GTK_SORT_ASCENDING);

	self->country_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (self->country_sort));

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (_("Country"), renderer, "text", 0, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (self->country_view), column);
	gtk_tree_view_column_set_clickable (column, TRUE);

	/* My country is not listed... */
	gtk_tree_store_append (GTK_TREE_STORE (self->country_store), &unlisted_iter, NULL);
	gtk_tree_store_set (GTK_TREE_STORE (self->country_store), &unlisted_iter,
	                    0, _("My country is not listed"),
	                    1, TRUE,
	                    -1);
	gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (self->country_sort),
	                                 0,
	                                 country_sort_func,
	                                 NULL,
	                                 NULL);

	/* Add the rest of the providers */
	if (self->providers)
		g_hash_table_foreach (self->providers, add_one_country, self);
	g_object_set (G_OBJECT (self->country_view), "enable-search", TRUE, NULL);

	/* If no row has focus yet, focus the first row so that the user can start
	 * incremental search without clicking.
	 */
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->country_view));
	g_assert (selection);
	if (!gtk_tree_selection_count_selected_rows (selection)) {
		GtkTreeIter first_iter;
		GtkTreePath *first_path;

		if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->country_sort), &first_iter)) {
			first_path = gtk_tree_model_get_path (GTK_TREE_MODEL (self->country_sort), &first_iter);
			if (first_path) {
				gtk_tree_selection_select_path (selection, first_path);
				gtk_tree_path_free (first_path);
			}
		}
	}

	g_signal_connect_swapped (selection, "changed", G_CALLBACK (country_update_complete), self);

	scroll = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroll), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
	                                GTK_POLICY_NEVER,
	                                GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (scroll), self->country_view);

	alignment = gtk_alignment_new (0, 0, 1, 1);
	gtk_container_add (GTK_CONTAINER (alignment), scroll);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, TRUE, TRUE, 6);

	self->country_idx = gtk_assistant_append_page (GTK_ASSISTANT (self->assistant), vbox);
	gtk_assistant_set_page_title (GTK_ASSISTANT (self->assistant), vbox, _("Choose your Provider's Country"));
	gtk_assistant_set_page_type (GTK_ASSISTANT (self->assistant), vbox, GTK_ASSISTANT_PAGE_CONTENT);
	gtk_assistant_set_page_complete (GTK_ASSISTANT (self->assistant), vbox, TRUE);
	gtk_widget_show_all (vbox);

	self->country_page = vbox;

	/* Initial completeness state */
	country_update_complete (self);
}

static gboolean
focus_country_view (gpointer user_data)
{
	MobileWizard *self = user_data;

	self->country_focus_id = 0;
	gtk_widget_grab_focus (self->country_view);
	return FALSE;
}

static void
country_prepare (MobileWizard *self)
{
	gtk_tree_view_set_search_column (GTK_TREE_VIEW (self->country_view), 0);
	gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (self->country_view), country_search_func, self, NULL);

	if (!self->country_focus_id)
		self->country_focus_id = g_idle_add (focus_country_view, self);

	country_update_complete (self);
}

/**********************************************************/
/* Intro page */
/**********************************************************/

#define INTRO_COL_NAME 0
#define INTRO_COL_DEVICE 1
#define INTRO_COL_SEPARATOR 2

static gboolean
__intro_device_added (MobileWizard *self, NMDevice *device, gboolean select_it)
{
	GtkTreeIter iter;
	const char *desc = utils_get_device_description (device);

	if (NM_IS_GSM_DEVICE (device)) {
		if (!desc)
			desc = _("Installed GSM device");
	} else if (NM_IS_CDMA_DEVICE (device)) {
		if (!desc)
			desc = _("Installed CDMA device");
	} else
		return FALSE;

	gtk_tree_store_append (GTK_TREE_STORE (self->dev_store), &iter, NULL);
	gtk_tree_store_set (GTK_TREE_STORE (self->dev_store),
	                    &iter,
	                    INTRO_COL_NAME, desc,
	                    INTRO_COL_DEVICE, device,
	                    -1);

	/* Select the device just added */
	if (select_it)
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (self->dev_combo), &iter);

	gtk_widget_set_sensitive (self->dev_combo, TRUE);
	return TRUE;
}

static void
intro_device_added_cb (NMClient *client, NMDevice *device, MobileWizard *self)
{
	__intro_device_added (self, device, TRUE);
}

static void
intro_device_removed_cb (NMClient *client, NMDevice *device, MobileWizard *self)
{
	GtkTreeIter iter;
	gboolean have_device = FALSE, removed = FALSE;

	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->dev_store), &iter))
		return;

	do {
		NMDevice *candidate = NULL;

		gtk_tree_model_get (GTK_TREE_MODEL (self->dev_store), &iter,
		                    INTRO_COL_DEVICE, &candidate, -1);
		if (candidate) {
			if (candidate == device) {
				gtk_tree_store_remove (GTK_TREE_STORE (self->dev_store), &iter);
				removed = TRUE;
			}
			g_object_unref (candidate);
		}
	} while (!removed && gtk_tree_model_iter_next (GTK_TREE_MODEL (self->dev_store), &iter));

	/* There's already a selected device item; nothing more to do */
	if (gtk_combo_box_get_active (GTK_COMBO_BOX (self->dev_combo)) > 1)
		return;

	/* If there are no more devices, select the "Any" item and disable the
	 * combo box.  If there is no selected item and there is at least one device
	 * item, select the first one.
	 */
	if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (self->dev_store), &iter))
		return;

	do {
		NMDevice *candidate = NULL;

		gtk_tree_model_get (GTK_TREE_MODEL (self->dev_store), &iter,
		                    INTRO_COL_DEVICE, &candidate, -1);
		if (candidate) {
			have_device = TRUE;
			g_object_unref (candidate);
		}
	} while (!have_device && gtk_tree_model_iter_next (GTK_TREE_MODEL (self->dev_store), &iter));

	if (have_device) {
		/* Iter should point to the last device item in the combo */
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (self->dev_combo), &iter);
	} else {
		gtk_combo_box_set_active (GTK_COMBO_BOX (self->dev_combo), 0);
		gtk_widget_set_sensitive (self->dev_combo, FALSE);
	}
}

static void
intro_add_initial_devices (MobileWizard *self)
{
	const GPtrArray *devices;
	gboolean selected_first = FALSE;
	int i;

	devices = nm_client_get_devices (self->client);
	for (i = 0; devices && (i < devices->len); i++) {
		if (__intro_device_added (self, g_ptr_array_index (devices, i), !selected_first)) {
			if (selected_first == FALSE)
				selected_first = TRUE;
		}
	}

	/* Otherwise the "Any device" item */
	if (!selected_first) {
		/* Select the first device item by default */
		gtk_combo_box_set_active (GTK_COMBO_BOX (self->dev_combo), 0);
		gtk_widget_set_sensitive (self->dev_combo, FALSE);
	}
}

static void
intro_remove_all_devices (MobileWizard *self)
{
	gtk_tree_store_clear (self->dev_store);

	/* Select the "Any device" item */
	gtk_combo_box_set_active (GTK_COMBO_BOX (self->dev_combo), 0);
	gtk_widget_set_sensitive (self->dev_combo, FALSE);
}

static void
intro_manager_running_cb (NMClient *client, GParamSpec *pspec, MobileWizard *self)
{
	if (nm_client_get_manager_running (client))
		intro_add_initial_devices (self);
	else
		intro_remove_all_devices (self);
}

static gboolean
intro_row_separator_func (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	gboolean separator = FALSE;
	gtk_tree_model_get (model, iter, INTRO_COL_SEPARATOR, &separator, -1);
	return separator;
}

static void
intro_combo_changed (MobileWizard *self)
{
	GtkTreeIter iter;
	NMDevice *selected = NULL;

	g_free (self->dev_desc);
	self->dev_desc = NULL;

	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (self->dev_combo), &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (self->dev_store), &iter,
	                    INTRO_COL_DEVICE, &selected, -1);
	if (selected) {
		self->dev_desc = g_strdup (utils_get_device_description (selected));
		if (NM_IS_GSM_DEVICE (selected))
			self->method_type = NMN_MOBILE_ACCESS_METHOD_TYPE_GSM;
		else if (NM_IS_CDMA_DEVICE (selected))
			self->method_type = NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA;
		else {
			g_warning ("%s: unknown device type '%s'", __func__,
			           G_OBJECT_TYPE_NAME (selected));
		}

		g_object_unref (selected);
	}
}

static void
intro_setup (MobileWizard *self)
{
	GtkWidget *vbox, *label, *alignment, *info_vbox;
	GtkCellRenderer *renderer;
	char *s;

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	label = gtk_label_new (_("This assistant helps you easily set up a mobile broadband connection to a cellular (3G) network."));
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 6);

	label = gtk_label_new (_("You will need the following information:"));
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 6);

	alignment = gtk_alignment_new (0, 0, 1, 0);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 25, 25, 0);
	info_vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_add (GTK_CONTAINER (alignment), info_vbox);
	gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 6);

	s = g_strdup_printf ("• %s", _("Your broadband provider's name"));
	label = gtk_label_new (s);
	g_free (s);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (info_vbox), label, FALSE, TRUE, 0);

	s = g_strdup_printf ("• %s", _("Your broadband billing plan name"));
	label = gtk_label_new (s);
	g_free (s);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (info_vbox), label, FALSE, TRUE, 0);

	s = g_strdup_printf ("• %s", _("(in some cases) Your broadband billing plan APN (Access Point Name)"));
	label = gtk_label_new (s);
	g_free (s);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_box_pack_start (GTK_BOX (info_vbox), label, FALSE, TRUE, 0);

	/* Device combo; only built if the wizard's caller didn't pass one in */
	if (!self->initial_method_type) {
		GtkTreeIter iter;

		self->client = nm_client_new ();
		g_signal_connect (self->client, "device-added",
		                  G_CALLBACK (intro_device_added_cb), self);
		g_signal_connect (self->client, "device-removed",
		                  G_CALLBACK (intro_device_removed_cb), self);
		g_signal_connect (self->client, "notify::manager-running",
		                  G_CALLBACK (intro_manager_running_cb), self);

		self->dev_store = gtk_tree_store_new (3, G_TYPE_STRING, NM_TYPE_DEVICE, G_TYPE_BOOLEAN);
		self->dev_combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (self->dev_store));
		gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (self->dev_combo),
		                                      intro_row_separator_func, NULL, NULL);

		renderer = gtk_cell_renderer_text_new ();
		gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (self->dev_combo), renderer, TRUE);
		gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (self->dev_combo), renderer, "text", INTRO_COL_NAME);

		label = gtk_label_new_with_mnemonic (_("Create a connection for _this mobile broadband device:"));
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), self->dev_combo);
		gtk_misc_set_alignment (GTK_MISC (label), 0, 1);
		gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

		alignment = gtk_alignment_new (0, 0, 0.5, 0);
		gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 0, 25, 0);
		gtk_container_add (GTK_CONTAINER (alignment), self->dev_combo);
		gtk_box_pack_start (GTK_BOX (vbox), alignment, FALSE, FALSE, 0);

		g_signal_connect_swapped (self->dev_combo, "changed", G_CALLBACK (intro_combo_changed), self);

		/* Any device */
		gtk_tree_store_append (GTK_TREE_STORE (self->dev_store), &iter, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (self->dev_store), &iter,
		                    INTRO_COL_NAME, _("Any device"), -1);

		/* Separator */
		gtk_tree_store_append (GTK_TREE_STORE (self->dev_store), &iter, NULL);
		gtk_tree_store_set (GTK_TREE_STORE (self->dev_store), &iter,
		                    INTRO_COL_SEPARATOR, TRUE, -1);

		intro_add_initial_devices (self);
	}

	gtk_widget_show_all (vbox);
	gtk_assistant_append_page (GTK_ASSISTANT (self->assistant), vbox);
	gtk_assistant_set_page_title (GTK_ASSISTANT (self->assistant),
	                              vbox, _("Set up a Mobile Broadband Connection"));

	gtk_assistant_set_page_complete (GTK_ASSISTANT (self->assistant), vbox, TRUE);
	gtk_assistant_set_page_type (GTK_ASSISTANT (self->assistant), vbox, GTK_ASSISTANT_PAGE_INTRO);
}

/**********************************************************/
/* General assistant stuff */
/**********************************************************/

static void
remove_provider_focus_idle (MobileWizard *self)
{
	if (self->providers_focus_id) {
		g_source_remove (self->providers_focus_id);
		self->providers_focus_id = 0;
	}
}

static void
remove_country_focus_idle (MobileWizard *self)
{
	if (self->country_focus_id) {
		g_source_remove (self->country_focus_id);
		self->country_focus_id = 0;
	}
}

static void
assistant_prepare (GtkAssistant *assistant, GtkWidget *page, gpointer user_data)
{
	MobileWizard *self = user_data;

	if (page != self->providers_page)
		remove_provider_focus_idle (self);
	if (page != self->country_page)
		remove_country_focus_idle (self);

	if (page == self->country_page)
		country_prepare (self);
	else if (page == self->providers_page)
		providers_prepare (self);
	else if (page == self->plan_page)
		plan_prepare (self);
	else if (page == self->confirm_page)
		confirm_prepare (self);
}

static gint
forward_func (gint current_page, gpointer user_data)
{
	MobileWizard *self = user_data;

	if (current_page == self->providers_idx) {
		NmnMobileAccessMethodType method_type = self->method_type;

		/* If the provider is unlisted, we can skip ahead of the user's
		 * access technology is CDMA.
		 */
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->provider_unlisted_radio))) {
			if (method_type == NMN_MOBILE_ACCESS_METHOD_TYPE_UNKNOWN)
				method_type = get_provider_unlisted_type (self);
		} else {
			/* Or, if the provider is only CDMA, then we can also skip ahead */
			NmnMobileProvider *provider;
			GSList *iter;
			gboolean gsm = FALSE, cdma = FALSE;

			provider = get_selected_provider (self);
			if (provider) {
				for (iter = provider->methods; iter; iter = g_slist_next (iter)) {
					NmnMobileAccessMethod *method = iter->data;

					if (method->type == NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA)
						cdma = TRUE;
					else if (method->type == NMN_MOBILE_ACCESS_METHOD_TYPE_GSM)
						gsm = TRUE;
				}
				nmn_mobile_provider_unref (provider);

				if (cdma && !gsm)
					method_type = NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA;
			}
		}

		/* Skip to the confirm page if we know its CDMA */
		if (method_type == NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA) {
			self->provider_only_cdma = TRUE;
			return self->confirm_idx;
		} else
			self->provider_only_cdma = FALSE;
	}

	return current_page + 1;
}

static char *
get_country_from_locale (void)
{
	char *p, *m, *cc, *lang;

	lang = getenv ("LC_ALL");
	if (!lang)
		lang = getenv ("LANG");
	if (!lang)
		return NULL;

	p = strchr (lang, '_');
	if (!p || !strlen (p)) {
		g_free (p);
		return NULL;
	}

	p = cc = g_strdup (++p);
	m = strchr (cc, '.');
	if (m)
		*m = '\0';

	while (*p) {
		*p = g_ascii_toupper (*p);
		p++;
	}

	return cc;
}

MobileWizard *
mobile_wizard_new (GtkWindow *parent,
                   GtkWindowGroup *window_group,
                   NMDeviceType devtype,
                   gboolean will_connect_after,
                   MobileWizardCallback cb,
                   gpointer user_data)
{
	MobileWizard *self;
	char *cc;

	self = g_malloc0 (sizeof (MobileWizard));
	g_return_val_if_fail (self != NULL, NULL);

	self->providers = nmn_mobile_providers_parse (&(self->country_codes));
	if (!self->providers || !self->country_codes) {
		mobile_wizard_destroy (self);
		return NULL;
	}

	cc = get_country_from_locale ();
	if (cc)
		self->country = g_hash_table_lookup (self->country_codes, cc);
	g_free (cc);

	self->will_connect_after = will_connect_after;
	self->callback = cb;
	self->user_data = user_data;
	if (devtype != NM_DEVICE_TYPE_UNKNOWN)
		self->initial_method_type = TRUE;
	switch (devtype) {
	case NM_DEVICE_TYPE_UNKNOWN:
		break;
	case NM_DEVICE_TYPE_GSM:
		self->method_type = NMN_MOBILE_ACCESS_METHOD_TYPE_GSM;
		break;
	case NM_DEVICE_TYPE_CDMA:
		self->method_type = NMN_MOBILE_ACCESS_METHOD_TYPE_CDMA;
		break;
	default:
		g_warning ("%s: invalid device type %d", __func__, devtype);
		mobile_wizard_destroy (self);
		return NULL;
	}

	self->assistant = gtk_assistant_new ();
	gtk_assistant_set_forward_page_func (GTK_ASSISTANT (self->assistant),
	                                     forward_func, self, NULL);
	gtk_window_set_title (GTK_WINDOW (self->assistant), _("New Mobile Broadband Connection"));
	gtk_window_set_position (GTK_WINDOW (self->assistant), GTK_WIN_POS_CENTER_ALWAYS);

	intro_setup (self);
	country_setup (self);
	providers_setup (self);
	plan_setup (self);
	confirm_setup (self);

	g_signal_connect (self->assistant, "close", G_CALLBACK (assistant_closed), self);
	g_signal_connect (self->assistant, "cancel", G_CALLBACK (assistant_cancel), self);
	g_signal_connect (self->assistant, "prepare", G_CALLBACK (assistant_prepare), self);

	/* Run the wizard */
	if (parent)
		gtk_window_set_transient_for (GTK_WINDOW (self->assistant), parent);
	gtk_window_set_modal (GTK_WINDOW (self->assistant), TRUE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (self->assistant), TRUE);
	gtk_window_set_type_hint (GTK_WINDOW (self->assistant), GDK_WINDOW_TYPE_HINT_DIALOG);

	if (window_group)
		gtk_window_group_add_window (window_group, GTK_WINDOW (self->assistant));

	return self;
}

void
mobile_wizard_present (MobileWizard *self)
{
	g_return_if_fail (self != NULL);

	gtk_window_present (GTK_WINDOW (self->assistant));
	gtk_widget_show_all (self->assistant);
}

void
mobile_wizard_destroy (MobileWizard *self)
{
	g_return_if_fail (self != NULL);

	g_free (self->dev_desc);

	if (self->assistant) {
		gtk_widget_hide (self->assistant);
		gtk_widget_destroy (self->assistant);
	}

	if (self->client)
		g_object_unref (self->client);

	remove_provider_focus_idle (self);
	remove_country_focus_idle (self);

	if (self->providers)
		g_hash_table_destroy (self->providers);

	if (self->country_codes)
		g_hash_table_destroy (self->country_codes);

	g_free (self);
}


