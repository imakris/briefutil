#include "briefutil/brief_service.h"
#include "briefutil/path_utils.h"
#include "briefutil/template_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

struct cli_options_t
{
    std::string recipient;
    std::string recipient_file;
    bool recipient_source_provided = false;
    std::string subject = "Letter";
    std::string body;
    std::string body_file;
    std::string profile_id;
    std::string template_dir;
    std::string output_dir;
    std::string output_path;
    std::string layout = "din_5008_form_b";
    std::string backend = "haru";
    double header_scale = 100.0;
    double body_scale = 100.0;
    double footer_scale = 100.0;
    bool force = false;
    bool help = false;
};

void print_help()
{
    std::cout
        << "Usage: briefutil_cli --to TEXT [options]\n"
        << "Options:\n"
        << "  --to TEXT              Recipient block; use \\n for line breaks\n"
        << "  --to-file PATH         Read recipient block from a UTF-8 text file\n"
        << "  --subject TEXT         Letter subject\n"
        << "  --body TEXT            Markdown body; use \\n for line breaks\n"
        << "  --body-file PATH       Read Markdown body from a UTF-8 text file\n"
        << "  --profile ID           Sender profile id; default is the first profile\n"
        << "  --template-dir PATH    Template/profile directory\n"
        << "  --output-dir PATH      Directory for generated PDF\n"
        << "  --output PATH          Exact output PDF path\n"
        << "  --layout NAME          din_5008_form_b, din_5008_form_a, or us_letter\n"
        << "  --backend NAME         haru or mark2haru\n"
        << "  --header-scale PCT     Header font scale percent\n"
        << "  --body-scale PCT       Body font scale percent\n"
        << "  --footer-scale PCT     Footer font scale percent\n"
        << "  --force                Replace an existing --output file\n";
}

std::optional<std::string> read_file(const std::string& path, std::string* error)
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString().toStdString();
        }
        return std::nullopt;
    }
    return QString::fromUtf8(file.readAll()).toStdString();
}

std::string decode_text_argument(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[++i];
            if (next == 'n') {
                out.push_back('\n');
            }
            else if (next == 'r') {
                out.push_back('\r');
            }
            else if (next == 't') {
                out.push_back('\t');
            }
            else {
                out.push_back(next);
            }
        }
        else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string lower_ascii(std::string value)
{
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

std::optional<std::string> value_for(
    int& index,
    const QStringList& args,
    const char* option)
{
    if (index + 1 >= args.size()) {
        std::cerr << option << " requires a value.\n";
        return std::nullopt;
    }
    return args[++index].toUtf8().toStdString();
}

std::optional<double> parse_double_value(const std::string& value, const char* option)
{
    std::istringstream stream(value);
    double result = 0.0;
    stream >> result;
    stream >> std::ws;
    if (!stream || !stream.eof()) {
        std::cerr << option << " must be a number.\n";
        return std::nullopt;
    }
    return result;
}

bool valid_layout_name(const std::string& value)
{
    const auto normalized = lower_ascii(value);
    return normalized == "din_5008_form_b"
        || normalized == "din_5008_form_a"
        || normalized == "us_letter";
}

bool valid_backend_name(const std::string& value)
{
    const auto normalized = lower_ascii(value);
    return normalized == "haru" || normalized == "mark2haru";
}

std::optional<cli_options_t> parse_args(const QStringList& args)
{
    cli_options_t options;
    for (int i = 1; i < args.size(); ++i) {
        const std::string arg = args[i].toUtf8().toStdString();
        auto read_value = [&](const char* option) -> std::optional<std::string> {
            return value_for(i, args, option);
        };

        if (arg == "--help" || arg == "-h") {
            options.help = true;
        }
        else if (arg == "--to") {
            auto value = read_value("--to");
            if (!value) return std::nullopt;
            options.recipient = decode_text_argument(*value);
            options.recipient_source_provided = true;
        }
        else if (arg == "--to-file") {
            auto value = read_value("--to-file");
            if (!value) return std::nullopt;
            options.recipient_file = *value;
            options.recipient_source_provided = true;
        }
        else if (arg == "--subject") {
            auto value = read_value("--subject");
            if (!value) return std::nullopt;
            options.subject = *value;
        }
        else if (arg == "--body") {
            auto value = read_value("--body");
            if (!value) return std::nullopt;
            options.body = decode_text_argument(*value);
        }
        else if (arg == "--body-file") {
            auto value = read_value("--body-file");
            if (!value) return std::nullopt;
            options.body_file = *value;
        }
        else if (arg == "--profile") {
            auto value = read_value("--profile");
            if (!value) return std::nullopt;
            options.profile_id = *value;
        }
        else if (arg == "--template-dir") {
            auto value = read_value("--template-dir");
            if (!value) return std::nullopt;
            options.template_dir = *value;
        }
        else if (arg == "--output-dir") {
            auto value = read_value("--output-dir");
            if (!value) return std::nullopt;
            options.output_dir = *value;
        }
        else if (arg == "--output") {
            auto value = read_value("--output");
            if (!value) return std::nullopt;
            options.output_path = *value;
        }
        else if (arg == "--layout") {
            auto value = read_value("--layout");
            if (!value) return std::nullopt;
            if (!valid_layout_name(*value)) {
                std::cerr << "--layout must be din_5008_form_b, din_5008_form_a, or us_letter.\n";
                return std::nullopt;
            }
            options.layout = lower_ascii(*value);
        }
        else if (arg == "--backend") {
            auto value = read_value("--backend");
            if (!value) return std::nullopt;
            if (!valid_backend_name(*value)) {
                std::cerr << "--backend must be haru or mark2haru.\n";
                return std::nullopt;
            }
            options.backend = lower_ascii(*value);
        }
        else if (arg == "--header-scale") {
            auto value = read_value("--header-scale");
            if (!value) return std::nullopt;
            auto parsed = parse_double_value(*value, "--header-scale");
            if (!parsed) return std::nullopt;
            options.header_scale = *parsed;
        }
        else if (arg == "--body-scale") {
            auto value = read_value("--body-scale");
            if (!value) return std::nullopt;
            auto parsed = parse_double_value(*value, "--body-scale");
            if (!parsed) return std::nullopt;
            options.body_scale = *parsed;
        }
        else if (arg == "--footer-scale") {
            auto value = read_value("--footer-scale");
            if (!value) return std::nullopt;
            auto parsed = parse_double_value(*value, "--footer-scale");
            if (!parsed) return std::nullopt;
            options.footer_scale = *parsed;
        }
        else if (arg == "--force") {
            options.force = true;
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return std::nullopt;
        }
    }
    return options;
}

std::string env_or_empty(const char* name)
{
    return qEnvironmentVariable(name).toStdString();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    auto parsed = parse_args(app.arguments());
    if (!parsed) {
        return 2;
    }
    auto options = *parsed;
    if (options.help) {
        print_help();
        return 0;
    }

    std::string error;
    if (!options.recipient_file.empty()) {
        auto text = read_file(options.recipient_file, &error);
        if (!text) {
            std::cerr << "Could not read recipient file: " << error << "\n";
            return 2;
        }
        options.recipient = *text;
    }
    if (!options.body_file.empty()) {
        auto text = read_file(options.body_file, &error);
        if (!text) {
            std::cerr << "Could not read body file: " << error << "\n";
            return 2;
        }
        options.body = *text;
    }
    if (options.recipient.empty() && !options.recipient_source_provided) {
        QTextStream in(stdin, QIODevice::ReadOnly);
        options.recipient = in.readAll().toStdString();
    }
    if (options.recipient.empty()) {
        std::cerr << "Recipient is required. Use --to, --to-file, or stdin.\n";
        return 2;
    }

    if (options.template_dir.empty()) {
        options.template_dir = env_or_empty("BRIEFUTIL_TEMPLATE_DIR");
    }
    if (options.template_dir.empty()) {
        options.template_dir = briefutil::default_template_dir();
    }
    if (options.output_dir.empty()) {
        options.output_dir = env_or_empty("BRIEFUTIL_OUTPUT_DIR");
    }
    if (options.output_dir.empty()) {
        options.output_dir = briefutil::configured_output_dir(
            QCoreApplication::applicationDirPath().toStdString(),
            QDir::currentPath().toStdString());
    }

    if (!briefutil::ensure_template_dir_ready(options.template_dir, &error)) {
        std::cerr << "Could not initialize template directory: " << error << "\n";
        return 1;
    }

    std::vector<std::string> profile_errors;
    auto profiles = briefutil::discover_profiles(options.template_dir, &profile_errors);
    for (const auto& profile_error : profile_errors) {
        std::cerr << "Profile load warning: " << profile_error << "\n";
    }
    if (profiles.empty()) {
        std::cerr << "No sender profiles found in " << options.template_dir << "\n";
        return 1;
    }

    const briefutil::profile_entry_t* selected = &profiles.front();
    if (!options.profile_id.empty()) {
        selected = nullptr;
        for (const auto& profile : profiles) {
            if (profile.profile.id == options.profile_id) {
                selected = &profile;
                break;
            }
        }
        if (!selected) {
            std::cerr << "Sender profile not found: " << options.profile_id << "\n";
            return 2;
        }
    }

    briefutil::generation_request_t request;
    request.profile.profile = selected->profile;
    request.profile.profile_path = selected->path;
    request.profile.profile_base_dir = selected->base_dir;
    request.recipient = options.recipient;
    request.subject = options.subject;
    request.body = options.body;
    request.output_dir = options.output_dir;
    request.output_path = options.output_path;
    request.overwrite_output = options.force;
    request.layout = briefutil::layout_spec_from_name(options.layout);
    request.backend = pdf_backend_from_name(options.backend);
    request.theme.typo.header_scale = briefutil::font_scale_from_percent(options.header_scale);
    request.theme.typo.body_scale = briefutil::font_scale_from_percent(options.body_scale);
    request.theme.typo.footer_scale = briefutil::font_scale_from_percent(options.footer_scale);

    auto result = briefutil::generate_brief_pdf(request);
    if (!result.ok) {
        std::cerr << result.message << "\n";
        if (!result.detail.empty()) {
            std::cerr << result.detail << "\n";
        }
        return result.code == briefutil::generation_result_code::Invalid_request
            || result.code == briefutil::generation_result_code::Output_exists
            ? 2 : 1;
    }

    std::cout << result.output_path << "\n";
    return 0;
}
