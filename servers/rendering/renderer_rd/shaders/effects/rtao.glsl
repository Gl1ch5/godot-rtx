#[compute]

#version 460

#VERSION_DEFINES

#extension GL_EXT_ray_query : enable

// Hardware ray traced ambient occlusion. Writes the same R8 output as the screen-space SSAO path,
// so the scene shaders consume it without changes.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_depth;
layout(rgba8, set = 0, binding = 1) uniform restrict readonly image2D source_normal;
layout(r8, set = 0, binding = 2) uniform restrict writeonly image2D dest_ao;
layout(set = 0, binding = 3) uniform accelerationStructureEXT scene_tlas;

layout(set = 0, binding = 4, std140) uniform Params {
	mat4 inv_view_projection;
	mat4 view_to_world;
	vec4 settings; // x = radius, y = intensity, z = power, w = unused
	uvec4 frame_data; // x = frame index, y = samples per pixel
}
params;

uint hash_uint(uint p_x) {
	p_x ^= p_x >> 16;
	p_x *= 0x7feb352dU;
	p_x ^= p_x >> 15;
	p_x *= 0x846ca68bU;
	p_x ^= p_x >> 16;
	return p_x;
}

float rand_float(uint p_seed) {
	return float(hash_uint(p_seed)) * (1.0 / 4294967296.0);
}

vec3 cosine_hemisphere(vec2 p_u, vec3 p_n) {
	vec3 up = abs(p_n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, p_n));
	vec3 bitangent = cross(p_n, tangent);
	float r = sqrt(p_u.x);
	float phi = 6.2831853 * p_u.y;
	vec3 local_dir = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - p_u.x)));
	return normalize(tangent * local_dir.x + bitangent * local_dir.y + p_n * local_dir.z);
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(dest_ao);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}

	// Reverse-Z or standard-Z: the far plane sits at one of the depth extremes, so skip both.
	float depth = texelFetch(source_depth, pos, 0).r;
	if (depth <= 0.0 || depth >= 1.0) {
		imageStore(dest_ao, pos, vec4(1.0));
		return;
	}

	vec2 uv = (vec2(pos) + 0.5) / vec2(size);
	vec4 world_h = params.inv_view_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec3 world_pos = world_h.xyz / world_h.w;

	vec4 encoded_normal = imageLoad(source_normal, pos);
	vec3 normal_view = encoded_normal.xyz * 2.0 - 1.0;
	if (dot(normal_view, normal_view) < 0.5) {
		imageStore(dest_ao, pos, vec4(1.0));
		return;
	}
	normal_view = normalize(normal_view);
	normal_view.z = -normal_view.z;
	vec3 normal_world = normalize((params.view_to_world * vec4(normal_view, 0.0)).xyz);

	const float radius = params.settings.x;
	const float intensity = params.settings.y;
	const float power = params.settings.z;
	const uint sample_count = max(params.frame_data.y, 1u);
	const uint pixel_seed = uint(pos.x) + uint(pos.y) * uint(size.x) + params.frame_data.x * uint(size.x) * uint(size.y);

	float occlusion = 0.0;
	for (uint i = 0; i < sample_count; i++) {
		vec2 u = vec2(rand_float(pixel_seed * 4u + i * 2u), rand_float(pixel_seed * 4u + i * 2u + 1u));
		vec3 dir = cosine_hemisphere(u, normal_world);
		// Offset along the normal and start the ray slightly away to avoid self-intersection on the source surface.
		vec3 origin = world_pos + normal_world * max(radius * 0.02, 0.02);

		rayQueryEXT rq;
		rayQueryInitializeEXT(rq, scene_tlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.02, dir, radius);
		while (rayQueryProceedEXT(rq)) {
		}
		if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
			occlusion += 1.0;
		}
	}

	float visibility = 1.0 - clamp(occlusion / float(sample_count) * intensity, 0.0, 1.0);
	visibility = pow(visibility, power);
	imageStore(dest_ao, pos, vec4(visibility, 0.0, 0.0, 0.0));
}
