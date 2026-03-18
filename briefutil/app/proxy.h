#pragma once

#include "briefutil/sender_profile.h"
#include "briefutil/typography_config.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

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

    Q_INVOKABLE bool validate_font_value(const QString& v, const QString& role = QString()) const;
    Q_INVOKABLE bool font_value_is_file_backed(const QString& v, const QString& role = QString()) const;
    Q_INVOKABLE bool validate_directory(const QString& v) const;
    Q_INVOKABLE QVariantMap get_sender_profile(int index) const;
    Q_INVOKABLE bool save_sender_profile(int index, const QVariantMap& profile);
    Q_INVOKABLE int  create_new_profile();
    Q_INVOKABLE bool delete_sender_profile(int index);
    Q_INVOKABLE bool profile_name_exists(const QString& name, int exclude_index) const;
    Q_INVOKABLE bool validate_profile_image_name(const QString& v) const;
    Q_INVOKABLE bool validate_hex_color(const QString& v) const;
    Q_INVOKABLE QString import_template_image(const QUrl& source_url) const;

private:
    struct Sender_profile_entry
    {
        Sender_profile profile;
        QString path;
    };

    void load_settings();
    void save_settings() const;
    void discover_profiles();
    void update_font_and_save(QString& slot, const QString& v);

    QString m_sender_template_dir;
    QString m_output_dir;
    std::vector<Sender_profile_entry> m_profiles;
    QString m_font_sans_input;
    QString m_font_sans_bold_input;
    QString m_font_sans_italic_input;
    QString m_font_sans_bold_italic_input;
    QString m_font_mono_input;
    Theme_config m_theme;
};
