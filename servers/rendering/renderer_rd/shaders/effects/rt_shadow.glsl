#[compute]

#version 460

#VERSION_DEFINES

#extension GL_EXT_ray_query : enable

// Hardware ray traced shadows for directional lights. Writes visibility (1 = lit, 0 = shadowed) into one layer of the
// contact shadow buffer. The scene multiplies directional shadows by that buffer, so the result combines with shadow maps.
// The light's angular size is sampled as a cone, which gives soft penumbrae. Each frame uses one sample per pixel.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_depth; // Nearest sampler.
layout(rgba8, set = 0, binding = 1) uniform restrict readonly image2D source_normal_roughness;
layout(r8, set = 0, binding = 2) uniform restrict writeonly image2D dest_visibility;
layout(set = 0, binding = 3) uniform accelerationStructureEXT scene_tlas;

layout(set = 0, binding = 4, std140) uniform Params {
	mat4 inv_view_projection;
	mat4 view_to_world;
	vec4 light_direction; // xyz = unit vector pointing toward the light, w = tangent of the light's angular radius
	uvec4 frame_data; // x = frame index
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

void main() {
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(dest_visibility);
	if (any(greaterThanEqual(pos, size))) {
		return;
	}

	vec2 uv = (vec2(pos) + 0.5) / vec2(size);
	float depth = textureLod(source_depth, uv, 0.0).r;
	if (depth <= 0.0 || depth >= 1.0) {
		imageStore(dest_visibility, pos, vec4(1.0));
		return;
	}

	vec4 world_h = params.inv_view_projection * vec4(uv * 2.0 - 1.0, depth, 1.0);
	vec3 world_pos = world_h.xyz / world_h.w;

	ivec2 normal_size = imageSize(source_normal_roughness);
	ivec2 normal_pos = clamp(ivec2(uv * vec2(normal_size)), ivec2(0), normal_size - ivec2(1));
	vec3 normal_view = imageLoad(source_normal_roughness, normal_pos).xyz * 2.0 - 1.0;
	if (dot(normal_view, normal_view) < 0.5) {
		imageStore(dest_visibility, pos, vec4(1.0));
		return;
	}
	normal_view = normalize(normal_view);
	vec3 normal_world = normalize((params.view_to_world * vec4(normal_view, 0.0)).xyz);

	vec3 light_dir = normalize(params.light_direction.xyz);
	float tan_radius = params.light_direction.w;

	// Sample a point on the light's disc. A zero radius gives hard shadows.
	const uint seed = uint(pos.x) + uint(pos.y) * uint(size.x) + params.frame_data.x * uint(size.x) * uint(size.y);
	vec2 u = vec2(rand_float(seed * 2u), rand_float(seed * 2u + 1u));
	vec3 tangent = normalize(cross(abs(light_dir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), light_dir));
	vec3 bitangent = cross(light_dir, tangent);
	float r = tan_radius * sqrt(u.x);
	float phi = 6.2831853 * u.y;
	vec3 dir = normalize(light_dir + tangent * (r * cos(phi)) + bitangent * (r * sin(phi)));

	// Only trace toward the light when the surface faces it. Otherwise the scene's N dot L term already removes the light.
	if (dot(normal_world, dir) <= 0.0) {
		imageStore(dest_visibility, pos, vec4(1.0));
		return;
	}

	vec3 origin = world_pos + normal_world * 0.02;

	rayQueryEXT rq;
	rayQueryInitializeEXT(rq, scene_tlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFF, origin, 0.02, dir, 1000.0);
	while (rayQueryProceedEXT(rq)) {
	}
	float visibility = rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
	imageStore(dest_visibility, pos, vec4(visibility));
}
