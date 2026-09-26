#pragma once

#include "error_codes.hpp"
#include "error_list.hpp"
#include <bsod/bsod.h>

#include <algorithm>
#include <string_view>

constexpr const ErrDesc &find_error(const ErrCode error_code) {
    // Iterating through error_list to find the error
    const auto error = std::ranges::find_if(error_list, [error_code](const auto &elem) { return (elem.err_code) == error_code; });
    if (error == std::end(error_list)) {
        bsod("Unknown error");
    }
    return *error;
}

/// Help link prefix of errors that do not name their own in the error codes YAML
inline constexpr std::string_view prusa_help_url_prefix = "prusa.io/";

/// Longest help link prefix; widgets showing help links check their buffers against it
inline constexpr size_t max_help_url_prefix_length = 48;

static_assert(prusa_help_url_prefix.size() <= max_help_url_prefix_length);
static_assert(std::ranges::all_of(error_help_urls, [](const ErrHelpUrl &help_url) { return std::string_view(help_url.url_prefix).size() <= max_help_url_prefix_length; }));

/// Where the help article for \p error_code lives: a URL without the scheme,
/// to which the printer-specific five-digit error code is appended.
constexpr std::string_view find_error_help_url_prefix(const ErrCode error_code) {
    const auto help_url = std::ranges::find(error_help_urls, error_code, &ErrHelpUrl::err_code);
    return help_url == std::end(error_help_urls) ? prusa_help_url_prefix : std::string_view(help_url->url_prefix);
}
