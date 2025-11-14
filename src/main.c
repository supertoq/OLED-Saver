/* OLED-Saver is part of my learning projects;
 * toq 2025  LICENSE: BSD 2-Clause "Simplified"
 *
 *
 * komplett:
 * gcc $(pkg-config --cflags gtk4 libadwaita-1 dbus-1) -o oledsaver main.c free.basti.oledsaver.gresource.c $(pkg-config --libs gtk4 libadwaita-1 dbus-1)

 *
 * 
 *
 *
 * Please note:
 * The Use of this code and execution of the applications is at your own risk, I accept no liability!
 * 
 * Version 0.9.3  free.basti.oledsaver (Fensterbasis von 'finden v0.6.1')
 */
#include <glib.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include "icon-gresource.h" // binäre Icons
#include <sys/wait.h>       // für waitpid()
#include <signal.h>         // für signal(),SIGTERM,SIGINT,kill()
#include <dbus/dbus.h>      // für DBusConnection,DBusMessage,dbus_bus_get(),dbus_message_new_method_call;
#include <locale.h>         // für setlocale(LC_ALL, "")
#include <glib/gi18n.h>     // für _();
#include <unistd.h>         // für fork(),sleep();


/* globale Referenz, wird beim UI-Aufbau gesetzt */
int inhibit_fd = -1;                          //Dateiskriptor
static GtkCheckButton *fullscr_check = NULL;
static gboolean dialog_finished = FALSE;
static pid_t inhibit_pid = 0;                 // PID des systemd-inhibit Prozesses


/* ----- Umgebung identifizieren -------------------------------------- */
typedef enum {
    DESKTOP_UNKNOWN,
    DESKTOP_GNOME,
    DESKTOP_KDE,
    DESKTOP_XFCE,
    DESKTOP_MATE
} DesktopEnvironment;
static DesktopEnvironment detect_desktop_environment(void) {
    const char *desktop = g_getenv("XDG_CURRENT_DESKTOP");
    g_print(_("Kommuniziere mit %s-System: \n"), desktop);
    if (!desktop) {
        desktop = g_getenv("DESKTOP_SESSION");
    }

    if (!desktop) {
        return DESKTOP_UNKNOWN;
    }
    g_autofree gchar *desktop_upper = g_ascii_strup(desktop, -1);

    if (strstr(desktop_upper, "GNOME")) {
        return DESKTOP_GNOME;
    } else if (strstr(desktop_upper, "KDE")) {
        return DESKTOP_KDE;
    } else if (strstr(desktop_upper, "XFCE")) {
        return DESKTOP_XFCE;
    } else if (strstr(desktop_upper, "MATE")) {
        return DESKTOP_MATE;
    }

    return DESKTOP_UNKNOWN;
}

static void prevent_standby_universal(void) {
    DesktopEnvironment desktop = detect_desktop_environment();
g_print("[Case] Wähle entsprechende Funktion ...  \n"); // testen
    switch (desktop) {
        case DESKTOP_GNOME:
            // GNOME-spezifische Methode
            g_print(_("[Case GNOME] gsettings ... \n"));
            system("gsettings set org.gnome.desktop.session idle-delay 0");
            system("gsettings set org.gnome.desktop.screensaver lock-enabled false");
            break;

        case DESKTOP_KDE:
            // KDE-spezifische Methode
            g_print(_("[Case KDE] qdbus ... \n"));
            system("qdbus org.kde.PowerManagement /org/kde/PowerManagement/PolicyAgent org.kde.PowerManagement.PolicyAgent.SuppressScreenSaver");
            break;

        case DESKTOP_XFCE:
            // XFCE-spezifische Methode
            g_print(_("[Case XFCE] xfconf-query ... \n"));
            system("xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/blank-on-ac -s 0");
            break;

        case DESKTOP_MATE:
            // MATE-spezifische Methode
            g_print(_("[Case KDE] gsettings ... \n"));
            system("gsettings set org.mate.power-manager sleep-display-ac 0");
            break;

        default:
            // Fallback: Systemd-Inhibit
            g_print(_("[Case Fallback] systemd ... \n"));
            system("systemd-inhibit --mode=block --what=idle:sleep:shutdown:handle-lid-switch sleep infinity");
            break;
    }
}

static void restore_standby_settings_universal(void) {
    DesktopEnvironment desktop = detect_desktop_environment();
g_print(_("Zustand wiederhergestellt. \n"));
    switch (desktop) {
        case DESKTOP_GNOME:
            system("gsettings set org.gnome.desktop.session idle-delay 300");
            system("gsettings set org.gnome.desktop.screensaver lock-enabled true");
            break;

        case DESKTOP_KDE:
            // KDE hat keine direkte Wiederherstellungsmethode
            break;

        case DESKTOP_XFCE:
            system("xfconf-query -c xfce4-power-manager -p /xfce4-power-manager/blank-on-ac -s 10");
            break;

        case DESKTOP_MATE:
            system("gsettings set org.mate.power-manager sleep-display-ac 10");
            break;

        default:
            break;
    }
}



/* ---------------- Teil 2 ------------------ */



/* -- [Methode1] -- STARTEN von "systemd-inhibit only" im Hintergrund -- */

/* Hindergrundprozess wird gestartet um Standby zu vermeiden */
static void start_inhibit_only_standby_prevention(void) {
    if (inhibit_pid == 0) {               // Prozess starten wenn nicht schon einer läuft! 
        pid_t pid = fork();               // fork erzeugt Kindprozess, wenn inhibit_pid nicht 1 ist!
        if (pid == 0) {
            g_print(_("[s-i] Kommuniziere mit Systemd \n"));
            execlp("/usr/bin/systemd-inhibit",     // Kindprozess startet systemd-inhibit sleep infinity
                   "systemd-inhibit",
                   "--mode=block",
                   "--what=idle:sleep:shutdown:handle-lid-switch:handle-suspend-key",
                   "--who=OLED-Saver",
                   "--why=Prevent Standby and Screen Lock",
                   "sleep", "infinity", 
                   NULL);
            _exit(1); // falls execlp fehlschlägt
        } else if (pid > 0) {
            inhibit_pid = pid;
            g_print(_("[s-i] Prozess (PID: %d) läuft bereits\n"), inhibit_pid);
        } else {
            g_warning(_("[s-i] Fehler beim Erstellen des Kindprozesses!\n"));
        }
    }
    system("gsettings set org.gnome.desktop.session idle-delay 0");
}

/* -- [Methode 1] -- BEENDEN des systemd-inhibit (only) Prozesses beim App-Ende -- */
static void stop_inhibit_only_standby_prevention(void) {
    if (inhibit_pid > 0) {
        g_print(_("[s-i] Kill Prozess (PID: %d )\n"), (int)inhibit_pid);
        if (kill(inhibit_pid, SIGTERM) == -1) {
            g_warning("Fehler beim Beenden von PID %d\n", (int)inhibit_pid);
        } else {
        int status;
        waitpid(inhibit_pid, &status, 0); // Kind reapen
        }
        inhibit_pid = 0;
    }
}


/* -- [Methode 2] -- STARTEN systemd-inhibit und dbus im Hintergrund:  ----- */
static void start_inhibit_dbus_standby_prevention(void) {
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusError err;
    DBusMessageIter args;
    int fd;

    /* Fehlerbehandlung initialisieren */
    dbus_error_init(&err);

    /* Verbindung zum Systembus herstellen */
    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) {
        g_warning(_("[d] DBus Verbindungsfehler: %s \n"), err.message);
        dbus_error_free(&err);
        return;
    }

    /* Methodenaufruf vorbereiten */
    msg = dbus_message_new_method_call(
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "Inhibit"
    );

    if (msg == NULL) {
        g_warning(_("[d] DBus Nachrichtenerstellung fehlgeschlagen \n"));
        return;
    }

    /* Argumente für Inhibit vorbereiten */
    dbus_message_iter_init_append(msg, &args);
    const char *what = "sleep:idle:shutdown:handle-lid-switch:handle-suspend-key";
    const char *who = "OLED-Saver";
    const char *why = "Prevent Standby and Screen Lock";
    const char *mode = "block";

    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &what);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &who);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &why);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &mode);

    /* Methode senden und Antwort empfangen */
    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);
    g_print(_("[d] Kommuniziere inhibit über DBus. \n"));
    if (dbus_error_is_set(&err)) {
        g_warning(_("[d] DBus Methodenaufruf-Fehler: %s \n"), err.message);
        dbus_message_unref(msg);
        dbus_error_free(&err);
        return;
    }

    /* Dateideskriptor aus der Antwort extrahieren */
    if (!dbus_message_iter_init(reply, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_UNIX_FD) {
        g_warning(_("[d] Ungültige DBus-Antwort \n"));
        dbus_message_unref(msg);
        dbus_message_unref(reply);
        return;
    }

    dbus_message_iter_get_basic(&args, &fd);
    
    /* Speichere den Dateideskriptor für später */
    inhibit_fd = fd;

    /* Aufräumen */
    dbus_message_unref(msg);
    dbus_message_unref(reply);

    g_print(_("[d] Methoden zur Standby-Verhinderung aktiviert\n"));
}

/* -- [Methode 2] -- BEENDEN von Inhibit aus "systemd-inhibit und dbus" --- */
static void stop_inhibit_dbus_standby_prevention(void) {
    if (inhibit_fd > 0) {
        close(inhibit_fd);
        inhibit_fd = -1;
        g_print(_("[d] Methoden zur Standby-Verhinderung beendet\n"));
    }
}


/* ------ Wrapperfunktionen zu allen Funktionen oberhalb ! ------------- */
static void cleanup_and_restore_universal(int signum);
static void start_standby_prevention(void);
static void stop_standby_prevention(void);

// Signal-Handler-Implementierung
static void cleanup_and_restore_universal(int signum) {
    stop_standby_prevention();
    exit(0);
}

// In Ihrer Hauptfunktion oder Initialisierung
static void setup_signal_handlers_universal(void) {
    signal(SIGINT, cleanup_and_restore_universal);
    signal(SIGTERM, cleanup_and_restore_universal);
}

static void start_standby_prevention(void) {
    // Bestehende Logik zur Standby-Verhinderung
    prevent_standby_universal();
    start_inhibit_dbus_standby_prevention();
//    start_inhibit_only_standby_prevention();
}

static void stop_standby_prevention(void) {
    // Bestehende Logik zur Wiederherstellung
    restore_standby_settings_universal();
    stop_inhibit_dbus_standby_prevention();
//    stop_inhibit_only_standby_prevention();
}

/* ---------------------------------------------------------------------------- */


/* ----- Mausbewegung beendet Fullscreen Fenster, 
  reaktiviert von enable_mouse_exit_after_delay() ----- */
static gboolean
on_mouse_move_exit_fullscreen(GtkEventControllerMotion *controller,
                              gdouble x, gdouble y,
                              gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);

    g_idle_add((GSourceFunc)gtk_window_destroy, window);
        g_print (_("Motionkontroller hat Fullscreen unterbrochen \n"));
    g_signal_handlers_disconnect_by_func(controller,
                                         on_mouse_move_exit_fullscreen,
                                         user_data);
    return GDK_EVENT_STOP;
}


/* ----- Message / Alert-Dialog Generisch,  show_alert_dialog (parent,*Titel, *Inhalttext) ----- */
static void on_alert_dialog_response (AdwAlertDialog *dialog,
                          const char     *response,
                          gpointer        user_data)
{
    if (g_strcmp0 (response, "ok") == 0)
        g_print ("Dialog btn - ok\n");
    else
        g_print ("Dialog btn - cancel\n");

    /* Hinweis, hier kein g_object_unref(dialog) ! */
}

/* ----- Callback Alert-Dialog anzeigen (generisch) ----- */
static void
show_alert_dialog (GtkWindow   *parent,
                   const char  *title,
                   const char  *body)
{
    if (!parent || !GTK_IS_WINDOW (parent)) {
        g_warning (_("Kein gültiges Elternfenster für Alert-Dialog \n"));
        return;
    }

    /* Dialog erzeugen – Titel und Body werden übergeben */
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (title, body));

    /* Buttons hinzufügen */
    adw_alert_dialog_add_response (dialog, "cancel", _("Abbrechen"));
    adw_alert_dialog_add_response (dialog, "ok",     _("OK"));
    adw_alert_dialog_set_default_response (dialog, "ok");

    /* Antwort‑Signal verbinden */
    g_signal_connect (dialog, "response",
                      G_CALLBACK (on_alert_dialog_response), NULL);

    /* Dialog präsentieren */
    adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (parent));
}

/* ----- Callback: About-Dialog öffnen ------ */
static void show_about (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    AdwApplication *app = ADW_APPLICATION (user_data);
    /* About‑Dialog anlegen */
    AdwAboutDialog *about = ADW_ABOUT_DIALOG (adw_about_dialog_new ());
    //adw_about_dialog_set_body(about, "Hierbei handelt es sich um ein klitzekleines Testprojekt."); //nicht in meiner adw Version?
    adw_about_dialog_set_application_name (about, "OLED-Saver");
    adw_about_dialog_set_version (about, "0.9.3");
    adw_about_dialog_set_developer_name (about, "Build for Basti™");
    adw_about_dialog_set_website (about, "https://github.com/super-toq");

    /* Lizenz – BSD2 wird als „custom“ angegeben */
    adw_about_dialog_set_license_type (about, GTK_LICENSE_CUSTOM);
    adw_about_dialog_set_license (about,
        "BSD 2-Clause License\n\n"
        "Copyright (c) 2025, toq\n"
        "Redistribution and use in source and binary forms, with or without "
        "modification, are permitted provided that the following conditions are met: "
        "1. Redistributions of source code must retain the above copyright notice, this "
        "list of conditions and the following disclaimer.\n"
        "2. Redistributions in binary form must reproduce the above copyright notice, "
        "this list of conditions and the following disclaimer in the documentation"
        "and/or other materials provided with the distribution.\n\n"
        "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ''AS IS'' "
        "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE "
        "IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE "
        "DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE "
        "FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL "
        "DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR "
        "SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER "
        "CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, "
        "OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE "
        "OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n\n"
        "Application Icon by SVG. \n"
        "https://www.svgrepo.com \n"
        "Respect and thanks to SVG for free use. \n"
        "LICENSE for the icon: \n"
        "CC Attribution License \n"
        "Follow the link to view details of the CC Attribution License: \n"
        "https://www.svgrepo.com/page/licensing/#CC%20Attribution \n");

//    adw_about_dialog_set_translator_credits (about, "toq: deutsch\n toq: englisch");
//    adw_about_dialog_set_application_icon (about, "/free/basti/oledsaver/icon1");   //IconName
// Setze das Anwendungssymbol von GResource

    /* Dialog modal zum aktiven Fenster zeigen */
    GtkWindow *parent = gtk_application_get_active_window (GTK_APPLICATION (app));
    adw_dialog_present (ADW_DIALOG (about), GTK_WIDGET (parent));
}

/* ----- Callback Beenden-Button ----- */
static void on_quitbutton_clicked (GtkButton *button, gpointer user_data)
{
    g_application_quit (G_APPLICATION (user_data));
}

/* Motion-Handler, Wartezeit, für Fullscreen-Button */
static gboolean
enable_mouse_exit_after_delay(gpointer user_data)
{
    GtkEventController *motion = GTK_EVENT_CONTROLLER(user_data);
    
    /* Motion-Handler aktivieren */
    gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_TARGET);
    
    return G_SOURCE_REMOVE;  // Timer nur einmal ausführen
}

/* ----- Callback zu Fullscreen-Button ----- */
static void
on_fullscreen_button_clicked(GtkButton *button, gpointer user_data)
{
   /* 1. Blackscreen erzeugen, hier als Fenster */
    GtkApplication *app = GTK_APPLICATION(user_data);

    /* CSS-Provider mit schwarzem Hintergrund */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        ".fullscreen-window { background-color: black; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    /* Vollbild-Fenster */
    GtkWidget *fullscreen_window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(fullscreen_window), _("Vollbild"));
    gtk_widget_add_css_class(fullscreen_window, "fullscreen-window");
    gtk_window_fullscreen(GTK_WINDOW(fullscreen_window));
    gtk_window_present(GTK_WINDOW(fullscreen_window));

    /* Motion-Controller erstellen */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion",
                     G_CALLBACK(on_mouse_move_exit_fullscreen), fullscreen_window);
    gtk_widget_add_controller(fullscreen_window, motion);

    /* Motion-Handler erst deaktivieren */
    gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_NONE);

    /* Sekunden-Verzögerung zur Aktivierung des Motion-Handlers */
    g_timeout_add_seconds(1, enable_mouse_exit_after_delay, motion);

    /* 2. Standbyzeit */
    start_standby_prevention();

}

/* --------------------------------------------------------------------------- */
/*       Aktivierungshandler                                                   */
/* ----- CALLBACK-Funktion wird aufgerufen wenn Anwendung aktiviert wird ----- */
static void on_activate (AdwApplication *app, gpointer)
{
    /* ----- Adwaita-Fenster ------------------------ */
    AdwApplicationWindow *win = ADW_APPLICATION_WINDOW (adw_application_window_new (GTK_APPLICATION (app))); 

    gtk_window_set_title (GTK_WINDOW(win), "OLED-Saver");   // Fenstertitel
    gtk_window_set_default_size (GTK_WINDOW(win), 380, 400);  // Standard-Fenstergröße
    gtk_window_present (GTK_WINDOW(win));                     // Fenster anzeigen lassen

    /* ----- ToolbarView (Root‑Widget) erstellt und als Inhalt des Fensters festgelegt -- */
    AdwToolbarView *toolbar_view = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
    adw_application_window_set_content (win, GTK_WIDGET (toolbar_view));

    /* ----- HeaderBar mit TitelWidget erstellt und dem ToolbarView hinzugefügt ------------ */
    AdwHeaderBar *header = ADW_HEADER_BAR (adw_header_bar_new());
    /* Label mit Pango‑Markup erzeugen */
    GtkLabel *title_label = GTK_LABEL(gtk_label_new (NULL));
    gtk_label_set_markup (title_label, "<b>Basti's OLED-Saver</b>");      // Fenstertitel in Markup
    gtk_label_set_use_markup (title_label, TRUE);                        //Markup‑Parsing aktivieren
    adw_header_bar_set_title_widget (header, GTK_WIDGET (title_label)); //Label als Title‑Widget einsetzen
    adw_toolbar_view_add_top_bar (toolbar_view, GTK_WIDGET (header));  //Header‑Bar zur Toolbar‑View hinzuf

    /* --- Hamburger‑Button innerhalb der Headerbar --- */
    GtkMenuButton *menu_btn = GTK_MENU_BUTTON (gtk_menu_button_new ());
    gtk_menu_button_set_icon_name (menu_btn, "open-menu-symbolic");
    adw_header_bar_pack_start (header, GTK_WIDGET (menu_btn));

    /* --- Popover‑Menu im Hamburger --- */
    GMenu *menu = g_menu_new ();
    g_menu_append (menu, _("Über OLED-Saver"), "app.show-about");
    GtkPopoverMenu *popover = GTK_POPOVER_MENU (
        gtk_popover_menu_new_from_model (G_MENU_MODEL (menu)));
    gtk_menu_button_set_popover (menu_btn, GTK_WIDGET (popover));

    /* --- Action die den About‑Dialog öffnet --- */
    const GActionEntry entries[] = {
        { "show-about", show_about, NULL, NULL, NULL }
    };
    g_action_map_add_action_entries (G_ACTION_MAP (app), entries, G_N_ELEMENTS (entries), app);


    /* ---- Haupt-Box erstellen ----------------------------------------------------------- */
    GtkBox *main_box = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 12));
    gtk_widget_set_margin_top    (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_bottom (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_start  (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_end    (GTK_WIDGET (main_box), 20);
    gtk_widget_set_hexpand (GTK_WIDGET (main_box), TRUE);
    gtk_widget_set_vexpand (GTK_WIDGET (main_box), TRUE);

    /* ----- Text-Label 1 erstellen  ----- */
    GtkWidget *label1 = gtk_label_new(_("Blackscreen statt Burn-in! \n"));
    gtk_widget_set_halign (label1, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (label1, GTK_ALIGN_CENTER);

    /* ----- Label1 als Inhalt zur hinzufügen ----- */ 
    gtk_box_append (main_box, label1);

    /*  Internes Icon anzeigen lassen */
//    GtkWidget *icon = gtk_image_new_from_icon_name("weather-clear-night-large");
    GtkWidget *icon = gtk_image_new_from_resource("/free/basti/oledsaver/icon2"); //alias in xml !
    // Icon horizontal zentrieren
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 128);
    gtk_box_append(GTK_BOX(main_box), icon);

    /* ----- Text-Label 2 erstellen ----- */
    GtkWidget *label2 = gtk_label_new(_("                            \n"
                                        "Standby wird bei Aktivierung verhindert.\n"));
    gtk_widget_set_halign (label2, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (label2, GTK_ALIGN_CENTER);
    gtk_box_append (main_box, label2);

    /* ----- Checkbox oberhalb der Schaltfläche (horizontal) ----- */
    static GtkWidget *chbx_box   = NULL;   /* Gtk-Box */

     /* Kontrollkästchen/Checkbox mit Namen "..." */
    if (!fullscr_check) {
        fullscr_check = GTK_CHECK_BUTTON (gtk_check_button_new_with_label (_("Checkbox")));
        gtk_check_button_set_active (fullscr_check, FALSE);
    }

    /* Horizontales Box‑Widget nur einmal erzeugen */
    if (!chbx_box) {
        chbx_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_hexpand (chbx_box, TRUE);
        /* Beide Widgets nur hier einfügen – danach besitzen sie einen Eltern‑Container */
        gtk_box_append (GTK_BOX (chbx_box), GTK_WIDGET (fullscr_check));
           /* Checkbox ist unsichtbar !! */
        gtk_widget_set_visible(GTK_WIDGET(fullscr_check), FALSE);
    }
    
    /* button_box erstellen, wo Schaltfläche hineinkommen */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    /* Das bereits vorbereitete horizontale Box-Widget in die vertikale Haupt-Box einfügen */
    gtk_box_append (main_box, chbx_box);

    /* ----- Schaltfläche-Fullscreen erzeugen ----- */
    GtkWidget *setfullscreen_button = gtk_button_new_with_label (_("Blackscreen"));
    gtk_widget_set_halign (setfullscreen_button, GTK_ALIGN_CENTER);
    g_signal_connect (setfullscreen_button, "clicked",
                  G_CALLBACK (on_fullscreen_button_clicked), app);

    /* ----- Schaltfläche Beenden erzeugen ----- */
    GtkWidget *quit_button = gtk_button_new_with_label(_(" Beenden "));
    gtk_widget_set_halign(quit_button, GTK_ALIGN_CENTER);

    /* ----- Beendenbutton signal verbinden ----- */
    g_signal_connect(quit_button, "clicked", G_CALLBACK(on_quitbutton_clicked), app);

    /* ----- Schaltflächen der button_box hinzufügen ----- */
    gtk_box_append(GTK_BOX(button_box), quit_button);
    gtk_box_append(GTK_BOX(button_box), setfullscreen_button);


    /* ----- button_box der Haupt-Box (box) hinzufügen ----- */
    gtk_widget_set_valign(button_box, GTK_ALIGN_END);    // Ausrichtung nach unten
    gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER); // Ausrichtung mittig
    gtk_box_append(GTK_BOX(main_box), button_box);
    gtk_widget_set_vexpand(button_box, TRUE);            // Platz über Buttons ausdehnen
    
    /* -----  Haupt-Box zur ToolbarView hinzufügen ------------ */
    adw_toolbar_view_set_content(toolbar_view, GTK_WIDGET(main_box));

    /* ----- System-Icon ----- */
    

    /* --- Dark-Mode erzwingen --- */
    AdwStyleManager *style_manager = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style_manager, ADW_COLOR_SCHEME_FORCE_DARK);

    /* ----- Fenster desktop‑konform anzeigen ----- */
    gtk_window_present(GTK_WINDOW(win));
    

}

/* ---------------------------------------------------------------------------
 * Anwendungshauptteil, main()
 * --------------------------------------------------------------------------- */
int main (int argc, char **argv)
{
    char *app_dir = g_get_current_dir();  // Ermit. den aktuellen Arbeitsverzeichnis-Pfad
    const char *locale_path = NULL;
    const char *flatpak_id = getenv("FLATPAK_ID"); //flatpak string free.toq.finden anderenfalls NULL !

    /* Resource‑Bundle (....g_resource) registrieren um den Inhalt verfügbar zu machen */
    g_resources_register (resources_get_resource ()); // reicht für Icon innerhalb der App



    /* ----- Erstelle den Pfad zu den locale-Dateien ----------------------------------- */
    setlocale(LC_ALL, "");
    textdomain("oledsaver");
    bind_textdomain_codeset("oledsaver", "UTF-8");    // Basisverzeichnis für Übersetzungen
    if (flatpak_id != NULL && flatpak_id[0] != '\0')  // Wenn ungleich NULL:
    {
        locale_path = "/app/share/locale"; // Flatpakumgebung /app/share/locale
    } else {
        locale_path = "/usr/share/locale"; // Native Hostumgebung /usr/share/locale
    }
    bindtextdomain("oledsaver", locale_path);
//    g_print (_("Lokalisierung in: %s \n"), locale_path); // testen


    g_autoptr (AdwApplication) app =      // Instanz erstellen + App-ID + Default-Flags;
        adw_application_new ("free.basti.oledsaver", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL); // Signal mit on_activate verbinden
    g_signal_connect(app, "shutdown", G_CALLBACK(stop_standby_prevention), NULL);
    /* --- g_application_run startet Anwendung u. wartet auf Ereignis --- */
    return g_application_run (G_APPLICATION (app), argc, argv);
}
