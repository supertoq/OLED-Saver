/* OLED-Saver is part of my learning project;
 * toq 2025  LICENSE: BSD 2-Clause "Simplified"
 *
 *
 *
 * gcc $(pkg-config --cflags gtk4 libadwaita-1 dbus-1) -o oledsaver main.c free.basti.oledsaver.gresource.c $(pkg-config --libs gtk4 libadwaita-1 dbus-1)
 *
 *
 *
 *
 * Please note:
 * The Use of this code and execution of the applications is at your own risk, I accept no liability!
 * 
 * Version 1.0.6.1  free.basti.oledsaver
 */
#include <glib.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include "icon-gresource.h" // binäre Icons
#include <string.h>         // für strstr()
#include <dbus/dbus.h>      // für DBusConnection,DBusMessage,dbus_bus_get(),dbus_message_new_method_call;
#include <locale.h>         // für setlocale(LC_ALL, "")
#include <glib/gi18n.h>     // für _();


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
static void start_gnome_inhibit(void) {
    DBusError err;
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusMessageIter args;

    dbus_error_init(&err);
    /* Session-Bus */
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn || dbus_error_is_set(&err)) {
        g_warning("[GNOME] DBus error: %s\n", err.message); dbus_error_free(&err);
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
        g_warning("[GNOME] Error creating the DBus message (1)\n");
        return; }

    const char *app = "OLED-Saver";
    const char *reason = "Prevent Standby";
    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &reason);

    /* Nachricht senden */
    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);

    /* Antwort auslesen (COOKIE als uint32) */
    DBusMessageIter iter;
    if (!reply) {
        g_warning("[GNOME] Inhibit failed: no reply received\n");
        dbus_message_unref(msg);
        return;
    }

    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
        g_warning("[GNOME] Inhibit reply invalid (no cookie)\n");
        dbus_message_unref(msg);
        dbus_message_unref(reply);
        return;
    }

    dbus_message_iter_get_basic(&iter, &gnome_cookie);
    g_print("[GNOME] Inhibit active (cookie=%u)\n", gnome_cookie);

    dbus_message_unref(msg);
    dbus_message_unref(reply);
}

/* ----- Stopt Gnome Inhibit ---------------------------------------- */
static void stop_gnome_inhibit(void) {
    if (!gnome_cookie) return;

    DBusError err;
    DBusConnection *conn;
    DBusMessage *msg;
    DBusMessageIter args;

    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!conn || dbus_error_is_set(&err)) {
       g_warning("[GNOME] DBus error (session): %s\n", err.message);
       dbus_error_free(&err); return; }

    msg = dbus_message_new_method_call(
        "org.freedesktop.ScreenSaver",
        "/ScreenSaver",
        "org.freedesktop.ScreenSaver",
        "UnInhibit"
    );

    if (!msg) {
        g_warning("[GNOME] Error creating the DBus message (2)\n");
        return;
    }

    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &gnome_cookie);

    dbus_connection_send(conn, msg, NULL);
    dbus_message_unref(msg);
    g_print("[GNOME] Inhibit closed (cookie=%u)\n", gnome_cookie);
    gnome_cookie = 0;
}

/* ----- systemd/KDE login1.Manager Inhibit ------------------------- */
static void start_system_inhibit(void) {
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
       g_warning("[SYSTEM] Error creating the DBus message\n");
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
       g_warning("[SYSTEM] Inhibit failed: %s\n", err.message);
       dbus_error_free(&err); dbus_message_unref(msg);
       return; 
    }

    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UNIX_FD) {
        g_warning("[SYSTEM] Inhibit reply invalid\n");
        dbus_message_unref(msg);
        dbus_message_unref(reply);
        return;
    }

    dbus_message_iter_get_basic(&iter, &system_fd);
    g_print("[SYSTEM] Inhibit active (fd=%d)\n", system_fd);
    /* Aufräumen */
    dbus_message_unref(msg);
    dbus_message_unref(reply);
}

/* ----- Stop System Inhibit ---------------------------------------- */
static void stop_system_inhibit(void) {
    if (system_fd < 0) return;
        close(system_fd);
        system_fd = -1;
        g_print("[System] Preventing standby has been stopped\n");
}

/* --- START --- ausgelöst in on_activate --------------------------- */
static void start_standby_prevention(void) {
    DesktopEnvironment de = detect_desktop();
    if (de == DESKTOP_GNOME) start_gnome_inhibit();
        start_system_inhibit(); // KDE, XFCE, MATE
}

/* --- STOP --- ausgelöst beim shutdown ----------------------------- */
static void stop_standby_prevention(void) {
    stop_gnome_inhibit();
    stop_system_inhibit();
}


/* ------------------------------------------------------------------ */


/* ----- Mausbewegung beendet Fullscreen Fenster, 
         aktiviert von enable_mouse_exit_after_delay() -------------- */
static gboolean exit_fullscreen(GtkEventControllerMotion *controller, gdouble x, gdouble y, gpointer user_data)
{

    /* Timer für Fenster-im-Vordergrund-halten beenden */
    GtkWindow *fullscreen_window = GTK_WINDOW(user_data);
    if (fullscreen_timer_id > 0) {
        g_source_remove(fullscreen_timer_id);
        fullscreen_timer_id = 0;
    }

    /* Motion-Handler von diesem Controller trennen. [r!] */
    g_signal_handlers_disconnect_by_func(controller, exit_fullscreen, user_data);

    /* Zerstörung, aus dem aktuellen Event-Stack heraus */
    g_object_ref(fullscreen_window);
    g_idle_add((GSourceFunc)gtk_window_destroy, fullscreen_window);
    g_object_unref(fullscreen_window);

    g_print("Mouse motion exits fullscreen\n");
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
    g_signal_connect(dialog, "response",
                      G_CALLBACK(on_alert_dialog_response), NULL);

    /* Dialog präsentieren */
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}


/* ----- Callback: About-Dialog öffnen ------------------------------ */
static void show_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    AdwApplication *app = ADW_APPLICATION(user_data);
    /* About‑Dialog anlegen */
    AdwAboutDialog *about = ADW_ABOUT_DIALOG(adw_about_dialog_new ());
    adw_about_dialog_set_application_name(about, "OLED Saver");
    adw_about_dialog_set_version(about, "1.0.6_1");
    adw_about_dialog_set_developer_name(about, "Built for Basti™");
    adw_about_dialog_set_website(about, "https://github.com/super-toq/OLED-Saver");
    //adw_about_dialog_set_comments(about, " ... ");

    /* Lizenz – BSD2 wird als „custom“ angegeben */
    adw_about_dialog_set_license_type(about, GTK_LICENSE_BSD);
    adw_about_dialog_set_license(about,
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
        "Application Icon by SVG Repo. \n"
        "https://www.svgrepo.com \n"
        "Thanks to SVG Repo for sharing their free icons, "
        "we appreciate your generosity and respect your work.\n"
        "The icons are licensed under the \n"
        "Creative Commons Attribution License.\n"
        "Follow the link to view details of the CC Attribution License: \n"
        "https://www.svgrepo.com/page/licensing/#CC%20Attribution \n");

//    adw_about_dialog_set_translator_credits(about, "toq: deutsch\n toq: englisch");
      adw_about_dialog_set_application_icon(about, "free.basti.oledsaver");   //IconName

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
                      wieder in den Vordergrund ruft ---------------- */
static gboolean keep_window_on_top(gpointer user_data) 
{
    GtkWindow *fullscreen_window = GTK_WINDOW(user_data);
    
    // Sicherheitsüberprüfungen
    if (fullscreen_window == NULL || !GTK_IS_WINDOW(fullscreen_window)) {
        g_print("Invalid window in keep_window_on_top\n");
        return G_SOURCE_REMOVE; // Timer beendet
    }

    if (!GTK_IS_WINDOW(fullscreen_window)) {
        g_print("Not a valid GTK window in keep_window_on_top\n");
        return G_SOURCE_REMOVE; // Timer beendet
    }

    
    g_print("keep window top\n");
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
    gtk_window_set_decorated(GTK_WINDOW(fullscreen_window), FALSE);  // randloses Overlay-Fenstern

    /* Fenster anzeigen */
    gtk_window_present(GTK_WINDOW(fullscreen_window));
    gtk_window_set_focus_visible(GTK_WINDOW(fullscreen_window), TRUE);

    /* 2. Motion-Controller erstellen */
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion",
                     G_CALLBACK(exit_fullscreen), fullscreen_window);
    gtk_widget_add_controller(fullscreen_window, motion);

    /* 2.1 Motion-Handler vorerst nicht aktivieren (NONE) */
    gtk_event_controller_set_propagation_phase(motion, GTK_PHASE_NONE);

    /* 2.2 Verzögerung in Sekunden, bis zur Aktivierung des Motion-Handlers */
    g_timeout_add_seconds(1, enable_motion_handler_after_delay, motion);

    /* 3 Checkbox-Zeiger aus g_object user_data holen */
    GtkWidget *set1_check = GTK_WIDGET(g_object_get_data(G_OBJECT(app), "check"));
    if (!set1_check) return; // Fehlerbehandlung
    /* 3.1 Checkbox-Wert in is_active speichern */
    gboolean is_active = gtk_check_button_get_active(GTK_CHECK_BUTTON(set1_check));

    /* 3.2 aufrufen von keep_window_on_top() wenn Checkbox True */
    if (is_active) {
    /* 3 Timer um Fenster wieder in den Vordergrund zu holen, siehe keep_window_on_top() */
    g_print ("window top is activated\n");
    fullscreen_timer_id = g_timeout_add_seconds(120, keep_window_on_top, fullscreen_window);
    }

}

/* ----- Callback Beenden-Button ------------------------------------ */
static void on_quitbutton_clicked(GtkButton *button, gpointer user_data)
{
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

    gtk_window_set_title(GTK_WINDOW(adw_win), "OLED Saver");     // WM-Titel
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
//    gtk_box_set_spacing(GTK_BOX(main_box), -0);               // Minus-Spacer, zieht unteren Teil nach oben
    gtk_widget_set_margin_top    (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_bottom (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_start  (GTK_WIDGET (main_box), 20);
    gtk_widget_set_margin_end    (GTK_WIDGET (main_box), 20);
    gtk_widget_set_hexpand     (GTK_WIDGET (main_box), TRUE);
    gtk_widget_set_vexpand     (GTK_WIDGET (main_box), TRUE);

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
    char *app_dir = g_get_current_dir();  // Ermit. den aktuellen Arbeitsverzeichnis-Pfad
    const char *locale_path = NULL;
    const char *flatpak_id = getenv("FLATPAK_ID"); //flatpak string free.toq.finden anderenfalls NULL !

    /* Resource‑Bundle registrieren um den Inhalt verfügbar zu machen */
    g_resources_register(resources_get_resource());


    //* ----- Localiziation-Pfad ---------------------------------- */
    setlocale(LC_ALL, "");               // ruft die aktuelle Locale des Prozesses ab
//    setlocale(LC_ALL, "en_US.UTF-8"); // testen!!
    textdomain("bastis-oledsaver");      // textdomain festlegen
    bind_textdomain_codeset("bastis-oledsaver", "UTF-8"); 
    if (flatpak_id != NULL && flatpak_id[0] != '\0')
    {
        locale_path = "/app/share/locale";
    } else {
        locale_path = "/usr/share/locale";
    }
    bindtextdomain("bastis-oledsaver", locale_path);
    //g_print("Localization files in: %s \n", locale_path); // testen!!


    g_autoptr(AdwApplication) app =      // Instanz erstellen + App-ID + Default-Flags;
        adw_application_new("free.basti.oledsaver", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL); // Signal mit on_activate verbinden
    g_signal_connect(app, "shutdown", G_CALLBACK(stop_standby_prevention), NULL);
    /* --- g_application_run startet Anwendung u. wartet auf Ereignis --- */
    return g_application_run(G_APPLICATION(app), argc, argv);
}
