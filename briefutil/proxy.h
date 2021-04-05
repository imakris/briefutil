#ifndef PROXY_H_INCLUDED
#define PROXY_H_INCLUDED

#include <QObject>
#include <QList>
#include <QProcess>

class Proxy: public QObject
{
    Q_OBJECT

public:

signals:
    void texify_finished();

public:
    explicit Proxy(QObject* parent = 0);
    Q_INVOKABLE QList<QString>  get_sender_templates() const;
    Q_INVOKABLE void make_pdf(int from, const QString& to, const QString& subject, const QString& body);

private:
    QProcess m_texify;
    QString m_sender_template_dir;
    QString m_output_dir;
    QList<QString> m_sender_templates;
};


Proxy* lgen();


#endif
