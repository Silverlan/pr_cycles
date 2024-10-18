/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include <optional>
#include <string>

export module pragma.modules.scenekit:texture;

export namespace pragma::modules::scenekit {
	std::optional<std::string> prepare_texture(const std::string &texPath, const std::optional<std::string> &defaultTexture = {}, bool translucent = false);
};
