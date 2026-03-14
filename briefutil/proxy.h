#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include "sender_profile.h"

class QWindow;

class Proxy: public QObject
{
    Q_OBJECT

signals:
    void pdf_generated(bool success, const QString& message);

public:
    explicit Proxy(QObject* parent = nullptr);
    Q_INVOKABLE QList<QString> get_sender_templates() const;
    Q_INVOKABLE void make_pdf(int from, const QString& to,
                              const QString& subject, const QString& body);
    Q_INVOKABLE void setWindowDarkMode(QWindow* window, bool dark);
    Q_INVOKABLE void saveDarkMode(bool dark);
    Q_INVOKABLE bool loadDarkMode() const;

private:
    QString m_sender_template_dir;
    QString m_output_dir;
    std::vector<Sender_profile> m_profiles;
    QList<QString> m_profile_names;
};
