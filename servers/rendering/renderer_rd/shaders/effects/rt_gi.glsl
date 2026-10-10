#[compute]

#version 460

#VERSION_DEFINES

#extension GL_EXT_ray_query : enable

// Hardware ray traced diffuse global illumination (one bounce). Replaces the screen-space indirect lighting pass.
// Each pixel traces cosine-weighted rays against the scene TLAS. The hit point is reprojected into the previous frame's
// lit color, so the bounce light is reused without shading hit surfaces. Output uses the SSIL layout:
// rgb = indirect irradiance, a = occlusion. The scene multiplies rgb by albedo and leaves occlusion at zero.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_depth; // Nearest sampler.
layout(rgba8, set = 0, binding = 1) uniform restrict readonly image2D source_normal_roughness;
layout(set = 0, binding = 2) uniform sampler2D last_frame_color; // Linear sampler.
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image2D dest_gi;
layout(set = 0, binding = 4) uniform accelerationStructureEXT scene_tlas;
layout(set = 0, binding = 6) uniform sampler2D history_color; // Linear sampler. rgb = accumulated irradiance, a = sample count.
layout(rgba16f, set = 0, binding = 7) uniform restrict writeonly image2D history_out;

layout(set = 0, binding = 5, std140) uniform Params {
	mat4 inv_view_projection;
	mat4 view_to_world;
	mat4 prev_view_projection;
	vec4 settings; // x = max ray distance, y = intensity, z = unused, w = unused
	uvec4 frame_data; // x = frame index, y = samples per pixel, z = history length cap
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
	ivec2 size = imageSize(dest_gi);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}

	// The target may be half size, so every input is sampled by UV rather than by texel position.
	vec2 uv = (vec2(pos) + 0.5) / vec2(size);

	float depth = textureLod(source_depth, uv, 0.0).r;
	if (depth <= 0.0 || depth >= 1.0) {
		imageStore(dest_gi, pos, vec4(0.0));
		return;
	}

	vec4 world_h = params.inv_view_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec3 world_pos = world_h.xyz / world_h.w;

	ivec2 normal_size = imageSize(source_normal_roughness);
	ivec2 normal_pos = clamp(ivec2(uv * vec2(normal_size)), ivec2(0), normal_size - ivec2(1));
	vec3 normal_view = imageLoad(source_normal_roughness, normal_pos).xyz * 2.0 - 1.0;
	if (dot(normal_view, normal_view) < 0.5) {
		imageStore(dest_gi, pos, vec4(0.0));
		return;
	}
	normal_view = normalize(normal_view);
	vec3 normal_world = normalize((params.view_to_world * vec4(normal_view, 0.0)).xyz);

	const float max_distance = params.settings.x;
	const float intensity = params.settings.y;
	const uint sample_count = max(params.frame_data.y, 1u);
	const uint pixel_seed = uint(pos.x) + uint(pos.y) * uint(size.x) + params.frame_data.x * uint(size.x) * uint(size.y);

	vec3 irradiance = vec3(0.0);
	for (uint i = 0; i < sample_count; i++) {
		vec2 u = vec2(rand_float(pixel_seed * 4u + i * 2u), rand_float(pixel_seed * 4u + i * 2u + 1u));
		vec3 dir = cosine_hemisphere(u, normal_world);
		vec3 origin = world_pos + normal_world * 0.02;

		rayQueryEXT rq;
		rayQueryInitializeEXT(rq, scene_tlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.02, dir, max_distance);
		while (rayQueryProceedEXT(rq)) {
		}
		if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
			// Rays that escape the scene are left out. The ambient term already covers the sky.
			continue;
		}

		float hit_t = rayQueryGetIntersectionTEXT(rq, true);
		vec3 hit_pos = origin + dir * hit_t;
		vec4 prev_clip = params.prev_view_projection * vec4(hit_pos, 1.0);
		if (prev_clip.w <= 0.0) {
			continue;
		}
		vec2 prev_uv = (prev_clip.xy / prev_clip.w) * 0.5 + 0.5;
		if (any(lessThan(prev_uv, vec2(0.0))) || any(greaterThan(prev_uv, vec2(1.0)))) {
			continue;
		}
		// With a cosine-weighted pdf the cosine term cancels, so the bounce radiance is added directly.
		irradiance += textureLod(last_frame_color, prev_uv, 0.0).rgb;
	}

	irradiance *= intensity / float(sample_count);
	// Temporal accumulation: reproject this pixel's surface into last frame's history and take a running average.
	// Samples are counted in the history alpha, so the weight drops as the history gets longer.
	float history_count = 0.0;
	vec3 accumulated = irradiance;
	vec4 prev_clip = params.prev_view_projection * vec4(world_pos, 1.0);
	if (prev_clip.w > 0.0) {
		vec2 history_uv = (prev_clip.xy / prev_clip.w) * 0.5 + 0.5;
		if (all(greaterThanEqual(history_uv, vec2(0.0))) && all(lessThanEqual(history_uv, vec2(1.0)))) {
			vec4 history = textureLod(history_color, history_uv, 0.0);
			if (history.a > 0.0) {
				history_count = min(history.a, float(params.frame_data.z) - 1.0);
				accumulated = mix(history.rgb, irradiance, 1.0 / (history_count + 1.0));
				history_count += 1.0;
			}
		}
	}
	if (history_count == 0.0) {
		history_count = 1.0;
	}

	imageStore(history_out, pos, vec4(accumulated, history_count));
	imageStore(dest_gi, pos, vec4(accumulated, 0.0));
}
