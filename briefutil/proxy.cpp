#include "proxy.h"

#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>

#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QFileInfo>
#include <QWindow>
#include <QSettings>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

#include "mustermann_signature.png.h"

using namespace std;
namespace fs = std::filesystem;


static Proxy* s_me = nullptr;
Proxy* lgen() { return s_me; }


Proxy::Proxy(QObject*)
{
    const QString portable_texify = QDir(QCoreApplication::applicationDirPath())
        .filePath("miktex/texmfs/install/miktex/bin/x64/texify.exe");
    if (QFile::exists(portable_texify)) {
        m_texify_path = portable_texify;
    }
    else {
        m_texify_path = "texify.exe";
    }

    std::ifstream t("./output_dir.conf");
    QString output_dir = QString::fromUtf8(
        std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>()).c_str()
    );
    output_dir = output_dir.trimmed();

    QDir qodir(output_dir);
    if (!output_dir.isEmpty() && qodir.exists()) {
        m_output_dir = output_dir;
    }
    else {
        m_output_dir = QDir::homePath() + "/briefutil/output/";
        qodir = QDir(m_output_dir);
    }

    // if it does not exist, try to create it
    if (!qodir.exists())
        qodir.mkpath(".");

    m_sender_template_dir = QDir::homePath() + "/briefutil/templates/";
    QDir qtemplatesdir(m_sender_template_dir);
    if (!qtemplatesdir.exists()) {
        qtemplatesdir.mkpath(".");

        std::ofstream t1_ofs(
            (m_sender_template_dir + "Max Mustermann.tex").toStdString(),
            std::ios::out|std::ios::binary
        );
        std::string t1_string =
#include "st_simple.tex.cppstring"
            ;
        t1_ofs << t1_string;
        t1_ofs.close();

        std::ofstream t2_ofs(
            (m_sender_template_dir + "Max Mustermann, Mustermann AG.tex").toStdString(),
            std::ios::out|std::ios::binary
        );
        std::string t2_string =
#include "st_commercial.tex.cppstring"
            ;
        t2_ofs << t2_string;
        t2_ofs.close();

        std::ofstream signature_ofs(
            (m_sender_template_dir + "mustermann_signature.png").toStdString(),
            std::ios::out|std::ios::binary
        );
        signature_ofs.write(
            (const char*)mustermann_signature_png::data().first,
            mustermann_signature_png::data().second);
        signature_ofs.close();
    }

    // detect tex templates in the working directory
    // and populate m_sender_templates.
    std::string ext(".tex");
    for (auto &p : fs::directory_iterator(m_sender_template_dir.toStdString())) {
        if (p.path().extension() == ".tex") {
            m_sender_templates.push_back(QString::fromStdString(p.path().stem().string()));
        }
    }

    QObject::connect(&m_texify, SIGNAL(finished(int, QProcess::ExitStatus)),
         this, SIGNAL(texify_finished()) );
}


QList<QString> Proxy::get_sender_templates() const
{
    return m_sender_templates;
}

static QString sanitize_filename(const QString& input)
{
    QString sanitized = input.trimmed();
    sanitized.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1F]"), "_");
    return sanitized;
}

QString fix_lf(const QString& str_in)
{
    QString str = str_in;
    str.replace(QRegularExpression("\\r"), "");
    str.replace(QRegularExpression("\\n[ \\t]+\\n"), "\n\n");
    str.replace(QRegularExpression("^\\n+"), "");

    size_t lf_count = 0;
    int locked_index = -1;
    QString ret;
    for (size_t i=0; i<str.size(); i++) {
        if (str.at(i)=='\n') {
            if (locked_index == -1) {
                locked_index=i;
            }
            else {
                lf_count++;
            }
        }
        else {
            if (locked_index != -1) {
                if (lf_count >= 1) {
                    ret.push_back(" \\\\[");
                    ret.push_back(QString::number( lf_count+1 ));
                    ret.push_back("\\baselineskip] ");
                }
                else {
                    ret.push_back(" \\\\ ");
                }
                lf_count = 0;
                locked_index = -1;
            }
            ret.push_back(str.at(i));
        }
    }
    return ret;
}


void Proxy::make_pdf(int from, const QString& to, const QString& subject, const QString& body)
{
    if (from < 0 || from >= m_sender_templates.size())
        return;

    auto used_template = m_sender_templates[from] + ".tex";

    // replace line feeds with '\\'
    QString recipient = fix_lf(to);
    QString lf_body = fix_lf(body);

    // make filename (used for temporary tex and pdf)
    QString prefix = QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss") + " ";
    QString mod_subject = subject.isEmpty() ? "[no subject]" : subject;
    mod_subject = sanitize_filename(mod_subject);
    if (mod_subject.isEmpty()) {
        mod_subject = "[no subject]";
    }
    QString tex_fn = prefix + mod_subject + " @FROM@ " +  m_sender_templates[from] + ".tex";

    // pdf2latex has a bug finding files with 2 or more consecutive spaces
    tex_fn.replace(QRegularExpression("\\s+"), " ");

    // make temporary .tex file contents
    QString tex_template_path = m_sender_template_dir + m_sender_templates[from] + ".tex";

    std::ifstream t(tex_template_path.toStdString());
    QString tex_content = QString::fromUtf8(
        std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>()).c_str()
    );

    QString tex_template_path_absolute = QFileInfo(tex_template_path).canonicalPath() + "/";

    tex_content.replace("%%ADDRESS%%", recipient);
    tex_content.replace("%%TITLE%%", mod_subject);
    tex_content.replace("%%BODY%%", lf_body);
    tex_content.replace("%%RESOURCE_DIR%%", tex_template_path_absolute);

    QString tex_path = m_output_dir + "/" + tex_fn;

    QFile tex_file(tex_path);
    if (tex_file.open(QIODevice::WriteOnly)) {
        QTextStream out(&tex_file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        out.setEncoding(QStringConverter::Utf8);
#else
        out.setCodec("UTF-8");
#endif
        out << tex_content;
        tex_file.close();
    }

    QStringList args = {"--pdf", "--synctex=1", "--clean", "--batch", "--run-viewer", tex_path};
    m_texify.setWorkingDirectory(m_output_dir);
    m_texify.start(m_texify_path, args);
}


void Proxy::setWindowDarkMode(QWindow* window, bool dark)
{
#ifdef Q_OS_WIN
    if (!window)
        return;

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    BOOL useDarkMode = dark ? TRUE : FALSE;

    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+)
    constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}


void Proxy::saveDarkMode(bool dark)
{
    QSettings settings("briefutil", "briefutil");
    settings.setValue("appearance/darkMode", dark);
}


bool Proxy::loadDarkMode() const
{
    QSettings settings("briefutil", "briefutil");
    return settings.value("appearance/darkMode", false).toBool();
}
