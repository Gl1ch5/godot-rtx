/**************************************************************************/
/*  rt_shadow.cpp                                                            */
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
#include "rt_shadow.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"

void RendererRD::RTShadow::initialize() {
	Vector<String> shadow_modes;
	shadow_modes.push_back("\n");
	shader.initialize(shadow_modes);
	shader_version = shader.version_create();
	pipeline.create_compute_pipeline(shader.version_get_shader(shader_version, 0));
	params_buffer = RD::get_singleton()->uniform_buffer_create(sizeof(Params));
	initialized = true;
}

void RendererRD::RTShadow::finalize() {
	if (!initialized) {
		return;
	}
	RD::get_singleton()->free_rid(params_buffer);
	params_buffer = RID();
	pipeline.free();
	shader.version_free(shader_version);
	shader_version = RID();
	initialized = false;
}

void RendererRD::RTShadow::generate(RID p_visibility_slice, RID p_tlas, RID p_normal_buffer, RID p_depth_texture, const Projection &p_inv_view_projection, const Transform3D &p_cam_transform, Vector3 p_light_direction, float p_tan_angular_radius, uint32_t p_frame) {
	ERR_FAIL_COND(!initialized);
	ERR_FAIL_COND(p_visibility_slice.is_null());
	ERR_FAIL_COND(p_tlas.is_null());
	ERR_FAIL_COND(p_normal_buffer.is_null());
	ERR_FAIL_COND(p_depth_texture.is_null());

	RD::get_singleton()->draw_command_begin_label("Process Ray Traced Shadows");

	RD::TextureFormat dest_format = RD::get_singleton()->texture_get_format(p_visibility_slice);

	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	if (nearest_sampler.is_null()) {
		nearest_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	}

	Params params;
	Projection inv_vp = p_inv_view_projection;
	memcpy(params.inv_view_projection, &inv_vp.columns[0][0], sizeof(float) * 16);
	Projection world_mat = Projection(p_cam_transform);
	memcpy(params.view_to_world, &world_mat.columns[0][0], sizeof(float) * 16);
	Vector3 light_dir = p_light_direction.normalized();
	params.light_direction[0] = light_dir.x;
	params.light_direction[1] = light_dir.y;
	params.light_direction[2] = light_dir.z;
	params.light_direction[3] = p_tan_angular_radius;
	params.frame_data[0] = p_frame;
	params.frame_data[1] = 0;
	params.frame_data[2] = 0;
	params.frame_data[3] = 0;
	RD::get_singleton()->buffer_update(params_buffer, 0, sizeof(Params), &params);

	RD::Uniform u_depth;
	u_depth.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
	u_depth.binding = 0;
	u_depth.append_id(nearest_sampler);
	u_depth.append_id(p_depth_texture);

	RD::Uniform u_normal;
	u_normal.uniform_type = RD::UNIFORM_TYPE_IMAGE;
	u_normal.binding = 1;
	u_normal.append_id(p_normal_buffer);

	RD::Uniform u_dest;
	u_dest.uniform_type = RD::UNIFORM_TYPE_IMAGE;
	u_dest.binding = 2;
	u_dest.append_id(p_visibility_slice);

	RD::Uniform u_tlas;
	u_tlas.uniform_type = RD::UNIFORM_TYPE_ACCELERATION_STRUCTURE;
	u_tlas.binding = 3;
	u_tlas.append_id(p_tlas);

	RD::Uniform u_params;
	u_params.uniform_type = RD::UNIFORM_TYPE_UNIFORM_BUFFER;
	u_params.binding = 4;
	u_params.append_id(params_buffer);

	Vector<RD::Uniform> uniforms;
	uniforms.push_back(u_depth);
	uniforms.push_back(u_normal);
	uniforms.push_back(u_dest);
	uniforms.push_back(u_tlas);
	uniforms.push_back(u_params);
	RID uniform_set = RD::get_singleton()->uniform_set_create(uniforms, shader.version_get_shader(shader_version, 0), 0);

	RD::ComputeListID compute_list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(compute_list, pipeline.get_rid());
	RD::get_singleton()->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
	RD::get_singleton()->compute_list_dispatch_threads(compute_list, dest_format.width, dest_format.height, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->free_rid(uniform_set);

	RD::get_singleton()->draw_command_end_label();
}
