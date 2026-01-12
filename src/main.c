/* free.basti.oledsaver by super-toq 2025 - 2026 
 * LICENSE: BSD 2-Clause "Simplified"
 *
 *
 *
 * gcc $(pkg-config --cflags gtk4 libadwaita-1 dbus-1) -o _build/bin/bastis-oledsaver src/main.c src/time_stamp.c src/log_file.c src/config.c src/free.basti.oledsaver.gresource.c $(pkg-config --libs gtk4 libadwaita-1 dbus-1) -lm -Wno-deprecated-declarations
 *
 *
 *
 * Please note:
 * The Use of this code and execution of the applications is at your own risk, I accept no liability!
 *
 */
#define APP_VERSION    "1.2.0"//_1
#define APP_ID         "free.basti.oledsaver"
#define APP_NAME       "OLED Saver"
#define APP_DOMAINNAME "bastis-oledsaver"
#define KEEP_WIN_TOP_TIME  120 // Timer in on_fullscreen_button_clicked()
/* Fenstergröße Breite, Höche (380, 400) */
#define WIN_WIDTH      370
#define WIN_HEIGHT     410

#include <glib.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include "icon-gresource.h" // binäre Icons;
#include <string.h>         // für strstr() in Desktopumgebung;
#include <math.h>           // Mathe.Bibliothek für Berechnung der Mauskoordinatenveränderung (sqrt);
#include <dbus/dbus.h>      // für DBusConnection,DBusMessage,dbus_bus_get(),dbus_message_new_method_call;
#include <locale.h>         // für setlocale(LC_ALL, "")
#include <glib/gi18n.h>     // für " _(); "

#include "config.h"         // für Konfigurationsdatei;
#include "time_stamp.h"     // für Zeitstempel in Meldungen;
#include "log_file.h"       // für externe Log-Datei (~/.var/app/<id>/state/), wenn aktiviert;

/* ----- Umgebung identifizieren ------------------------------------ */
typedef enum {
    DESKTOP_UNKNOWN,
    DESKTOP_GNOME,
    DESKTOP_KDE,
    DESKTOP_XFCE,
    DESKTOP_MATE
} DesktopEnvironment;
static DesktopEnvironment detect_desktop(void) {
    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    if (!desktop) desktop = g_getenv("DESKTOP_SESSION");
    if (!desktop) return DESKTOP_UNKNOWN;
    g_autofree gchar *upper = g_ascii_strup(desktop, -1);
    if (strstr(upper, "GNOME")) return DESKTOP_GNOME;
    if (strstr(upper, "KDE"))   return DESKTOP_KDE;
    if (strstr(upper, "XFCE"))  return DESKTOP_XFCE;
    if (strstr(upper, "MATE"))  return DESKTOP_MATE;
    return DESKTOP_UNKNOWN;
}

typedef struct {                                  // Struktur für Combo_row
    const char *label;                            // Bezeichner der Combo-Optionen
    int value;
} PixelOption;
static const PixelOption pixel_options[] = {
        { "  50 px ",  50 },
        { " 100 px ", 100 },
        { " 200 px ", 200 },
};

typedef struct {
    AdwToastOverlay          *toast_overlay;      // Pointer auf die AdwToastOverlay
} ToastManager;
static ToastManager toast_manager = { NULL };

typedef struct {
    GtkWindow                *fullscreen_window;  // Screensaver Vollbild
    GtkWindow                *main_window;        // Hauptfenster
    GtkEventController       *key_controller;     // Event-Controller für Taste
    GtkEventController       *motion_controller;  // Event-Controller für motion
    guint                    timeout_id;          // Timer-ID zum stoppen von g_timeout_add_seconds(), zukünftig !!
    gboolean                 motion_initialized;  // Bewegungs-initialisierung für mouse_move_limit
    guint                    fullscreen_timer_id; // Timer (guint vergibt die ID zum beenden)
} ScreensaverStruct;

/* --- Globale Variablen für Inhibit --- */
static uint32_t gnome_cookie = 0;  // GNOME-Inhibit uint32-Cookie, geliefert von org.freedesktop.ScreenSaver.Inhibit;
static int system_fd = -1;         // systemd/KDE-Inhibit (fd = File Descriptor/Verbindung zu einem Systemdienst)
                                   // geliefert von org.freedesktop.login1.Manager.Inhibit;



/* ----- Toast Mitteilungen ---------------------------------------- */
static void show_toast(const char *msg)
{
    if (!toast_manager.toast_overlay) // toast_manager siehe Strukt.
        return;

    AdwToast *toast = adw_toast_new(msg);
    adw_toast_set_timeout(toast, 1.5); // Sekunden
    adw_toast_set_priority(toast, ADW_TOAST_PRIORITY_HIGH);
    adw_toast_overlay_add_toast(toast_manager.toast_overlay, toast);
}

/* ----- GNOME ScreenSaver Inhibit ---------------------------------- */
static void start_gnome_inhibit(void) { // Noch umbauen mit Rückmeldung bei Erfolg, wie STOP-Vorgang
    DBusError err;
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusMessageIter args;

    dbus_error_init(&err);
    /* Session-Bus */
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn || dbus_error_is_set(&err)) {
        g_warning("[GNOME-INHIBIT] DBus error: %s\n", err.message); dbus_error_free(&err);
        return; 
    }

    /* DBus-Auffruf vorbereiten */
    msg = dbus_message_new_method_call(
        "org.freedesktop.ScreenSaver",
        "/ScreenSaver",
        "org.freedesktop.ScreenSaver",
        "Inhibit"
    );
    
    if (!msg) {
        g_warning("[GNOME-INHIBIT] Error creating the DBus message (1)\n");
        return; 
    }

    const char *app = APP_NAME;
    const char *reason = "Prevent Standby";
    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &reason);

    /* Nachricht senden */
    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);

    /* Antwort auslesen (COOKIE als uint32) */
    DBusMessageIter iter;
    if (!reply) {
        g_warning("[GNOME-INHIBIT] Inhibit failed: no reply received\n");
        dbus_message_unref(msg);
        return;
    }

    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
        g_warning("[GNOME-INHIBIT] Inhibit reply invalid (no cookie)\n");
        dbus_message_unref(msg);
        dbus_message_unref(reply);
        return;
    }

    dbus_message_iter_get_basic(&iter, &gnome_cookie);
    g_print("[%s] [GNOME-INHIBIT] Inhibit activate (cookie=%u)\n", time_stamp(), gnome_cookie);

    dbus_message_unref(msg);
    dbus_message_unref(reply);
}

/* ----- Stop Gnome Inhibit ----------------------------------------- */
static gboolean stop_gnome_inhibit(GError **error) 
{
(void)error; // Bewusst noch ungenutzt

    if (!gnome_cookie) return TRUE; // True wenn kein Cookie vorhanden

    DBusError err;
    DBusConnection *conn;
    DBusMessage *msg;
    DBusMessageIter args;

    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn || dbus_error_is_set(&err)) {
       g_warning("[GNOME-INHIBIT] DBus error (session): %s\n", err.message);
       dbus_error_free(&err); return FALSE; }

    msg = dbus_message_new_method_call(
        "org.freedesktop.ScreenSaver",
        "/ScreenSaver",
        "org.freedesktop.ScreenSaver",
        "UnInhibit"
    );

    if (!msg) {
        g_warning("[GNOME-INHIBIT] Error creating the DBus message (2)\n");
        return FALSE;
    }

    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &gnome_cookie);

    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);
    g_print("[%s] [GNOME-INHIBIT] Inhibit closed (cookie=%u)\n", time_stamp(), gnome_cookie);
    gnome_cookie = 0;
    return TRUE; // Erfolgreich beendet - Rückgabe true
}

/* ----- systemd/KDE login1.Manager Inhibit ------------------------- */
static void start_system_inhibit(void) { // Noch umbauen mit Rückmeldung bei Erfolg, wie STOP-Vorgang
    DBusError err;
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusMessageIter args;

    /* Fehlerbehandlung */
    dbus_error_init(&err);

    /* Verbindung zum Systembus herstellen */
    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (!conn || dbus_error_is_set(&err)) {
       g_warning("System DBus error: %s\n", err.message);
       dbus_error_free(&err); return; 
    }

    /* Methodenaufruf vorbereiten */
    msg = dbus_message_new_method_call(
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "Inhibit");

    if (!msg) {
       g_warning("[SYSTEM-INHIBIT] Error creating the DBus message\n");
       return;
    }

    /* Argumente für Inhibit vorbereiten */
    const char *what = "sleep:idle:shutdown:handle-lid-switch:handle-suspend-key";
    const char *who  = "OLED-Saver";
    const char *why  = "Prevent Standby";
    const char *mode = "block";

    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &what);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &who);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &why);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &mode);

     /* Methode senden und Antwort empfangen */
    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);
    if (!reply || dbus_error_is_set(&err)) {
       g_warning("[SYSTEM-INHIBIT] Inhibit failed: %s\n", err.message);
       dbus_error_free(&err); dbus_message_unref(msg);
       return; 
    }

    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UNIX_FD) {
        g_warning("[SYSTEM-INHIBIT] Inhibit reply invalid\n");
        dbus_message_unref(msg);
        dbus_message_unref(reply);
        return;
    }

    dbus_message_iter_get_basic(&iter, &system_fd);
    g_print("[%s] [SYSTEM-INHIBIT] Inhibit activate (fd=%d)\n", time_stamp(), system_fd);
    /* Aufräumen */
    dbus_message_unref(msg);
    dbus_message_unref(reply);
}

/* ----- Stop System Inhibit ---------------------------------------- */
static gboolean stop_system_inhibit(GError **error) 
{
(void)error; // Bewusst noch ungenutzt

    if (system_fd < 0) return FALSE; // kein fd - Rückgabe false
    close(system_fd);
    g_print("[%s] [SYSTEM-INHIBIT] Inhibit closed (fd=%d)\n", time_stamp(), system_fd);
    system_fd = -1;
    return TRUE; // erfolgreich beendet - Rückgabe true
}

/* --- START-Wrapper --- ausgelöst in on_activate ------------------- */
static void start_standby_prevention(void) {  // Noch umbauen mit Rückmeldung bei Erfolg, wie STOP !!
    /* Entsprechende Funktion zur Umgebung starten: */
    DesktopEnvironment de = detect_desktop();
    if (de == DESKTOP_GNOME) start_gnome_inhibit();
    start_system_inhibit(); // KDE, XFCE, MATE

    /* Toast-Message ausgeben: */
     if (!g_cfg.start_in_fs) show_toast(_("Standby wird nun verhindert!")); // nicht anzeigen wenn g_cfg.start_in_fs=true
}
/* --- STOP-Wrapper --- ausgelöst durch die Buttons ----------------- */
static gboolean stop_standby_prevention(GError **error) 
{   // stop_sp true/false Mechanik ist vorbereitet aber unfertig !!
(void)error; // Bewusst noch ungenutzt

    gboolean stop_sp = TRUE; // TRUE wenn kein Fehler

    if (!stop_gnome_inhibit (error))
        stop_sp = FALSE;

    if (!stop_system_inhibit (error))
        stop_sp = FALSE;

    if (stop_sp)
    {
        g_print("[%s] [INFO] Preventing standby has been stopped\n", time_stamp());
        //
    }

    return stop_sp;
}
/* ------------------------------------------------------------------ */


/* ----- Alle Timer stoppen! ---------------------------------------- */
static void stop_all_timers(ScreensaverStruct *data)
{
    /* Struktur auf Gültigkeit prüfen */
    if (!data) return;

    /* wenn Timer existiert, stoppen. (Grundlage für weitere Timer) - */
    if (data->fullscreen_timer_id) {
        g_source_remove(data->fullscreen_timer_id);
        data->fullscreen_timer_id = 0;
    }

    /* Timer2 hier ... */

}

/* ----- Fullscreen-Fenster Beenden --------------------------------- */
static gboolean exit_screensaver(gpointer user_data)
{
    /* 0. Struktur holen und auf Gültigkeit prüfen */
    ScreensaverStruct *data = user_data;
    if (!data) return G_SOURCE_REMOVE;

    /* 1. Mögliche Kontroller schließen */
    if (data->key_controller)
        gtk_widget_remove_controller(GTK_WIDGET(data->fullscreen_window), data->key_controller);
    if (data->motion_controller)
        gtk_widget_remove_controller(GTK_WIDGET(data->fullscreen_window), data->motion_controller);


    /* 2. Fenster schließen */
    gtk_window_close(data->fullscreen_window);

    /* 3. Alle möglichen Timer beenden */
    stop_all_timers(data); // data wird in Funktion erwartet
    
    /* 4. Aufräumen */
    g_free(data);
    return G_SOURCE_REMOVE; // Quelle löschen (auch g_idle_add)
}

/* ----- Mausbewegung beendet Fullscreen Fenster, 
            aktiviert von on_fullscreen_button_clicked -------------- */
static gboolean on_fullscreen_by_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{ (void)controller; // erwartete Signatur


    /* 0. Daten aus ScreensaverStruktur */
    ScreensaverStruct *data = user_data;
    //ScreensaverStruct *data = (ScreensaverStruct *)user_data; // ??
    /* 1. Prüfung auf vorhandene Strukt und Fenster-Gültigkeit */
    if (!data || !data->fullscreen_window) return GDK_EVENT_STOP;

    const  gint64   ignore_duration  = 800000; // 1000000 µs = 1 Sekunde
    const  gdouble  threshold        = CLAMP(g_cfg.mouse_move_limit, 50.0, 500.0);

    static gboolean init_phase       = TRUE;   // Puffer-Phase aktiv/beendet
    static gint64   start_time       = 0;       // Zeit des Fensters-Starts
    static gdouble  total_distance   = 0.0;     // akkumulierte Distanz
    static gdouble  last_x           = 5.0;
    static gdouble  last_y           = 5.0;

    /* 2. Fensterstart = Startzeit */
    if (start_time == 0) 
    {
        /* 2.1 Startzeit speichern */
        start_time = g_get_monotonic_time(); // GLib's Monotonic Clock als Zeitgeber für Anfangsbewegung
        last_x = x;
        last_y = y;
        return GDK_EVENT_STOP;
    }

    /* 3. Puffer für Anfangsbewegung */
    if (init_phase) { // initialized phase = true
        /* 3.1 Jetziger Zeitpunkt */
        gint64 now = g_get_monotonic_time();
        if (now - start_time < ignore_duration) // Anfangs-Bewegung für unter 1 Sek. ignoriert.
        { 
            /* 3.2 Koor.punkte speichern */
            last_x = x;
            last_y = y;
            return GDK_EVENT_STOP;
        }
        /* 3.2 Pufferzeit vorbei */
        init_phase = FALSE;
        return GDK_EVENT_STOP; // aktuelles Event, welches den Puffer beendet, wird nicht zur Distanz addiert
    }

    /* 4. Gesamtdistanz seit letzten Event (Formel für Bewegung in 2D Raum) */
    gdouble dx = x - last_x;
    gdouble dy = y - last_y;
    //total_distance += sqrt(dx * dx + dy * dy);   // sqrt = Quadratwurzelberechnung [ d=(x2​−x1​)2+(y2​−y1​)2 ]
    total_distance = sqrt(dx * dx + dy * dy); // ohne Summierung!

    /* 5. Schwellenwert prüfen und bei entspr. Motion weiter zum Beenden... */
    if (total_distance < threshold) {
        return GDK_EVENT_STOP;
    }

    /* 6. Timer beenden:  (>0 heißt, es existiert eine ID vom Typ guint)*/
    if (data->fullscreen_timer_id > 0) {
        g_source_remove(data->fullscreen_timer_id);
        data->fullscreen_timer_id = 0;
    }
    /* 6.2 Ausgangswerte zurückstellen */
    init_phase     = TRUE;
    start_time     =    0;
    total_distance =  0.0;

    /* 7. Fullscreen-Fenster (einmal) schließen */
    g_idle_add(exit_screensaver, data);
    g_print("[%s] [INFO] Mouse motion exits fullscreen\n", time_stamp());
    return GDK_EVENT_STOP;

}

/* ----- Taste beendet Fullscreen, (alternativ zu mouse-motion) ----- */
static gboolean on_fullscreen_by_key(GtkEventControllerKey *controller,
                                                            guint keyval,  // Key-value      (die logische Taste)
                                                           guint keycode,  // Key-Scancode   (wird nicht benötigt)
                                                   GdkModifierType state,  // ob Zusatztaste (+ Alt, Shft ...)
                                                      gpointer user_data)  // = fullscreen_window
{
(void)controller; // erwartete genau diese Signatur
(void)keycode; (void)state;

    ScreensaverStruct *data = user_data;
    /* Prüfung auf vorhandene Strukt und Fenster-Gültigkeit */
    if (!data || !data->fullscreen_window) return GDK_EVENT_STOP;

    if (keyval !=GDK_KEY_space )
        return FALSE;             /*        Beispiel für Tasten:
                                                    GDK_KEY_Escape
                                                    GDK_KEY_Return
                                                    GDK_KEY_space
                                                    GDK_KEY_Shift_L
                                                    GDK_KEY_Control_L
                                                    GDK_KEY_Super_L
                                                    GDK_KEY_Delete
                                                    GDK_KEY_A
                                                    GDK_KEY_Up
                                                    GDK_KEY_F1             */

    if (keyval == GDK_KEY_space) 
    {

        /* Timer für Fenster-im-Vordergrund-halten beenden */
        if (data->fullscreen_timer_id > 0) { // prüfe ob der Timer existiert
            g_source_remove(data->fullscreen_timer_id); // entfernen
            data->fullscreen_timer_id = 0;   // auf 0 stellen
        }

        g_idle_add(exit_screensaver, data);
        //exit_screensaver(data);
        g_print("[%s] [INFO] Key press exits fullscreen\n", time_stamp());
    }

    return GDK_EVENT_STOP;
}

/* ----- Callback: About-Dialog öffnen ------------------------------ */
static void show_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{ (void)action; (void)parameter;

    AdwApplication *app = ADW_APPLICATION(user_data);
    /* About‑Dialog anlegen */
    AdwAboutDialog *about = ADW_ABOUT_DIALOG(adw_about_dialog_new ());
    adw_about_dialog_set_application_name(about, APP_NAME);
    adw_about_dialog_set_version(about, APP_VERSION);
    adw_about_dialog_set_developer_name(about, "Built for Basti™");
    adw_about_dialog_set_website(about, "https://github.com/super-toq/OLED-Saver");
    //adw_about_dialog_set_comments(about, " ... ");

    /* Lizenz – BSD2 wird als „custom“ angegeben */
    adw_about_dialog_set_license_type(about, GTK_LICENSE_BSD);
    adw_about_dialog_set_license(about,
        "Copyright © 2025 - 2026, super-toq\n\n"
        "This program comes WITHOUT ANY WARRANTY.\n"
        "Follow the link to view the license details: "
        "<a href=\"https://opensource.org/license/BSD-2-Clause\"><b>BSD 2-Clause License</b></a>\n"
        "\n"
        "Application Icons by SVG Repo. \n"
        "<a href=\"https://www.svgrepo.com\">www.svgrepo.com</a> \n"
        "Thanks to SVG Repo for sharing their free icons, "
        "we appreciate your generosity and respect your work.\n"
        "The icons are licensed under the \n"
        "<a href=\"https://www.svgrepo.com/page/licensing/#CC%20Attribution\">"
        "Creative Commons Attribution License.</a> \n"
        "Colours, shapes, and sizes of the symbols (icons) have been slightly modified from the original, "
        "some symbols have been combined with each other.\n"
        );

      /* Dialog-Icon aus g_resource */
      adw_about_dialog_set_application_icon(about, APP_ID);   //IconName

    /* Dialog innerhalb (modal) des Haupt-Fensters anzeigen */
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(
       gtk_application_get_active_window(GTK_APPLICATION(app)) )));
    adw_dialog_present(ADW_DIALOG(about), GTK_WIDGET(parent));
} // Ende About-Dialog


/* ----- ComboRow-IndexNr dem Werte aus settings.cfg > mouse_move_limit zuordnen - */
static guint value_to_combo_index(int value)
{
    /* Schleife durchläuft von 0 bis Ende in pixel_options, 
       G_N_ELEMENTS-Macro gibt Anzahl der Elemente zurück, i=Indexwert */ 
    for (guint i = 0; i < G_N_ELEMENTS(pixel_options); i++) { //
        if (pixel_options[i].value == value)
            return i;
    }

    /* Ungültiger Config-Wert wird immer zu Index 0 */
    g_cfg.mouse_move_limit = pixel_options[0].value;
    return 0;
}

/* ----- Wert aus ComboRow in g_cfg.mouse_move_limit einfügen ------- */
static void on_combo_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{ (void)pspec; (void)user_data;

    guint idx = adw_combo_row_get_selected(ADW_COMBO_ROW(object));
    g_cfg.mouse_move_limit = pixel_options[idx].value;
    g_print("[%s] [INFO] Settings: mouse_move_limit=%dpx\n", time_stamp(), g_cfg.mouse_move_limit);

    /* Wert auch in Settings.cfg speichern */
    save_config();
}

/* ----- Verzeichnis zur Log-Datei öffnen ----------------------------------------------- */
static void open_log_folder(GtkButton *button, gpointer user_data)
{  (void)button; (void)user_data; // erwartete Signatur

    /* Fehlerroutine, Verzeichnis anlegen, falls vom Benutzer gelöscht wurde */
    log_folder_init();

    const gchar *home = config_get_home_path();                                  // aus config.c
    gchar *homepath = g_build_filename(home, ".var/app/free.basti.oledsaver/.local/state/bastis-oledsaver", NULL);
    char *uri = g_filename_to_uri(homepath, NULL, NULL);

    if (uri) {
        gtk_show_uri(NULL, uri, GDK_CURRENT_TIME);
        g_free(uri);
        g_free(homepath); //!!
    }
}

/* ----- In Einstellungen: Schalter1-Toggle (AdwSwitchRow)--------------------------------- */
static void on_settings_use_key_switch_row_toggled(GObject *object1, GParamSpec *pspec, gpointer user_data)
{ (void)pspec; (void)user_data;

    AdwSwitchRow *use_key_switch = ADW_SWITCH_ROW(object1);
    gboolean active = adw_switch_row_get_active(use_key_switch);
    g_cfg.use_key = active;
    save_config(); // speichern
    g_print("[%s] [IFNO] Settings: use_key=%s\n", time_stamp(), g_cfg.use_key ? "true" : "false"); // zum testen !!

    /* combo_row1 deaktivieren wenn use_key aktiviert wird; combo_row1 wird per user_data übergeben */
    GtkWidget *combo_row1 = GTK_WIDGET(user_data);
    gtk_widget_set_sensitive(combo_row1, g_cfg.use_key ? FALSE : TRUE);
}
/* ----- In Einstellungen: Schalter2-Toggle (AdwSwitchRow) ------------ */
static void on_settings_start_in_fs_switch_row_toggled(GObject *object2, GParamSpec *pspec, gpointer user_data)
{ (void)pspec; (void)user_data;

    AdwSwitchRow *start_in_fs_switch = ADW_SWITCH_ROW(object2); 
    gboolean active = adw_switch_row_get_active(start_in_fs_switch);
    g_cfg.start_in_fs = active;
    save_config(); // speichern
    g_print("[%s] [INFO] Settings: start_in_fs=%s\n", time_stamp(), g_cfg.start_in_fs ? "true" : "false"); // zum testen !!
}
/* ----- In Einstellungen: ActionRowGtkSwitch-Toggle ------------------ */ // ab version 1.1.5
static void on_settings_log_enable_gtkswitch_toggled(GObject *object, GParamSpec *pspec, gpointer user_data)
{ (void)pspec; (void)user_data;

    GtkSwitch *log_enable_switch = GTK_SWITCH(object);
    gboolean active = gtk_switch_get_active(log_enable_switch);
    g_cfg.log_enable = active;
    save_config(); // speichern
    g_print("[%s] [INFO] Settings: log_enable=%s\n", time_stamp(), g_cfg.log_enable ? "true" : "false"); // zum testen !!
    if (active) log_file_init(APP_ID); // Logging sofort beginnen (in config.c)
}
/* ----- Einstellungen-Page ------------------------------------------- */
static void show_settings(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{ (void)action; (void)parameter;

    AdwNavigationView *settings_nav = ADW_NAVIGATION_VIEW(user_data);

    /* ----- ToolbarView für Settings-Seite ----- */
    AdwToolbarView *settings_toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());

    /* ----- Headerbar erzeugen ----- */
    AdwHeaderBar *settings_header = ADW_HEADER_BAR(adw_header_bar_new());
    GtkWidget *settings_label = gtk_label_new(_("Einstellungen"));
    gtk_widget_add_css_class(settings_label, "heading");
    adw_header_bar_set_title_widget(settings_header, settings_label);

    /* ----- Headerbar einfügen ----- */
    adw_toolbar_view_add_top_bar(settings_toolbar, GTK_WIDGET(settings_header));

    /* ----- Haupt-BOX der Settings-Seite ----- */
    GtkWidget *settings_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(settings_box,    12);   // Rand unterhalb Toolbar
    gtk_widget_set_margin_bottom(settings_box, 12);   // unterer Rand unteh. der Buttons
    gtk_widget_set_margin_start(settings_box,  12);   // links
    gtk_widget_set_margin_end(settings_box,    12);   // rechts

    /* ----- PreferencesGroup erstellen ----- */
    AdwPreferencesGroup *settings_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(settings_group, _("Präferenzoptionen"));
    //adw_preferences_group_set_description(settings_group, _("Zusatzbeschreibung - Platzhalter"));

    /* ----- Combo Row erstellen ------------------------------------------------------------------- */
    GtkWidget *combo_row1 = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(combo_row1), 
                                                    _("Minimalbewegungen zulassen"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(combo_row1),
     _("Mausbewegungsschwelle in Pixeln"));

    /* ----- String_List der verfügbaren Optionen bauen ----- */
    GtkStringList *string_list = gtk_string_list_new(NULL);

    for (guint i = 0; i < G_N_ELEMENTS(pixel_options); i++) {
    gtk_string_list_append(string_list, pixel_options[i].label);
    }
    adw_combo_row_set_model(ADW_COMBO_ROW(combo_row1), G_LIST_MODEL(string_list));
    
    /* ----- ComboRow-Option per Index setzen [ guint in value_to_combo_index()] ----- !! */ 
    adw_combo_row_set_selected(ADW_COMBO_ROW(combo_row1),value_to_combo_index(g_cfg.mouse_move_limit));

    /* ----- ComboRow verbinden ----------------------------- */
    g_signal_connect(combo_row1, "notify::selected", G_CALLBACK(on_combo_changed), NULL);
    gtk_widget_set_sensitive(combo_row1, g_cfg.use_key ? FALSE : TRUE); // abhängig von cfg de/aktiviert
    
    /* ----- Combo Row zur PreferencesGroup hinzufügen ----- */
    adw_preferences_group_add(settings_group, combo_row1);
    /* ------------------------------------------------------------------------------ Ende Combo Box */

    /* ----- AdwSwitchRow1 erzeugen --------- */
    AdwSwitchRow *switch_row1 = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(switch_row1), 
                                                    _("Leertaste verwenden"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(switch_row1),
     _("Verwende die Leertaste anstelle der Maus, um den Blackscreen-Modus zu beenden"));
    /* Schalter-Aktivierung abhängig von gesetzten g_cfg.-Wert: */
    adw_switch_row_set_active(ADW_SWITCH_ROW(switch_row1), g_cfg.use_key);
    gtk_widget_set_sensitive(GTK_WIDGET(switch_row1), TRUE);    //Aktiviert/Deaktiviert

    /* ----- AdwSwitchRow2 erzeugen --------- */
    AdwSwitchRow *switch_row2 = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(switch_row2), 
                                                    _("Im Fullscreen-Modus starten"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(switch_row2),
     _("App direkt im Fullscreen-Modus starten"));
    /* Schalter-Aktivierung abhängig von gesetzten g_cfg.-Wert: */
    adw_switch_row_set_active(ADW_SWITCH_ROW(switch_row2), g_cfg.start_in_fs);
    gtk_widget_set_sensitive(GTK_WIDGET(switch_row2), TRUE);    //Aktiviert/Deaktiviert


    /* ----- ActionRow erstellen --(ab Version 1.1.5)----------------------------- Action ROW ------ */
    AdwActionRow *action_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(action_row), _("Debug-Datei erstellen"));
    adw_action_row_set_subtitle(action_row, _("Protokollausgaben in eine Datei schreiben"));

    /* ----- Button für Log-Folder erstellen ------------------- */
    GtkWidget *folder_button = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(folder_button), FALSE);                  // Button ohne Rahmen
    gtk_widget_set_valign(folder_button, GTK_ALIGN_CENTER);
    g_signal_connect(folder_button, "clicked", G_CALLBACK(open_log_folder),NULL);

    /* ----- Switch für ActionRow erstellen ------ */
    GtkWidget *log_enable_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(log_enable_switch), g_cfg.log_enable);      // Schalterstellung abhängig von config
    gtk_widget_set_valign(log_enable_switch, GTK_ALIGN_CENTER);

    /* ----- Objekte der ActionRow einfügen ------ */
    adw_action_row_add_prefix(action_row, folder_button);
    adw_action_row_add_suffix(action_row, log_enable_switch);
    adw_action_row_set_activatable_widget(action_row, log_enable_switch); /*<----- Action ROW Ende -- */

    /* ----- Schalter verbinden --------------------------------- */
    g_signal_connect(switch_row1,       "notify::active",               // use_key
                                  G_CALLBACK(on_settings_use_key_switch_row_toggled), combo_row1); // combo_row1 ebenfalls übergeben, zum de/aktivieren
    g_signal_connect(switch_row2, "notify::active",                     // start_in_fs
                                  G_CALLBACK(on_settings_start_in_fs_switch_row_toggled), NULL);
    g_signal_connect(log_enable_switch, "notify::active",               // log_enable 
                                  G_CALLBACK(on_settings_log_enable_gtkswitch_toggled),   NULL);

    /* ----- Rows zur PreferencesGruppe hinzufügen ----- */
    adw_preferences_group_add(settings_group, GTK_WIDGET(switch_row1)); // SwitchRow  - use_key
    adw_preferences_group_add(settings_group, GTK_WIDGET(switch_row2)); // SwitchRow  - start_in_fs
    adw_preferences_group_add(settings_group, GTK_WIDGET(action_row));  // AchtionRow - log_enable

    /* ----- Pref.Gruppe in die Page einbauen ----- */
    gtk_box_append(GTK_BOX(settings_box), GTK_WIDGET(settings_group));

    /* ----- ScrolledWindow erstellen und in die settingsBOX einfügen ----- */
    GtkWidget *scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), settings_box);

    /* ----- ToolbarView Inhalt in ScrolledWindow einsetzen ----- */
    adw_toolbar_view_set_content(settings_toolbar, scrolled_window);

    /* ----- NavigationPage anlegen ----- */
    AdwNavigationPage *settings_page = 
                      adw_navigation_page_new(GTK_WIDGET(settings_toolbar), _("Einstellungen"));
    /* ----- Größe nur zum Ausgleichen der Textlänge bei "Große Schrift" ----- */
    gtk_widget_set_size_request(GTK_WIDGET(settings_page), WIN_WIDTH, WIN_HEIGHT);  // Fenster-Breite u Höche (380, 400)

    /* ----- Page der Settings_nav hinzufügen ----- */
    adw_navigation_view_push(settings_nav, settings_page);
}// Ende Einstellungen-Fenster


/* ----- Funktion die im Intervall das Fullscreen-Fenster 
                      zurück in den Vordergrund ruft ---------------- */
static gboolean keep_window_on_top(gpointer user_data) 
{
    /* 0. Daten aus ScreensaverStruktur */
    ScreensaverStruct *data = user_data;
    
    // Sicherheitsüberprüfungen, existiert Struktur und existiert Fenster
    if (!data || !data->fullscreen_window)
        return G_SOURCE_REMOVE; // Timer beendet

    if (!GTK_IS_WINDOW(data->fullscreen_window))
        return G_SOURCE_REMOVE; // Timer beendet


    g_print("[%s] keep window top (2-minute refresh time)\n", time_stamp());
    
    // Fenster verstecken
    gtk_widget_hide(GTK_WIDGET(data->fullscreen_window));
    // Fenster erneut anzeigen
    gtk_widget_show(GTK_WIDGET(data->fullscreen_window));
    gtk_window_present(data->fullscreen_window);

    return G_SOURCE_CONTINUE;  // Timer läuft weiter
}

/* ----- Callback Fullscreen-Button ------------------- */
static void on_fullscreen_button_clicked(GtkButton *button, gpointer user_data)
{ (void)button; // erwartete Signatur
// Für diesen Fullscreen gilt, alles GTK kein Adw!

    GtkApplication *app = GTK_APPLICATION(user_data);

    /* 1. Blackscreen erzeugen, Methode: Fenster */
    GtkWindow *main_win = GTK_WINDOW(g_object_get_data(G_OBJECT(app), "main-window"));

    /* 1.1 Prüfen ob das GDK-Display gültig ist */
    GdkDisplay *display = gdk_display_get_default();
    if (display == NULL) {
        g_warning("Error: Unable to obtain default display.\n");
        return;
    }
    /* 1.2 Prüfen ob das Hauptfenster gültig ist */
    if (!main_win) {
        g_warning("Error: Unable to obtain main window");
        return;
    }

    /* 1.2 CSS-Provider für Hintergrundfarbe */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
                       ".fullscreen-window {"  // Für OLED Schwarz
    "  background-color: rgba(0, 0, 0, 1.0);"  // RGB und Alpha (1.0 = voll Opak)
                                          "}"
                                           );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
          GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* 1.3 Vollbild-Fenster */
    GtkWidget *fullscreen_window = gtk_application_window_new(app);

    /*  Transient + Modal zwingen Child_window zu modalen Dialog */
    gtk_window_set_transient_for(GTK_WINDOW(fullscreen_window), GTK_WINDOW(main_win));
    /* App blockieren solange Fenster besteht */
    gtk_window_set_modal(GTK_WINDOW(fullscreen_window), TRUE);

    /* Fenstereigenschaften */
    gtk_window_set_decorated(GTK_WINDOW(fullscreen_window), FALSE);
    gtk_widget_add_css_class(fullscreen_window, "fullscreen-window");
    gtk_window_fullscreen(GTK_WINDOW(fullscreen_window));

    /* Child-Window zum Hauptfenster erstellen */
    GtkWidget *child_windowbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(fullscreen_window), child_windowbox); 

    /* Fenster anzeigen */
    gtk_window_present(GTK_WINDOW(fullscreen_window));


    /* ----- Screensaver-Struktur initialisieren ----- */
    ScreensaverStruct *data = g_new0(ScreensaverStruct, 1); 
    data->fullscreen_window = GTK_WINDOW(fullscreen_window);
    data->main_window       = GTK_WINDOW(main_win);

    /* ButtonBox blockieren, doch diese wurde noch nicht übergeben !! Merkzettel */
    //gtk_widget_set_sensitive(button_box, FALSE);

    /* 2. Unterbrecher für Fullscreen durch Key oder Mausbewegung */
    if (g_cfg.use_key)        // in config.c
    {
        /* Key-Controller erstellen */
        data->key_controller = gtk_event_controller_key_new();
        g_signal_connect(data->key_controller, "key-pressed", G_CALLBACK(on_fullscreen_by_key), data);
        gtk_widget_add_controller(GTK_WIDGET(fullscreen_window), data->key_controller);
    } else {
            /* Motion-Controller erstellen */
            data->motion_controller = gtk_event_controller_motion_new();
            g_signal_connect(data->motion_controller, "motion", G_CALLBACK(on_fullscreen_by_motion), data);
            gtk_widget_add_controller(GTK_WIDGET(fullscreen_window), data->motion_controller);
    }

    /* 3 Checkbox-Zeiger aus g_object user_data holen */
                          //Hinweis: g_object_steal_data besser als g_object_get_data, zwecks Freigabe
    GtkWidget *set1_check = GTK_WIDGET(g_object_steal_data(G_OBJECT(app), "check"));
    if (!set1_check) return; // Fehlerbehandlung
    /* 3.1 Checkbox-Wert in is_active speichern */
    gboolean is_active = gtk_check_button_get_active(GTK_CHECK_BUTTON(set1_check));

    /* 3.2 aufrufen von keep_window_on_top() wenn Checkbox=true */
    if (is_active) {
    /* 3 Timer um Fenster wieder in den Vordergrund zu holen, siehe keep_window_on_top() */
    g_print ("[%s] window top is activated\n", time_stamp());
    // old //fullscreen_timer_id = g_timeout_add_seconds(120, keep_window_on_top, fullscreen_window);
    data->fullscreen_timer_id = g_timeout_add_seconds(KEEP_WIN_TOP_TIME, keep_window_on_top, data); //data übergeben

    }
}

/* ----- Callback Beenden-Button ------------------------------------ */
static void on_quitbutton_clicked(GtkButton *button, gpointer user_data)
{  (void)button; // erwartete Signatur

    GError *error = NULL;
    g_print("[%s] [INFO] Applicaton will now shut down\n", time_stamp());
    if (stop_standby_prevention(&error)) {
        /* Rückmeldung aus stop_standby_prevention (stop_sp) */
     // Hier Ausbau für Rückmeldung (TRUE) !!

    } else {
        g_warning ("Failed to stop standby prevention: %s\n", error->message);
        g_error_free(error); // nur bei FALSE ist Fehler

     // Hier Ausbau für Rückmeldung (FALSE) !!

    }
    /* zu schließendes Fenster holen */
    GtkWindow *window = GTK_WINDOW(user_data);
    gtk_window_destroy(window);
    /* Logging beenden, falls aktiv */
    log_file_shutdown();
}

/* ------------------------------------------------------------------ */
/*       Aktivierungshandler                                          */
/* ------------------------------------------------------------------ */
static void on_activate(AdwApplication *app, gpointer user_data) 
{ (void)user_data;
    /* ----- CSS-Provider für zusätzliche Anpassungen --------------- */
    // orange=#db9c4a , lightred=#ff8484 , grey=#c0bfbc
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
                            ".opaque.custom-suggested-action-button1 {" // Blackscreen-btn
                                         "  background-color: #c0bfbc;"
                                                      "  color: black;"
                                                                    "}"
                            ".opaque.custom-suggested-action-button2 {"
                                         "  background-color: #434347;"
                                                    "  color: #ff8484;"
                                                                    "}"
                                                        "checkbutton {"
                                                    "  color: #c0bfbc;"
                                                                    "}"
                                                             "label1 {"
                                                    "  color: #c0bfbc;"
                                                                    "}"
                                                                     );

    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
    GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* ----- Adwaita-Fenster ----------------------------------------- */
    AdwApplicationWindow *adw_win = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app)));
    gtk_window_set_title(GTK_WINDOW(adw_win), APP_NAME);
    gtk_window_set_default_size(GTK_WINDOW(adw_win), WIN_WIDTH, WIN_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(adw_win), FALSE);

    /* --- NavigationView -------------------------------------------- */
    AdwNavigationView *nav_view = ADW_NAVIGATION_VIEW(adw_navigation_view_new());

    /* ----- ToolbarView --------------------------------------------- */
    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());

    /* ----- HeaderBar ----------------------------------------------- */
    AdwHeaderBar *headerbar = ADW_HEADER_BAR(adw_header_bar_new());
    GtkWidget *title_label = gtk_label_new("Basti's OLED Saver");
    gtk_widget_add_css_class(title_label, "heading");
    adw_header_bar_set_title_widget(headerbar, title_label);
    adw_toolbar_view_add_top_bar(toolbar_view, GTK_WIDGET(headerbar));      // Headerbar in ToolbarView

    /* --- Hauptseite in NavigationView ------------------------------ */
    AdwNavigationPage *main_page = adw_navigation_page_new(GTK_WIDGET(toolbar_view), APP_NAME);
    adw_navigation_view_push(nav_view, main_page);                         // NavView als MainPage

    /* ----- ToastOverlay -------------------------------------------- */
    //toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    //adw_toast_overlay_set_child(toast_overlay, GTK_WIDGET(nav_view));
    toast_manager.toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new()); // ToastOverlay in Strukt.
    adw_toast_overlay_set_child(toast_manager.toast_overlay, GTK_WIDGET(nav_view));
    /* ----- ToastOverlay in das Fenster einfügen  ------------------- */
    //adw_application_window_set_content(adw_win, GTK_WIDGET(toast_overlay)); // ToastOverl. in AdwWin
    adw_application_window_set_content(adw_win, GTK_WIDGET(toast_manager.toast_overlay)); // ToastOverl. in AdwWin / Strukt.

     //  Widget Hierarchie:                                               (ab version 1.1.5)
     //      ApplicationWindow
     //      └---ToastOverlay
     //          └---NavigationView
     //              └---NavigationPage
     //                  └---ToolbarView
     //                      │
     //                      ├---HeaderBar
     //                      │   └---Titel
     //                      ├---Seiteninhalt
     //                      .

    /* --- Hamburger-Button innerhalb der Headerbar ------------------ */
    GtkMenuButton *menu_btn = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_menu_button_set_icon_name(menu_btn, "open-menu-symbolic");
    adw_header_bar_pack_start(headerbar, GTK_WIDGET(menu_btn)); // Link in Headerbar

    /* --- Popover-Menu im Hamburger --------------------------------- */
    GMenu *menu = g_menu_new();
    g_menu_append(menu, _("Einstellungen         "), "app.show-settings");
    g_menu_append(menu, _("Infos zu OLED Saver   "), "app.show-about");
    GtkPopoverMenu *menu_popover = GTK_POPOVER_MENU(
        gtk_popover_menu_new_from_model(G_MENU_MODEL(menu)));
    gtk_menu_button_set_popover(menu_btn, GTK_WIDGET(menu_popover));

    /* --- Aktion die den About-Dialog öffnet ------------------------ */
    /* --- Action die die Einstellungen öffnet --- */
    const GActionEntry action_entries[] = {
    { "show-about", show_about, NULL, NULL, NULL, {0} },
    { "show-settings", show_settings, NULL, NULL, NULL, {0} }
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), action_entries, G_N_ELEMENTS(action_entries), nav_view);

    /* ---- Haupt-Box ------------------------------------------------ */
    GtkBox *main_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 12));
//    gtk_box_set_spacing(GTK_BOX(main_box), -0);                // Minus-Spacer, zieht unteren Teil nach oben
    gtk_widget_set_margin_top   (GTK_WIDGET(main_box),    24);   // Rand unterhalb Toolbar
    gtk_widget_set_margin_bottom(GTK_WIDGET(main_box),    24);   // unterer Rand unteh. der Buttons
    gtk_widget_set_margin_start (GTK_WIDGET(main_box),    12);   // links
    gtk_widget_set_margin_end   (GTK_WIDGET(main_box),    12);   // rechts
//    gtk_widget_set_hexpand      (GTK_WIDGET(main_box), TRUE);
//    gtk_widget_set_vexpand      (GTK_WIDGET(main_box), TRUE);

    /* ----- Label1 -------------------------------------------------- */
    GtkWidget *label1 = gtk_label_new(_("Blackscreen statt Burn-in! \n"));
    gtk_widget_set_halign(label1, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label1, GTK_ALIGN_CENTER);

    /* ----- Icon ---------------------------------------------------- */
    GtkWidget *icon = gtk_image_new_from_resource("/free/basti/oledsaver/icon2"); //alias in xml !
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);                 // Icon horizontal zentrieren
    gtk_image_set_pixel_size(GTK_IMAGE(icon),  128);
    gtk_widget_set_margin_top(GTK_WIDGET(icon), 10);               // Abstand zum Label1

    /* ----- Checkbox "set1_check" ----------------------------------- */
    GtkWidget *set1_check = gtk_check_button_new_with_label(_("Alle 2min auffrischen")); 
    gtk_check_button_set_active(GTK_CHECK_BUTTON(set1_check), FALSE); // standardmäßig auf inaktiv
    gtk_widget_set_visible(GTK_WIDGET(set1_check), TRUE); // Checkbox Sichtbarkeit

    /* ----- Checkbox-BOX-Widget ------------------------------------- */
    GtkWidget *chbx_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_hexpand(chbx_box, FALSE);
    gtk_widget_set_halign(chbx_box, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(chbx_box), GTK_WIDGET(set1_check));

    /* --- G_object, Checkbox an "app" speichern, um diese im Callback abrufen zu können --- */
    g_object_set_data(G_OBJECT(app), "check", set1_check); // Freigeben anhand g_object_steal_data
        // Hinweis "set1_check" wird in on_fullscr.btn.clicked abgerufen.
    /* ----- Buttons-Box ---------------------------------------------- */
    GtkWidget *buttons_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_widget_set_halign(buttons_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(buttons_box, GTK_ALIGN_END); // Box fest am unteren Rand

    /* ----- Schaltfläche-Fullscreen --------------------------------- */
    GtkWidget *setfullscreen_button = gtk_button_new_with_label(_("Blackscreen"));
    //gtk_widget_set_halign(setfullscreen_button, GTK_ALIGN_CENTER);
    //gtk_widget_add_css_class(setfullscreen_button, "suggested-action");
    gtk_widget_add_css_class(setfullscreen_button, "custom-suggested-action-button1"); // CSS-Provider...
    gtk_widget_add_css_class(setfullscreen_button, "opaque"); // durchsichtig machen...

    /* ----- Schaltfläche Beenden ------------------------------------ */
    GtkWidget *quit_button = gtk_button_new_with_label(_("  Beenden  "));
    gtk_widget_set_halign(quit_button, GTK_ALIGN_CENTER);
    //gtk_widget_add_css_class(quit_button, "raised");
    gtk_widget_add_css_class(quit_button, "custom-suggested-action-button2"); // CSS-Provider...
    gtk_widget_add_css_class(quit_button, "opaque"); // durchsichtig,
    //gtk_widget_add_css_class(quit_button, "destructive-action");

    /* --- Schaltflächen verbinden und "app" übergeben --------------- */
    g_signal_connect(setfullscreen_button, "clicked", G_CALLBACK(on_fullscreen_button_clicked), app);
    g_signal_connect(quit_button, "clicked", G_CALLBACK(on_quitbutton_clicked), adw_win);
    /* ------ Close-request vom originalen "Window-close-button" abfangen ----- */
    g_signal_connect(adw_win, "close-request", G_CALLBACK(on_quitbutton_clicked), adw_win);

    /* ----- Beide Schaltflächen der BOX hinzufügen ------------------ */
    gtk_box_append(GTK_BOX(buttons_box), quit_button);
    gtk_box_append(GTK_BOX(buttons_box), setfullscreen_button);

    /* ----- Spacer hinzufügen --------------------------------------- */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(spacer, TRUE);

    /* ----- Objekte der MainBOX hinzufügen -------------------------- */
    gtk_box_append(main_box, label1);
    gtk_box_append(GTK_BOX(main_box), icon);
    gtk_box_append(GTK_BOX(main_box), spacer);              // Spacer
    gtk_box_append(main_box, chbx_box);                     // Checkbox-BOX hinzufügen
    gtk_box_append(GTK_BOX(main_box), buttons_box);

    /* -----  Haupt-Box zur ToolbarView hinzufügen ------------------ */
    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(main_box));

    /* --- Dark-Mode erzwingen --- */
    AdwStyleManager *style_manager = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style_manager, ADW_COLOR_SCHEME_FORCE_DARK);

    /* ----- Hauptfenster im Application-Objekt ablegen (!) --------- */
    g_object_set_data(G_OBJECT(app), "main-window", adw_win);

    /* ----- Hauptfenster desktop‑konform anzeigen ------------------ */
    gtk_window_present(GTK_WINDOW(adw_win));

    /* +++++ Funktion zum umgehen der Standbyzeit starten ++++++++++++ */
    start_standby_prevention();

    /* Settings: start_in_fs = true:   */
    if (g_cfg.start_in_fs) {
    g_print("[%s] [WARNING] Settings: start_in_fs=true - Start in fullscreen mode!\n", time_stamp());
//    gtk_window_minimize(GTK_WINDOW(adw_win));                    // App Fenster minimieren
//    window_handler(adw_win);
    g_signal_emit_by_name(setfullscreen_button, "clicked");      // FullscreenButton klicken
    }

}

/* --------------------------------------------------------------------------- */
/* Anwendungshauptteil, main()                                                 */
/* --------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    /* 0. Print nur für Terminalausgabe */
    g_print("[%s] [INFO] Application is starting...\n", time_stamp());

    /* 1. Instanz erstellen, mit App-ID und Default-Flags */
    g_autoptr(AdwApplication) app =
                        adw_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);

    /* 2. Config-Datei initialisieren */
    init_environment();                                     // Config.c: Environment ermitteln
    init_config();                                          // Config.c: Config File laden/erstellen

    /* 2.1 Config-Werte zum testen auslesen: !! */
    g_print("Settings:\n mouse_move_limit=%d,\n use_key=%d\n start_in_fs=%d\n log_enable=%d\n",
                         g_cfg.mouse_move_limit, g_cfg.use_key, g_cfg.start_in_fs, g_cfg.log_enable);

    /* 3. Externes Logging starten und APP_ID übermitteln */
    log_folder_init();                                      // Logfolder immer erstellen
    if (g_cfg.log_enable) log_file_init(APP_ID);            // Logging aktivieren

    /* 4. Localiziation-Setup */
    const char *flatpak_id   = getenv("FLATPAK_ID");        // flatpak string einlesen, wenn vorhanden
    const gchar *locale_path = NULL;                        // Variable mit auto. Freigabe
    setlocale(LC_ALL, "");                                  // ruft die aktuelle Locale des Prozesses ab
//    setlocale(LC_ALL, "en_US.UTF-8");                     // explizit, zum testen!!
    textdomain             (APP_DOMAINNAME);                // textdomain festlegen
//    g_print("[%s] [INFO] flatpak_id %s\n",time_stamp(), flatpak_id);
    bind_textdomain_codeset(APP_DOMAINNAME, "UTF-8"); 
    if (flatpak_id != NULL && flatpak_id[0] != '\0')
    {
        locale_path = "/app/share/locale";
    } else {
        locale_path = "/usr/share/locale";
    }
    bindtextdomain         (APP_DOMAINNAME, locale_path);
    g_print("[%s] [INFO] Localization files in: %s \n", time_stamp(), locale_path);

    /* 5. Resource-Bundle registrieren */
    g_resources_register(resources_get_resource());

    /* 7. Verbindung zu UI */
    g_signal_connect(app, "activate",     G_CALLBACK(on_activate),  NULL);

    /* 8. Anwendung starten und Ereignis warten */
    return g_application_run(G_APPLICATION(app), argc, argv);
}
