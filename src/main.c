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
#define APP_VERSION    "1.1.0"//_2
#define APP_ID         "free.basti.oledsaver"
#define APP_NAME       "OLED Saver"
#define APP_DOMAINNAME "bastis-oledsaver"

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

/* --- Globale Variablen für Inhibit --- */
static uint32_t gnome_cookie = 0;  // GNOME-Inhibit uint32-Cookie, geliefert von org.freedesktop.ScreenSaver.Inhibit;
static int system_fd = -1;         // systemd/KDE-Inhibit (fd = File Descriptor/Verbindung zu einem Systemdienst)
                                   // geliefert von org.freedesktop.login1.Manager.Inhibit;
guint fullscreen_timer_id = 0;


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
    g_print("[%s] [GNOME-INHIBIT] Inhibit active (cookie=%u)\n", time_stamp(), gnome_cookie);

    dbus_message_unref(msg);
    dbus_message_unref(reply);
}

/* ----- Stop Gnome Inhibit ----------------------------------------- */
static gboolean stop_gnome_inhibit(GError **error) 
{
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
    g_print("[%s] [SYSTEM-INHIBIT] Inhibit active (fd=%d)\n", time_stamp(), system_fd);
    /* Aufräumen */
    dbus_message_unref(msg);
    dbus_message_unref(reply);
}

/* ----- Stop System Inhibit ---------------------------------------- */
static gboolean stop_system_inhibit(GError **error) 
{
    if (system_fd < 0) return FALSE; // kein fd - Rückgabe false
    close(system_fd);
    system_fd = -1;
    g_print("[%s] [SYSTEM-INHIBIT] Preventing standby has been stopped\n", time_stamp());
    return TRUE; // erfolgreich beendet - Rückgabe true
}

/* --- START-Wrapper --- ausgelöst in on_activate ------------------- */
static void start_standby_prevention(void) {  // Noch umbauen mit Rückmeldung bei Erfolg, wie STOP !!
    /* Entsprechende Funktion zur Umgebung starten: */
    DesktopEnvironment de = detect_desktop();
    if (de == DESKTOP_GNOME) start_gnome_inhibit();
    start_system_inhibit(); // KDE, XFCE, MATE
}
/* --- STOP-Wrapper --- ausgelöst durch die Buttons ----------------- */
static gboolean stop_standby_prevention(GError **error) 
{

    gboolean stop_sp = TRUE; // Erfolg meldet TRUE zurück und als GError übermittelt

    if (!stop_gnome_inhibit (error))
        stop_sp = FALSE;

    if (!stop_system_inhibit (error))
        stop_sp = FALSE;

    return stop_sp;
}
/* ------------------------------------------------------------------ */


/* ----- Mausbewegung beendet Fullscreen Fenster, 
            aktiviert von on_fullscreen_button_clicked -------------- */
static gboolean exit_fullscreen_by_motion(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{
    /* 1. Koordinaten zur Mausbewegung */
    static  gdouble last_x = 0, last_y = 0;        // Koordinaten
    static  gdouble logging_total_distance = 0;    // nur für g_print Ausgabe

    /* Threshold-Wert aus settings.cfg, mit unteren und oberen Grenzwert anhand von CLAMP */
    gdouble threshold = CLAMP(g_cfg.mouse_move_limit, 10, 500); // Wenn mouse_move_limit leer, Standardwert in config.c

    /* 1.1 Anfangsbewegung */
    if (last_x == 0 && last_y == 0) {
        last_x = x;
        last_y = y;
        return TRUE; // Anfangs-Bewegung wird ignoriert (im Moment Doppelmoppel da Anfangstimer , testen !! )
    }

    /* 1.2 Distanz zur letzten Position berechnen (Formel für Bewegung in 2D Raum, "Euklidische Distanz") */
    gdouble dx = x - last_x;
    gdouble dy = y - last_y;
    gdouble distance = sqrt(dx * dx + dy * dy);    // sqrt = Quadratwurzelberechnung [ d=(x2​−x1​)2+(y2​−y1​)2 ]

    /* 1.3 Distanz-Werte nur für die Log-Ausgabe akkumulieren   */
    logging_total_distance += distance;

    /* 1.4 Koordinaten oder Distanz als Print-Ausgabe anzeigen: */ // DEAKTIVIERT da nicht zufriedenstellend
    //if (logging_total_distance >= 1.0) {
    //    g_print("[%s] Mouse movement: x%.0f y%.0f\n", time_stamp(), dx, dy); // für runden round(...)
    //    logging_total_distance = 0;                // Reset nach Log-Eintrag
    //}

    /* 1.5 Wenn Bewegung kleiner als der Schwellenwert, für exit_fullscreen_by_motion ignorieren */
    if (distance < threshold) {
        return TRUE; // Abbrechen, da minimal Bewegung ignoriert werden soll, "Mouse-Shake Protection"
    }

    /* 1.6 aktuelle Koordinaten für den nächsten Aufruf der Funktion speichern */
    last_x = x;
    last_y = y;


    /* 2. Zeiger auf fullscreen_window für diese Funktion setzen */
    GtkWindow *fullscreen_window = GTK_WINDOW(user_data);

    /* 2.1 Überprüfen, ob das Fenster überhaupt gültig ist */
    if (fullscreen_window == NULL) { 
        g_print("[%s] Error: No valid window found to close.\n", time_stamp());

        // <- Hier noch einbauen, disconnect-Routine für Motioncontroller falls noch aktiv ?? !!
        return FALSE; 
    }

    /* 3. Timer für Fenster-im-Vordergrund-halten beenden */
    if (fullscreen_timer_id > 0) {
        g_source_remove(fullscreen_timer_id);
        fullscreen_timer_id = 0;
    }

    /* 4. Motion-Handler von diesem Controller trennen */
    g_signal_handlers_disconnect_by_func(controller, exit_fullscreen_by_motion, user_data);

    /* 4.1 Handler trennen und Fenster zerstören */
    g_object_ref(fullscreen_window);
    g_idle_add((GSourceFunc)gtk_window_destroy, fullscreen_window);
    g_object_unref(fullscreen_window);

    g_print("[%s] Mouse motion exits fullscreen\n", time_stamp());
    return TRUE;
}

/* ----- Taste beendet Fullscreen, (alternativ zu mouse-motion) ----- */
static gboolean exit_fullscreen_by_key(GtkEventControllerKey *controller,
                                                            guint keyval,  // Key-value      (die logische Taste)
                                                           guint keycode,  // Key-Scancode   (wird nicht benötigt)
                                                   GdkModifierType state,  // ob Zusatztaste (+ Alt, Shft ...)
                                                      gpointer user_data)  // = fullscreen_window
{
    GtkWindow *fullscreen_window = GTK_WINDOW(user_data);

    /* Auf eine entsprechende Taste beschränken:                           */
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

    /* Timer für Fenster-im-Vordergrund-halten beenden */
    if (fullscreen_timer_id > 0) {
        g_source_remove(fullscreen_timer_id);
        fullscreen_timer_id = 0;
    }

    /* Handler trennen und Fenster zerstören */
    g_signal_handlers_disconnect_by_func(controller, exit_fullscreen_by_key, user_data);
    g_object_ref(fullscreen_window);
    g_idle_add((GSourceFunc)gtk_window_destroy, fullscreen_window);
    g_object_unref(fullscreen_window);

    g_print("[%s] Key pressed, exiting fullscreen\n", time_stamp());
    return TRUE;
}


/* ----- Message / Alert-Dialog generisch, 
         show_alert_dialog (parent,*Titel, *Inhalttext) ------------- */
static void on_alert_dialog_response(AdwAlertDialog *dialog, const char *response, gpointer user_data)
{
    if (g_strcmp0 (response, "ok") == 0)
        g_print("Dialog btn - ok\n");
    else
        g_print("Dialog btn - cancel\n");

    // Hinweis, hier kein g_object_unref(dialog)
}

/* ----- Callback Alert-Dialog anzeigen ----------------------------- */
static void show_alert_dialog (GtkWindow   *parent, const char *title, const char *body)
{
    if (!parent || !GTK_IS_WINDOW(parent)) {
        g_warning("No valid parent window for alert dialog \n");
        return;
    }

    /* Dialog erzeugen – Titel und Body werden übergeben */
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(title, body));

    /* Buttons hinzufügen */
    adw_alert_dialog_add_response(dialog, "cancel", _("Abbrechen"));
    adw_alert_dialog_add_response(dialog, "ok",     _("OK"));
    adw_alert_dialog_set_default_response (dialog, "ok");

    /* Antwort‑Signal verbinden */
    g_signal_connect(dialog, "response", G_CALLBACK(on_alert_dialog_response), NULL);

    /* Dialog präsentieren */
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}

/* ----- Callback: About-Dialog öffnen ------------------------------ */
static void show_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
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

//    adw_about_dialog_set_translator_credits(about, "toq: deutsch\n toq: englisch");
      adw_about_dialog_set_application_icon(about, APP_ID);   //IconName

    /* Setze das Anwendungssymbol von GResource: +/


    /* Dialog innerhalb (modal) des Haupt-Fensters anzeigen */
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(
       gtk_application_get_active_window(GTK_APPLICATION(app)) )));
    adw_dialog_present(ADW_DIALOG(about), GTK_WIDGET(parent));

} // Ende About-Dialog



/* ----- Motion-Handler nach Verzögerung hier aktivieren (TARGET) ---- */
static gboolean enable_motion_handler_after_delay(gpointer user_data)
{
    /* Cursor ausblenden */
    // hier einbauen...

    GtkEventController *motion = GTK_EVENT_CONTROLLER(user_data);

    /* Motion-Handler aktivieren */
    gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_TARGET);
    return G_SOURCE_REMOVE;  // Timer soll nur einmal ausgefüht werden
}

/* ----- Funktion die im Intervall das Fullscreen-Fenster 
                      zurück in den Vordergrund ruft ---------------- */
static gboolean keep_window_on_top(gpointer user_data) 
{
    GtkWindow *fullscreen_window = GTK_WINDOW(user_data);
    
    // Sicherheitsüberprüfungen
    if (fullscreen_window == NULL || !GTK_IS_WINDOW(fullscreen_window)) {
        g_print("[%s] Invalid window in keep_window_on_top\n", time_stamp());
        return G_SOURCE_REMOVE; // Timer beendet
    }

    if (!GTK_IS_WINDOW(fullscreen_window)) {
        g_print("[%s] Not a valid GTK window in keep_window_on_top\n", time_stamp());
        return G_SOURCE_REMOVE; // Timer beendet
    }

    
    g_print("[%s] keep window top\n", time_stamp());
    // Fenster verstecken
    gtk_widget_hide(GTK_WIDGET(fullscreen_window));

    // Fenster erneut anzeigen
    gtk_widget_show(GTK_WIDGET(fullscreen_window));
    gtk_window_present(fullscreen_window);
    gtk_window_set_focus_visible(fullscreen_window, TRUE);

    return G_SOURCE_CONTINUE;  // Timer läuft weiter
}

/* ----- Callback Fullscreen-Button --------------------------------- */
static void on_fullscreen_button_clicked(GtkButton *button, gpointer user_data)
{
    /* 1. Blackscreen erzeugen, Methode: Fenster */
    GtkApplication *app = GTK_APPLICATION(user_data);

    /* 1.1 CSS-Provider für Hintergrundfarbe */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".fullscreen-window { background-color: black; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
          GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* 1.2 Vollbild-Fenster */
    GtkWidget *fullscreen_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(fullscreen_window), _("Vollbild"));
    gtk_widget_add_css_class(fullscreen_window, "fullscreen-window");
    gtk_window_fullscreen(GTK_WINDOW(fullscreen_window));
    gtk_window_set_default_widget(GTK_WINDOW(fullscreen_window), NULL); 
    gtk_window_set_decorated(GTK_WINDOW(fullscreen_window), FALSE);                 // randloses Overlay-Fenstern

    /* Fenster anzeigen */
    gtk_window_present(GTK_WINDOW(fullscreen_window));
    gtk_window_set_focus_visible(GTK_WINDOW(fullscreen_window), TRUE);

    /* 2. Unterbrecher für Fullscreen durch Key oder Mausbewegung */
    if (g_cfg.use_key) {       // in config.c
        GtkEventController *key_controller = gtk_event_controller_key_new();
        g_signal_connect(key_controller, "key-pressed",
                        G_CALLBACK(exit_fullscreen_by_key), fullscreen_window);    // verbunden mit exit_fullscreen_by_key
        gtk_widget_add_controller(GTK_WIDGET(fullscreen_window), key_controller);    
    } else {
            /* 2b.1 Motion-Controller erstellen */
            GtkEventController *motion_controller = gtk_event_controller_motion_new();
            g_signal_connect(motion_controller, "motion",
                        G_CALLBACK(exit_fullscreen_by_motion), fullscreen_window); // verbunden mit exit_fullscreen_by_motion
            gtk_widget_add_controller(fullscreen_window, motion_controller);

            /* 2b.2 Motion-Handler vorerst nicht aktivieren (NONE) */
            gtk_event_controller_set_propagation_phase(motion_controller, GTK_PHASE_NONE);

            /* 2b.3 Verzögerung in Sekunden, bis zur Aktivierung des Motion-Handlers */
            g_timeout_add_seconds(1, enable_motion_handler_after_delay, motion_controller); // Settings: use_key=false
    }
    /* 3 Checkbox-Zeiger aus g_object user_data holen */
    GtkWidget *set1_check = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "check"));
    if (!set1_check) return; // Fehlerbehandlung
    /* 3.1 Checkbox-Wert in is_active speichern */
    gboolean is_active = gtk_check_button_get_active(GTK_CHECK_BUTTON(set1_check));

    /* 3.2 aufrufen von keep_window_on_top() wenn Checkbox=true */
    if (is_active) {
    /* 3 Timer um Fenster wieder in den Vordergrund zu holen, siehe keep_window_on_top() */
    g_print ("[%s] window top is activated\n", time_stamp());
    fullscreen_timer_id = g_timeout_add_seconds(120, keep_window_on_top, fullscreen_window);
    }

}

/* ----- Callback Beenden-Button ------------------------------------ */
static void on_quitbutton_clicked(GtkButton *button, gpointer user_data)
{
    GError *error = NULL;
    g_print("[%s] Applicaton will now shut down\n", time_stamp());
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
}


/* ------------------------------------------------------------------ */
/*       Aktivierungshandler                                          */
/* ------------------------------------------------------------------ */
static void on_activate(AdwApplication *app, gpointer user_data) 
{
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
                            "checkbutton                             {"
                                                    "  color: #c0bfbc;"
                                                                    "}"
                            "label1                                  {"
                                                    "  color: #c0bfbc;"
                                                                    "}"
                                                                     );

    gtk_style_context_add_provider_for_display( gdk_display_get_default(),
    GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* ----- Adwaita-Fenster ---------------------------------------- */
    AdwApplicationWindow *adw_win = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(app))); 

    gtk_window_set_title(GTK_WINDOW(adw_win), APP_NAME);     // WM-Titel
    gtk_window_set_default_size(GTK_WINDOW(adw_win), 380, 400);  // Standard-Fenstergröße
    gtk_window_set_resizable(GTK_WINDOW(adw_win), FALSE);       // Skalierung nicht erlauben

    /* ----- ToolbarView (Root-Widget)  ----------------------------- */
    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_application_window_set_content(adw_win, GTK_WIDGET(toolbar_view));

    /* ----- HeaderBar mit TitelWidget ------------------------------ */
    AdwHeaderBar *header = ADW_HEADER_BAR(adw_header_bar_new());
    GtkWidget * title_label = gtk_label_new("Basti's OLED Saver");     // Label für Fenstertitel
    gtk_widget_add_css_class(title_label, "heading");                  // .heading class
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(title_label)); // Label einsetzen
    adw_toolbar_view_add_top_bar(toolbar_view, GTK_WIDGET(header));   // Header‑Bar zur Toolbar‑View hinzuf

    /* --- Hamburger-Button innerhalb der Headerbar ----------------- */
    GtkMenuButton *menu_btn = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_menu_button_set_icon_name(menu_btn, "open-menu-symbolic");
    adw_header_bar_pack_start(header, GTK_WIDGET(menu_btn));

    /* --- Popover-Menu im Hamburger -------------------------------- */
    GMenu *menu = g_menu_new();
    g_menu_append(menu, _(" Über OLED Saver "), "app.show-about");
    GtkPopoverMenu *popover = GTK_POPOVER_MENU(
        gtk_popover_menu_new_from_model(G_MENU_MODEL(menu)));
    gtk_menu_button_set_popover(menu_btn, GTK_WIDGET(popover));

    /* --- Aktion die den About-Dialog öffnet ----------------------- */
    const GActionEntry entries[] = {
        { "show-about", show_about, NULL, NULL, NULL }
    };
    g_action_map_add_action_entries(G_ACTION_MAP(app), entries, G_N_ELEMENTS(entries), app);


    /* ---- Haupt-Box ----------------------------------------------- */
    GtkBox *main_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 12));
//    gtk_box_set_spacing(GTK_BOX(main_box), -0);                // Minus-Spacer, zieht unteren Teil nach oben
    gtk_widget_set_margin_top   (GTK_WIDGET(main_box),    20);   // Rand unterhalb Toolbar
    gtk_widget_set_margin_bottom(GTK_WIDGET(main_box),    20);   // unterer Rand unteh. der Buttons
    gtk_widget_set_margin_start (GTK_WIDGET(main_box),    15);   // links
    gtk_widget_set_margin_end   (GTK_WIDGET(main_box),    15);   // rechts
    gtk_widget_set_hexpand      (GTK_WIDGET(main_box),  TRUE);
    gtk_widget_set_vexpand      (GTK_WIDGET(main_box), FALSE);

    /* ----- Label1 ------------------------------------------------- */
    GtkWidget *label1 = gtk_label_new(_("Blackscreen statt Burn-in! \n"));
    gtk_widget_set_halign(label1, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label1, GTK_ALIGN_CENTER);

    /* A. ---- Platzhalter für Label1-BOX-Widget ----- */

    /* B. ----- Label1 hier als Inhalt zur entspr.Box hinzufügen ---- */ 
    gtk_box_append(main_box, label1);

    /* ----- Icon --------------------------------------------------- */
    GtkWidget *icon = gtk_image_new_from_resource("/free/basti/oledsaver/icon2"); //alias in xml !
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);                 // Icon horizontal zentrieren
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 128);
    gtk_box_append(GTK_BOX(main_box), icon);

    /* ----- Label2 ------------------------------------------------- */
    GtkWidget *label2 = gtk_label_new(_(" \nStandby wird jetzt blockiert!\n"));
    gtk_widget_set_halign(label2, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label2, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(label2, "warning");

    /* ----- Label2-BOX --------------------------------------------- */
    GtkWidget *label2_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_hexpand(label2_box, FALSE); // Keine Expansion, Platz für Label2 lassen
    gtk_widget_set_vexpand(label2_box, FALSE);
      /* Box selbst zentrieren */
    gtk_widget_set_halign(label2_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(label2_box, GTK_ALIGN_CENTER);
      /* Label2 der Label2-BOX zufügen */
    gtk_box_append(GTK_BOX(label2_box), GTK_WIDGET(label2));
      /* Label2-BOX der Haupt-Box hinzufügen */
    gtk_box_append(GTK_BOX(main_box), GTK_WIDGET(label2_box));

    /* ----- Checkbox "set1_check" ---------------------------------- */
    GtkWidget *set1_check = gtk_check_button_new_with_label(_("Alle 2min auffrischen")); 
    gtk_check_button_set_active(GTK_CHECK_BUTTON(set1_check), FALSE); // standardmäßig auf inaktiv
    gtk_widget_set_visible(GTK_WIDGET(set1_check), TRUE); // Checkbox Sichtbarkeit

    /* ----- Checkbox-BOX-Widget ------------------------------------ */
    GtkWidget *chbx_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_hexpand(chbx_box, FALSE);
    gtk_widget_set_halign(chbx_box, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(chbx_box), GTK_WIDGET(set1_check));

    /* ----- Button-Box --------------------------------------------- */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(main_box, chbx_box);

    /* ----- Schaltfläche-Fullscreen -------------------------------- */
    GtkWidget *setfullscreen_button = gtk_button_new_with_label(_("Blackscreen"));
    gtk_widget_set_halign(setfullscreen_button, GTK_ALIGN_CENTER);
    //gtk_widget_add_css_class(setfullscreen_button, "suggested-action");
    gtk_widget_add_css_class(setfullscreen_button, "custom-suggested-action-button1"); // CSS-Provider...
    gtk_widget_add_css_class(setfullscreen_button, "opaque"); // durchsichtig machen...

    /* --- g_object, Checkbox an "app" speichern, um diese im Callback abrufen zu können --- */
    g_object_set_data(G_OBJECT(app), "check", set1_check);
    /* --- Schaltfläche-Fullscreen verbinden und "app" übergeben --- */
    g_signal_connect(setfullscreen_button, "clicked", G_CALLBACK(on_fullscreen_button_clicked), app);

    /* ----- Schaltfläche Beenden ----------------------------------- */
    GtkWidget *quit_button = gtk_button_new_with_label(_("  Beenden  "));
    gtk_widget_set_halign(quit_button, GTK_ALIGN_CENTER);
    //gtk_widget_add_css_class(quit_button, "raised");
    gtk_widget_add_css_class(quit_button, "custom-suggested-action-button2"); // CSS-Provider...
    gtk_widget_add_css_class(quit_button, "opaque"); // durchsichtig,
    //gtk_widget_add_css_class(quit_button, "destructive-action");
    // Schaltfläche Beenden Signal verbinden:
    g_signal_connect(quit_button, "clicked", G_CALLBACK(on_quitbutton_clicked), adw_win);
    /* ------ Close-request vom originalen "Window-close-button" abfangen ----- */
    g_signal_connect(adw_win, "close-request", G_CALLBACK(on_quitbutton_clicked), adw_win);

    /* ----- Beide Schaltflächen der BOX hinzufügen ---------------- */
    gtk_box_append(GTK_BOX(button_box), quit_button);
    gtk_box_append(GTK_BOX(button_box), setfullscreen_button);

    /* ----- button_box der Haupt-Box (box) hinzufügen ------------- */
    gtk_widget_set_valign(button_box, GTK_ALIGN_END);    // Ausrichtung nach unten
    gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER); // Ausrichtung mittig
    gtk_box_append(GTK_BOX(main_box), button_box);
    gtk_widget_set_vexpand(button_box, TRUE);            // Platz über Buttons ausdehnen
    
    /* -----  Haupt-Box zur ToolbarView hinzufügen ----------------- */
    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(main_box));

    /* --- Dark-Mode erzwingen --- */
    AdwStyleManager *style_manager = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style_manager, ADW_COLOR_SCHEME_FORCE_DARK);

    /* ----- Haupt-Fenster desktop‑konform anzeigen --------------- */
    gtk_window_present(GTK_WINDOW(adw_win));

    /* +++++ Funktion zum umgehen der Standbyzeit starten +++++++++ */
    start_standby_prevention();
}

/* --------------------------------------------------------------------------- */
/* Anwendungshauptteil, main()                                                 */
/* --------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    /* 0. Print nur für Terminalausgabe */
    g_print("[%s] Application is starting...\n", time_stamp());

    /* 1. Instanz erstellen, mit App-ID und Default-Flags */
    g_autoptr(AdwApplication) app =
                        adw_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);

    /* 2. Config-Datei initialisieren */
    init_environment(); // Config.c: Environment ermitteln
    init_config();      // Config.c: Config File laden/erstellen

    /* 2.1 Config-Werte zum testen auslesen: !! */
//    g_print("Settings:\n mouse_move_limit=%d,\n use_key=%d\n log_enable=%d\n",
//                         g_cfg.mouse_move_limit, g_cfg.use_key, g_cfg.log_enable);

    /* 3. externes Logging starten und APP_ID übermitteln */
    if (g_cfg.log_enable) log_file_init(APP_ID);

    /* 4. Localiziation-Setup */
    const char *flatpak_id  = config_get_flatpak_id();      // flatpak string einlesen, wenn vorhanden
    const gchar *locale_path = NULL;                        // Variable mit auto. Freigabe
    setlocale(LC_ALL, "");                                  // ruft die aktuelle Locale des Prozesses ab
//    setlocale(LC_ALL, "en_US.UTF-8");                     // explizit, zum testen!!
    textdomain             (APP_DOMAINNAME);                // textdomain festlegen
    bind_textdomain_codeset(APP_DOMAINNAME, "UTF-8"); 
    if (flatpak_id != NULL && flatpak_id[0] != '\0')
    {
        locale_path = "/app/share/locale";
    } else {
        locale_path = "/usr/share/locale";
    }
    bindtextdomain         (APP_DOMAINNAME, locale_path);
    //g_print("Localization files in: %s \n", locale_path); // testen!!

    /* 5. Resource‑Bundle registrieren */
    g_resources_register(resources_get_resource());

    /* 6. Verbindung zu UI */
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    //g_signal_connect(app, "shutdown", G_CALLBACK(stop_standby_prevention), NULL);

    /* 7. Anwendung starten und Ereignis warten */
    return g_application_run(G_APPLICATION(app), argc, argv);
}
