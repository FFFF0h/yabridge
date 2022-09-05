// yabridge: a Wine plugin bridge
// Copyright (C) 2020-2022 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "plugin.h"

namespace clap {
namespace plugin {

descriptor::descriptor(const clap_plugin_descriptor_t& original)
    : clap_version(original.clap_version),
      id((assert(original.id), original.id)),
      name((assert(original.id), original.id)),
      vendor(original.vendor ? std::optional(original.vendor) : std::nullopt),
      url(original.url ? std::optional(original.url) : std::nullopt),
      manual_url(original.manual_url ? std::optional(original.manual_url)
                                     : std::nullopt),
      support_url(original.support_url ? std::optional(original.support_url)
                                       : std::nullopt),
      version(original.version ? std::optional(original.version)
                               : std::nullopt),
      description(original.description ? std::optional(original.description)
                                       : std::nullopt) {
    // The features array is stored as an envp-style null terminated array
    const char** orig_features = original.features;
    if (orig_features) {
        while (*orig_features) {
            features.push_back(*orig_features);
            orig_features++;
        }
    }
}

const clap_plugin_descriptor_t* descriptor::get() const {
    // This should be the minimum of yabridge's supported CLAP version and
    // the plugin's supported CLAP version
    clap_version_t supported_clap_version = clap_version;
    if (CLAP_VERSION_MAJOR < clap_version.major ||
        (clap_version.major == CLAP_VERSION_MAJOR &&
         (CLAP_VERSION_MINOR < clap_version.minor ||
          (clap_version.minor == CLAP_VERSION_MINOR &&
           CLAP_VERSION_REVISION < clap_version.revision)))) {
        supported_clap_version = CLAP_VERSION;
    }

    // `features_ptrs` needs to be populated as an envp-style null terminated
    // array
    features_ptrs.resize(features.size() + 1);
    for (size_t i = 0; i < features.size(); i++) {
        features_ptrs[i] = features[i].c_str();
    }
    features_ptrs[features.size()] = nullptr;

    clap_descriptor = clap_plugin_descriptor_t{
        .clap_version = supported_clap_version,
        .id = id.c_str(),
        .name = name.c_str(),
        .vendor = vendor ? vendor->c_str() : nullptr,
        .url = url ? url->c_str() : nullptr,
        .manual_url = manual_url ? manual_url->c_str() : nullptr,
        .support_url = support_url ? support_url->c_str() : nullptr,
        .version = version ? version->c_str() : nullptr,
        .description = description ? description->c_str() : nullptr,
        .features = features_ptrs.data(),
    };

    return &clap_descriptor;
}

}  // namespace plugin
}  // namespace clap