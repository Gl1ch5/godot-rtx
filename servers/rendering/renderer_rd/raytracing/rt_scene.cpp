/**************************************************************************/
/*  rt_scene.h                                                           */
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
#include "rt_scene.h"

#include "core/config/project_settings.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"

namespace RendererRD {

bool RTScene::is_supported() {
	RenderingDevice *rd = RenderingDevice::get_singleton();
	return rd && rd->has_feature(RenderingDevice::SUPPORTS_RAY_QUERY) && GLOBAL_GET("rendering/ray_tracing/enabled");
}

void RTScene::update(const PagedArray<RenderGeometryInstance *> &p_instances) {
	frame++;

	MeshStorage *mesh_storage = MeshStorage::get_singleton();
	ERR_FAIL_NULL(mesh_storage);

	LocalVector<RenderingDevice::AccelerationStructureInstance> as_instances;

	for (uint64_t i = 0; i < p_instances.size(); i++) {
		RenderGeometryInstanceBase *geometry = static_cast<RenderGeometryInstanceBase *>(p_instances[i]);
		if (!geometry->data || geometry->data->base_type != RSE::INSTANCE_MESH) {
			continue;
		}

		// Skinned meshes need per-frame vertex skinning, which the static BLAS path does not support yet.
		if (geometry->data->skeleton.is_valid()) {
			continue;
		}

		RID mesh = geometry->data->base;
		int surface_count = mesh_storage->mesh_get_surface_count(mesh);
		for (int s = 0; s < surface_count; s++) {
			void *surface = mesh_storage->mesh_get_surface(mesh, uint32_t(s));
			if (!surface || mesh_storage->mesh_surface_get_primitive(surface) != RSE::PRIMITIVE_TRIANGLES) {
				continue;
			}

			// Only plain float3 positions are supported. Compressed and 2D vertex layouts are skipped.
			uint64_t format = mesh_storage->mesh_surface_get_format(surface);
			if (format & (RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES | RSE::ARRAY_FLAG_USE_2D_VERTICES)) {
				continue;
			}

			RID index_buffer = mesh_storage->mesh_surface_get_index_buffer_rd_rid(mesh, s);
			if (index_buffer.is_null()) {
				continue;
			}

			RID vertex_buffer = mesh_storage->mesh_surface_get_vertex_buffer_rd_rid(mesh, s);
			uint32_t vertex_count = mesh_storage->mesh_surface_get_vertex_count(surface);
			uint32_t index_count = mesh_storage->mesh_surface_get_vertices_drawn_count(surface);
			if (vertex_buffer.is_null() || vertex_count == 0 || index_count == 0) {
				continue;
			}

			RID blas = _get_or_create_blas(vertex_buffer, index_buffer, vertex_count, index_count);
			if (blas.is_null()) {
				continue;
			}

			RenderingDevice::AccelerationStructureInstance as_instance;
			as_instance.transform = geometry->transform;
			as_instance.id = as_instances.size();
			as_instance.blas = blas;
			// Ray queries never read the hit shader binding table, but tlas_build rejects a zero range.
			// Use a non-zero placeholder (one group at offset 0).
			as_instance.hit_sbt_range = RenderingDevice::HitShaderBindingTableRange(1) << 32;
			as_instances.push_back(as_instance);
		}
	}

	_ensure_tlas(as_instances.size());
	if (instance_count > 0) {
		RenderingDevice::get_singleton()->tlas_build(tlas, as_instances);
	}

	_cleanup_blas();
}

RID RTScene::_get_or_create_blas(RID p_vertex_buffer, RID p_index_buffer, uint32_t p_vertex_count, uint32_t p_index_count) {
	BLASKey key;
	key.vertex_buffer = p_vertex_buffer;
	key.index_buffer = p_index_buffer;

	RenderingDevice *rd = RenderingDevice::get_singleton();
	ERR_FAIL_NULL_V(rd, RID());

	BLASEntry *entry = blas_cache.getptr(key);
	if (entry) {
		// The BLAS is freed implicitly when its vertex or index buffer is freed, so the entry may be stale.
		if (rd->acceleration_structure_is_valid(entry->blas)) {
			entry->last_used_frame = frame;
			return entry->blas;
		}
		blas_cache.erase(key);
	}

	RenderingDevice::AccelerationStructureGeometry geometry;
	geometry.flags = RenderingDevice::ACCELERATION_STRUCTURE_GEOMETRY_OPAQUE_BIT;
	geometry.vertex_buffer = p_vertex_buffer;
	geometry.vertex_offset = 0;
	geometry.vertex_stride = sizeof(float) * 3;
	geometry.vertex_count = p_vertex_count;
	geometry.vertex_format = RenderingDevice::DATA_FORMAT_R32G32B32_SFLOAT;
	geometry.index_buffer = p_index_buffer;
	geometry.index_offset = 0;
	geometry.index_count = p_index_count;

	RID blas = rd->blas_create(Span<RenderingDevice::AccelerationStructureGeometry>(&geometry, 1), RenderingDevice::ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT);
	ERR_FAIL_COND_V_MSG(blas.is_null(), RID(), "Failed to create a bottom level acceleration structure for a mesh surface.");

	if (rd->blas_build(blas) != OK) {
		rd->free_rid(blas);
		ERR_FAIL_V_MSG(RID(), "Failed to build a bottom level acceleration structure for a mesh surface.");
	}

	blas_cache.insert(key, BLASEntry{ blas, frame });
	return blas;
}

void RTScene::_ensure_tlas(uint32_t p_instance_count) {
	RenderingDevice *rd = RenderingDevice::get_singleton();
	ERR_FAIL_NULL(rd);

	instance_count = p_instance_count;

	if (p_instance_count == 0) {
		if (tlas.is_valid() && rd->acceleration_structure_is_valid(tlas)) {
			rd->free_rid(tlas);
			tlas = RID();
			tlas_capacity = 0;
		}
		return;
	}

	if (tlas.is_valid() && rd->acceleration_structure_is_valid(tlas) && p_instance_count <= tlas_capacity) {
		return;
	}

	if (tlas.is_valid() && rd->acceleration_structure_is_valid(tlas)) {
		rd->free_rid(tlas);
	}

	// Leave headroom so the TLAS doesn't need to be recreated every time an instance is added.
	tlas_capacity = MAX(p_instance_count * 2, 64u);
	tlas = rd->tlas_create(tlas_capacity, RenderingDevice::ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT);
	ERR_FAIL_COND_MSG(tlas.is_null(), "Failed to create the top level acceleration structure.");
}

void RTScene::_cleanup_blas() {
	RenderingDevice *rd = RenderingDevice::get_singleton();
	ERR_FAIL_NULL(rd);

	LocalVector<BLASKey> stale_keys;
	for (const KeyValue<BLASKey, BLASEntry> &E : blas_cache) {
		if (frame - E.value.last_used_frame > BLAS_MAX_UNUSED_FRAMES) {
			stale_keys.push_back(E.key);
		}
	}

	for (const BLASKey &key : stale_keys) {
		rd->free_rid(blas_cache[key].blas);
		blas_cache.erase(key);
	}
}

void RTScene::finalize() {
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (!rd) {
		return;
	}

	// Acceleration structures may already be gone if their buffers were freed first, so check before freeing.
	for (const KeyValue<BLASKey, BLASEntry> &E : blas_cache) {
		if (rd->acceleration_structure_is_valid(E.value.blas)) {
			rd->free_rid(E.value.blas);
		}
	}
	blas_cache.clear();

	if (tlas.is_valid() && rd->acceleration_structure_is_valid(tlas)) {
		rd->free_rid(tlas);
	}
	tlas = RID();
	tlas_capacity = 0;
	instance_count = 0;
}

} // namespace RendererRD
