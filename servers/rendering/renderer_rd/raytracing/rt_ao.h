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
#pragma once

#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shader_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/rtao.glsl.gen.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"

namespace RendererRD {

// Ray traced ambient occlusion. Traces hemisphere rays against the scene TLAS and writes the
// result into the same RB_FINAL target the screen-space SSAO uses.
class RTAO {
public:
	struct Settings {
		float radius = 1.0;
		float intensity = 2.0;
		float power = 1.5;
		uint32_t samples = 8;
	};

	void initialize();
	void finalize();

	void generate(Ref<RenderSceneBuffersRD> p_render_buffers, uint32_t p_view, RID p_tlas, RID p_normal_buffer, const Projection &p_inv_view_projection, const Transform3D &p_cam_transform, const Settings &p_settings, uint32_t p_frame);

private:
	struct Params {
		float inv_view_projection[16];
		float view_to_world[16];
		float settings[4];
		uint32_t frame_data[4];
	};

	RtaoShaderRD shader;
	RID shader_version;
	PipelineDeferredRD pipeline;
	RID params_buffer;
	RID nearest_sampler;
	bool initialized = false;
};

} // namespace RendererRD
