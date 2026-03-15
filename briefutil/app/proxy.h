#pragma once

#include "briefutil/sender_profile.h"
#include "briefutil/typography_config.h"

#include <QList>
#include <QObject>
#include <QString>

class QWindow;

class Proxy: public QObject
{
    Q_OBJECT

signals:
    void pdf_generated(bool success, const QString& message);
    void sender_templates_changed();

public:
    explicit Proxy(QObject* parent = nullptr);
    Q_INVOKABLE QList<QString> get_sender_templates() const;
    Q_INVOKABLE void make_pdf(int from, const QString& to,
                              const QString& subject, const QString& body);
    Q_INVOKABLE void set_window_dark_mode(QWindow* window, bool dark);
    Q_INVOKABLE void save_dark_mode(bool dark);
    Q_INVOKABLE bool load_dark_mode() const;

    // Settings
    Q_INVOKABLE QString get_font_sans() const;
    Q_INVOKABLE QString get_font_sans_bold() const;
    Q_INVOKABLE QString get_font_sans_italic() const;
    Q_INVOKABLE QString get_font_sans_bold_italic() const;
    Q_INVOKABLE QString get_font_mono() const;
    Q_INVOKABLE double  get_body_size() const;
    Q_INVOKABLE double  get_body_leading() const;
    Q_INVOKABLE QString get_template_dir() const;

    Q_INVOKABLE void set_font_sans(const QString& v);
    Q_INVOKABLE void set_font_sans_bold(const QString& v);
    Q_INVOKABLE void set_font_sans_italic(const QString& v);
    Q_INVOKABLE void set_font_sans_bold_italic(const QString& v);
    Q_INVOKABLE void set_font_mono(const QString& v);
    Q_INVOKABLE void set_body_size(double v);
    Q_INVOKABLE void set_body_leading(double v);
    Q_INVOKABLE void set_template_dir(const QString& v);

private:
    void load_settings();
    void save_settings() const;
    void discover_profiles();

    QString m_sender_template_dir;
    QString m_output_dir;
    std::vector<Sender_profile> m_profiles;
    Theme_config m_theme;
};
