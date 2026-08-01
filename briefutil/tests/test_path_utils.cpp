#include "briefutil/path_utils.h"

#include <cstdlib>
#include <iostream>
#include <string>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

int main()
{
    using briefutil::Path_class;

    require(
        briefutil::classify_windows_path("C:/tmp/out.pdf") == Path_class::ABSOLUTE,
        "drive absolute path should be absolute");
    require(
        briefutil::classify_windows_path("C:tmp/out.pdf") == Path_class::DRIVE_RELATIVE,
        "drive-relative path should be detected");
    require(
        briefutil::classify_windows_path("/tmp/out.pdf") == Path_class::DRIVE_ROOT_RELATIVE,
        "drive-root-relative path should be detected");
    require(
        briefutil::is_current_drive_dependent_windows_path("\\tmp\\out.pdf"),
        "rooted path should be current-drive-dependent");

    require(
        briefutil::sanitize_filename_component("CON.report") == "_CON.report",
        "reserved stem with extension should be prefixed");
    require(
        briefutil::sanitize_filename_component("a<b>:c\"d/e\\f|g?h*.") == "a_b__c_d_e_f_g_h__",
        "invalid filename characters should be replaced");
    require(
        briefutil::sanitize_filename_component("  NUL  ") == "_NUL",
        "reserved trimmed filename should be prefixed");
    require(
        briefutil::sanitize_filename_component("Hello \t  World..") == "Hello World__",
        "whitespace should collapse and trailing dots should be replaced");

    // A filename component has to fit a directory entry, so an unbounded
    // subject line must not produce an unbounded stem.
    const std::string long_ascii(500, 'a');
    require(
        briefutil::sanitize_filename_component(long_ascii).size() ==
            briefutil::k_max_filename_component_bytes,
        "an over-long component should be truncated to the documented bound");

    // "\xc3\xa4" is a two-byte code point. With one leading ASCII byte the
    // bound falls in the middle of one, so the cut has to back up by one byte
    // instead of emitting a half sequence.
    std::string long_utf8 = "x";
    while (long_utf8.size() < 500) {
        long_utf8 += "\xc3\xa4";
    }
    require(
        briefutil::sanitize_filename_component(long_utf8).size() ==
            briefutil::k_max_filename_component_bytes - 1,
        "truncation should stop on a UTF-8 code-point boundary");

    require(
        briefutil::sanitize_filename_component(std::string(119, 'a') + " tail").back() == '_',
        "a trailing space exposed by truncation should be replaced");

    require(
        briefutil::is_valid_profile_image_name("signature.png"),
        "simple PNG asset should be valid");
    require(
        !briefutil::is_valid_profile_image_name("../signature.png"),
        "traversal asset should be invalid");
    require(
        !briefutil::is_valid_profile_image_name("C:signature.png"),
        "drive-relative asset should be invalid");
    require(
        !briefutil::is_valid_profile_image_name("signature.jpg"),
        "non-PNG asset should be invalid");

    return 0;
}
