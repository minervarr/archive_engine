#pragma once

#include <string>

struct android_app;

namespace archive {

// Resolves the absolute path to the public Documents directory with an optional subfolder.
// Creates the directory structure if it doesn't exist.
std::string get_documents_path(android_app* app, const std::string& subfolder = "");

} // namespace archive
