/**************************************************************************/
/*  rt_denoise.cpp                                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/
#include "rt_denoise.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"

void RendererRD::RTDenoise::initialize() {
	Vector<String> denoise_modes;
	denoise_modes.push_back("\n");
	shader.initialize(denoise_modes);
	shader_version = shader.version_create();
	pipeline.create_compute_pipeline(shader.version_get_shader(shader_version, 0));
	params_buffer = RD::get_singleton()->uniform_buffer_create(sizeof(Params));
	initialized = true;
}

void RendererRD::RTDenoise::finalize() {
	if (!initialized) {
		return;
	}
	if (temp_texture.is_valid()) {
		RD::get_singleton()->free_rid(temp_texture);
		temp_texture = RID();
	}
	RD::get_singleton()->free_rid(params_buffer);
	params_buffer = RID();
	pipeline.free();
	shader.version_free(shader_version);
	shader_version = RID();
	initialized = false;
}

void RendererRD::RTDenoise::denoise(RID p_value, RID p_depth_texture, RID p_normal_buffer, const Projection &p_inv_view_projection, Vector3 p_camera_position, uint32_t p_radius) {
	ERR_FAIL_COND(!initialized);
	ERR_FAIL_COND(p_value.is_null());
	ERR_FAIL_COND(p_depth_texture.is_null());
	ERR_FAIL_COND(p_normal_buffer.is_null());

	RD::TextureFormat value_format = RD::get_singleton()->texture_get_format(p_value);
	ERR_FAIL_COND_MSG(value_format.format != RD::DATA_FORMAT_R8_UNORM, "RT denoise expects an R8 target.");
	Size2i size(value_format.width, value_format.height);

	// The temporary texture holds the first pass output. It is recreated whenever the target size changes.
	if (temp_texture.is_null() || temp_size != size) {
		if (temp_texture.is_valid()) {
			RD::get_singleton()->free_rid(temp_texture);
		}
		RD::TextureFormat tf;
		tf.format = RD::DATA_FORMAT_R8_UNORM;
		tf.width = size.width;
		tf.height = size.height;
		tf.depth = 1;
		tf.array_layers = 1;
		tf.mipmaps = 1;
		tf.texture_type = RD::TEXTURE_TYPE_2D;
		tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		RD::TextureView view;
		temp_texture = RD::get_singleton()->texture_create(tf, view);
		temp_size = size;
	}

	RD::get_singleton()->draw_command_begin_label("Denoise Ray Traced Result");

	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	if (nearest_sampler.is_null()) {
		nearest_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	}

	Params params;
	Projection inv_vp = p_inv_view_projection;
	memcpy(params.inv_view_projection, &inv_vp.columns[0][0], sizeof(float) * 16);
	params.camera_position[0] = p_camera_position.x;
	params.camera_position[1] = p_camera_position.y;
	params.camera_position[2] = p_camera_position.z;
	params.camera_position[3] = 0.0;
	params.settings[0] = float(p_radius);
	params.settings[1] = 0.02; // Relative world-space distance tolerance.
	params.settings[2] = 32.0; // Normal sharpness.
	params.settings[3] = 0.0;
	RD::get_singleton()->buffer_update(params_buffer, 0, sizeof(Params), &params);

	// Two passes: value -> temp, then temp -> value. Each pass gets its own compute list so the barrier is explicit.
	_dispatch(p_value, temp_texture, size, p_depth_texture, p_normal_buffer);
	_dispatch(temp_texture, p_value, size, p_depth_texture, p_normal_buffer);

	RD::get_singleton()->draw_command_end_label();
}

void RendererRD::RTDenoise::_dispatch(RID p_source, RID p_dest, Size2i p_size, RID p_depth_texture, RID p_normal_buffer) {
	RD::Uniform u_value;
	u_value.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
	u_value.binding = 0;
	u_value.append_id(nearest_sampler);
	u_value.append_id(p_source);

	RD::Uniform u_depth;
	u_depth.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
	u_depth.binding = 1;
	u_depth.append_id(nearest_sampler);
	u_depth.append_id(p_depth_texture);

	RD::Uniform u_normal;
	u_normal.uniform_type = RD::UNIFORM_TYPE_IMAGE;
	u_normal.binding = 2;
	u_normal.append_id(p_normal_buffer);

	RD::Uniform u_dest;
	u_dest.uniform_type = RD::UNIFORM_TYPE_IMAGE;
	u_dest.binding = 3;
	u_dest.append_id(p_dest);

	RD::Uniform u_params;
	u_params.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
	u_params.binding = 4;
	u_params.append_id(params_buffer);

	Vector<RD::Uniform> uniforms;
	uniforms.push_back(u_value);
	uniforms.push_back(u_depth);
	uniforms.push_back(u_normal);
	uniforms.push_back(u_dest);
	uniforms.push_back(u_params);
	RID uniform_set = RD::get_singleton()->uniform_set_create(uniforms, shader.version_get_shader(shader_version, 0), 0);

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipeline.get_rid());
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
	RD::get_singleton()->compute_list_dispatch_threads(compute_list, p_size.width, p_size.height, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->free_rid(uniform_set);
}
