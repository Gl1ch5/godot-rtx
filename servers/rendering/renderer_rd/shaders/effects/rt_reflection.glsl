#[compute]

#version 460

#VERSION_DEFINES

#extension GL_EXT_ray_query : enable

// Hardware ray traced reflections. Each reflection ray is traced against the scene TLAS. The hit point is
// reprojected into the previous frame's lit color, so the result can be used without shading hit surfaces.
// Output uses the same layout as the screen-space reflection target: rgb = radiance, a = blend weight.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_depth; // Nearest sampler.
layout(rgba8, set = 0, binding = 1) uniform restrict readonly image2D source_normal_roughness;
layout(set = 0, binding = 2) uniform sampler2D last_frame_color; // Linear sampler.
layout(rgba16f, set = 0, binding = 3) uniform restrict writeonly image2D dest_reflection;
layout(set = 0, binding = 4) uniform accelerationStructureEXT scene_tlas;

layout(set = 0, binding = 5, std140) uniform Params {
	mat4 inv_view_projection;
	mat4 view_to_world;
	mat4 prev_view_projection;
	vec4 settings; // x = max ray distance, y = roughness cutoff, z = unused, w = unused
}
params;

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(dest_reflection);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}

	// The target may be half size, so every input is sampled by UV rather than by texel position.
	vec2 uv = (vec2(pos) + 0.5) / vec2(size);

	float depth = textureLod(source_depth, uv, 0.0).r;
	if (depth <= 0.0 || depth >= 1.0) {
		imageStore(dest_reflection, pos, vec4(0.0));
		return;
	}

	vec4 world_h = params.inv_view_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec3 world_pos = world_h.xyz / world_h.w;

	// The normal buffer stores the view space normal, the same convention the screen-space reflection shader uses.
	ivec2 normal_size = imageSize(source_normal_roughness);
	ivec2 normal_pos = clamp(ivec2(uv * vec2(normal_size)), ivec2(0), normal_size - ivec2(1));
	vec4 normal_roughness = imageLoad(source_normal_roughness, normal_pos);
	vec3 normal_view = normal_roughness.xyz * 2.0 - 1.0;
	if (dot(normal_view, normal_view) < 0.5) {
		imageStore(dest_reflection, pos, vec4(0.0));
		return;
	}
	normal_view = normalize(normal_view);
	vec3 normal_world = normalize((params.view_to_world * vec4(normal_view, 0.0)).xyz);

	// Same roughness encoding as the screen-space reflection shader.
	float roughness = normal_roughness.w;
	if (roughness > 0.5) {
		roughness = 1.0 - roughness;
	}
	roughness /= (127.0 / 255.0);
	const float roughness_cutoff = params.settings.y;
	if (roughness >= roughness_cutoff) {
		imageStore(dest_reflection, pos, vec4(0.0));
		return;
	}

	vec3 cam_pos = params.view_to_world[3].xyz;
	vec3 view_dir = normalize(world_pos - cam_pos);
	vec3 refl_dir = reflect(view_dir, normal_world);
	// Start the ray slightly off the surface to avoid self-intersection.
	vec3 origin = world_pos + normal_world * 0.02;

	rayQueryEXT rq;
	rayQueryInitializeEXT(rq, scene_tlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.02, refl_dir, params.settings.x);
	while (rayQueryProceedEXT(rq)) {
	}
	if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
		imageStore(dest_reflection, pos, vec4(0.0));
		return;
	}

	float hit_t = rayQueryGetIntersectionTEXT(rq, true);
	vec3 hit_pos = origin + refl_dir * hit_t;

	vec4 prev_clip = params.prev_view_projection * vec4(hit_pos, 1.0);
	if (prev_clip.w <= 0.0) {
		imageStore(dest_reflection, pos, vec4(0.0));
		return;
	}
	vec2 prev_uv = (prev_clip.xy / prev_clip.w) * 0.5 + 0.5;
	if (any(lessThan(prev_uv, vec2(0.0))) || any(greaterThan(prev_uv, vec2(1.0)))) {
		// The hit point was not visible last frame, so there is no color to reuse.
		imageStore(dest_reflection, pos, vec4(0.0));
		return;
	}

	vec3 color = textureLod(last_frame_color, prev_uv, 0.0).rgb;
	float weight = clamp(1.0 - roughness / max(roughness_cutoff, 1e-4), 0.0, 1.0);
	imageStore(dest_reflection, pos, vec4(color, weight));
}
