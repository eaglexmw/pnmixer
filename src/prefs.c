/* prefs.c
 * PNmixer is written by Nick Lanham, a fork of OBmixer
 * which was programmed by Lee Ferrett, derived
 * from the program "AbsVolume" by Paul Sherman
 * This program is free software; you can redistribute
 * it and/or modify it under the terms of the GNU General
 * Public License v3. source code is available at
 * <http://github.com/nicklan/pnmixer>
 */

/**
 * @file prefs.c
 * This file holds the preferences subsystem,
 * managing the user config file as well as interaction
 * with the gtk preferences window.
 * @brief preferences subsystem
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

#include "alsa.h"
#include "callbacks.h"
#include "prefs.h"
#include "support.h"
#include "main.h"
#include "hotkeys.h"
#include "debug.h"

#ifdef WITH_GTK3
#define PREFS_UI_FILE "prefs-gtk3.glade"
#else
#define PREFS_UI_FILE "prefs-gtk2.glade"
#endif

#define DEFAULT_PREFS "[PNMixer]\n\
SliderOrientation=vertical\n\
DisplayTextVolume=true\n\
TextVolumePosition=0\n\
ScrollStep=5\n\
FineScrollStep=1\n\
HotkeyVolumeStep=1\n\
MiddleClickAction=0\n\
CustomCommand=\n\
VolMuteKey=-1\n\
VolUpKey=-1\n\
VolDownKey=-1\n\
AlsaCard=default\n\
SystemTheme=false"

static GKeyFile *keyFile;

/**
 * Gets a boolean value from preferences.
 * On error, returns def as default value.
 *
 * @param key the specific settings key
 * @param def the default value to return on error
 * @return the preference value or def on error
 */
gboolean
prefs_get_boolean(gchar *key, gboolean def)
{
	gboolean ret;
	GError *error = NULL;
	ret = g_key_file_get_boolean(keyFile, "PNMixer", key, &error);
	if (error) {
		g_error_free(error);
		return def;
	}
	return ret;
}

/**
 * Gets an int value from a preferences.
 * On error, returns def as default value.
 *
 * @param key the specific settings key
 * @param def the default value to return on error
 * @return the preference value or def on error
 */
gint
prefs_get_integer(gchar *key, gint def)
{
	gint ret;
	GError *error = NULL;
	ret = g_key_file_get_integer(keyFile, "PNMixer", key, &error);
	if (error) {
		g_error_free(error);
		return def;
	}
	return ret;
}

/**
 * Gets a double value from preferences.
 * On error, returns def as default value.
 *
 * @param key the specific settings key
 * @param def the default value to return on error
 * @return the preference value or def on error
 */
gdouble
prefs_get_double(gchar *key, gdouble def)
{
	gdouble ret;
	GError *error = NULL;
	ret = g_key_file_get_double(keyFile, "PNMixer", key, &error);
	if (error) {
		g_error_free(error);
		return def;
	}
	return ret;
}

/**
 * Gets a string value from preferences.
 * On error, returns def as default value.
 *
 * @param key the specific settings key
 * @param def the default value to return on error
 * @return the preference value or def on error, must be freed.
 */
gchar *
prefs_get_string(gchar *key, const gchar *def)
{
	gchar *ret = NULL;
	GError *error = NULL;

	ret = g_key_file_get_string(keyFile, "PNMixer", key, &error);
	if (error) {
		g_error_free(error);
		return g_strdup(def);
	}
	return ret;
}

/**
 * Gets the currently selected channel of the specified Alsa Card
 * from the global keyFile and returns the result.
 *
 * @param card the Alsa Card to get the currently selected channel of
 * @return the currently selected channel as newly allocated string,
 * NULL on failure
 */
gchar *
prefs_get_channel(const gchar *card)
{
	if (!card)
		return NULL;
	return g_key_file_get_string(keyFile, card, "Channel", NULL);
}


/**
 * Default volume commands.
 */
static const gchar *vol_commands[] = {
	"pavucontrol",
	"gnome-alsamixer",
	"xfce4-mixer",
	"alsamixergui",
	NULL
};

/**
 * Gets the current volume command from the user preferences
 * and returns it. If none is set, iterates through the list vol_commands to
 * determine the volume command.
 *
 * @return volume command from user preferences or valid command
 * from vol_commands or NULL on failure. Must be freed.
 */
gchar *
prefs_get_vol_command(void)
{
	gchar *ret;

	ret = prefs_get_string("VolumeControlCommand", NULL);

	if (ret == NULL) {
		gchar buf[256];
		const char **cmd = vol_commands;
		while (*cmd) {
			snprintf(buf, 256, "which %s | grep /%s > /dev/null", *cmd, *cmd);
			if (!system(buf))
				return g_strdup(*cmd);
			cmd++;
		}
	}

	return ret;
}

/**
 * Gets the volume meter colors which are drawn on top of the
 * tray_icon by reading the VolMeterColor entry of the config
 * file.
 *
 * @return array of doubles which holds the RGB values, from
 * 0 to 1.0
 */
gdouble *
prefs_get_vol_meter_colors(void)
{
	gdouble *colors;
	gsize i, numcols;

	colors = g_key_file_get_double_list(keyFile, "PNMixer", "VolMeterColor",
	                                    &numcols, NULL);

	if (!colors) {
		colors = g_malloc(3 * sizeof(gdouble));
		colors[0] = 0.909803921569;
		colors[1] = 0.43137254902;
		colors[2] = 0.43137254902;
	}	

	for (i =0; i < 3; i++) {
		if (colors[i] < 0)
			colors[i] = 0;
		if (colors[i] > 1)
			colors[i] = 1;
	}

	return colors;
}

/**
 * Sets a boolean value to preferences.
 *
 * @param key the specific settings key
 * @param def the value to set
 */
void
prefs_set_boolean(const gchar *key, gboolean value)
{
	g_key_file_set_boolean(keyFile, "PNMixer", key, value);
}

/**
 * Sets a integer value to preferences.
 *
 * @param key the specific settings key
 * @param def the value to set
 */
void
prefs_set_integer(const gchar *key, gint value)
{
	g_key_file_set_integer(keyFile, "PNMixer", key, value);
}

/**
 * Sets a double value to preferences.
 *
 * @param key the specific settings key
 * @param def the value to set
 */
void
prefs_set_double(const gchar *key, gdouble value)
{
	g_key_file_set_double(keyFile, "PNMixer", key, value);
}

/**
 * Sets a string value to preferences.
 *
 * @param key the specific settings key
 * @param def the value to set
 */
void
prefs_set_string(const gchar *key, const gchar *value)
{
	g_key_file_set_string(keyFile, "PNMixer", key, value);
}

/**
 * Sets the channel for a given card in preferences.
 *
 * @param card the Alsa Card associated with the channel
 * @param channel the channel to save in the preferences.
 */
void
prefs_set_channel(const gchar *card, const gchar *channel)
{
	g_key_file_set_string(keyFile, card, "Channel", channel);
}

/**
 * Sets the volume meter colors which are drawn on top of the
 * tray_icon.
 *
 * @param colors an array of color swhich holds the RGB values,
 *        from 0 to 1.0
 * @param n the array size
 */
void
prefs_set_vol_meter_colors(gdouble *colors, gsize n)
{
	g_key_file_set_double_list(keyFile, "PNMixer", "VolMeterColor", colors, n);
}

/**
 * Loads the preferences from the config file to the keyFile object (GKeyFile type).
 * Creates the keyFile object if it doesn't exist.
 */
void
prefs_load(void)
{
	GError *err = NULL;
	gchar *filename = g_build_filename(g_get_user_config_dir(),
	                                   "pnmixer", "config", NULL);

	if (keyFile != NULL)
		g_key_file_free(keyFile);

	keyFile = g_key_file_new();

	if (g_file_test(filename, G_FILE_TEST_EXISTS)) {
		if (!g_key_file_load_from_file(keyFile, filename, 0, &err)) {
			report_error(_("Couldn't load preferences file: %s"),
				     err->message);
			g_error_free(err);
			g_key_file_free(keyFile);
			keyFile = NULL;
		}
	} else {
		if (!g_key_file_load_from_data
		    (keyFile, DEFAULT_PREFS, strlen(DEFAULT_PREFS), 0, &err)) {
			report_error(_("Couldn't load default preferences: %s"),
				     err->message);
			g_error_free(err);
			g_key_file_free(keyFile);
			keyFile = NULL;
		}
	}

	g_free(filename);
}

/**
 * Save the preferences from the keyFile object to the config file.
 */
void
prefs_save(void)
{
	gsize len;
	GError *err = NULL;
	gchar *filename = g_build_filename(g_get_user_config_dir(),
	                                   "pnmixer", "config", NULL);
	gchar *filedata = g_key_file_to_data(keyFile, &len, NULL);

	g_file_set_contents(filename, filedata, len, &err);

	if (err != NULL) {
		report_error(_("Couldn't write preferences file: %s"), err->message);
		g_error_free(err);
	}

	g_free(filename);
	g_free(filedata);
}

/**
 * Checks if the preferences dir for saving is present and accessible.
 * Creates it if doesn't exist. Reports errors via report_error().
 */
void
prefs_ensure_save_dir(void)
{
	gchar *prefs_dir = g_build_filename(g_get_user_config_dir(),
	                                   "pnmixer", NULL);

	if (!g_file_test(prefs_dir, G_FILE_TEST_IS_DIR)) {
		if (g_file_test(prefs_dir, G_FILE_TEST_EXISTS))
			report_error(_
				     ("Error: %s exists but is not a directory, will "
				      "not be able to save preferences."), prefs_dir);
		else {
			if (g_mkdir(prefs_dir, S_IRWXU))
				report_error(_("Couldn't make prefs directory: %s"),
					     strerror(errno));
		}
	}

	g_free(prefs_dir);
}

/**
 * Sets the global options enable_noti, hotkey_noti, mouse_noti, popup_noti,
 * noti_timeout and external_noti from the user settings.
 */
static void
set_notification_options(void)
{
	enable_noti = prefs_get_boolean("EnableNotifications", FALSE);
	hotkey_noti = prefs_get_boolean("HotkeyNotifications", TRUE);
	mouse_noti = prefs_get_boolean("MouseNotifications", TRUE);
	popup_noti = prefs_get_boolean("PopupNotifications", FALSE);
	external_noti = prefs_get_boolean("ExternalNotifications", FALSE);
	noti_timeout = prefs_get_integer("NotificationTimeout", 1500);
}

/**
 * Applies the preferences, usually triggered by on_ok_button_clicked()
 * in callbacks.c, but also initially called from main().
 *
 * @param alsa_change whether we want to trigger alsa-reinitalization
 */
void
apply_prefs(gint alsa_change)
{
	gdouble *vol_meter_clrs;

	scroll_step = prefs_get_integer("ScrollStep", 5);
	gtk_adjustment_set_page_increment(vol_adjustment, scroll_step);

	fine_scroll_step = prefs_get_integer("FineScrollStep", 1);
	gtk_adjustment_set_step_increment(vol_adjustment, fine_scroll_step);

	if (prefs_get_boolean("EnableHotKeys", FALSE)) {
		gint mk, uk, dk, mm, um, dm, hstep;
		mk = prefs_get_integer("VolMuteKey", -1);
		uk = prefs_get_integer("VolUpKey", -1);
		dk = prefs_get_integer("VolDownKey", -1);
		mm = prefs_get_integer("VolMuteMods", 0);
		um = prefs_get_integer("VolUpMods", 0);
		dm = prefs_get_integer("VolDownMods", 0);
		hstep = prefs_get_integer("HotkeyVolumeStep", 1);
		grab_keys(mk, uk, dk, mm, um, dm, hstep);
	} else
		// will actually just ungrab everything
		grab_keys(-1, -1, -1, 0, 0, 0, 1);

	set_notification_options();

	vol_meter_clrs = prefs_get_vol_meter_colors();
	set_vol_meter_color(vol_meter_clrs[0], vol_meter_clrs[1],
			    vol_meter_clrs[2]);
	g_free(vol_meter_clrs);

	update_status_icons();
	update_vol_text();

	if (alsa_change)
		do_alsa_reinit();
}

/**
 * Fills the GtkComboBoxText chan_combo with the currently available
 * channels of the card.
 *
 * @param channels list of available channels
 * @param combo the GtkComboBoxText widget for the channels
 * @param selected the currently selected channel
 */
void
fill_channel_combo(GSList *channels, GtkWidget *combo, gchar *selected)
{
	int idx = 0, sidx = 0;
	GtkTreeIter iter;
	GtkListStore *store =
		GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(combo)));
	gtk_list_store_clear(store);
	while (channels) {
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, channels->data, -1);
		if (selected && !strcmp(channels->data, selected))
			sidx = idx;
		idx++;
		channels = channels->next;
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), sidx);
}

/**
 * Fills the GtkComboBoxText card_combo with the currently available
 * cards and calls fill_channel_combo() as well.
 *
 * @param combo the GtkComboBoxText widget for the alsa cards
 * @param channels_combo the GtkComboBoxText widget for the card channels
 */
void
fill_card_combo(GtkWidget *combo, GtkWidget *channels_combo)
{
	struct acard *c;
	GSList *cur_card;
	struct acard *active_card;
	int idx, sidx = 0;

	GtkTreeIter iter;
	GtkListStore *store =
		GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(combo)));

	cur_card = cards;
	active_card = alsa_get_active_card();
	idx = 0;
	while (cur_card) {
		c = cur_card->data;
		if (!c->channels) {
			cur_card = cur_card->next;
			continue;
		}
		if (active_card && !strcmp(c->name, active_card->name)) {
			gchar *sel_chan = prefs_get_channel(c->name);
			sidx = idx;
			fill_channel_combo(c->channels, channels_combo, sel_chan);
			if (sel_chan)
				g_free(sel_chan);
		}
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, c->name, -1);
		cur_card = cur_card->next;
		idx++;
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), sidx);
}

/**
 * Handler for the signal 'changed' on the GtkComboBoxText widget
 * card_combo. This basically refills the channel list if the card
 * changes.
 *
 * @param box the box which received the signal
 * @param data user data set when the signal handler was connected
 */
void
on_card_changed(GtkComboBox *box, PrefsData *data)
{
	struct acard *card;
	gchar *card_name;

	card_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(box));
	card = find_card(card_name);
	g_free(card_name);

	if (card) {
		gchar *sel_chan = prefs_get_channel(card->name);
		fill_channel_combo(card->channels, data->chan_combo, sel_chan);
		g_free(sel_chan);
	}
}

/**
 * Handler for the signal 'toggled' on the GtkCheckButton vol_text_check.
 * Updates the preferences window.
 *
* @param button the button which received the signal
* @param data user data set when the signal handler was connected
 */
void
on_vol_text_toggle(GtkToggleButton *button, PrefsData *data)
{
	gboolean active = gtk_toggle_button_get_active(button);
	gtk_widget_set_sensitive(data->vol_pos_label, active);
	gtk_widget_set_sensitive(data->vol_pos_combo, active);
}

/**
 * Handler for the signal 'toggled' on the GtkCheckButton draw_vol_check.
 * Updates the preferences window.
 *
 * @param button the button which received the signal
 * @param data user data set when the signal handler was connected
 */
void
on_draw_vol_toggle(GtkToggleButton *button, PrefsData *data)
{
	gboolean active = gtk_toggle_button_get_active(button);
	gtk_widget_set_sensitive(data->vol_meter_pos_label, active);
	gtk_widget_set_sensitive(data->vol_meter_pos_spin, active);
	gtk_widget_set_sensitive(data->vol_meter_color_label, active);
	gtk_widget_set_sensitive(data->vol_meter_color_button, active);
}

/**
 * Handler for the signal 'changed' on the GtkComboBox middle_click_combo.
 * Updates the preferences window.
 *
 * @param box the combobox which received the signal
 * @param data user data set when the signal handler was connected
 */
void
on_middle_changed(GtkComboBox *box, PrefsData *data)
{
	gboolean cust = gtk_combo_box_get_active(GTK_COMBO_BOX(box)) == 3;
	gtk_widget_set_sensitive(data->custom_label, cust);
	gtk_widget_set_sensitive(data->custom_entry, cust);
}

/**
 * Handler for the signal 'toggled' on the GtkCheckButton enable_noti_check.
 * Updates the preferences window.
 *
 * @param button the button which received the signal
 * @param data user data set when the signal handler was connected
 */
#ifdef HAVE_LIBN
void
on_notification_toggle(GtkToggleButton *button, PrefsData *data)
{
	gboolean active = gtk_toggle_button_get_active(button);
	gtk_widget_set_sensitive(data->noti_timeout_label, active);
	gtk_widget_set_sensitive(data->noti_timeout_spin, active);
	gtk_widget_set_sensitive(data->hotkey_noti_check, active);
	gtk_widget_set_sensitive(data->mouse_noti_check, active);
	gtk_widget_set_sensitive(data->popup_noti_check, active);
	gtk_widget_set_sensitive(data->external_noti_check, active);
}
#else
void
on_notification_toggle(G_GNUC_UNUSED GtkToggleButton *button,
		G_GNUC_UNUSED PrefsData *data)
{
}
#endif

/**
 * Handler for the signal 'toggled' on the GtkCheckButton
 * enable_hotkeys_check.
 * Updates the preferences window.
 *
 * @param button the button which received the signal
 * @param data user data set when the signal handler was connected
 */
void
on_hotkey_toggle(GtkToggleButton *button, PrefsData *data)
{
	gboolean active = gtk_toggle_button_get_active(button);
	gtk_widget_set_sensitive(data->hotkey_vol_label, active);
	gtk_widget_set_sensitive(data->hotkey_vol_spin, active);
}

/**
 * This is called from within the callback function
 * on_hotkey_button_click() in callbacks. which is triggered when
 * one of the hotkey boxes mute_eventbox, up_eventbox or
 * down_eventbox (GtkEventBox) in the preferences received
 * the button-press-event signal.
 *
 * Then this function grabs the keyboard, opens the hotkey_dialog
 * and updates the GtkLabel with the pressed hotkey.
 * The GtkLabel is later read by on_ok_button_clicked() in
 * callbacks.c which stores the result in the global keyFile.
 *
 * @param widget_name the name of the widget (mute_eventbox, up_eventbox
 * or down_eventbox)
 * @param data struct holding the GtkWidgets of the preferences windows
 */
void
acquire_hotkey(const char *widget_name, PrefsData *data)
{
	gint resp, action;
	GtkWidget *diag = data->hotkey_dialog;

	action =
		(!strcmp(widget_name, "mute_eventbox")) ? 0 :
		(!strcmp(widget_name, "up_eventbox")) ? 1 :
		(!strcmp(widget_name, "down_eventbox")) ? 2 : -1;

	if (action < 0) {
		report_error(_("Invalid widget passed to acquire_hotkey: %s"),
			     widget_name);
		return;
	}

	switch (action) {
	case 0:
		gtk_label_set_text(GTK_LABEL(data->hotkey_key_label),
				   _("Mute/Unmute"));
		break;
	case 1:
		gtk_label_set_text(GTK_LABEL(data->hotkey_key_label),
				   _("Volume Up"));
		break;
	case 2:
		gtk_label_set_text(GTK_LABEL(data->hotkey_key_label),
				   _("Volume Down"));
		break;
	default:
		break;
	}

	// grab keyboard
	if (G_LIKELY(
#ifdef WITH_GTK3
		    gdk_device_grab(gtk_get_current_event_device(),
				    gdk_screen_get_root_window(gdk_screen_get_default()),
				    GDK_OWNERSHIP_APPLICATION,
				    TRUE, GDK_ALL_EVENTS_MASK, NULL, GDK_CURRENT_TIME)
#else
		    gdk_keyboard_grab(gtk_widget_get_root_window(
					      GTK_WIDGET(diag)), TRUE, GDK_CURRENT_TIME)
#endif
		    == GDK_GRAB_SUCCESS)) {
		resp = gtk_dialog_run(GTK_DIALOG(diag));
#ifdef WITH_GTK3
		gdk_device_ungrab(gtk_get_current_event_device(), GDK_CURRENT_TIME);
#else
		gdk_keyboard_ungrab(GDK_CURRENT_TIME);
#endif
		if (resp == GTK_RESPONSE_OK) {
			const gchar *key_name =
				gtk_label_get_text(GTK_LABEL(data->hotkey_key_label));
			if (!strcasecmp(key_name, "<Primary>c")) {
				key_name = "(None)";
			}
			switch (action) {
			case 0:
				gtk_label_set_text(GTK_LABEL(data->mute_hotkey_label),
						   key_name);
				break;
			case 1:
				gtk_label_set_text(GTK_LABEL(data->up_hotkey_label),
						   key_name);
				break;
			case 2:
				gtk_label_set_text(GTK_LABEL(data->down_hotkey_label),
						   key_name);
				break;
			default:
				break;
			}
		}
	} else
		report_error(_("Could not grab the keyboard."));
	gtk_widget_hide(diag);
}

/**
 * Handler for the signal 'key-press-event' on the GtkDialog hotkey_dialog
 * which was opened by acquire_hotkey().
 *
 * @param dialog the dialog window which received the signal
 * @param ev the GdkEventKey which triggered the signal
 * @param data struct holding the GtkWidgets of the preferences windows
 * @return TRUE to stop other handlers from being invoked for the event.
 * FALSE to propagate the event further.
 */
gboolean
hotkey_pressed(G_GNUC_UNUSED GtkWidget *dialog,
		GdkEventKey *ev,
		PrefsData *data)
{
	gchar *key_text;
	guint keyval;
	GdkModifierType state, consumed;

	state = ev->state;
	gdk_keymap_translate_keyboard_state(gdk_keymap_get_default(),
					    ev->hardware_keycode,
					    state, ev->group, &keyval, NULL, NULL, &consumed);

	state &= ~consumed;
	state &= gtk_accelerator_get_default_mod_mask();

	key_text = gtk_accelerator_name(keyval, state);
	gtk_label_set_text(GTK_LABEL(data->hotkey_key_label), key_text);
	g_free(key_text);
	return FALSE;
}

/**
 * Handler for the signal 'key-release-event' on the GtkDialog
 * hotkey_dialog which was opened by acquire_hotkey().
 *
 * @param dialog the dialog window which received the signal
 * @param ev the GdkEventKey which triggered the signal
 * @param data struct holding the GtkWidgets of the preferences windows
 * @return TRUE to stop other handlers from being invoked for the event.
 * FALSE to propagate the event further.
 */
gboolean
hotkey_released(GtkWidget *dialog,
		G_GNUC_UNUSED GdkEventKey *ev,
		G_GNUC_UNUSED PrefsData *data)
{
#ifdef WITH_GTK3
	gdk_device_ungrab(gtk_get_current_event_device(), GDK_CURRENT_TIME);
#else
	gdk_keyboard_ungrab(GDK_CURRENT_TIME);
#endif
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	return FALSE;
}

/**
 * Sets one of the hotkey labels in the Hotkeys settings
 * to the specified keycode (converted to a accelerator name).
 *
 * @param label the label to set
 * @param code the keycode to convert to accelerator name
 * @param mods the pressed keymod
 */
static void
set_label_for_keycode(GtkWidget *label, gint code, GdkModifierType mods)
{
	int keysym;
	gchar *key_text;
	if (code < 0)
		return;
	keysym = XkbKeycodeToKeysym(gdk_x11_get_default_xdisplay(), code, 0, 0);
	key_text = gtk_accelerator_name(keysym, mods);
	gtk_label_set_text(GTK_LABEL(label), key_text);
	g_free(key_text);
}

/**
 * Creates the whole preferences window by reading prefs-gtk3.glade
 * or prefs-gtk2.glade and returns the result.
 *
 * @return the newly created preferences window, NULL on failure
 */
GtkWidget *
create_prefs_window(void)
{
	GtkBuilder *builder;
	GError *error = NULL;

	gdouble *vol_meter_clrs;
	gchar *uifile, *slider_orientation, *vol_cmd, *custcmd;

	PrefsData *prefs_data;

	uifile = get_ui_file(PREFS_UI_FILE);
	if (!uifile) {
		report_error(_
			     ("Can't find preferences user interface file. "
			      "Please ensure PNMixer is installed correctly."));
		return NULL;
	}

	DEBUG_PRINT("Loading prefs ui from '%s'", uifile);
	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, uifile, &error)) {
		g_warning("%s", error->message);
		report_error(error->message);
		g_error_free(error);
		g_free(uifile);
		g_object_unref(G_OBJECT(builder));
		return NULL;
	}

	g_free(uifile);

	prefs_data = g_slice_new(PrefsData);
#define GO(name) prefs_data->name = GTK_WIDGET(gtk_builder_get_object(builder, #name))
	GO(prefs_window);
	GO(card_combo);
	GO(chan_combo);
	GO(normalize_vol_check);
	GO(vol_pos_label);
	GO(vol_pos_combo);
	GO(vol_meter_pos_label);
	GO(vol_meter_pos_spin);
	GO(vol_meter_color_label);
	GO(vol_meter_color_button);
	GO(custom_label);
	GO(custom_entry);
	GO(slider_orientation_combo);
	GO(vol_text_check);
	GO(draw_vol_check);
	GO(system_theme);
	GO(vol_control_entry);
	GO(scroll_step_spin);
	GO(fine_scroll_step_spin);
	GO(middle_click_combo);
	GO(enable_hotkeys_check);
	GO(hotkey_vol_label);
	GO(hotkey_vol_spin);
	GO(hotkey_dialog);
	GO(hotkey_key_label);
	GO(mute_hotkey_label);
	GO(up_hotkey_label);
	GO(down_hotkey_label);
#ifdef HAVE_LIBN
	GO(enable_noti_check);
	GO(noti_timeout_spin);
	GO(noti_timeout_label);
	GO(hotkey_noti_check);
	GO(mouse_noti_check);
	GO(popup_noti_check);
	GO(external_noti_check);
#endif
#undef GO

	// slider orientation
	slider_orientation = prefs_get_string("SliderOrientation", NULL);
	if (slider_orientation) {
		GtkComboBox *combo_box = GTK_COMBO_BOX
			(prefs_data->slider_orientation_combo);
#ifndef WITH_GTK3
		/* Gtk2 ComboBoxes don't have item ids */
		if (!strcmp(slider_orientation, "horizontal"))
			gtk_combo_box_set_active(combo_box, 1);
		else
			gtk_combo_box_set_active(combo_box, 0);
#else
		gtk_combo_box_set_active_id(combo_box, slider_orientation);
#endif
		g_free(slider_orientation);
	}
	
	// vol text display
	gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON(prefs_data->vol_text_check),
	 prefs_get_boolean("DisplayTextVolume", FALSE));

	gtk_combo_box_set_active
	(GTK_COMBO_BOX(prefs_data->vol_pos_combo),
	 prefs_get_integer("TextVolumePosition", 0));

	// volume meter
	gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON(prefs_data->draw_vol_check),
	 prefs_get_boolean("DrawVolMeter", FALSE));

	gtk_adjustment_set_upper
	(GTK_ADJUSTMENT(gtk_builder_get_object(builder,
	                                       "vol_meter_pos_adjustment")),
	 tray_icon_size() - 10);

	gtk_spin_button_set_value
	(GTK_SPIN_BUTTON(prefs_data->vol_meter_pos_spin),
	 prefs_get_integer("VolMeterPos", 0));

	gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON(prefs_data->system_theme),
	 prefs_get_boolean("SystemTheme", FALSE));

	// set color button color
	vol_meter_clrs = prefs_get_vol_meter_colors();
#ifdef WITH_GTK3
	GdkRGBA vol_meter_color_button_color;

	vol_meter_color_button_color.red = vol_meter_clrs[0];
	vol_meter_color_button_color.green = vol_meter_clrs[1];
	vol_meter_color_button_color.blue = vol_meter_clrs[2];
	vol_meter_color_button_color.alpha = 1.0;

	gtk_color_chooser_set_rgba(
	GTK_COLOR_CHOOSER(prefs_data->vol_meter_color_button),
	&vol_meter_color_button_color);
#else
	GdkColor vol_meter_color_button_color;

	vol_meter_color_button_color.red = (guint32) (vol_meter_clrs[0] * 65536);
	vol_meter_color_button_color.green = (guint32) (vol_meter_clrs[1] * 65536);
	vol_meter_color_button_color.blue = (guint32) (vol_meter_clrs[2] * 65536);

	gtk_color_button_set_color(
	GTK_COLOR_BUTTON(prefs_data->vol_meter_color_button),
	&vol_meter_color_button_color);
#endif
	g_free(vol_meter_clrs);

	// fill in card/channel combo boxes
	fill_card_combo(prefs_data->card_combo, prefs_data->chan_combo);

	// volume normalization (ALSA mapped)
	gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON(prefs_data->normalize_vol_check),
	 prefs_get_boolean("NormalizeVolume", FALSE));

	// volume command
	vol_cmd = prefs_get_vol_command();
	if (vol_cmd) {
		gtk_entry_set_text(GTK_ENTRY(prefs_data->vol_control_entry), vol_cmd);
		g_free(vol_cmd);
	}
	                           
	// volume scroll steps
	gtk_spin_button_set_value
	(GTK_SPIN_BUTTON(prefs_data->scroll_step_spin),
	 prefs_get_double("ScrollStep", 5));

	gtk_spin_button_set_value
	(GTK_SPIN_BUTTON(prefs_data->fine_scroll_step_spin),
	 prefs_get_double("FineScrollStep", 1));

	//  middle click
	gtk_combo_box_set_active
	(GTK_COMBO_BOX(prefs_data->middle_click_combo),
	 prefs_get_integer("MiddleClickAction", 0));

	// custom command
	gtk_entry_set_invisible_char(GTK_ENTRY(prefs_data->custom_entry), 8226);

	custcmd = prefs_get_string("CustomCommand", NULL);
	if (custcmd) {
		gtk_entry_set_text(GTK_ENTRY(prefs_data->custom_entry), custcmd);
		g_free(custcmd);
	}

	on_vol_text_toggle(GTK_TOGGLE_BUTTON(prefs_data->vol_text_check),
			   prefs_data);
	on_draw_vol_toggle(GTK_TOGGLE_BUTTON(prefs_data->draw_vol_check),
			   prefs_data);
	on_middle_changed(GTK_COMBO_BOX(prefs_data->middle_click_combo),
			  prefs_data);

	// hotkeys
	gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON(prefs_data->enable_hotkeys_check),
	 prefs_get_boolean("EnableHotKeys", FALSE));

	// hotkey step
	gtk_spin_button_set_value
	(GTK_SPIN_BUTTON(prefs_data->hotkey_vol_spin),
	 prefs_get_integer("HotkeyVolumeStep", 1));

	if (g_key_file_has_key(keyFile, "PNMixer", "VolMuteKey", NULL))
		set_label_for_keycode(prefs_data->mute_hotkey_label,
		                      prefs_get_integer("VolMuteKey", 0),
		                      prefs_get_integer("VolMuteMods", 0));

	if (g_key_file_has_key(keyFile, "PNMixer", "VolUpKey", NULL))
		set_label_for_keycode(prefs_data->up_hotkey_label,
		                      prefs_get_integer("VolUpKey", 0),
		                      prefs_get_integer("VolUpMods", 0));

	if (g_key_file_has_key(keyFile, "PNMixer", "VolDownKey", NULL))
		set_label_for_keycode(prefs_data->down_hotkey_label,
		                      prefs_get_integer("VolDownKey", 0),
		                      prefs_get_integer("VolDownMods", 0));

	on_hotkey_toggle(GTK_TOGGLE_BUTTON(prefs_data->enable_hotkeys_check),
			 prefs_data);

	gtk_notebook_append_page(GTK_NOTEBOOK(gtk_builder_get_object(builder,
					      "notebook1")),
#ifdef HAVE_LIBN
				 GTK_WIDGET(gtk_builder_get_object(builder,
						 "notification_vbox")),
#else
				 GTK_WIDGET(gtk_builder_get_object
					    (builder, "no_notification_label")),
#endif
				 gtk_label_new(_("Notifications")));

#ifdef HAVE_LIBN
	// notification checkboxes
	set_notification_options();
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
					     prefs_data->enable_noti_check), enable_noti);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
					     prefs_data->hotkey_noti_check), hotkey_noti);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
					     prefs_data->mouse_noti_check), mouse_noti);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
					     prefs_data->popup_noti_check), popup_noti);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
					     prefs_data->external_noti_check),
				     external_noti);
	on_notification_toggle(GTK_TOGGLE_BUTTON(prefs_data->enable_noti_check),
			       prefs_data);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(prefs_data->noti_timeout_spin),
				  noti_timeout);
#endif

	gtk_builder_connect_signals(builder, prefs_data);
	g_object_unref(G_OBJECT(builder));

	return prefs_data->prefs_window;
}
