/* 2025 - 2026 super-toq
 * LICENSE: BSD 2-Clause "Simplified"
 *
 * log_file.c
 *
 * log_file_init(APP_ID);
 *
 * Version 2026-01-06
 */
#include <glib.h>
#include <stdio.h>
#include "log_file.h"
#include "time_stamp.h"

static FILE *log_file = NULL;

/* ----- Alle g_print-Meldungen in Datei schreiben ---------------------------------------- */
static void g_print_to_log(const gchar *print_msg)
{
    /* In Log-Datei schreiben */
    if (!log_file) return;
    fprintf(log_file, "%s", print_msg); // g_print in fprintf umleiten
    fflush(log_file);

    /* zusätzlich weiterhin im Terminal ausgeben (da Umleitung) */
    fputs(print_msg, stdout);
    fflush(stdout);
}

/* ----- Log-Handler, Alle Log-Meldungen in Datei schreiben ------------------------------- */
static void log_handler(const gchar *domain, GLogLevelFlags level, const gchar *handler_msg, gpointer user_data)
{
    if (!log_file)
        return;

    const   char                          *char_level =     "INFO";
    if      (level & G_LOG_LEVEL_ERROR)    char_level =    "ERROR";
    else if (level & G_LOG_LEVEL_CRITICAL) char_level = "CRITICAL";
    else if (level & G_LOG_LEVEL_WARNING)  char_level =  "WARNING";
    else if (level & G_LOG_LEVEL_DEBUG)    char_level =    "DEBUG";

    /* file_print Snipplet */
    fprintf(log_file, "[%s] [%s] %s\n", time_stamp(), char_level, handler_msg);

    /* Puffer direkt flushen */
    fflush(log_file);
}

/* ----- API ----------------------------------------------------------------------------- */
void log_file_init(const gchar *app_name)
{
     /* Mini-Beispiel:
       FILE *f = fopen("test.log", "a");
       fprintf(f, "Hello\n");
       fclose(f);
    */

    if (log_file)
        return;

    /* Pfad = ~/.local/state/<app-id>/  oder  ~/.var/app/<app-id>/state/  */
    gchar *log_dir = g_build_filename(g_get_user_state_dir(), "bastis-oledsaver", NULL);

    if (g_mkdir_with_parents(log_dir, 0700) != 0) {
        g_free(log_dir);
        return;
    }

    /* Log-File, Pfad + Name  */
    // Flatpak zwei Ebenen-Kapselung: ~/.var/app/free.basti.oledsaver/.local/state/free.basti.oledsaver/oledsaver.log
    gchar *log_path = g_build_filename(log_dir, "debug.log", NULL);

    /* Log-File öffnen und Logging anhängen (w=write a=add) */
    log_file = fopen(log_path, "a");

    g_free(log_dir);
    g_free(log_path);

    if (!log_file)
        return;

    /* Default-Handler ersetzen */
    g_log_set_default_handler(log_handler, NULL);

    g_message("Logging initialized");

    /* Stdout umgeleiten auf Handler */
    g_set_print_handler(g_print_to_log);
}

/* ----- Log Prozess beenden ------------------------------------------------------------- */
void log_file_shutdown(void)
{
    if (!log_file)
        return;

    g_message("Logging shutdown");

    fclose(log_file);
    log_file = NULL;
}
