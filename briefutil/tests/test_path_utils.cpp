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
