/**************************************************************************/
/*  rt_denoise.h                                                              */
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
#include "servers/rendering/renderer_rd/pipeline_deferred_rd.h"
#include "servers/rendering/renderer_rd/shader_rd.h"
#include "servers/rendering/renderer_rd/shaders/effects/rt_denoise.glsl.gen.h"

namespace RendererRD {

// Edge-aware spatial denoiser for single-channel ray traced results. Runs two ping-pong passes through a temporary texture.
class RTDenoise {
public:
	void initialize();
	void finalize();

	// p_value is an R8 storage texture (or slice) that is filtered in place. p_radius is in pixels.
	void denoise(RID p_value, RID p_depth_texture, RID p_normal_buffer, const Projection &p_inv_view_projection, Vector3 p_camera_position, uint32_t p_radius);

private:
	struct Params {
		float inv_view_projection[16];
		float camera_position[4];
		float settings[4];
	};

	void _dispatch(RID p_source, RID p_dest, Size2i p_size, RID p_depth_texture, RID p_normal_buffer);

	RtDenoiseShaderRD shader;
	RID shader_version;
	PipelineDeferredRD pipeline;
	RID params_buffer;
	RID nearest_sampler;
	RID temp_texture;
	Size2i temp_size;
	bool initialized = false;
};

} // namespace RendererRD
