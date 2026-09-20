#include "window.hpp"

HoneyBeeWindow::HoneyBeeWindow() {
    set_title("HoneyBee Browser");
    set_default_size(1024, 768);

    m_label.set_text("HoneyBee Browser\n\nv0.1.0 — Phase 0\n\nThe browser will render here.");
    m_label.set_justify(Gtk::Justification::CENTER);
    m_label.set_margin(24);

    set_child(m_label);
}
