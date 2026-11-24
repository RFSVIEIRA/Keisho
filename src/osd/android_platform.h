// ██╗  ██╗███████╗██╗███████╗██╗  ██╗ ██████╗
// ██║ ██╔╝██╔════╝██║██╔════╝██║  ██║██╔═══██╗
// █████╔╝ █████╗  ██║███████╗███████║██║   ██║
// ██╔═██╗ ██╔══╝  ██║╚════██║██╔══██║██║   ██║
// ██║  ██╗███████╗██║███████║██║  ██║╚██████╔╝
// ╚═╝  ╚═╝╚══════╝╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝
//            継 承  ( k e i s h ō )
//
// Android platform helpers: APK asset extraction, storage roots, and
// storage-permission bridges to KeishoActivity.
#pragma once

#include <string>

namespace sm2::osd {

/// Extract bundled APK assets (games.xml, lang/*.po, lang/fonts/*) into
/// internal storage so pugixml / std::filesystem can read them normally.
/// Returns the extracted data root directory (or empty on failure/non-Android).
[[nodiscard]] std::string android_prepare_assets();

/// Directory holding extracted APK data (games.xml, lang/). Empty on non-Android.
[[nodiscard]] std::string android_assets_dir();

/// App-specific external files directory (no permission needed, user-accessible
/// via USB/file managers at /Android/data/<pkg>/files), falling back to internal.
[[nodiscard]] std::string android_external_root_dir();

/// Shared internal storage root (/storage/emulated/0).
[[nodiscard]] std::string android_shared_storage_dir();

/// True when the app holds permission to browse shared storage (/storage/emulated/0).
[[nodiscard]] bool android_has_storage_access();

/// Prompt the user to grant All Files Access (Android 11+) or storage permission.
void android_request_storage_access();

}  // namespace sm2::osd
