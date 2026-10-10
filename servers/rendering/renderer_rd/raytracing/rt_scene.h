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

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/paged_array.h"
#include "servers/rendering/renderer_geometry_instance.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// Owns the hardware ray tracing acceleration structures for the scene.
// Keeps one cached BLAS per mesh surface (vertex buffer + index buffer pair)
// and rebuilds a single TLAS each frame from the visible geometry instances.
class RTScene {
public:
	// True when ray tracing is enabled in the project settings and the device supports ray queries.
	static bool is_supported();

	// Rebuilds the TLAS for the given instances, creating BLAS for any new mesh surfaces.
	void update(const PagedArray<RenderGeometryInstance *> &p_instances);

	// Releases all acceleration structures. Must be called before the RenderingDevice is destroyed.
	void finalize();

	RID get_tlas() const { return tlas; }
	uint32_t get_instance_count() const { return instance_count; }

private:
	struct BLASKey {
		RID vertex_buffer;
		RID index_buffer;

		bool operator==(const BLASKey &p_other) const {
			return vertex_buffer == p_other.vertex_buffer && index_buffer == p_other.index_buffer;
		}
	};

	struct BLASKeyHasher {
		static uint32_t hash(const BLASKey &p_key) {
			return hash_murmur3_one_64(p_key.vertex_buffer.get_id(), hash_murmur3_one_64(p_key.index_buffer.get_id()));
		}
	};

	struct BLASEntry {
		RID blas;
		uint64_t last_used_frame = 0;
	};

	// BLAS not referenced for this many frames are freed.
	static constexpr uint64_t BLAS_MAX_UNUSED_FRAMES = 120;

	HashMap<BLASKey, BLASEntry, BLASKeyHasher> blas_cache;

	RID tlas;
	uint32_t tlas_capacity = 0;
	uint32_t instance_count = 0;
	uint64_t frame = 0;

	RID _get_or_create_blas(RID p_vertex_buffer, RID p_index_buffer, uint32_t p_vertex_count, uint32_t p_index_count);
	void _ensure_tlas(uint32_t p_instance_count);
	void _cleanup_blas();
};

} // namespace RendererRD
