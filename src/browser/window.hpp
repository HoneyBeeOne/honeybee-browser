#pragma once

#include <gtkmm.h>

class HoneyBeeWindow : public Gtk::Window {
public:
    HoneyBeeWindow();

private:
    Gtk::Label m_label;
};
