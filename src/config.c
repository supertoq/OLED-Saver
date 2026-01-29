/* 2025 - 2026 supertoq
 * LICENSE: BSD 2-Clause "Simplified"
 *
 * config.c for oledsaver
 *
 * Implementierung:
 * • init_environment() - ermittelt app_dir und config_path
 * • init_config()      - lädt (oder erzeugt) die *.cfg-Datei, füllt g_cfg.mouse_move_limit
 * • save_config()      - schreibt zurück
 * • config_cleanup()   - räumt auf
 *
 *  g_cfg.mouse_move_limit
 *  g_cfg.use_key
 *  g_cfg.quit_key
 *  g_cfg.start_in_fs
 *  g_cfg.log_enable
 *  config_get_config_path()
 *  home_path - verwenden anhand: " const gchar *home = config_get_home_path(); " = readonly, nicht g_free
 *  flatpak_id - per config_get_flatpak_id()
 *
 * Version 2026-01-29  created in Allstedt with joy 
 */

#include "time_stamp.h"
#include "config.h"
#include <glib.h>

/* ----- Globale Variablen ------------------ */
static gchar       *config_path = NULL;    // Vollständiger Pfad zur .cfg-Datei / "eigenes" -> g_free()
static GKeyFile    *key_file    = NULL;    // In-Memory-Repräsentation der Datei
static const gchar *home_path   = NULL;    // Homeverzeichnis readonly / GLib-owned
static const gchar *flatpak_id  = NULL;    // Flatpak ID readonly / GLib-owned

/* ----- Globale Struktur der Keys ---------- */
FindConfig g_cfg = {            // Standard-Werte, falls alles fehlschlägt
    .mouse_move_limit = 50,
    .keep_wot         = FALSE,
    .use_key          = FALSE,
    .quit_key         = FALSE,
    .start_in_fs      = FALSE,
    .sys_ib_off       = FALSE,
    .log_enable       = FALSE,
    .adv_debug_opt    = FALSE
};
/* ---- Getter-Funktionen ---- */ // (Info für mich: nur so übermitteln)
const gchar *config_get_config_path(void)
{
    return config_path;
}

const gchar *config_get_flatpak_id(void) 
{
    return flatpak_id;
}

const gchar *config_get_home_path(void)
{
    return home_path;
}

/* ----- Environment --------------------------------------------------------------------- */
void init_environment(void)
{
    /* Pfadangaben auslesen */
    if (config_path && home_path)
        return;

    home_path = g_get_home_dir();

    const gchar *user_config_dir = g_get_user_config_dir();
    config_path = g_build_filename(user_config_dir, "bastis-oledsaver", "settings.cfg", NULL);    // flatpak

    g_print("[%s] [Config] Configuration file path: %s\n", time_stamp(), config_path);
}

/* ----- Config-Initialisierung - Laden / Anlegen ---------------------------------------- */
void init_config(void)
{
    GError *error = NULL;

    if (!config_path) {
        g_warning("[Config] No config path!\n");
        return;
    }

    /* KeyFile erstellen */
    key_file = g_key_file_new();

    /* Config-Verzeichnis anlegen */
    gchar *config_dir = g_path_get_dirname(config_path);
    if (g_mkdir_with_parents(config_dir, 0700) != 0) {  // Zugriffsrechte
        g_warning("[%s] [Config] Failed to create config directory: %s\n", time_stamp(), config_dir);
        g_free(config_dir);
        return;
    }
    g_free(config_dir);

    /* Datei laden, wenn sie existiert */
    if (!g_key_file_load_from_file(key_file, config_path, G_KEY_FILE_KEEP_COMMENTS, &error)) 
    {
        g_message("[%s] [Config] Config file does not exist, using defaults", time_stamp());
        g_clear_error(&error);
    } 

    /* Gruppe [General] mit den zugehörigen Werten erzwingen, wenn nicht vorhanden */
    if (!g_key_file_has_group(key_file, "General")) {
        g_key_file_set_integer(key_file, "General",   "mouse_move_limit",   50);
        g_key_file_set_boolean(key_file, "General",   "keep_wot",        FALSE);
        g_key_file_set_boolean(key_file, "General",   "use_key",         FALSE);
        g_key_file_set_boolean(key_file, "General",   "quit_key",        FALSE);
        g_key_file_set_boolean(key_file, "General",   "start_in_fs",     FALSE);
        g_key_file_set_boolean(key_file, "Debugging", "sys_ib_off",      FALSE);
        g_key_file_set_boolean(key_file, "Debugging", "log_enable",      FALSE);
        g_key_file_set_boolean(key_file, "Debugging", "adv_debug_opt",   FALSE);
    }

    /* Gruppe existiert aber Keys darin sind leer, ebenfalls Werte erzwingen */
    // key1
    if (!g_key_file_has_key(key_file, "General", "mouse_move_limit", NULL))
        g_key_file_set_integer(key_file, "General", "mouse_move_limit", 50);
    // key2
    if (!g_key_file_has_key(key_file, "General", "keep_wot", NULL))
        g_key_file_set_boolean(key_file, "General", "keep_wot", FALSE);
    // key3
    if (!g_key_file_has_key(key_file, "General", "use_key", NULL))
        g_key_file_set_boolean(key_file, "General", "use_key", FALSE);
    // key4
    if (!g_key_file_has_key(key_file, "General", "quit_key", NULL))
        g_key_file_set_boolean(key_file, "General", "quit_key", FALSE);
    // key5
    if (!g_key_file_has_key(key_file, "General", "start_in_fs", NULL))
        g_key_file_set_boolean(key_file, "General", "start_in_fs", FALSE);
    // key6
    if (!g_key_file_has_key(key_file, "Debugging", "sys_ib_off", NULL))
        g_key_file_set_boolean(key_file, "Debugging", "sys_ib_off", FALSE);
    // key7
    if (!g_key_file_has_key(key_file, "Debugging", "log_enable", NULL))
        g_key_file_set_boolean(key_file, "Debugging", "log_enable", FALSE);
    // Key8 (dev)
    if (!g_key_file_has_key(key_file, "Debugging", "adv_debug_opt", NULL))
        g_key_file_set_boolean(key_file, "Debugging", "adv_debug_opt", FALSE);

    /* Werte Fehlersicher auslesen */
    // key1
    error = NULL;
    g_cfg.mouse_move_limit = g_key_file_get_integer(key_file, "General", "mouse_move_limit", &error);
    if (error) {
        g_cfg.mouse_move_limit = 50;
        g_clear_error(&error);
    }
    // key2
    error = NULL;
    g_cfg.keep_wot = g_key_file_get_boolean(key_file, "General", "keep_wot", &error);
    if (error) {
        g_cfg.keep_wot = FALSE;
        g_clear_error(&error);
    }
    // key3
    error = NULL;
    g_cfg.use_key = g_key_file_get_boolean(key_file, "General", "use_key", &error);
    if (error) {
        g_cfg.use_key = FALSE;
        g_clear_error(&error);
    }
    // key4
    error = NULL;
    g_cfg.quit_key = g_key_file_get_boolean(key_file, "General", "quit_key", &error);
    if (error) {
        g_cfg.quit_key = FALSE;
        g_clear_error(&error);
    }
    // key5
    error = NULL;
    g_cfg.start_in_fs = g_key_file_get_boolean(key_file, "General", "start_in_fs", &error);
    if (error) {
        g_cfg.start_in_fs = FALSE;
        g_clear_error(&error);
    }
    // key6
    error = NULL;
    g_cfg.sys_ib_off = g_key_file_get_boolean(key_file, "Debugging", "sys_ib_off", &error);
    if (error) {
        g_cfg.sys_ib_off = FALSE;
        g_clear_error(&error);
    }
    // key7
    error = NULL;
    g_cfg.log_enable = g_key_file_get_boolean(key_file, "Debugging", "log_enable", &error);
    if (error) {
        g_cfg.log_enable = FALSE;
        g_clear_error(&error);
    }
    // key8 (dev)
    error = NULL;
    g_cfg.adv_debug_opt = g_key_file_get_boolean(key_file, "Debugging", "adv_debug_opt", &error);
    if (error) {
        g_cfg.adv_debug_opt = FALSE;
        g_clear_error(&error);
    }

    /* Config speichern */
    save_config();
}


/* ----- Config speichern (aus globaler Struktur) ---------------------------------------- */
void save_config(void)
{
    if (!key_file || !config_path)
        return;
    // key1
    g_key_file_set_integer(key_file, "General",   "mouse_move_limit", g_cfg.mouse_move_limit);
    // key2
    g_key_file_set_boolean(key_file, "General",   "keep_wot",         g_cfg.keep_wot);
    // key3
    g_key_file_set_boolean(key_file, "General",   "use_key",          g_cfg.use_key);
    // key4
    g_key_file_set_boolean(key_file, "General",   "quit_key",         g_cfg.quit_key);
    // key5
    g_key_file_set_boolean(key_file, "General",   "start_in_fs",      g_cfg.start_in_fs);
    // key6
    g_key_file_set_boolean(key_file, "Debugging", "sys_ib_off",       g_cfg.sys_ib_off);
    // key7
    g_key_file_set_boolean(key_file, "Debugging", "log_enable",       g_cfg.log_enable);
    // key8 (dev)
    g_key_file_set_boolean(key_file, "Debugging", "adv_debug_opt",    g_cfg.adv_debug_opt);

    GError *error = NULL;
    if (!g_key_file_save_to_file(key_file, config_path, &error)) {
        g_warning("[%s] [Config] Failed to save config: %s\n", time_stamp(), error->message);
        g_clear_error(&error);
    }
}

/* ------ Aufräumen --------------------------------------------------------------------- */
void config_cleanup(void)
{
    if (key_file)
    g_key_file_free(key_file);
    g_clear_pointer(&config_path, g_free);
}
