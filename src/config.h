/* 2025 - 2026 supertoq
 * LICENSE: BSD 2-Clause "Simplified"
 *
 * config.h
 *
 * Globalen Struktur FindConfig g_cfg und 
 * Deklarationen:
 * - init_environment()
 * - init_config()
 * - save_config()
 * - config_cleanup()
 *
 * Version 2026-01-29
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

/* ----- Globale Struktur um Konfigurations-Parameter zu kapseln ------------ */
typedef struct {
    int      mouse_move_limit;             // Zahl zwischen 50 - 200
    gboolean         keep_wot;             // kepp-window-on-top Option
    gboolean          use_key;             // Fullscreen per Leertaste beenden
    gboolean         quit_key;             // Leertaste schließt auch die Anwendung
    gboolean      start_in_fs;             // direkt im Fullscreen-Modus starten
    gboolean       sys_ib_off;             // systemd-inhibit bei Gnome anwenden
    gboolean       log_enable;             // logging in Datei
    gboolean    adv_debug_opt;             // erweiterte Debug Optionen
} FindConfig;
extern FindConfig       g_cfg;             // Globale Instanz

/* ----- Prototype, Funktionen, die von außen aufgerufen werden ------------- */
void init_environment(void);               // Pfade ermitteln
void init_config     (void);               // Datei anlegen / laden g_cfg befüllen
void save_config     (void);               // g_cfg Datei schreiben
void config_cleanup  (void);               // Aufräumen

const gchar *config_get_config_path(void); // Pfad zur Config-Verzeichnis abfragen
const gchar *config_get_home_path  (void); // Pfad zur Config-Verzeichnis abfragen
const gchar *config_get_flatpak_id (void); // abfragen ob es ein Flatpak ID gibt

#endif //CONFIG_H
