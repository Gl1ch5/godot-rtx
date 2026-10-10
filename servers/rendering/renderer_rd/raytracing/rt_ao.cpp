/**************************************************************************/
/*  rt_scene.h                                                            */
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
#include "rt_ao.h"

#include "servers/rendering/renderer_rd/effects/ss_effects.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"

void RendererRD::RTAO::initialize() {
	Vector<String> ao_modes;
	ao_modes.push_back("\n");
	shader.initialize(ao_modes);
	shader_version = shader.version_create();
	pipeline.create_compute_pipeline(shader.version_get_shader(shader_version, 0));
	params_buffer = RD::get_singleton()->uniform_buffer_create(sizeof(Params));
	initialized = true;
}

void RendererRD::RTAO::finalize() {
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

void RendererRD::RTAO::generate(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_view, RID p_tlas, RID p_normal_buffer, const Projection &p_inv_view_projection, const Transform3D &p_cam_transform, const Settings &p_settings, uint32_t p_frame) {
	ERR_FAIL_COND(!initialized);
	ERR_FAIL_COND(p_render_buffers.is_null());
	ERR_FAIL_COND(p_tlas.is_null());
	ERR_FAIL_COND(p_normal_buffer.is_null());

	RD::get_singleton()->draw_command_begin_label("Process Ray Traced Ambient Occlusion");

	RID ao_final = p_render_buffers->get_texture_slice(RB_SCOPE_SSAO, RB_FINAL, p_view, 0);
	RID depth_texture = p_render_buffers->get_depth_texture(p_view);
	Size2i size = p_render_buffers->get_internal_size();

	if (nearest_sampler.is_null()) {
		MaterialStorage *material_storage = MaterialStorage::get_singleton();
		ERR_FAIL_NULL(material_storage);
		nearest_sampler = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	}

	Params params;
	Transform3D view_to_world = p_cam_transform;
	Projection inv_vp = p_inv_view_projection;
	memcpy(params.inv_view_projection, &inv_vp.columns[0][0], sizeof(float) * 16);
	Projection world_mat = Projection(view_to_world);
	memcpy(params.view_to_world, &world_mat.columns[0][0], sizeof(float) * 16);
	params.settings[0] = p_settings.radius;
	params.settings[1] = p_settings.intensity;
	params.settings[2] = p_settings.power;
	params.settings[3] = 0.0;
	params.frame_data[0] = p_frame;
	params.frame_data[1] = p_settings.samples;
	params.frame_data[2] = 0;
	params.frame_data[3] = 0;
	RD::get_singleton()->buffer_update(params_buffer, 0, sizeof(Params), &params);

	RD::Uniform u_depth;
	u_depth.uniform_type = RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE;
	u_depth.binding = 0;
	u_depth.append_id(nearest_sampler);
	u_depth.append_id(depth_texture);

	RD::Uniform u_normal;
	u_normal.uniform_type = RD::UNIFORM_TYPE_IMAGE;
	u_normal.binding = 1;
	u_normal.append_id(p_normal_buffer);

	RD::Uniform u_dest;
	u_dest.uniform_type = RD::UNIFORM_TYPE_IMAGE;
	u_dest.binding = 2;
	u_dest.append_id(ao_final);

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
	RD::get_singleton()->compute_list_dispatch_threads(compute_list, size.x, size.y, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->free_rid(uniform_set);

	RD::get_singleton()->draw_command_end_label();
}
