/* 2025 - 2026 super-toq
 * LICENSE: BSD 2-Clause "Simplified"
 *
 *
 * Implementierung:
 * • init_environment() - ermittelt app_dir und config_path
 * • init_config()      - lädt (oder erzeugt) die *.cfg-Datei, füllt g_cfg.mouse_move_limit
 * • save_config()      - schreibt zurück
 * • config_cleanup()   - räumt auf
 *
 *  g_cfg.mouse_move_limit
 *  g_cfg.use_key
 *  g_cfg.log_enable
 *  config_get_path()
 *  home_path() verwenden anhand: " const gchar *home = home_get_path(); " = readonly, nicht g_free
 * Version 2026-01-04
 */

#include "config.h"
#include <glib.h>

/* ----- Globale Variablen ------------------ */
static gchar       *config_path = NULL;    // Vollständiger Pfad zur .cfg-Datei / eigenes -> g_free()
static GKeyFile    *key_file    = NULL;    // In-Memory-Repräsentation der Datei
static const gchar *home_path   = NULL;    // Homeverzeichnis / GLib-owned

/* ----- Globale Struktur der Keys ---------- */
FindConfig g_cfg = {            // Standard-Werte, falls alles fehlschlägt
    .mouse_move_limit = 50,
    .use_key          = FALSE,
    .log_enable       = FALSE
};
/* ---- Getter-Funktion ---- */ // (Info für mich: nur so übermitteln)
const gchar *config_get_path(void)
{
    return config_path;
}

const gchar *home_get_path(void)
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
    //config_path = g_build_filename(user_config_dir, "basti-oledsaver", "oledsaver.cfg", NULL);                  // normal
    config_path = g_build_filename(user_config_dir, "oledsaver", "oledsaver.cfg", NULL);                                     // flatpak

    g_print("[Config] Configuration file path: %s\n", config_path);
}

/* ----- Config-Initialisierung - Laden / Anlegen ---------------------------------------- */
void init_config(void)
{
    GError *error = NULL;

    if (!config_path) {
        g_warning("[Config] init_environment() not called\n");
        return;
    }

    /* KeyFile erzeugen */
    key_file = g_key_file_new();

    /* Config-Verzeichnis anlegen */
    gchar *config_dir = g_path_get_dirname(config_path);
    if (g_mkdir_with_parents(config_dir, 0700) != 0) {
        g_warning("[Config] Failed to create config directory: %s\n", config_dir);
        g_free(config_dir);
        return;
    }
    g_free(config_dir);

    /* Datei laden, wenn sie existiert */
    if (!g_key_file_load_from_file(key_file, config_path, G_KEY_FILE_KEEP_COMMENTS, &error)) 
    {
        g_message("[Config] Config file does not exist, using defaults");
        g_clear_error(&error);
    } 

    /* Gruppe [General] mit den zugehörigen Werten erzwingen, wenn nicht vorhanden */
    if (!g_key_file_has_group(key_file, "General")) {
        g_key_file_set_integer(key_file, "General", "mouse_move_limit", 50);
        g_key_file_set_boolean(key_file, "General", "use_key", FALSE);
        g_key_file_set_boolean(key_file, "General", "log_enable", FALSE);
    }

    /* Gruppe existiert aber Keys darin sind leer, ebenfals Werte erzwingen */
    // key1
    if (!g_key_file_has_key(key_file, "General", "mouse_move_limit", NULL))
        g_key_file_set_integer(key_file, "General", "mouse_move_limit", 50);
    // key2
    if (!g_key_file_has_key(key_file, "General", "use_key", NULL))
        g_key_file_set_boolean(key_file, "General", "use_key", FALSE);
    // key3
    if (!g_key_file_has_key(key_file, "General", "log_enable", NULL))
        g_key_file_set_boolean(key_file, "General", "log_enable", FALSE);

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
    g_cfg.use_key = g_key_file_get_boolean(key_file, "General", "use_key", &error);
    if (error) {
        g_cfg.use_key = FALSE;
        g_clear_error(&error);
    }
    // key3
    error = NULL;
    g_cfg.log_enable = g_key_file_get_boolean(key_file, "General", "log_enable", &error);
    if (error) {
        g_cfg.log_enable = FALSE;
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
    g_key_file_set_integer(key_file, "General", "mouse_move_limit", g_cfg.mouse_move_limit);
    // key2
    g_key_file_set_boolean(key_file, "General", "use_key",          g_cfg.use_key);
    // key3
    g_key_file_set_boolean(key_file, "General", "log_enable",       g_cfg.log_enable);

    GError *error = NULL;
    if (!g_key_file_save_to_file(key_file, config_path, &error)) {
        g_warning("[Config] Failed to save config: %s\n", error->message);
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