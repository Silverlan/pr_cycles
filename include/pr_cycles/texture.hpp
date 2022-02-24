/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2020 Florian Weischer
*/

#include <optional>
#include <string>

namespace pragma::modules::cycles
{
	std::optional<std::string> prepare_texture(const std::string &texPath,const std::optional<std::string> &defaultTexture={},bool translucent=false);
};
