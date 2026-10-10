#[compute]

#version 460

#VERSION_DEFINES

// Edge-aware spatial filter for single-sample ray traced results (AO and shadows). Each tap is weighted by a Gaussian in
// screen space, by world-space distance to the center surface, and by normal similarity, so the blur does not cross edges.
// Only one-channel R8 targets are used. The pass reads one texture and writes another, so callers ping-pong between two.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_value; // Nearest sampler.
layout(set = 0, binding = 1) uniform sampler2D source_depth; // Nearest sampler.
layout(rgba8, set = 0, binding = 2) uniform restrict readonly image2D source_normal_roughness;
layout(r8, set = 0, binding = 3) uniform restrict writeonly image2D dest_value;

layout(set = 0, binding = 4, std140) uniform Params {
	mat4 inv_view_projection;
	vec4 camera_position; // xyz = world camera position
	vec4 settings; // x = blur radius in pixels, y = relative depth tolerance, z = normal sharpness, w = unused
}
params;

vec3 reconstruct_world(vec2 p_uv, float p_depth) {
	vec4 h = params.inv_view_projection * vec4(p_uv * 2.0 - 1.0, p_depth, 1.0);
	return h.xyz / h.w;
}

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(dest_value);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}

	vec2 uv = (vec2(pos) + 0.5) / vec2(size);
	float center_value = textureLod(source_value, uv, 0.0).r;
	float center_depth = textureLod(source_depth, uv, 0.0).r;
	if (center_depth <= 0.0 || center_depth >= 1.0) {
		imageStore(dest_value, pos, vec4(center_value));
		return;
	}

	vec3 center_world = reconstruct_world(uv, center_depth);
	// Tolerance scales with distance from the camera, so near and far surfaces are judged on the same relative basis.
	float depth_tolerance = params.settings.y * max(distance(center_world, params.camera_position.xyz), 1.0);

	ivec2 normal_size = imageSize(source_normal_roughness);
	vec3 center_normal = imageLoad(source_normal_roughness, clamp(ivec2(uv * vec2(normal_size)), ivec2(0), normal_size - ivec2(1))).xyz * 2.0 - 1.0;
	center_normal = normalize(center_normal);

	const int radius = int(max(params.settings.x, 1.0));
	const float sigma = max(float(radius) * 0.5, 0.5);
	float sum = 0.0;
	float weight_sum = 0.0;
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius; x <= radius; x++) {
			ivec2 tap = clamp(pos + ivec2(x, y), ivec2(0), size - ivec2(1));
			vec2 tap_uv = (vec2(tap) + 0.5) / vec2(size);
			float tap_depth = textureLod(source_depth, tap_uv, 0.0).r;
			if (tap_depth <= 0.0 || tap_depth >= 1.0) {
				continue;
			}
			float tap_value = textureLod(source_value, tap_uv, 0.0).r;

			float spatial = exp(-float(x * x + y * y) / (2.0 * sigma * sigma));

			vec3 tap_world = reconstruct_world(tap_uv, tap_depth);
			float depth_diff = distance(tap_world, center_world) / max(depth_tolerance, 1e-6);
			float depth_weight = exp(-depth_diff * depth_diff);

			vec3 tap_normal = imageLoad(source_normal_roughness, clamp(ivec2(tap_uv * vec2(normal_size)), ivec2(0), normal_size - ivec2(1))).xyz * 2.0 - 1.0;
			if (dot(tap_normal, tap_normal) < 0.5) {
				continue;
			}
			tap_normal = normalize(tap_normal);
			float normal_weight = pow(max(dot(center_normal, tap_normal), 0.0), params.settings.z);

			float w = spatial * depth_weight * normal_weight;
			sum += tap_value * w;
			weight_sum += w;
		}
	}

	float result = weight_sum > 1e-6 ? sum / weight_sum : center_value;
	imageStore(dest_value, pos, vec4(result));
}
