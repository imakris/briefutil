#include "briefutil/brief_service.h"
#include "briefutil/path_utils.h"
#include "briefutil/template_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <charconv>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct Cli_options
{
    std::string        recipient;
    std::string        recipient_file;
    bool               recipient_source_provided = false;
    bool               recipient_text_provided   = false;
    std::string        subject                   = "Letter";
    std::string        body;
    std::string        body_file;
    bool               body_text_provided        = false;
    std::string        profile_id;
    std::string        profile_path;
    std::string        template_dir;
    std::string        output_dir;
    std::string        output_path;
    std::string        layout                    = "din_5008_form_b";
    Font_family_config fonts                     = default_font_family();
    double             body_size                 = 10.0;
    double             body_leading              = 12.0;
    double             header_scale              = 100.0;
    double             body_scale                = 100.0;
    double             footer_scale              = 100.0;
    bool               force                     = false;
    bool               help                      = false;
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
        << "  --profile-path PATH    Exact sender profile JSON path\n"
        << "  --template-dir PATH    Template/profile directory\n"
        << "  --output-dir PATH      Directory for generated PDF\n"
        << "  --output PATH          Exact output PDF path\n"
        << "  --layout NAME          din_5008_form_b, din_5008_form_a, or us_letter\n"
        << "  --font-sans PATH       .ttf font path; omit for bundled default\n"
        << "  --font-sans-bold PATH  .ttf font path; omit for bundled default\n"
        << "  --font-sans-italic PATH .ttf font path; omit for bundled default\n"
        << "  --font-sans-bold-italic PATH .ttf font path; omit for bundled default\n"
        << "  --font-mono PATH       .ttf font path; omit for bundled default\n"
        << "  --body-size PT         Body font size in points, 6..24\n"
        << "  --body-leading PT      Body line leading in points, 6..36\n"
        << "  --header-scale PCT     Header font scale percent, 50..200\n"
        << "  --body-scale PCT       Body font scale percent, 50..200\n"
        << "  --footer-scale PCT     Footer font scale percent, 50..200\n"
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
            else
            if (next == 'r') {
                out.push_back('\r');
            }
            else
            if (next == 't') {
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
    int&               index,
    const QStringList& args,
    const char*        option)
{
    if (index + 1 >= args.size()) {
        std::cerr << option << " requires a value.\n";
        return std::nullopt;
    }
    return args[++index].toUtf8().toStdString();
}

std::optional<double> parse_double_value(const std::string& value, const char* option)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    const auto last  = value.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) {
        std::cerr << option << " must be a number.\n";
        return std::nullopt;
    }
    const std::string trimmed = value.substr(first, last - first + 1);
    double            result  = 0.0;
    const char*       begin   = trimmed.data();
    const char*       end     = begin + trimmed.size();
    const auto        parsed  = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(result)) {
        std::cerr << option << " must be a number.\n";
        return std::nullopt;
    }
    return result;
}

bool validate_range(
    double         value,
    double         minimum,
    double         maximum,
    const char*    option)
{
    if (value >= minimum && value <= maximum) {
        return true;
    }
    std::cerr << option << " must be between " << minimum << " and " << maximum << ".\n";
    return false;
}

bool parse_numeric_option(
    int&               index,
    const QStringList& args,
    const char*        option,
    double             minimum,
    double             maximum,
    double&            target)
{
    auto raw = value_for(index, args, option);
    if (!raw) {
        return false;
    }
    auto parsed = parse_double_value(*raw, option);
    if (!parsed) {
        return false;
    }
    if (!validate_range(*parsed, minimum, maximum, option)) {
        return false;
    }
    target = *parsed;
    return true;
}

bool valid_layout_name(const std::string& value)
{
    const auto normalized = lower_ascii(value);
    return
        normalized == "din_5008_form_b" ||
        normalized == "din_5008_form_a" ||
        normalized == "us_letter";
}

std::optional<Cli_options> parse_args(const QStringList& args)
{
    Cli_options options;
    for (int i = 1; i < args.size(); ++i) {
        const std::string arg = args[i].toUtf8().toStdString();
        auto read_value = [&](const char* option) -> std::optional<std::string> {
            return value_for(i, args, option);
        };

        if (arg == "--help" || arg == "-h") {
            options.help = true;
        }
        else
        if (arg == "--to") {
            auto value = read_value("--to");
            if (!value) {
                return std::nullopt;
            }
            options.recipient                 = decode_text_argument(*value);
            options.recipient_source_provided = true;
            options.recipient_text_provided   = true;
        }
        else
        if (arg == "--to-file") {
            auto value = read_value("--to-file");
            if (!value) {
                return std::nullopt;
            }
            options.recipient_file = *value;
            options.recipient_source_provided = true;
        }
        else
        if (arg == "--subject") {
            auto value = read_value("--subject");
            if (!value) {
                return std::nullopt;
            }
            options.subject = *value;
        }
        else
        if (arg == "--body") {
            auto value = read_value("--body");
            if (!value) {
                return std::nullopt;
            }
            options.body = decode_text_argument(*value);
            options.body_text_provided = true;
        }
        else
        if (arg == "--body-file") {
            auto value = read_value("--body-file");
            if (!value) {
                return std::nullopt;
            }
            options.body_file = *value;
        }
        else
        if (arg == "--profile") {
            auto value = read_value("--profile");
            if (!value) {
                return std::nullopt;
            }
            options.profile_id = *value;
        }
        else
        if (arg == "--profile-path") {
            auto value = read_value("--profile-path");
            if (!value) {
                return std::nullopt;
            }
            options.profile_path = *value;
        }
        else
        if (arg == "--template-dir") {
            auto value = read_value("--template-dir");
            if (!value) {
                return std::nullopt;
            }
            options.template_dir = *value;
        }
        else
        if (arg == "--output-dir") {
            auto value = read_value("--output-dir");
            if (!value) {
                return std::nullopt;
            }
            options.output_dir = *value;
        }
        else
        if (arg == "--output") {
            auto value = read_value("--output");
            if (!value) {
                return std::nullopt;
            }
            options.output_path = *value;
        }
        else
        if (arg == "--layout") {
            auto value = read_value("--layout");
            if (!value) {
                return std::nullopt;
            }
            if (!valid_layout_name(*value)) {
                std::cerr << "--layout must be din_5008_form_b, din_5008_form_a, or us_letter.\n";
                return std::nullopt;
            }
            options.layout = lower_ascii(*value);
        }
        else
        if (arg == "--font-sans") {
            auto value = read_value("--font-sans");
            if (!value) {
                return std::nullopt;
            }
            options.fonts.sans = *value;
        }
        else
        if (arg == "--font-sans-bold") {
            auto value = read_value("--font-sans-bold");
            if (!value) {
                return std::nullopt;
            }
            options.fonts.sans_bold = *value;
        }
        else
        if (arg == "--font-sans-italic") {
            auto value = read_value("--font-sans-italic");
            if (!value) {
                return std::nullopt;
            }
            options.fonts.sans_italic = *value;
        }
        else
        if (arg == "--font-sans-bold-italic") {
            auto value = read_value("--font-sans-bold-italic");
            if (!value) {
                return std::nullopt;
            }
            options.fonts.sans_bold_italic = *value;
        }
        else
        if (arg == "--font-mono") {
            auto value = read_value("--font-mono");
            if (!value) {
                return std::nullopt;
            }
            options.fonts.mono = *value;
        }
        else
        if (arg == "--body-size") {
            if (!parse_numeric_option(i, args, "--body-size", 6.0, 24.0, options.body_size)) {
                return std::nullopt;
            }
        }
        else
        if (arg == "--body-leading") {
            if (!parse_numeric_option(i, args, "--body-leading", 6.0, 36.0, options.body_leading)) {
                return std::nullopt;
            }
        }
        else
        if (arg == "--header-scale") {
            if (!parse_numeric_option(i, args, "--header-scale", 50.0, 200.0, options.header_scale)) {
                return std::nullopt;
            }
        }
        else
        if (arg == "--body-scale") {
            if (!parse_numeric_option(i, args, "--body-scale", 50.0, 200.0, options.body_scale)) {
                return std::nullopt;
            }
        }
        else
        if (arg == "--footer-scale") {
            if (!parse_numeric_option(i, args, "--footer-scale", 50.0, 200.0, options.footer_scale)) {
                return std::nullopt;
            }
        }
        else
        if (arg == "--force") {
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
    if (!options.profile_path.empty() && !options.profile_id.empty()) {
        std::cerr << "Use either --profile or --profile-path, not both.\n";
        return 2;
    }
    if (options.recipient_text_provided && !options.recipient_file.empty()) {
        std::cerr << "Use either --to or --to-file, not both.\n";
        return 2;
    }
    if (options.body_text_provided && !options.body_file.empty()) {
        std::cerr << "Use either --body or --body-file, not both.\n";
        return 2;
    }
    if (!options.output_path.empty() && !options.output_dir.empty()) {
        std::cerr << "Use either --output or --output-dir, not both.\n";
        return 2;
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
    if (options.recipient.empty() && !options.recipient_source_provided) {
        std::cerr << "Recipient is required. Use --to, --to-file, or stdin.\n";
        return 2;
    }

    if (options.template_dir.empty()) { options.template_dir = env_or_empty("BRIEFUTIL_TEMPLATE_DIR"); }
    if (options.template_dir.empty()) { options.template_dir = briefutil::default_template_dir();      }
    if (options.output_dir.empty())   { options.output_dir   = env_or_empty("BRIEFUTIL_OUTPUT_DIR");   }
    if (options.output_dir.empty()) {
        options.output_dir = briefutil::configured_output_dir(
            QCoreApplication::applicationDirPath().toStdString(),
            QDir::currentPath().toStdString());
    }

    briefutil::Profile_entry selected_entry;
    if (!options.profile_path.empty()) {
        auto loaded = load_sender_profile(options.profile_path);
        if (!loaded.ok) {
            std::cerr << "Could not load sender profile: " << loaded.error << "\n";
            return 2;
        }
        QFileInfo profile_info(QString::fromStdString(options.profile_path));
        selected_entry.profile  = std::move(loaded.profile);
        selected_entry.path     = profile_info.absoluteFilePath().toStdString();
        selected_entry.base_dir = profile_info.absoluteDir().absolutePath().toStdString();
    }
    else {
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

        const briefutil::Profile_entry* selected = &profiles.front();
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
        selected_entry = *selected;
    }

    briefutil::Generation_request request;
    request.profile.profile = selected_entry.profile;
    request.profile.profile_path = selected_entry.path;
    request.profile.profile_base_dir = selected_entry.base_dir;
    request.recipient = options.recipient;
    request.subject = options.subject;
    request.body = options.body;
    request.output_dir = options.output_dir;
    request.output_path = options.output_path;
    request.overwrite_output = options.force;
    request.layout = briefutil::layout_spec_from_name(options.layout);
    request.theme.fonts = options.fonts;
    request.theme.typo.body_size_pt = static_cast<float>(options.body_size);
    request.theme.typo.body_lead_pt = static_cast<float>(options.body_leading);
    request.theme.typo.header_scale = briefutil::font_scale_from_percent(options.header_scale);
    request.theme.typo.body_scale = briefutil::font_scale_from_percent(options.body_scale);
    request.theme.typo.footer_scale = briefutil::font_scale_from_percent(options.footer_scale);

    auto result = briefutil::generate_brief_pdf(request);
    if (!result.ok) {
        std::cerr << result.message << "\n";
        if (!result.detail.empty()) {
            std::cerr << result.detail << "\n";
        }
        const bool usage_error =
            result.code == briefutil::Generation_result_code::INVALID_REQUEST ||
            result.code == briefutil::Generation_result_code::OUTPUT_EXISTS;
        if (usage_error) {
            return 2;
        }
        return 1;
    }

    std::cout << result.output_path << "\n";
    return 0;
}
