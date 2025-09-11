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

#include "mustermann_signature.png.h"

using namespace std;
namespace fs = std::filesystem;


static Proxy* s_me = nullptr;
Proxy* lgen() { return s_me; }


Proxy::Proxy(QObject*)
{
    std::ifstream t("./output_dir.conf");
    QString output_dir = QString::fromUtf8(
        std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>()).c_str()
    );

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


QString escape_latex(const QString& input)
{
    QString output = input;
    // First, escape backslashes. This is a simple string replacement
    // and doesn't need the regex complexity.
    output.replace("\\", "\\textbackslash ");

    // Then, escape other special characters.
    QMap<QString, QString> latexSpecialChars{
        {"&", "\\&"}, {"%", "\\%"},
        {"$", "\\$"}, {"#", "\\#"}, {"_", "\\_"}, {"{", "\\{"},
        {"}", "\\}"}, {"~", "\\textasciitilde "}, {"^", "\\textasciicircum "},
        {"<", "\\textless "}, {">", "\\textgreater "}, {"|", "\\textbar "},
        {"\"", "\\textquotedbl "}, {"'", "\\textquotesingle "}, {"/", "\\slash "}
    };
    for (auto it = latexSpecialChars.begin(); it != latexSpecialChars.end(); ++it) {
        output.replace(it.key(), it.value());
    }
    return output;
}


QString sanitize_filename(const QString& input)
{
    QString sanitized = input;
    sanitized.replace(QRegularExpression("[\\\\/:*?\"<>|%~]+"), "_"); // Replace problematic filesystem characters
    sanitized.replace(QRegularExpression("\\s+"), "_"); // Replace spaces with underscores to avoid issues
    sanitized = sanitized.trimmed(); // Trim whitespace from the start and end
    return sanitized;
}

void Proxy::make_pdf(int from, const QString& to, const QString& subject, const QString& body) {
    auto used_template = m_sender_templates[from] + ".tex";

    // Escape text for LaTeX processing
    QString recipient   = fix_lf(escape_latex(to));
    QString lf_body     = fix_lf(escape_latex(body));
    QString mod_subject = escape_latex(subject.isEmpty() ? "[no subject]" : subject);

    // Sanitize the original input for filenames
    QString filename_to      = sanitize_filename(to);
    QString filename_subject = sanitize_filename(subject);

    // Make filename (used for temporary tex and pdf)
    QString prefix = QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss") + " ";
    QString tex_fn = prefix + filename_subject + "_FROM_" + filename_to + ".tex";

    // Make temporary .tex file contents
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
        QTextStream out(&tex_file); out << tex_content.toUtf8();
        out.setCodec("UTF-8");
        tex_file.close();
    }

    QStringList args = {"--pdf", "--synctex=1", "--clean", "--batch", "--run-viewer", tex_path};
    m_texify.setWorkingDirectory(m_output_dir);
    m_texify.start("texify.exe", args);
}
