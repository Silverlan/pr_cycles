// SPDX-FileCopyrightText: (c) 2024 Silverlan <opensource@pragma-engine.com>
// SPDX-License-Identifier: MIT

export module pragma.modules.scenekit:texture;

export import std;

export namespace pragma::modules::scenekit {
	std::optional<std::string> prepare_texture(const std::string &texPath, const std::optional<std::string> &defaultTexture = {}, bool translucent = false);
};
