#include <renderer/submission_state.h>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <renderer/level_renderer.h>
#include <renderer/backdrop.h>
#include <renderer/transparent_order.h>
#include <openrayman/game/profiler.h>

#include <openrayman/gf_texture.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <tuple>

namespace openrayman
{
	namespace
	{
		// Every draw goes through here, so the profiler can count calls and triangles.
		void draw_triangles(GLint first, GLsizei vertices)
		{
			submission::DrawArrays(GL_TRIANGLES, first, vertices);
			game::profiler::add(game::profiler::counter::triangles, std::uint64_t(vertices / 3));
		}

		const char* vertex_shader = R"(
			layout(location = 0) in vec3 a_position;
			layout(location = 1) in vec2 a_uv;
			layout(location = 2) in vec3 a_normal;
			layout(location = 3) in vec4 a_uv_bounds;
			layout(location = 4) in vec4 a_color; // the game's lights summed, worked out at load (static geometry)
			layout(location = 5) in vec4 a_cull;  // xyz: the object's bounding-sphere centre; w 1 when known
			uniform float u_fog_infinite;
			uniform mat4 u_view_projection;
			uniform mat4 u_model;
			uniform mat4 u_fog_model; // undo the renderer-only sky stretch for the game's distance fog
			uniform vec3 u_camera;
			uniform vec3 u_camera_forward; // fog goes by depth along the view, like the game's (Glide's W)
			// 0: a soft stand-in light from above. 1: the game's lights, from a_color. 2: the game's
			// lights, from the u_light_* list below (actors, which move between lights). 3: unlit.
			uniform int u_lighting;
			const int max_lights = 8;
			uniform int u_light_count;
			uniform int u_light_kind[max_lights];    // 0 ambient, 1 parallel, 2 spherical, 3 spherical painting
			uniform vec4 u_material_ambient, u_material_diffuse; // the surface's own colours and alpha
			// The game's luminosity setting, as an offset from its middle (0.5): it brightens surfaces
			// and strengthens the lights on them.
			uniform float u_luminosity;
			vec4 lit(vec4 sum)
			{
				// As the game turns light into vertex colours: doubled, then clamped, so a surface is
				// fully lit at half and never brighter than its texture.
				vec3 color = clamp(2.0 * (u_luminosity + u_material_ambient.rgb + sum.rgb * (u_luminosity + u_material_diffuse.rgb)), 0.0, 1.0);
				// Lights also make surfaces more or less see-through (water, the waterfall's edges).
				float alpha = clamp(2.0 * (u_material_ambient.a + sum.a * u_material_diffuse.a), 0.0, 1.0);
				return vec4(color, alpha);
			}
			uniform vec3 u_light_vector[max_lights]; // parallel: the way it shines; spherical: its position
			uniform vec4 u_light_color[max_lights];
			uniform vec2 u_light_range[max_lights];  // spherical: near, far
			out vec2 v_uv;
			out vec3 v_local_position;
			out vec3 v_world_position;
			flat out vec4 v_uv_bounds;
			out vec4 v_light;
			out float v_distance;
			out vec3 v_sky_direction;
			uniform vec3 u_sky_eye;
			flat out float v_culled;
			void main()
			{
				vec4 world = u_model * vec4(a_position, 1.0);
				vec3 normal = normalize(mat3(u_model) * a_normal);
				if(u_lighting == 3)
					v_light = vec4(1.0);
				else if(u_lighting == 4)
					v_light = u_material_diffuse;
				else if(u_lighting == 5)
					v_light = a_color; // as given: particle tints, collision overlay and backdrop
				else if(u_lighting == 1)
					v_light = lit(a_color);
				else if(u_lighting == 2)
				{
					vec4 sum = a_color; // saved vertex colours initialize the light sum (0x42AAE0)
					for(int i = 0; i < u_light_count; i++)
					{
						float strength = 1.0;
						if(u_light_kind[i] == 1)
							strength = max(0.0, -dot(normal, u_light_vector[i]));
						else if(u_light_kind[i] >= 2)
						{
							vec3 to = u_light_vector[i] - world.xyz;
							float d = length(to);
							vec2 range = u_light_range[i];
							vec2 range2 = range * range;
							float falloff = d <= range.x || range2.y <= range2.x ? 1.0 : clamp((range2.y - d * d) / (range2.y - range2.x), 0.0, 1.0);
							strength = falloff * (d > 1e-5 && u_light_kind[i] == 2 ? max(0.0, dot(normal, to / d)) : 1.0);
						}
						sum += u_light_color[i] * strength;
					}
					v_light = lit(sum);
				}
				else
				{
					// Stand-in lighting, to compare with: a soft light from above, so shapes are readable.
					vec3 sun = normalize(vec3(0.3, 0.2, 0.9));
					v_light = vec4(vec3(0.6 + 0.4 * abs(dot(normal, sun))), 1.0);
				}
				v_uv = a_uv;
				v_local_position = a_position;
				v_world_position = world.xyz;
				v_uv_bounds = a_uv_bounds;
				vec3 fog_world = (u_fog_model * world).xyz;
				v_distance = dot(fog_world - u_camera, u_camera_forward);
				v_sky_direction = world.xyz - u_sky_eye;
				// 0x42AF70: with a fog on, an object whose bounding-sphere centre lies at or past the
				// fog's infinite distance from the camera isn't drawn at all (straight-line distance).
				v_culled = 0.0;
				if(u_fog_infinite > 0.0 && a_cull.w > 0.5)
					v_culled = distance((u_fog_model * u_model * vec4(a_cull.xyz, 1.0)).xyz, u_camera) >= u_fog_infinite ? 1.0 : 0.0;
				gl_Position = u_view_projection * world;
			}
		)";

		// Hard-edged alpha (leaves, ferns) is cut out; soft alpha is blended in a second pass.
		const char* fragment_shader = R"(
			in vec2 v_uv;
			in vec3 v_local_position;
			in vec3 v_world_position;
			uniform vec3 u_camera;
			in vec4 v_light;
			in float v_distance;
			in vec3 v_sky_direction;
			flat in float v_culled;
			flat in vec4 v_uv_bounds;
			out vec4 o_color;
			uniform sampler2D u_texture;
			uniform float u_alpha_cut;
			uniform float u_edge_clamp;
			uniform float u_lightning;
			uniform float u_sky_haze;
			uniform vec3 u_sky_haze_color;
			uniform vec2 u_uv_offset; // scrolling textures (water, mist, clouds)
			uniform vec4 u_well; // star depth: enabled, depth scale, slow drift u, v; zero for all other surfaces
			uniform vec2 u_lake; // the star lake's ripples: enabled, seconds
			uniform float u_twinkle; // the star layers' stars twinkle (Learn_10's well)
			uniform vec4 u_falls; // the Hall's river below the waterfall: enabled, the fall's foot (x, y), seconds
			uniform vec2 u_star_floor; // Hall of Doors: the star field's lowest height, texture tiles per world unit (0: follow the mesh)
			// Ours (docs/IMPROVEMENTS.md): the star texture of the portal well and the Hall of Doors lake
			// is seen as layers lying below the surface. Each pixel follows the view ray through the
			// surface down to each layer, so the stars slide against the camera as it moves, like deep
			// space seen through a hole. The texture coordinates' change per world unit comes from the
			// screen derivatives, so it works on any surface (the well's floor and walls alike).
			// The lake's surface: a sideways bend of the view (xy, in texture units) and a brightness factor.
			vec3 lake_ripple(vec2 uv)
			{
				float t = u_lake.y;
				vec2 p = uv * 6.2831853;
				float a = sin(dot(p, vec2(3.0, 2.0)) + t * 0.9) + 0.6 * sin(dot(p, vec2(-2.0, 5.0)) + t * 1.3);
				float b = sin(dot(p, vec2(-4.0, 1.0)) + t * 0.7) + 0.6 * sin(dot(p, vec2(1.0, -6.0)) + t * 1.1);
				float shimmer = 1.0 + 0.1 * sin(dot(p, vec2(5.0, 3.0)) - t * 1.6) * sin(dot(p, vec2(-3.0, 4.0)) + t * 1.2);
				return vec3(vec2(a, b) * 0.006, shimmer);
			}
			// A smooth random phase across the texture (value noise, no cell edges), so each star
			// twinkles on its own; callers apply it only to bright texels (the stars), the dark sky stays still.
			float twinkle_at(vec2 at, float t)
			{
				vec2 g = at * 12.0, i = floor(g), f = fract(g);
				f = f * f * (3.0 - 2.0 * f);
				float h00 = fract(sin(dot(i, vec2(12.9898, 78.233))) * 43758.5453);
				float h10 = fract(sin(dot(i + vec2(1.0, 0.0), vec2(12.9898, 78.233))) * 43758.5453);
				float h01 = fract(sin(dot(i + vec2(0.0, 1.0), vec2(12.9898, 78.233))) * 43758.5453);
				float h11 = fract(sin(dot(i + vec2(1.0, 1.0), vec2(12.9898, 78.233))) * 43758.5453);
				float seed = mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
				return 0.7 + 0.5 * sin(t * 1.8 + seed * 12.566);
			}
			// The star texture without its repeating pattern: each tile of it gets its own random shift,
			// quarter-turn and mirroring, and the four tiles around a point are faded into each other
			// near their edges. Taking the brightest of them keeps the stars bright (the sky between is
			// black), so the joins don't show.
			vec4 scrambled(vec2 st, vec2 dx, vec2 dy)
			{
				vec2 cell = floor(st), f = fract(st);
				vec4 best = vec4(0.0);
				for(int j = 0; j < 4; j++)
				{
					vec2 corner = vec2(float(j & 1), float(j >> 1));
					vec2 c = cell + corner;
					float h1 = fract(sin(dot(c, vec2(127.1, 311.7))) * 43758.5453);
					float h2 = fract(sin(dot(c, vec2(269.5, 183.3))) * 43758.5453);
					float h3 = fract(sin(dot(c, vec2(419.2, 371.9))) * 43758.5453);
					int turn = int(h3 * 4.0);
					mat2 r = turn == 0 ? mat2(1.0, 0.0, 0.0, 1.0) : turn == 1 ? mat2(0.0, 1.0, -1.0, 0.0)
						: turn == 2 ? mat2(-1.0, 0.0, 0.0, -1.0) : mat2(0.0, -1.0, 1.0, 0.0);
					if(h1 > 0.5)
						r = r * mat2(-1.0, 0.0, 0.0, 1.0);
					// Each corner's tile owns the square of one tile around that corner point, at full
					// strength, and fades out only in a narrow band at its borders (half-way lines).
					vec2 w2 = mix(1.0 - f, f, corner);
					float w = smoothstep(0.4, 0.6, w2.x) * smoothstep(0.4, 0.6, w2.y);
					vec4 s = textureGrad(u_texture, r * st + vec2(h1, h2), r * dx, r * dy);
					best = max(best, s * w);
				}
				return best;
			}
			// One star layer's colour, twinkling when u_twinkle is on.
			vec3 star_layer(vec2 st, vec2 dx, vec2 dy)
			{
				vec3 c = scrambled(st, dx, dy).rgb;
				if(u_twinkle > 0.5)
					c *= mix(1.0, twinkle_at(st, u_lake.y), smoothstep(0.25, 0.7, max(c.r, max(c.g, c.b))));
				return c;
			}
			// The overlay's stars twinkle each on its own, and a soft band of light glides slowly over
			// the water: a few cells of the texture brighten together as it passes.
			vec4 lake_overlay(vec2 uv, vec2 uv_dx, vec2 uv_dy)
			{
				vec3 r = lake_ripple(uv);
				vec2 at = uv + r.xy;
				vec4 texel = scrambled(at, uv_dx, uv_dy);
				float t = u_lake.y;
				float twinkle = twinkle_at(at, t);
				vec2 p = at * 6.2831853;
				float sheen = pow(max(0.0, sin(dot(p, vec2(0.7, 0.4)) - t * 0.35)), 12.0) * 0.6;
				float star = smoothstep(0.25, 0.7, max(texel.r, max(texel.g, texel.b)));
				float glow = mix(1.0, max(twinkle + sheen, 0.0), star) * r.z;
				return vec4(texel.rgb * glow, texel.a * clamp(glow, 0.0, 1.0));
			}
			vec3 star_depth(vec2 uv, vec2 uv_dx, vec2 uv_dy)
			{
				vec3 dp1 = dFdx(v_world_position), dp2 = dFdy(v_world_position);
				vec3 n = cross(dp1, dp2);
				if(dot(n, n) < 1e-12)
					return textureGrad(u_texture, uv, uv_dx, uv_dy).rgb;
				n = normalize(n);
				vec3 view = normalize(v_world_position - u_camera);
				if(dot(n, view) > 0.0)
					n = -n; // facing the camera
				float det = dot(n, cross(dp1, dp2));
				vec3 to_u = (cross(dp2, n) * uv_dx.x + cross(n, dp1) * uv_dy.x) / det;
				vec3 to_v = (cross(dp2, n) * uv_dx.y + cross(n, dp1) * uv_dy.y) / det;
				float slant = max(dot(-view, n), 0.2);
				const vec3 depth = vec3(2.0, 8.0, 25.0);   // world units below the surface
				const vec3 scale = vec3(1.0, 1.45, 2.1);   // farther layers: finer stars
				const vec3 weight = vec3(1.0, 0.6, 0.45);
				// The Hall of Doors lake (u_lake.x): slow ripples bend the view down to the stars, the
				// deeper layers more, and the surface shimmers faintly. Nothing flows; it moves in place.
				vec2 ripple = vec2(0.0);
				float shimmer = 1.0;
				if(u_lake.x > 0.5)
				{
					vec3 r = lake_ripple(uv);
					ripple = r.xy;
					shimmer = r.z;
				}
				vec3 sum = vec3(0.0);
				if(u_star_floor.y > 0.0)
				{
					// The Hall's star field is a bowl whose sides curve up into view. Instead of its
					// surface, every pixel of it is a window: the view ray goes on down to flat star
					// planes below the bowl's lowest point, which reach as far as the eye can see.
					for(int i = 0; i < 3; i++)
					{
						float plane = u_star_floor.x - depth[i] * u_well.y;
						float down = max(-view.z, 0.03);
						vec3 hit = v_world_position + view * ((v_world_position.z - plane) / down);
						vec2 at = hit.xy * u_star_floor.y + ripple * float(i + 1);
						vec2 drift = u_well.zw * float(i);
						vec2 st = at * scale[i] + drift + u_uv_offset;
						sum += star_layer(st, dFdx(st), dFdy(st)) * weight[i];
					}
					return sum * shimmer;
				}
				for(int i = 0; i < 3; i++)
				{
					vec3 step = view * (depth[i] * u_well.y / slant);
					vec2 at = uv + vec2(dot(to_u, step), dot(to_v, step)) + ripple * float(i + 1);
					vec2 drift = u_well.zw * float(i);
					sum += star_layer(at * scale[i] + drift, uv_dx * scale[i], uv_dy * scale[i]) * weight[i];
				}
				return sum * shimmer;
			}
			uniform vec2 u_swirl; // optional pause-funnel centre: enabled, animation seconds
			uniform vec4 u_fog_color;
			uniform float u_fog_infinite;
			uniform vec4 u_fog;       // near, far, how much fog covers at near and at far (0..1)
			uniform float u_additive; // additive surfaces fade out in fog instead of taking its colour
			uniform float u_light_alpha; // see-through surfaces: the lights' alpha counts
			// Ours: screen objects (the intro's Ubisoft logo) stretch small cut-out textures over the
			// screen, and bilinear filtering smears their edges over many pixels. Where the alpha
			// jumps from one texel to the next (a hard edge in the picture) and the texture is
			// magnified, redraw the edge about one pixel wide at the filtered alpha's half-way line.
			// Soft gradients in the picture are left as they are.
			uniform float u_sharpen;
			uniform float u_part_alpha; // the actor's transparency level (procedures 11 and 12), 0..1
			float sharpened(float a, vec2 uv)
			{
				vec2 texels = fwidth(uv * vec2(textureSize(u_texture, 0)));
				float per_pixel = max(max(texels.x, texels.y), 1e-5); // texels a pixel covers
				float change = fwidth(a);
				float hard = smoothstep(0.25, 0.5, change / per_pixel) * (1.0 - smoothstep(0.75, 1.0, per_pixel));
				float w = max(change, 1e-4) * 0.75;
				return mix(a, smoothstep(0.5 - w, 0.5 + w, a), hard);
			}
			void main()
			{
				// A triangle that stays within one copy of its texture keeps half a texel inside
				// that copy's edges, so filtering doesn't bring in the opposite edge as a line.
				// From afar the smaller mipmap copies are used, whose texels are larger, so the margin
				// follows the mip level in use (rounded up, as trilinear filtering blends two levels).
				vec2 uv = v_uv;
				vec2 uv_dx = dFdx(v_uv), uv_dy = dFdy(v_uv);
				// 2: nothing outside the bounds shows (the shadow blob), instead of clamping to its edge
				// texels, which stretched the blob's middle row and column out along the actor's axes.
				if(u_edge_clamp > 1.5 && (any(lessThan(v_uv, v_uv_bounds.xy)) || any(greaterThan(v_uv, v_uv_bounds.zw))))
					discard;
				if(u_edge_clamp > 0.5)
				{
					vec2 size = vec2(textureSize(u_texture, 0));
					vec2 dx = uv_dx * size, dy = uv_dy * size;
					float lod = max(0.0, ceil(0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1e-8))));
					vec2 extent = v_uv_bounds.zw - v_uv_bounds.xy;
					vec2 half_texel = min(0.5 * exp2(lod) / size, 0.5 * extent);
					uv = clamp(uv, v_uv_bounds.xy + half_texel, v_uv_bounds.zw - half_texel);
				}
				// The level of detail comes from the unclamped coordinates, so clamping doesn't change it.
				// Scrolling textures tile seamlessly, so their offset goes on after the clamp.
				float core_fade = 1.0;
				float polar_mix = 0.0;
				vec2 polar_uv = vec2(0.0), polar_dx = vec2(0.0), polar_dy = vec2(0.0);
				if(u_swirl.x > 0.5)
				{
					// The pause funnel narrows along local +Y. Its mesh maps the texture about as
					// U = 0.24 (r - 1.37) along it and V = -8 turns - 0.156 r around it (read from its
					// vertices near the tip: 8 repeats a turn, V falling as the angle grows, the lines
					// twisting outward), but ring by ring, so the pinch shows seams. Near the axis the
					// same mapping is computed smoothly per pixel and cross-faded in: the funnel's own
					// spiral without the seams, carried by the same scroll (u_uv_offset). The point
					// itself, where every line meets, fades dark.
					float radius = length(v_local_position.xz);
					float turn = atan(v_local_position.z, v_local_position.x) / 6.2831853;
					// Plus a little more twist towards the point, the same way, so the lines keep curling.
					float inward = 1.0 - smoothstep(0.0, 3.0, radius);
					polar_uv = vec2(0.24 * (radius - 1.37), -8.0 * turn - 0.156 * radius + 0.8 * inward * inward);
					// The angle jumps by a whole turn across one line; its derivatives must not.
					polar_dx = dFdx(polar_uv);
					polar_dy = dFdy(polar_uv);
					polar_dx.y += 8.0 * round(dFdx(turn));
					polar_dy.y += 8.0 * round(dFdy(turn));
					polar_mix = 1.0 - smoothstep(1.5, 4.0, radius);
					core_fade = mix(0.35, 1.0, smoothstep(0.0, 1.4, radius));
				}
				vec4 texel = textureGrad(u_texture, uv + u_uv_offset, uv_dx, uv_dy);
				if(u_falls.x > 0.5)
				{
					// The water spreads out from the waterfall's foot: the texture laid in rings around
					// it (8 repeats a turn, the seam behind the fall), moving outward over time.
					vec2 d = v_world_position.xy - u_falls.yz;
					float turn = atan(-d.x, -d.y) / 6.2831853;
					vec2 fuv = vec2(turn * 8.0, length(d) / 8.0 - u_falls.w * 0.25);
					vec2 fdx = dFdx(fuv), fdy = dFdy(fuv);
					fdx.x -= 8.0 * round(dFdx(turn));
					fdy.x -= 8.0 * round(dFdy(turn));
					texel = textureGrad(u_texture, fuv, fdx, fdy);
				}
				if(polar_mix > 0.0)
					texel = mix(texel, textureGrad(u_texture, polar_uv + u_uv_offset, polar_dx, polar_dy), polar_mix);
				if(u_well.x > 1.5)
					texel = lake_overlay(uv + u_uv_offset, uv_dx, uv_dy);
				else if(u_well.x > 0.0)
					texel.rgb = star_depth(uv + u_uv_offset, uv_dx, uv_dy);
				if(u_sharpen > 0.5)
					texel.a = sharpened(texel.a, v_uv);
				if(v_culled > 0.5)
					discard;
				float alpha = texel.a * (u_light_alpha > 0.5 ? v_light.a : 1.0) * u_part_alpha;
				if(alpha < u_alpha_cut)
					discard;
				vec3 color = texel.rgb * v_light.rgb;
				if(u_sky_haze > 0.0)
				{
					// Z is up: fade over the lowest 12 degrees, independent of camera pitch.
					float elevation = v_sky_direction.z / max(length(v_sky_direction), 1e-5);
					float haze = u_sky_haze * (1.0 - smoothstep(0.0, 0.207912, elevation));
					// Brightening cards fade away in haze rather than painting fog-coloured rectangles.
					color = u_additive > 0.5 || u_lightning > 0.5 ? color * (1.0 - haze) : mix(color, u_sky_haze_color, haze);
				}
				if(u_fog.y > u_fog.x)
				{
					float fog = mix(u_fog.z, u_fog.w, clamp((v_distance - u_fog.x) / (u_fog.y - u_fog.x), 0.0, 1.0));
					if(u_fog_infinite > 0.0 && v_distance > u_fog_infinite) fog = 1.0;
					color = u_additive > 0.5 ? color * (1.0 - fog) : mix(color, u_fog_color.rgb, fog);
				}
				if(u_lightning > 0.5)
				{
					// Brighten the existing sky, preserving its cloud pattern. Fade over the
					// outer fifth of the card, using unclamped coordinates.
					const float edge_width = 0.20;
					vec2 edge = min(v_uv, vec2(1.0) - v_uv);
					float fade = smoothstep(0.0, edge_width, edge.x) * smoothstep(0.0, edge_width, edge.y);
					// The dark sky needs extra gain for a visible flash when brightening by
					// its existing colour. The animation's alpha still controls the pulse.
					const float gain = 4.0;
					color *= alpha * fade * gain;
				}
				if(core_fade < 1.0)
				{
					color *= core_fade;
					alpha = mix(1.0, alpha, core_fade); // a dark opaque tip, rather than a transparent hole
				}
				o_color = vec4(color, u_alpha_cut > 0.25 ? 1.0 : alpha);
			}
		)";

		// Which surfaces the game blends isn't decoded yet, so guess from the texture: one where
		// many pixels are partly transparent is a soft effect rather than a cut-out.
		bool has_soft_alpha(const gf_texture& gf)
		{
			if(gf.channels < 4)
				return false;
			std::size_t soft = 0, pixels = gf.rgba.size() / 4;
			for(std::size_t i = 3; i < gf.rgba.size(); i += 4)
				soft += gf.rgba[i] > 16 && gf.rgba[i] < 240;
			return soft * 10 > pixels;
		}

		struct vertex
		{
			glm::vec3 position;
			glm::vec2 uv;
			glm::vec3 normal;
			glm::vec4 uv_bounds{ -1e6f, -1e6f, 1e6f, 1e6f }; // min u, v, max u, v: see the fragment shader
			glm::vec4 color{ 1.0f };                          // the game's light on this corner
			std::uint32_t source_index = 0;
			glm::vec4 cull{ 0.0f };                           // bounding-sphere centre (xyz), w 1 when known
		};

		// The copy of the texture a triangle's UVs stay within, per axis, or no bounds if they cross
		// from one copy into the next (the texture tiles across the triangle).
		glm::vec4 uv_bounds(const glm::vec2 (&uv)[3])
		{
			glm::vec4 bounds{ -1e6f, -1e6f, 1e6f, 1e6f };
			for(int axis = 0; axis < 2; axis++)
			{
				float lo = std::min({ uv[0][axis], uv[1][axis], uv[2][axis] });
				float hi = std::max({ uv[0][axis], uv[1][axis], uv[2][axis] });
				float tile = std::floor(lo + 1e-4f);
				if(hi <= tile + 1 + 1e-4f)
				{
					bounds[axis] = tile;
					bounds[axis + 2] = tile + 1;
				}
			}
			return bounds;
		}

		// 0x40AE5E / 0x424BD0 advance by elapsed nominal frames (0x421670 defaults to 60 Hz).
		constexpr float scroll_frames_per_second = 60.0f;

		glm::mat4 to_mat4(const affine& a)
		{
			glm::mat4 m(1.0f);
			for(int r = 0; r < 3; r++)
			{
				for(int c = 0; c < 4; c++)
					m[c][r] = a.m[r][c];
			}
			return m;
		}

		// Turns a card (flat in its own YZ plane, facing +X) towards the camera, keeping its
		// position and the size along each of its axes. As the exe does it (0x41DAD0, called from
		// 0x42AAE0 for every drawn mesh with a look-at mode): the card keeps one axis as its Z and
		// swings about it until +X points at the camera as nearly as that axis allows. Mode bit 2
		// set: the axis is the card's own Z (a rod, a chain link, a flame stays along its part).
		// Bit 2 clear: the axis is the camera's up, so the card stands upright on the screen.
		glm::mat4 face_camera(const glm::mat4& model, look_at look, const glm::mat4& camera)
		{
			if(look == look_at::none)
				return model;
			glm::vec3 position(model[3]);
			glm::vec3 to_camera = glm::vec3(camera[3]) - position;
			glm::vec3 z = (std::uint32_t(look) & 2) ? glm::vec3(model[2]) : glm::vec3(camera[1]);
			if(glm::length(to_camera) < 1e-5f || glm::length(z) < 1e-5f)
				return model;
			to_camera = glm::normalize(to_camera);
			z = glm::normalize(z);
			glm::vec3 y = glm::cross(z, to_camera);
			if(glm::length(y) < 1e-5f) // seen exactly along the axis: the exe leaves it as it is
				return model;
			y = glm::normalize(y);
			glm::vec3 x = glm::cross(y, z);
			glm::mat4 out(1.0f);
			out[0] = glm::vec4(x * glm::length(glm::vec3(model[0])), 0);
			out[1] = glm::vec4(y * glm::length(glm::vec3(model[1])), 0);
			out[2] = glm::vec4(z * glm::length(glm::vec3(model[2])), 0);
			out[3] = model[3];
			return out;
		}

		// See-through pixels keep a colour (usually black) that filtering and smaller mipmaps blend into
		// the edges of leaves as a dark outline. Give them their opaque neighbours' colour instead,
		// spreading outwards a few pixels at a time; alpha stays as it is.
		void bleed_edges(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t>& rgba)
		{
			std::vector<std::uint8_t> known(std::size_t(width) * height);
			for(std::size_t i = 0; i < known.size(); i++)
				known[i] = rgba[i * 4 + 3] >= 128;
			for(int pass = 0; pass < 16; pass++)
			{
				std::vector<std::uint8_t> next = known;
				bool changed = false;
				for(std::uint32_t y = 0; y < height; y++)
				{
					for(std::uint32_t x = 0; x < width; x++)
					{
						std::size_t i = std::size_t(y) * width + x;
						if(known[i])
							continue;
						int sum[3] = {}, count = 0;
						for(int dy = -1; dy <= 1; dy++)
						{
							for(int dx = -1; dx <= 1; dx++)
							{
								// Textures tile, so neighbours wrap around.
								std::size_t j = std::size_t((y + height + dy) % height) * width + (x + width + dx) % width;
								if(!known[j])
									continue;
								for(int c = 0; c < 3; c++)
									sum[c] += rgba[j * 4 + c];
								count++;
							}
						}
						if(count == 0)
							continue;
						for(int c = 0; c < 3; c++)
							rgba[i * 4 + c] = std::uint8_t(sum[c] / count);
						next[i] = 1;
						changed = true;
					}
				}
				known.swap(next);
				if(!changed)
					break;
			}
		}

		GLuint upload_texture(std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba)
		{
			GLuint texture = 0;
			glGenTextures(1, &texture);
			submission::BindTexture(GL_TEXTURE_2D, texture);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
			glGenerateMipmap(GL_TEXTURE_2D);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			// Anisotropic filtering, where the driver has it, keeps surfaces seen at a slant sharp
			// (the game's look from afar) instead of blurring them with smaller mipmaps.
			constexpr GLenum max_anisotropy = 0x84FF, texture_max_anisotropy = 0x84FE; // EXT_texture_filter_anisotropic
			static const float anisotropy = [] {
				float most = 1;
				glGetFloatv(max_anisotropy, &most);
				return glGetError() == GL_NO_ERROR ? std::min(most, 16.0f) : 1.0f;
			}();
			if(anisotropy > 1)
				glTexParameterf(GL_TEXTURE_2D, texture_max_anisotropy, anisotropy);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			// Level UVs run past 0..1 to tile textures across large surfaces.
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			return texture;
		}

		// Replaces the bound texture's mipmaps with our own box-filtered ones whose one-texel border
		// is cleared at every size. The driver's mipmaps average the cleared border with the picture
		// inside, and a distant Lum's wings then showed that faint edge again (card 124).
		void upload_faded_mipmaps(std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> rgba, bool additive)
		{
			for(GLint level = 1; width > 1 || height > 1; ++level)
			{
				const std::uint32_t w = std::max(1u, width / 2), h = std::max(1u, height / 2);
				std::vector<std::uint8_t> next(std::size_t(w) * h * 4);
				for(std::uint32_t y = 0; y < h; ++y)
					for(std::uint32_t x = 0; x < w; ++x)
						for(int c = 0; c < 4; ++c)
						{
							unsigned sum = 0;
							for(std::uint32_t dy = 0; dy < 2; ++dy)
								for(std::uint32_t dx = 0; dx < 2; ++dx)
									sum += rgba[(std::size_t(std::min(height - 1, y * 2 + dy)) * width + std::min(width - 1, x * 2 + dx)) * 4 + c];
							next[(std::size_t(y) * w + x) * 4 + c] = std::uint8_t((sum + 2) / 4);
						}
				if(w > 2 && h > 2)
					for(std::uint32_t y = 0; y < h; ++y)
						for(std::uint32_t x = 0; x < w; ++x)
							if(x == 0 || y == 0 || x == w - 1 || y == h - 1)
							{
								std::uint8_t* p = &next[(std::size_t(y) * w + x) * 4];
								if(additive) p[0] = p[1] = p[2] = 0;
								else p[3] = 0;
							}
				glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, GLsizei(w), GLsizei(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, next.data());
				rgba = std::move(next); width = w; height = h;
			}
		}
	}

	level_renderer::level_renderer(gl::api api)
	{
		m_program = gl::create_program(api, vertex_shader, fragment_shader);
		m_u_view_projection = glGetUniformLocation(m_program, "u_view_projection");
		m_u_model = glGetUniformLocation(m_program, "u_model");
		m_u_uv_offset = glGetUniformLocation(m_program, "u_uv_offset");
		m_u_well = glGetUniformLocation(m_program, "u_well");
		m_u_lake = glGetUniformLocation(m_program, "u_lake");
		m_u_star_floor = glGetUniformLocation(m_program, "u_star_floor");
		m_u_falls = glGetUniformLocation(m_program, "u_falls");
		m_u_twinkle = glGetUniformLocation(m_program, "u_twinkle");
		m_u_swirl = glGetUniformLocation(m_program, "u_swirl");
		if(const auto* option = std::getenv("OPENRAYMAN_SWIRL_CENTRE"))
			m_swirl_centre = std::strcmp(option, "0") != 0;
		m_u_sky_haze = glGetUniformLocation(m_program, "u_sky_haze");
		m_u_fog_model = glGetUniformLocation(m_program, "u_fog_model");
		m_u_sky_eye = glGetUniformLocation(m_program, "u_sky_eye");
		m_u_sky_haze_color = glGetUniformLocation(m_program, "u_sky_haze_color");
		m_u_alpha_cut = glGetUniformLocation(m_program, "u_alpha_cut");
		m_u_sharpen = glGetUniformLocation(m_program, "u_sharpen");
		m_u_part_alpha = glGetUniformLocation(m_program, "u_part_alpha");
		m_u_edge_clamp = glGetUniformLocation(m_program, "u_edge_clamp");
		m_u_lightning = glGetUniformLocation(m_program, "u_lightning");
		m_u_camera = glGetUniformLocation(m_program, "u_camera");
		m_u_camera_forward = glGetUniformLocation(m_program, "u_camera_forward");
		m_u_lighting = glGetUniformLocation(m_program, "u_lighting");
		m_u_light_count = glGetUniformLocation(m_program, "u_light_count");
		m_u_light_kind = glGetUniformLocation(m_program, "u_light_kind");
		m_u_light_vector = glGetUniformLocation(m_program, "u_light_vector");
		m_u_light_color = glGetUniformLocation(m_program, "u_light_color");
		m_u_light_range = glGetUniformLocation(m_program, "u_light_range");
		m_u_material_ambient = glGetUniformLocation(m_program, "u_material_ambient");
		m_u_material_diffuse = glGetUniformLocation(m_program, "u_material_diffuse");
		m_u_luminosity = glGetUniformLocation(m_program, "u_luminosity");
		m_u_light_alpha = glGetUniformLocation(m_program, "u_light_alpha");
		m_u_fog_color = glGetUniformLocation(m_program, "u_fog_color");
		m_u_fog = glGetUniformLocation(m_program, "u_fog");
		m_u_fog_infinite = glGetUniformLocation(m_program, "u_fog_infinite");
		m_u_additive = glGetUniformLocation(m_program, "u_additive");
		submission::UseProgram(m_program);
		submission::Uniform1i(glGetUniformLocation(m_program, "u_texture"), 0);

		auto vertex_layout = [](GLuint& vao, GLuint& vbo) {
			glGenVertexArrays(1, &vao);
			glGenBuffers(1, &vbo);
			submission::BindVertexArray(vao);
			submission::BindBuffer(GL_ARRAY_BUFFER, vbo);
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, position)));
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, uv)));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, normal)));
			glEnableVertexAttribArray(3);
			glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, uv_bounds)));
			glEnableVertexAttribArray(4);
			glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, color)));
			glEnableVertexAttribArray(5);
			glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(vertex), reinterpret_cast<void*>(offsetof(vertex, cull)));
			submission::BindVertexArray(0);
		};
		vertex_layout(m_vao, m_vbo);
		glGenBuffers(1, &m_deferred_ebo);
		vertex_layout(m_sprite_vao, m_sprite_vbo);

		const std::uint8_t white[4] = { 255, 255, 255, 255 };
		m_white = upload_texture(1, 1, white);
	}

	level_renderer::~level_renderer()
	{
		clear();
		glDeleteTextures(1, &m_white);
		glDeleteBuffers(1, &m_vbo);
		glDeleteBuffers(1, &m_deferred_ebo);
		glDeleteVertexArrays(1, &m_vao);
		glDeleteBuffers(1, &m_sprite_vbo);
		glDeleteVertexArrays(1, &m_sprite_vao);
		glDeleteProgram(m_program);
	}

	void level_renderer::clear()
	{
		for(const auto& [path, texture] : m_textures)
		{
			if(texture.id)
				glDeleteTextures(1, &texture.id);
		}
		m_textures.clear();
		m_lightning_textures.clear();
		m_batches.clear();
		m_see_through.clear();
		m_see_through_triangles.clear();
		m_camera_sector.reset();
		m_pause_background_frame = 0;
		m_backdrop_sectors.clear();
		m_backdrop_skies.clear();
		m_background.reset();
		m_sprites.clear();
		m_effect_materials.clear();
		m_particles.clear();
		m_collision.clear();
		m_meshes.clear();
		m_parts.clear();
		m_level_cards.clear();
		m_animations.clear();
		m_animation_by_material.clear();
		m_lights = {};
		m_skies.clear();
		m_solid_cores.clear();
		m_floor.clear();
		m_floor_sector.clear();
		m_shadow_texture = 0;
		m_shadows.clear();
	}

	bool level_renderer::catches_shadow(std::size_t triangle) const
	{
		// The backdrop is drawn first, behind everything, so it looks far away; a shadow laid on
		// its geometry showed while Rayman fell past it in Jail_20 (user, 2026-11-08).
		if(triangle >= m_floor_sector.size())
			return true;
		const std::size_t sector = m_floor_sector[triangle];
		return std::ranges::find(m_backdrop_sectors, sector) == m_backdrop_sectors.end() &&
			std::ranges::find(m_backdrop_skies, sector) == m_backdrop_skies.end();
	}

	std::optional<std::pair<glm::vec3, glm::vec3>> level_renderer::ground_below(const glm::vec3& from) const
	{
		// A ray straight down from a little above the point, against every solid triangle.
		const glm::vec3 start = from + glm::vec3(0, 0, 1);
		constexpr float reach = 20;
		std::optional<std::pair<glm::vec3, glm::vec3>> best;
		float nearest = reach;
		for(std::size_t i = 0; i + 2 < m_floor.size(); i += 3)
		{
			if(!catches_shadow(i / 3))
				continue;
			const glm::vec3 &a = m_floor[i], &b = m_floor[i + 1], &c = m_floor[i + 2];
			if(std::min({ a.x, b.x, c.x }) > start.x || std::max({ a.x, b.x, c.x }) < start.x ||
				std::min({ a.y, b.y, c.y }) > start.y || std::max({ a.y, b.y, c.y }) < start.y)
				continue;
			// Where the vertical line through the start crosses the triangle, by barycentric coordinates.
			glm::vec2 p(start), v0(b - a), v1(c - a), v2 = p - glm::vec2(a);
			float d = v0.x * v1.y - v1.x * v0.y;
			if(std::abs(d) < 1e-8f)
				continue;
			float s = (v2.x * v1.y - v1.x * v2.y) / d, t = (v0.x * v2.y - v2.x * v0.y) / d;
			if(s < 0 || t < 0 || s + t > 1)
				continue;
			float z = a.z + s * (b.z - a.z) + t * (c.z - a.z);
			float depth = start.z - z;
			if(depth < 0 || depth >= nearest)
				continue;
			glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
			if(n.z < 0)
				n = -n;
			nearest = depth;
			best = { glm::vec3(start.x, start.y, z), n };
		}
		return best;
	}

	void level_renderer::set_blend(blend mode) const
	{
		submission::blending(int(mode));
		// See-through surfaces go over the solid scene, without hiding each other.
		if(mode == blend::none)
		{
			submission::EnableBlend(false);
			glDepthMask(GL_TRUE);
			submission::Uniform1f(m_u_alpha_cut, 0.5f);
			submission::Uniform1f(m_u_additive, 0.0f);
			submission::Uniform1f(m_u_light_alpha, 0.0f);
			return;
		}
		submission::EnableBlend(true);
		glDepthMask(GL_FALSE);
		submission::Uniform1f(m_u_alpha_cut, 0.01f);
		submission::Uniform1f(m_u_additive, mode == blend::additive ? 1.0f : 0.0f);
		submission::Uniform1f(m_u_light_alpha, 1.0f);
		if(mode == blend::additive)
			submission::BlendFunc(GL_SRC_ALPHA, GL_ONE);
		else
			submission::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	void level_renderer::load_effect_materials(const std::map<std::uint32_t, visual_material>& materials,
		std::span<const std::unique_ptr<cnt_archive>> archives)
	{
		statistics stats;
		for(const auto& [address, material] : materials)
		{
			if(!material.texture)
				continue;
			gpu_texture texture = texture_for(*material.texture, archives, stats);
			if(!texture.id)
				continue;
			// A texture that says it uses its alpha ("_aaa") blends, even when the frame the material
			// starts from is nearly empty and looks like a cut-out (the splash ends on such a frame).
			if(texture.mode == blend::none && (material.texture->flags & texture_flag::alpha))
				texture.mode = blend::alpha;
			m_effect_materials[address] = { glm::vec3(0), glm::vec2(1), texture.id, texture.mode,
				glm::vec2(texture.mirror_u ? 2.0f : 1.0f, texture.mirror_v ? 2.0f : 1.0f), animation_for(material, archives, stats) };
		}
	}

	level_renderer::gpu_texture level_renderer::texture_for(const texture_info& info, std::span<const std::unique_ptr<cnt_archive>> archives, statistics& stats)
	{
		std::string path = info.cnt_path();
		if(auto it = m_textures.find(path); it != m_textures.end())
			return it->second;

		gpu_texture texture;
		for(const auto& archive : archives)
		{
			if(const cnt_entry* entry = archive->find(path))
			{
				try
				{
					gf_texture gf = decode_gf(archive->read(*entry));
					// Diagnostic (card 124): OPENRAYMAN_HIDE_TEXTURES=a,b draws textures whose name contains
					// any of those parts fully transparent, to find which one shows something.
					if(const char* hide = std::getenv("OPENRAYMAN_HIDE_TEXTURES"); hide && *hide)
					{
						std::string lower = info.name;
						std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
						std::string list = hide;
						for(std::size_t start = 0; start <= list.size();)
						{
							std::size_t end = list.find(',', start);
							if(end == std::string::npos) end = list.size();
							if(end > start && lower.find(list.substr(start, end - start)) != std::string::npos)
								std::ranges::fill(gf.rgba, std::uint8_t(0));
							start = end + 1;
						}
					}
					// How it blends goes by the texture's own alpha, before keying: keying a leaf
					// texture's soft fringe away must not turn it into a solid one.
					const gf_texture own = gf;
					// The texture's key colour is transparent (mostly black; Jail_10's mountains key out
					// their sky grey; the forest's leaf textures their background green).
					apply_color_key(info, gf.rgba);
					// The game converts textures to 16-bit colour (5 bits a channel), which turns anything
					// darker than 8 of 255 to black. Additive textures rely on that: the Lums' halo frames
					// (gra01..03_lum_my_ad) each carry one stray row of values 1..9 below the gradient, which
					// at full 8 bits showed as a faint line under every Lum, moving as the frames change
					// (card 124). Values below 8 go to black, as in the game; the rest stay exact.
					if(info.flags & texture_flag::additive)
						for(std::size_t i = 0; i < gf.rgba.size(); i += 4)
							for(std::size_t c = 0; c < 3; c++)
								if(gf.rgba[i + c] < 8)
									gf.rgba[i + c] = 0;
					bleed_edges(gf.width, gf.height, gf.rgba);
					// A texture not set to repeat that is one picture fading out towards its border
					// (the Lums' wing frames: lum_aile01_aaa's top row has alpha 45 of 255, the Fix
					// lum_aile01_ad's top row brightness 47) is drawn on a card larger than the
					// picture, and that faint border row showed as a thin line across the card's whole
					// width (Learn_30, user 2026-11-15). Its one-pixel border is cleared: alpha for
					// blended textures, colour for additive ones. Textures meant to tile or scroll are
					// solid at their edges and are left alone.
					bool faded_border = false;
					if(info.tiling == 0 && gf.width > 2 && gf.height > 2)
					{
						const bool additive = info.flags & texture_flag::additive;
						auto weight = [&](int x, int y) {
							const std::uint8_t* p = &gf.rgba[(std::size_t(y) * gf.width + std::size_t(x)) * 4];
							return additive ? int(std::max({ p[0], p[1], p[2] })) : int(p[3]);
						};
						int border = 0, inside = 0;
						for(int y = 0; y < int(gf.height); y++)
							for(int x = 0; x < int(gf.width); x++)
							{
								const bool edge = x == 0 || y == 0 || x == int(gf.width) - 1 || y == int(gf.height) - 1;
								int& most = edge ? border : inside;
								most = std::max(most, weight(x, y));
							}
						faded_border = border > 0 && border < 64 && inside >= 128;
						if(faded_border)
							for(int y = 0; y < int(gf.height); y++)
								for(int x = 0; x < int(gf.width); x++)
									if(x == 0 || y == 0 || x == int(gf.width) - 1 || y == int(gf.height) - 1)
									{
										std::uint8_t* p = &gf.rgba[(std::size_t(y) * gf.width + std::size_t(x)) * 4];
										if(additive)
											p[0] = p[1] = p[2] = 0;
										else
											p[3] = 0;
									}
					}
					texture.id = upload_texture(gf.width, gf.height, gf.rgba.data());
					if(faded_border)
						upload_faded_mipmaps(gf.width, gf.height, gf.rgba, info.flags & texture_flag::additive);
					texture.mirror_u = info.tiling & texture_tiling::mirror_u;
					texture.mirror_v = info.tiling & texture_tiling::mirror_v;
					// Cut-outs that aren't set to repeat at all (tiling 0, like the cliff-edge foliage
					// foret_bordtri_nz, whose UVs run 6% past its edges) stop at their transparent edge
					// instead of showing the opposite one. Solid textures repeat regardless: many use
					// UVs outside 0..1 without the bits.
					bool cut_out = false;
					for(std::size_t i = 3; i < gf.rgba.size() && !cut_out; i += 4)
						cut_out = gf.rgba[i] < 128;
					GLint wrap_u = texture.mirror_u ? GL_MIRRORED_REPEAT : GL_REPEAT, wrap_v = texture.mirror_v ? GL_MIRRORED_REPEAT : GL_REPEAT;
					if(info.tiling == 0 && (cut_out || faded_border))
						wrap_u = wrap_v = GL_CLAMP_TO_EDGE;
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_u);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_v);
					if(info.flags & texture_flag::additive)
						texture.mode = blend::additive;
					else if((info.flags & texture_flag::alpha) || has_soft_alpha(own))
					{
						texture.mode = blend::alpha;
						// Leaves are mostly fully opaque with a soft fringe; light rays, water and
						// mist are mostly partly see-through.
						std::size_t opaque = 0, partial = 0;
						for(std::size_t i = 3; i < own.rgba.size(); i += 4)
						{
							if(own.rgba[i] >= 230)
								opaque++;
							else if(own.rgba[i] > 16)
								partial++;
						}
						if(opaque * 2 >= partial * 3)
							m_solid_cores.insert(texture.id);
					}
				}
				catch(const std::exception& e)
				{
					SDL_Log("Texture %s: %s", path.c_str(), e.what());
				}
				break;
			}
		}
		if(texture.id)
			stats.textures++;
		else
			stats.missing_textures++;
		m_textures[path] = texture;
		if(texture.id && (path == "textures_cinematique_rhop/beach_sky2_aaa.gf" ||
			path == "textures_cinematique_rhop/beach_sky3_aaa.gf" || path == "textures_cinematique_rhop/beach_sky4_aaa.gf"))
			m_lightning_textures.insert(texture.id);
		return texture;
	}

	int level_renderer::animation_for(const visual_material& material, std::span<const std::unique_ptr<cnt_archive>> archives, statistics& stats)
	{
		if(material.frames.size() < 2)
			return -1;
		if(auto it = m_animation_by_material.find(material.at.address()); it != m_animation_by_material.end())
			return it->second;
		texture_animation animation;
		float end = 0;
		for(const texture_frame& frame : material.frames)
		{
			const auto texture = texture_for(frame.texture, archives, stats);
			const GLuint id = texture.id;
			if(texture.mode == blend::additive)
				animation.mode = blend::additive;
			else if(animation.mode == blend::none && (texture.mode == blend::alpha || (frame.texture.flags & texture_flag::alpha)))
				animation.mode = blend::alpha;
			animation.textures.push_back(id ? id : m_white);
			end += std::max(frame.duration, 0.0f);
			animation.ends.push_back(end);
		}
		int index = -1;
		if(end > 0) // all-zero durations would never move on
		{
			index = int(m_animations.size());
			m_animations.push_back(std::move(animation));
		}
		m_animation_by_material[material.at.address()] = index;
		return index;
	}

	GLuint level_renderer::texture_at(int animation, GLuint still) const
	{
		if(animation < 0 || std::size_t(animation) >= m_animations.size())
			return still;
		const texture_animation& a = m_animations[std::size_t(animation)];
		float t = float(std::fmod(m_time, double(a.ends.back())));
		auto it = std::ranges::upper_bound(a.ends, t);
		return it == a.ends.end() ? a.textures.back() : a.textures[std::size_t(it - a.ends.begin())];
	}

	GLuint level_renderer::texture_once(int animation, GLuint still, float age) const
	{
		if(animation < 0 || std::size_t(animation) >= m_animations.size())
			return still;
		const texture_animation& a = m_animations[std::size_t(animation)];
		auto it = std::ranges::upper_bound(a.ends, age);
		return it == a.ends.end() ? a.textures.back() : a.textures[std::size_t(it - a.ends.begin())];
	}

	level_renderer::statistics level_renderer::load(std::span<const placed_geometry> geometry, std::span<const geometric_object> meshes,
		std::span<const std::unique_ptr<cnt_archive>> archives, level_lights lights, std::span<const std::optional<std::size_t>> sectors,
		const visual_material* shadow_material)
	{
		clear();
		m_lights = std::move(lights);
		m_star_floor = 1e30f;
		m_star_texels = 0;
		double star_uv_length = 0, star_world_length = 0;
		glm::vec2 fall_min(1e30f), fall_max(-1e30f);
		statistics stats;
		stats.min = glm::vec3(1e30f);
		stats.max = glm::vec3(-1e30f);

		// Triangles grouped by texture. Corners are expanded into separate vertices, because a
		// corner's UV is indexed separately from its position.
		using by_texture = std::map<std::tuple<blend, GLuint, int, std::array<float, 2>, std::array<float, 8>, cull, int,
			bool, std::array<float, 2>, std::size_t>, std::vector<vertex>>;
		// Static geometry is lit once here, by the lights its sector lists for scenery; meshes placed
		// per frame (`lit` empty) are lit as they're drawn.
		auto add = [&](const geometric_object& object, const affine& world, by_texture& triangles, std::vector<sprite_instance>& sprites,
			std::span<const light* const> lit, bool element_order = false) {
			stats.meshes++;
			glm::vec4 cull_centre(0.0f);
			if(object.bounding_centre)
			{
				vec3 c = world.apply(*object.bounding_centre);
				cull_centre = glm::vec4(c[0], c[1], c[2], 1.0f);
			}
			std::size_t ordinal = 0;
			for(const triangle_element& element : object.elements)
			{
				++ordinal;
				// The portal well's star texture: our own deeper look (docs/IMPROVEMENTS.md, OPENRAYMAN_WELL=0 off).
				// 1: the star field itself; 2: the Hall of Doors' see-through star overlay above it.
				const std::string* star_name = element.material && element.material->texture ? &element.material->texture->name : nullptr;
				// 3: the Hall of Doors' river surface below the waterfall, whose mapping the original lays
				// in a V that flows into itself; 4: the waterfall column (only measured, for its foot).
				const int well = !star_name ? 0 : *star_name == "textures_foret\\foret_fondetoiles_txy.tga" ? 1
					: *star_name == "textures_background\\stars_txy_aaa.tga" ? 2
					: *star_name == "textures_temple_eau\\eau_morf_anu_nzwtxy.tga" ? 3
					: *star_name == "textures_temple_eau\\eau_bouillon_anu_nzwtxy.tga" ? 4 : 0;
				gpu_texture texture{ m_white, blend::none };
				int animation = -1;
				std::array<float, 2> scroll{};
				// The material's ambient colour is its least light; lights add on top, times its diffuse colour.
				std::array<float, 8> colors{ 0, 0, 0, 1, 1, 1, 1, 1 };
				cull faces = cull::none;
				bool depth_write = true;
				std::array<float, 2> scroll_offset{};
				if(element.material)
				{
					const visual_material& m = *element.material;
					colors = { m.ambient[0], m.ambient[1], m.ambient[2], m.ambient[3], m.diffuse[0], m.diffuse[1], m.diffuse[2], m.diffuse[3] };
					if(m.additive_fullbright()) colors = { 1, 1, 1, 1, 0, 0, 0, 0 };
					const auto flags = m.texture_draw_flags();
					faces = cull_for(flags);
					depth_write = (flags & 0x08000000) != 0;
					scroll_offset = m.scroll_offset;
				}
				if(element.material && element.material->texture)
				{
					texture = texture_for(*element.material->texture, archives, stats);
					if(!texture.id)
						texture = { m_white, blend::none };
					animation = animation_for(*element.material, archives, stats);
					// Invisible first frames (inv_nz) must not put an entire lightning/cloud
					// animation in the opaque pass. Its later frames need their soft alpha.
					if(animation >= 0 && texture.mode == blend::none)
						texture.mode = m_animations[std::size_t(animation)].mode;
					scroll = element.material->scroll;
				}
				std::vector<vertex>& out = triangles[{ texture.mode, texture.id, animation, scroll, colors, faces, well,
					depth_write, scroll_offset, element_order ? ordinal : 0 }];
				for(std::size_t t = 0; t < element.triangles.size(); t++)
				{
					// The game's v runs up from the bottom row; our textures are uploaded top row first.
					glm::vec2 uvs[3];
					for(int k = 0; k < 3; k++)
					{
						vec2 uv = element.uvs[element.uv_triangles[t][k]];
						uvs[k] = glm::vec2(uv[0], 1.0f - uv[1]);
					}
					glm::vec4 bounds = uv_bounds(uvs);
					if(well == 4)
						for(int k = 0; k < 3; k++)
						{
							vec3 p = world.apply(object.vertices[element.triangles[t][k]]);
							fall_min = glm::min(fall_min, glm::vec2(p[0], p[1]));
							fall_max = glm::max(fall_max, glm::vec2(p[0], p[1]));
						}
					if(well == 1)
					{
						// The star field's lowest point, and how long a texture tile is in the world,
						// for the Hall of Doors' flat star floor (see star_depth).
						glm::vec3 c[3];
						for(int k = 0; k < 3; k++)
						{
							vec3 p = world.apply(object.vertices[element.triangles[t][k]]);
							c[k] = glm::vec3(p[0], p[1], p[2]);
							m_star_floor = std::min(m_star_floor, c[k].z);
						}
						for(int k = 0; k < 3; k++)
						{
							star_uv_length += glm::length(uvs[(k + 1) % 3] - uvs[k]);
							star_world_length += glm::length(c[(k + 1) % 3] - c[k]);
						}
					}
					for(int k = 0; k < 3; k++)
					{
						std::uint16_t index = element.triangles[t][k];
						vec3 p = world.apply(object.vertices[index]);
							vec3 n = index < object.normals.size() ? object.normals[index] : vec3{ 0, 0, 1 };
						glm::vec3 position(p[0], p[1], p[2]);
						stats.min = glm::min(stats.min, position);
						stats.max = glm::max(stats.max, position);
						glm::vec4 color(0.0f, 0.0f, 0.0f, 0.0f);
						if(!lit.empty())
						{
							// The normal turns with the object, without moving.
							vec3 origin = world.apply({ 0, 0, 0 }), tip = world.apply(n);
							glm::vec3 turned = glm::normalize(glm::vec3(tip[0] - origin[0], tip[1] - origin[1], tip[2] - origin[2]));
							rgba sum = light_at(lit, light_target::scenery, p, { turned.x, turned.y, turned.z });
							color = glm::vec4(sum[0], sum[1], sum[2], sum[3]);
						}
						out.push_back({ position, uvs[k], glm::vec3(n[0], n[1], n[2]), bounds, color, index, cull_centre });
					}
					stats.triangles++;
				}
			}

			for(const sprite& sp : object.sprites)
			{
				gpu_texture texture{ m_white, blend::alpha };
				int animation = -1;
				if(sp.material && sp.material->texture)
				{
					texture = texture_for(*sp.material->texture, archives, stats);
					if(!texture.id)
						continue;
					animation = animation_for(*sp.material, archives, stats);
				}
				vec3 c = world.apply(object.vertices[sp.vertex]);
				// Sprites scale with the part they belong to (a Lum's blue core is about half its glow).
				float scale = world.uniform_scale();
				sprites.push_back({ glm::vec3(c[0], c[1], c[2]), glm::vec2(sp.half_size[0], sp.half_size[1]) * scale, texture.id, texture.mode,
					glm::vec2(texture.mirror_u ? 2.0f : 1.0f, texture.mirror_v ? 2.0f : 1.0f), animation });
				stats.sprites++;
			}
		};

		// One vertex buffer: the static geometry first, then each actor mesh.
		std::vector<vertex> vertices;
		auto append = [&](const by_texture& triangles, std::vector<batch>& batches) {
			for(const auto& [key, list] : triangles)
			{
				// The game scrolls per frame of its 60 Hz clock; our v runs the other way.
				const std::array<float, 2>& scroll = std::get<3>(key);
				glm::vec2 per_second(scroll[0] * scroll_frames_per_second, -scroll[1] * scroll_frames_per_second);
				const std::array<float, 8>& c = std::get<4>(key);
				batches.push_back({ std::get<1>(key), GLint(vertices.size()), GLsizei(list.size()), std::get<0>(key), std::get<2>(key), per_second,
					glm::vec4(c[0], c[1], c[2], c[3]), glm::vec4(c[4], c[5], c[6], c[7]), no_sector, std::get<5>(key), std::get<6>(key),
					std::get<7>(key), glm::vec2(std::get<8>(key)[0], -std::get<8>(key)[1]), std::get<9>(key) });
				vertices.insert(vertices.end(), list.begin(), list.end());
			}
		};

		std::map<std::size_t, by_texture> level_triangles; // by sector
		std::vector<std::size_t> cards;
		for(std::size_t g = 0; g < geometry.size(); g++)
		{
			const placed_geometry& placed = geometry[g];
			// Exe 0x45C360: hidden IPOs still exist (e.g. Jail_20's white doorway quad).
			if(!placed.visible)
				continue;
			if(placed.object.look != look_at::none)
			{
				cards.push_back(g);
				continue;
			}
			std::vector<const light*> lit;
			if(g < sectors.size() && sectors[g] && *sectors[g] < m_lights.sectors.size())
			{
				for(std::size_t i : m_lights.sectors[*sectors[g]].lights)
					lit.push_back(&m_lights.lights[i]);
			}
			// Solid surfaces join the level's batches; see-through ones stay per object, to be drawn
			// back to front (a leaf in front of the waterfall must go over it, not under).
			by_texture own;
			add(placed.object, placed.world, own, m_sprites, lit);
			for(auto& [key, list] : own)
			{
				const std::size_t sector = g < sectors.size() && sectors[g] ? *sectors[g] : no_sector;
				if(std::get<0>(key) == blend::none)
				{
					auto& into = level_triangles[sector][key];
					into.insert(into.end(), list.begin(), list.end());
					continue;
				}
				glm::vec3 lo(1e30f), hi(-1e30f);
				for(const vertex& v : list)
					lo = glm::min(lo, v.position), hi = glm::max(hi, v.position);
				std::vector<batch> one;
				append(by_texture{ { key, list } }, one);
				one.front().sector = sector;
				m_see_through.push_back({ one.front(), (lo + hi) * 0.5f });
				// Each triangle sorts on its own: big see-through objects (a light ray, a curtain of
				// moss) cross each other, so no order of whole objects is right from everywhere.
				const batch& b = one.front();
				for(GLint t = 0; t + 2 < b.count; t += 3)
				{
					const std::size_t at = std::size_t(b.first + t);
					glm::vec3 centre = (vertices[at].position + vertices[at + 1].position + vertices[at + 2].position) / 3.0f;
					m_see_through_triangles.push_back({ centre, m_see_through.size() - 1, b.first + t,
						{ vertices[at].position, vertices[at + 1].position, vertices[at + 2].position } });
				}
			}
		}
		for(auto& [sector, triangles] : level_triangles)
		{
			std::size_t from = m_batches.size();
			append(triangles, m_batches);
			for(std::size_t i = from; i < m_batches.size(); i++)
				m_batches[i].sector = sector;
		}
		for(const batch& b : m_batches)
		{
			for(GLint i = b.first; i < b.first + b.count; i++)
				m_floor.push_back(vertices[std::size_t(i)].position);
			m_floor_sector.insert(m_floor_sector.end(), std::size_t(b.count / 3), b.sector);
		}
		if(shadow_material && shadow_material->texture)
		{
			// A quarter of a blob (its centre in a corner), mirrored into a whole one.
			m_shadow_texture = texture_for(*shadow_material->texture, archives, stats).id;
			if(m_shadow_texture)
			{
				submission::BindTexture(GL_TEXTURE_2D, m_shadow_texture);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
			}
		}
		// Meshes placed per frame are in their own space, so they don't count towards the level's bounds.
		glm::vec3 level_min = stats.min, level_max = stats.max;
		auto add_mesh = [&](const geometric_object& object) {
			by_texture triangles;
			mesh& m = m_meshes.emplace_back();
			m.look = object.look;
			m.geometry = object;
			add(object, affine{}, triangles, m.sprites, {}, true);
			m.first = GLint(vertices.size());
			append(triangles, m.batches);
			// The shared pause funnel, identified by its two material resources and mesh shape.
			// Requiring both names avoids affecting other uses of the forest-water texture.
			const bool swirl = object.vertices.size() == 258 && object.elements.size() == 2 &&
				object.elements[0].material && object.elements[0].material->texture &&
				object.elements[1].material && object.elements[1].material->texture &&
				object.elements[0].material->texture->cnt_path() == "textures_foret/foret_eau_anu_nzwtxy.gf" &&
				object.elements[1].material->texture->cnt_path() == "textures_cinematique_rhop/bato_vortexnzw_txy.gf";
			for(auto& b : m.batches) b.pause_swirl = swirl;
			// 0x42A990 submits elements in stored order, even when both use alpha blending.
			std::ranges::stable_sort(m.batches, {}, &batch::element_order);
			if(vertices.size() > std::size_t(m.first))
			{
				const auto* begin = reinterpret_cast<const std::byte*>(vertices.data() + m.first);
				m.vertices.assign(begin, begin + (vertices.size() - m.first) * sizeof(vertex));
			}
		};
		for(const geometric_object& object : meshes)
			add_mesh(object);
		for(std::size_t g : cards)
		{
			std::optional<std::size_t> sector = g < sectors.size() ? sectors[g] : std::nullopt;
			m_level_cards.push_back({ m_meshes.size(), to_mat4(geometry[g].world), sector });
			add_mesh(geometry[g].object);
		}
		stats.min = level_min;
		stats.max = level_max;

		// The sky: each sector's sky material, shown on a dome around the camera (how the game maps it
		// isn't known yet; see docs/formats/lighting.md).
		for(std::size_t s = 0; s < m_lights.sectors.size(); s++)
		{
			const auto& sky = m_lights.sectors[s].sky;
			if(!sky || !sky->texture)
				continue;
			gpu_texture texture = texture_for(*sky->texture, archives, stats);
			if(!texture.id)
				continue;
			batch b;
			b.texture = texture.id;
			b.animation = animation_for(*sky, archives, stats);
			b.scroll = glm::vec2(sky->scroll[0] * scroll_frames_per_second, -sky->scroll[1] * scroll_frames_per_second);
			b.scroll_offset = glm::vec2(sky->scroll_offset[0], -sky->scroll_offset[1]);
			m_skies[s] = b;
		}
		if(!m_skies.empty())
		{
			// From a little below the horizon to the top, the texture repeating around it.
			constexpr int rings = 12, segments = 32;
			constexpr float lowest = -0.35f, repeats = 4.0f;
			GLint first = GLint(vertices.size());
			auto at = [&](int ring, int segment) {
				float elevation = lowest + (1.5707964f - lowest) * float(ring) / rings;
				float azimuth = 6.2831853f * float(segment) / segments;
				glm::vec3 p(std::cos(elevation) * std::cos(azimuth), std::cos(elevation) * std::sin(azimuth), std::sin(elevation));
				vertex v{ p, glm::vec2(repeats * float(segment) / segments, 2.0f * float(ring) / rings), -p };
				return v;
			};
			for(int ring = 0; ring < rings; ring++)
			{
				for(int segment = 0; segment < segments; segment++)
				{
					vertex a = at(ring, segment), b = at(ring, segment + 1), c = at(ring + 1, segment), d = at(ring + 1, segment + 1);
					vertices.insert(vertices.end(), { a, b, d, a, d, c });
				}
			}
			for(auto& [s, b] : m_skies)
				b.first = first, b.count = GLsizei(vertices.size()) - first;
		}

		submission::BindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(vertex)), vertices.data(), GL_STATIC_DRAW);
		if(stats.min.x > stats.max.x) // no level vertices
			stats.min = stats.max = glm::vec3(0);
		if(star_world_length > 0)
			m_star_texels = float(star_uv_length / star_world_length);
		m_fall_foot = fall_min.x <= fall_max.x ? std::optional<glm::vec2>(0.5f * (fall_min + fall_max)) : std::nullopt;
		return stats;
	}

	std::optional<glm::vec3> level_renderer::background(const glm::vec3& eye) const
	{
		if(!m_lighting)
			return std::nullopt;
		follow_camera(eye);
		auto s = m_camera_sector;
		// 0x40AD00 sets the screen clear from the first fog light of the camera's sector, and leaves
		// it as it was when that sector has none (the intro's cave keeps the valley's dark blue).
		const light* fog = s ? m_lights.fog(*s) : nullptr;
		if(fog)
			m_background = glm::vec3(fog->background[0], fog->background[1], fog->background[2]);
		// Level start (0x403730, 0x4032A0 -> 0x421620) clears it to black, so a level whose first
		// sectors have no fog light shows black through gaps (Jail_20's cages, not a light blue).
		if(!m_background)
			m_background = glm::vec3(0.0f);
		return m_background;
	}

	level_renderer::part_lights level_renderer::lights_for(const part& p) const
	{
		part_lights out;
		if(p.no_lights) return out; // 0x426BD0 switches sector lights off; saved colours still apply
		if(p.screen)
		{
			// The exe's screen pass is a world of its own (0x419A30 draws 0x5003E0 with the fixed
			// camera 0x4052C0), so level lights don't reach it. Keep the white stand-in for
			// surfaces without saved colours. The logo's own light is switched off by its script.
			const auto& sets = m_meshes[p.mesh].geometry.colour_sets;
			const auto selected = p.colour_set.value_or(m_meshes[p.mesh].geometry.lod_count == 1 ? 1 : 0);
			bool saved = selected && selected <= sets.size() && !sets[selected - 1].empty()
				&& sets[selected - 1][0].size() == m_meshes[p.mesh].geometry.vertices.size();
			if(!saved)
			{
				out.count = 1;
				out.kind[0] = 0;
				out.color[0] = glm::vec4(1);
			}
			out.sharpen = true;
			return out;
		}
		glm::vec3 at(p.model[3]);
		vec3 position{ at.x, at.y, at.z };
		std::optional<std::size_t> sector = p.sector ? p.sector : m_lights.sector_at(position);
		if(!sector || *sector >= m_lights.sectors.size())
			return out;
		// How strongly each light reaches the part's origin, ignoring which way its surfaces face.
		std::vector<std::pair<float, const light*>> candidates;
		for(std::size_t i : m_lights.sectors[*sector].lights)
		{
			const light& l = m_lights.lights[i];
			if(!l.on || !(l.targets & light_target::actors) || l.type == light_type::fog)
				continue;
			float reach = 1;
			if(l.type == light_type::spherical)
			{
				vec3 c = l.position();
				float d = glm::length(glm::vec3(c[0], c[1], c[2]) - at);
				reach = d <= l.near ? 1.0f : d >= l.far ? 0.0f : (l.far * l.far - d * d) / (l.far * l.far - l.near * l.near);
			}
			float size = std::abs(l.color[0]) + std::abs(l.color[1]) + std::abs(l.color[2]);
			if(reach > 0 && size > 0)
				candidates.push_back({ reach * size, &l });
		}
		std::ranges::sort(candidates, [](const auto& a, const auto& b) { return a.first > b.first; });
		for(const auto& [strength, l] : candidates)
		{
			if(out.count == max_part_lights)
				break;
			std::size_t i = std::size_t(out.count++);
			vec3 v = l->type == light_type::spherical ? l->position() : l->direction();
			out.kind[i] = l->type == light_type::ambient ? 0 : l->type != light_type::spherical ? 1 : l->painting ? 3 : 2;
			out.vector[i] = glm::vec3(v[0], v[1], v[2]);
			out.color[i] = glm::vec4(l->color[0], l->color[1], l->color[2], l->color[3]);
			if(l->channels == light::affects::alpha)
				out.color[i].r = out.color[i].g = out.color[i].b = 0;
			else if(l->channels == light::affects::color)
				out.color[i].a = 0;
			out.range[i] = glm::vec2(l->near, l->far);
		}
		return out;
	}

	void level_renderer::set_backdrop(const std::string& level)
	{
		const char* well = std::getenv("OPENRAYMAN_WELL");
		const bool hall = level == "Mapmonde" || level == "mapmonde"; // the game's own name is "Mapmonde"
		m_well = (level == "Learn_10" || hall) && !(well && std::strcmp(well, "0") == 0);
		m_well_depth = 1.0f;
		const char* falls = std::getenv("OPENRAYMAN_FALLS");
		m_falls_reversed = hall && !(falls && std::strcmp(falls, "0") == 0);
		const char* flat = std::getenv("OPENRAYMAN_STAR_FLOOR");
		m_star_flat = m_well && !(flat && std::strcmp(flat, "0") == 0);
		// Learn_10's well: its stars twinkle (the Hall's lake has its own twinkling overlay).
		const char* twinkle = std::getenv("OPENRAYMAN_TWINKLE");
		m_twinkle = m_well && !hall && !(twinkle && std::strcmp(twinkle, "0") == 0);
		const char* lake = std::getenv("OPENRAYMAN_LAKE");
		// On in the Hall of Doors; OPENRAYMAN_LAKE=0 off, =2 also in the Learn_10 well.
		m_lake = m_well && (hall || (lake && std::strcmp(lake, "2") == 0)) && !(lake && std::strcmp(lake, "0") == 0);
		if(const char* depth = std::getenv("OPENRAYMAN_WELL_DEPTH"))
		{
			char* end = nullptr;
			const float f = std::strtof(depth, &end);
			if(end != depth && *end == '\0' && std::isfinite(f) && f >= 0 && f <= 20)
				m_well_depth = f;
		}
		m_backdrop_sectors.clear();
		m_backdrop_skies.clear();
		const char* enabled = std::getenv("OPENRAYMAN_BACKDROP");
		if(enabled && std::strcmp(enabled, "0") == 0)
			return;
		for(const auto& r : backdrop_recipes)
		{
			if(level != r.level) continue;
			m_backdrop_sectors.assign(r.sectors.begin(), r.sectors.end());
			m_backdrop_skies.assign(r.skies.begin(), r.skies.end());
			m_backdrop_anchor = { r.anchor[0], r.anchor[1], r.anchor[2] };
			m_backdrop_parallax = r.parallax;
			m_sky_parallax = 0.02f;
			auto sky_option = [](const char* name, float fallback, float low, float high) {
				if(const char* value = std::getenv(name))
				{
					char* end = nullptr;
					const float f = std::strtof(value, &end);
					if(end != value && *end == '\0' && std::isfinite(f) && f >= low && f <= high)
						return f;
				}
				return fallback;
			};
			m_sky_stretch = sky_option("OPENRAYMAN_SKY_STRETCH", 1.0f, 1.0f, 32.0f);
			m_sky_drift = sky_option("OPENRAYMAN_SKY_DRIFT", 0.07f, 0.0f, 4.0f);
			m_sky_haze = sky_option("OPENRAYMAN_SKY_HAZE", 0.3f, 0.0f, 1.0f);
			if(const char* value = std::getenv("OPENRAYMAN_SKY_PARALLAX"))
			{
				char* end = nullptr;
				const float f = std::strtof(value, &end);
				if(end != value && *end == '\0' && std::isfinite(f) && f >= 0 && f <= 1)
					m_sky_parallax = f;
			}
			if(const char* value = std::getenv("OPENRAYMAN_BACKDROP_PARALLAX"))
			{
				char* end = nullptr;
				const float f = std::strtof(value, &end);
				if(end != value && *end == '\0' && std::isfinite(f) && f >= 0 && f <= 1)
					m_backdrop_parallax = f;
			}
			break;
		}
	}

	void level_renderer::follow_camera(const glm::vec3& eye) const
	{
		auto was = m_camera_sector;
		m_camera_sector = m_lights.follow(m_camera_sector, { eye.x, eye.y, eye.z });
		static const bool log = std::getenv("OPENRAYMAN_SECTORS") != nullptr;
		if(log && was != m_camera_sector)
		{
			std::string list;
			if(m_camera_sector)
				for(std::size_t s : m_lights.sectors[*m_camera_sector].drawn_with)
					list += std::format(" {}", s);
			std::fprintf(stderr, "camera sector %d at (%.1f %.1f %.1f), drawn with:%s, fog %s\n", m_camera_sector ? int(*m_camera_sector) : -1,
				eye.x, eye.y, eye.z, list.c_str(), m_camera_sector && m_lights.fog(*m_camera_sector) ? "yes" : "no");
		}
		// OPENRAYMAN_ALL_SECTORS=1 draws every sector, as before (for looking around in the viewer).
		static const bool all = std::getenv("OPENRAYMAN_ALL_SECTORS") != nullptr;
		m_drawn_sectors.assign(m_lights.sectors.size(), all || m_camera_sector == std::nullopt);
		if(!m_camera_sector)
			return;
		m_drawn_sectors[*m_camera_sector] = true;
		for(std::size_t s : m_lights.sectors[*m_camera_sector].drawn_with)
			m_drawn_sectors[s] = true;
	}

	void level_renderer::draw(const glm::mat4& view, const glm::mat4& projection, const light* fog_override, float fog_infinite) const
	{
		submission::scope submissions(true);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE); // which faces are double sided isn't decoded yet
		submission::UseProgram(m_program);
		submission::Uniform4f(m_u_well, 0, 0, 0, 0);
		submission::Uniform4f(m_u_falls, 0, 0, 0, 0);
		submission::Uniform2f(m_u_swirl, 0, 0);
		glm::mat4 view_projection = projection * view;
		const glm::mat4 fog_identity(1.0f);
		submission::UniformMatrix4fv(m_u_fog_model, 1, GL_FALSE, glm::value_ptr(fog_identity));
		submission::UniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
		submission::Uniform1f(m_u_edge_clamp, m_edge_clamp ? 1.0f : 0.0f);
		glActiveTexture(GL_TEXTURE0);

		// Where everything goes this frame: cards turn towards the camera.
		const glm::mat4 camera = glm::inverse(view);
		const glm::vec3 eye(camera[3]);
		submission::Uniform3f(m_u_camera, eye.x, eye.y, eye.z);
		const glm::vec3 forward = -glm::normalize(glm::vec3(camera[2]));
		submission::Uniform3f(m_u_camera_forward, forward.x, forward.y, forward.z);
		submission::Uniform1f(m_u_luminosity, m_luminosity - 0.5f);
		// Uniforms keep their values between frames and start at 0: a part alpha of 0 on the first frame
		// cut every solid surface away (the --screenshot frame showed no solid scenery).
		submission::Uniform1f(m_u_part_alpha, 1.0f);
		submission::Uniform1f(m_u_sharpen, 0.0f);
		submission::Uniform1f(m_u_sky_haze, 0.0f);
		std::vector<part> parts;
		parts.reserve(m_level_cards.size() + m_parts.size());
		follow_camera(eye);
		for(const auto* list : { &m_level_cards, &m_parts })
		{
			for(const part& p : *list)
			{
				if(list == &m_level_cards && p.sector && !drawn(*p.sector))
					continue;
				if(p.mesh < m_meshes.size())
				{
					parts.push_back(p);
					parts.back().model = face_camera(p.model, m_meshes[p.mesh].look, camera);
				}
			}
		}

		// Fog goes by the sector being drawn: before each sector's scenery (0x44AF30) and its actors
		// (0x44B1B0) the exe sets the first fog light of that sector's own list, or none (0x44AE80).
		// So from the cave the valley outside is fogged, though the cave has no fog. The camera's
		// sector gives the background (and, here, what has no sector of its own).
		submission::Uniform1f(m_u_fog_infinite, fog_override ? fog_infinite : 0.0f);
		std::optional<std::size_t> fogged = no_sector; // what the fog uniforms hold now
		auto use_fog = [&](std::optional<std::size_t> sector) {
			if(sector == fogged)
				return;
			fogged = sector;
			const light* fog = m_lighting ? (fog_override ? fog_override : (sector && *sector < m_lights.sectors.size() ? m_lights.fog(*sector) : nullptr)) : nullptr;
			if(!fog)
			{
				submission::Uniform4f(m_u_fog, 0, 0, 0, 0);
				return;
			}
			submission::Uniform4f(m_u_fog_color, fog->color[0], fog->color[1], fog->color[2], 1);
			submission::Uniform4f(m_u_fog, fog->near, fog->far, fog->fog_near_blend, fog->fog_far_blend);
		};
		use_fog(m_camera_sector);
		if(m_world_hidden)
		{
			// 0x40B003 -> 0x424440: the pause draws a 6-by-5 colour grid before flushing
			// translucent triangles. Its dark colours replace the level's fog-clear background.
			// 0x4241C0 supplies separable sine waves and a cool/dark/warm palette, in byte RGB.
			const float frame = float(m_pause_background_frame++);
			std::array<vertex, 30> grid;
			for(int y = 0; y < 5; ++y)
				for(int x = 0; x < 6; ++x)
				{
					const float horizontal = 14 * std::sin(frame * 0.0122718466f + x * 1.04719758f)
						+ 8 * std::sin(frame * 0.036815539f + x * 1.57079637f)
						+ 6 * std::sin(frame * 0.0245436933f + x * 2.09439516f);
					const float vertical = 12 * std::sin(frame * 0.0336599201f + y * 1.57079637f)
						+ 10 * std::sin(frame * 0.0168299600f + y * 0.942477822f);
					const int wave = int(horizontal) + int(vertical); // truncated separately by the exe
					const glm::vec3 end = wave < 0 ? glm::vec3(8, 23, 34) : glm::vec3(94, 17, 9);
					glm::vec3 color = glm::vec3(5) + (end - glm::vec3(5)) * (std::abs(wave) / 52.0f);
					if((x == 2 || x == 3) && (y == 2 || y == 3)) color = glm::vec3(5);
					for(int c = 0; c < 3; ++c) color[c] = float(int(color[c])) / 255.0f;
					grid[y * 6 + x] = { glm::vec3(x * 0.4f - 1, 1 - y * 0.5f, 0), {}, {}, {}, glm::vec4(color, 1) };
				}
			std::vector<vertex> triangles;
			for(int y = 0; y < 4; ++y)
				for(int x = 0; x < 5; ++x)
					for(int corner : { y * 6 + x, y * 6 + x + 1, (y + 1) * 6 + x,
						y * 6 + x + 1, (y + 1) * 6 + x + 1, (y + 1) * 6 + x })
						triangles.push_back(grid[corner]);
			glDisable(GL_DEPTH_TEST);
			set_blend(blend::none);
			glDepthMask(GL_FALSE);
			submission::UniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(fog_identity));
			submission::UniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(fog_identity));
			submission::Uniform1i(m_u_lighting, 5);
			submission::Uniform4f(m_u_fog, 0, 0, 0, 0);
			submission::Uniform2f(m_u_uv_offset, 0, 0);
			submission::BindTexture(GL_TEXTURE_2D, m_white);
			submission::BindVertexArray(m_sprite_vao);
			submission::BindBuffer(GL_ARRAY_BUFFER, m_sprite_vbo);
			glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(triangles.size() * sizeof(vertex)), triangles.data(), GL_STREAM_DRAW);
			draw_triangles(0, GLsizei(triangles.size()));
			glEnable(GL_DEPTH_TEST);
			glDepthMask(GL_TRUE);
			submission::UniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
			fogged = no_sector;
			use_fog(m_camera_sector);
		}

		submission::stage(submission::pass::sky);
		// The sky first, behind everything: the camera's sector's, unlit and unfogged, centred on the
		// camera so it never comes nearer. It counts as infinitely far, so a fog override's infinite
		// distance hides it like any far object (the real game shows only the background colour
		// during Jail_10's big flash).
		if(m_lighting && !m_world_hidden && !(fog_override && fog_infinite > 0))
		{
			auto s = m_camera_sector;
			auto sky = s ? m_skies.find(*s) : m_skies.end();
			if(sky != m_skies.end())
			{
				glm::vec4 fog_was(0.0f);
				glGetUniformfv(m_program, m_u_fog, glm::value_ptr(fog_was));
				submission::Uniform4f(m_u_fog, 0, 0, 0, 0);
				set_blend(blend::none);
				submission::Uniform1f(m_u_alpha_cut, -1.0f);
				glDisable(GL_DEPTH_TEST);
				glDepthMask(GL_FALSE);
				submission::Uniform1i(m_u_lighting, 3);
				glm::mat4 model = glm::translate(glm::mat4(1.0f), eye) * glm::scale(glm::mat4(1.0f), glm::vec3(500.0f));
				submission::UniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(model));
				const batch& b = sky->second;
				submission::BindVertexArray(m_vao);
				submission::BindTexture(GL_TEXTURE_2D, texture_at(b.animation, b.texture));
				submission::Uniform2f(m_u_uv_offset, float(std::fmod(b.scroll_offset.x + double(b.scroll.x) * m_time, 1.0)), float(std::fmod(b.scroll_offset.y + double(b.scroll.y) * m_time, 1.0)));
				draw_triangles(b.first, b.count);
				glEnable(GL_DEPTH_TEST);
				glDepthMask(GL_TRUE);
				submission::Uniform4fv(m_u_fog, 1, glm::value_ptr(fog_was));
			}
		}

		// The lights on each part: its sector's, the strongest few where it stands.
		std::vector<part_lights> part_light(parts.size());
		if(m_lighting)
		{
			for(std::size_t i = 0; i < parts.size(); i++)
				part_light[i] = lights_for(parts[i]);
		}
		// Each part's sector, for its fog. Screen objects are drawn in a pass without fog (0x45CC80
		// clears it, 0x424190).
		std::vector<std::optional<std::size_t>> part_sector(parts.size());
		for(std::size_t i = 0; i < parts.size(); i++)
		{
			const part& p = parts[i];
			if(p.screen)
				continue;
			glm::vec3 at(p.model[3]);
			part_sector[i] = p.sector ? p.sector : m_lights.sector_at({ at.x, at.y, at.z });
		}

		submission::BindVertexArray(m_vao);
		const glm::mat4 identity(1.0f);
		auto use_lights = [this](const part_lights& l) {
			submission::Uniform1f(m_u_sharpen, l.sharpen ? 1.0f : 0.0f);
			submission::Uniform1i(m_u_light_count, l.count);
			if(l.count == 0)
				return;
			submission::Uniform1iv(m_u_light_kind, l.count, l.kind.data());
			submission::Uniform3fv(m_u_light_vector, l.count, glm::value_ptr(l.vector[0]));
			submission::Uniform4fv(m_u_light_color, l.count, glm::value_ptr(l.color[0]));
			submission::Uniform2fv(m_u_light_range, l.count, glm::value_ptr(l.range[0]));
		};
		// One-sided surfaces (material flag 0x400): the drivers drop triangles by the sign of their
		// projected area (GliVd1vf.dll 0x1000311B, 0x10003520/0x10003580; GliDX6vf.dll 0x1000237B).
		// Keeping a positive area there is keeping counter-clockwise here (the other way round shows
		// Rayman's face through the back of his head).
		static const bool no_cull = std::getenv("OPENRAYMAN_NO_CULL") != nullptr;
		cull culled = cull::none;
		// A mirrored part (negative determinant: Rayman's left shoe is the right one with a
		// negative scale) turns its triangles' winding over on screen, so the side kept swaps.
		bool mirrored = false, culled_mirrored = false;
		glDisable(GL_CULL_FACE);
		auto use_cull = [&](cull faces) {
			if(mirrored && faces != cull::none)
				faces = faces == cull::keep_positive ? cull::keep_negative : cull::keep_positive;
			if(no_cull || (faces == culled && mirrored == culled_mirrored))
				return;
			culled = faces;
			culled_mirrored = mirrored;
			if(faces == cull::none)
			{
				glDisable(GL_CULL_FACE);
				return;
			}
			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);
			glFrontFace(faces == cull::keep_positive ? GL_CCW : GL_CW);
		};
		auto set_model = [&](const glm::mat4& model) {
			submission::UniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(model));
			mirrored = glm::determinant(glm::mat3(model)) < 0.0f;
		};
		bool indexed_deferred = false;
		bool backdrop_pass = false;
		bool sky_pass = false;
		submission::stage(submission::pass::setup);
		// The sky pass's own view, model and eye, to return to after a lightning card.
		glm::mat4 sky_vp_now(1.0f), sky_model_now(1.0f);
		glm::vec3 sky_eye_now(0.0f);
		auto is_backdrop = [&](std::size_t sector) {
			return std::ranges::find(m_backdrop_sectors, sector) != m_backdrop_sectors.end();
		};
		auto visible = [&](std::size_t sector) {
			const bool sky = std::ranges::find(m_backdrop_skies, sector) != m_backdrop_skies.end();
			if(sky_pass) return sky && drawn(sector);
			// The backdrop shows only where the game would draw its sector (the camera sector's
			// +0x24 list), so it stays out of views where the game doesn't show it.
			return backdrop_pass ? is_backdrop(sector) && drawn(sector) : !sky && !is_backdrop(sector) && drawn(sector);
		};
		auto draw_batches = [&, this](std::span<const batch> batches, blend mode, bool cores = false, bool screen_depth = false) {
			for(const batch& b : batches)
			{
				if(b.mode != mode || !visible(b.sector))
					continue;
				if(b.sector != no_sector)
					use_fog(b.sector);
				const light* sky_fog = sky_pass && m_lighting && b.sector < m_lights.sectors.size() ? m_lights.fog(b.sector) : nullptr;
				// Sector 4's overlay cards have no fog light. Use the visible shell's
				// colour so their horizon fade agrees with the sky they brighten.
				if(sky_pass && m_lighting && !sky_fog)
					for(const auto sector : m_backdrop_skies)
						if(drawn(sector) && (sky_fog = m_lights.fog(sector))) break;
				submission::Uniform1f(m_u_sky_haze, sky_fog ? m_sky_haze : 0.0f);
				if(sky_fog)
					submission::Uniform3fv(m_u_sky_haze_color, 1, sky_fog->color.data());
				use_cull(b.faces);
				// Depth writes are independent of alpha blending (0x424A00; GliVd1vf 0x10007E18).
				// Deferred triangles have depth writes cleared by the driver queue (0x1000125F).
				glDepthMask(cores || ((mode == blend::none || screen_depth) && b.depth_write) ? GL_TRUE : GL_FALSE);
				const GLuint texture = texture_at(b.animation, b.texture);
				submission::BindTexture(GL_TEXTURE_2D, texture);
				static const bool classic_lightning = [] {
					const char* v = std::getenv("OPENRAYMAN_LIGHTNING_CLASSIC");
					return v && std::strcmp(v, "1") == 0;
				}();
				const bool lightning = mode == blend::alpha && !classic_lightning && m_lightning_textures.contains(texture);
				submission::Uniform1f(m_u_lightning, lightning ? 1.0f : 0.0f);
				if(lightning) submission::BlendFunc(GL_DST_COLOR, GL_ONE);
				// Lightning cards stay where the game puts them, seen from the real camera: drawn from
				// the far sky eye they sank to the horizon, far ahead (user, 2026-11-13). They only
				// brighten what is behind them, so the far clouds they light can stay far. No depth
				// test: the shell's depth comes from the other eye.
				const bool flash_near = lightning && sky_pass;
				if(flash_near)
				{
					submission::UniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
					submission::UniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(fog_identity));
					submission::UniformMatrix4fv(m_u_fog_model, 1, GL_FALSE, glm::value_ptr(fog_identity));
					submission::Uniform3fv(m_u_sky_eye, 1, glm::value_ptr(eye));
					submission::Uniform3fv(m_u_camera, 1, glm::value_ptr(eye));
					glDisable(GL_DEPTH_TEST);
				}
				// 0x424C00 wraps the current offset at a whole texture width, including mirrored textures.
				const double scroll_time = m_time * (sky_pass ? m_sky_drift : 1.0f);
				// Ours: the Hall of Doors' river spreads out from the waterfall's foot (u_falls).
				const bool falls = m_falls_reversed && b.well == 3 && m_fall_foot;
				submission::Uniform4f(m_u_falls, falls ? 1.0f : 0.0f, falls ? m_fall_foot->x : 0.0f, falls ? m_fall_foot->y : 0.0f, falls ? float(std::fmod(m_time, 3600.0)) : 0.0f);
				// The star materials keep counting past whole widths: their scrambled tiles are picked by
				// position, so wrapping every width would reshuffle the whole field.
				const double wrap = m_well && (b.well == 1 || b.well == 2) ? 1000.0 : 1.0;
				glm::vec2 offset(std::fmod(b.scroll_offset.x + double(b.scroll.x) * scroll_time, wrap), std::fmod(b.scroll_offset.y + double(b.scroll.y) * scroll_time, wrap));
				submission::Uniform2f(m_u_uv_offset, offset.x, offset.y);
				submission::Uniform2f(m_u_swirl, m_swirl_centre && m_world_hidden && b.pause_swirl ? 1.0f : 0.0f, m_world_hidden && b.pause_swirl ? float(m_time) : 0.0f);
				if(m_lake && b.well == 2)
				{
					// The Hall of Doors' star overlay: it ripples in place and twinkles (no depth, no flow).
					submission::Uniform4f(m_u_well, 2.0f, 0.0f, 0.0f, 0.0f);
					submission::Uniform2f(m_u_lake, 1.0f, float(std::fmod(m_time, 6283.1853)));
				}
				else if(m_well && b.well == 1)
				{
					// Star layers below the surface; the deeper ones also drift slowly on their own.
					// The lake's deep layers drift at a quarter of the well's pace: a lake, not a river.
					const double drift = m_lake ? 0.25 : 1.0;
					submission::Uniform4f(m_u_well, 1.0f, m_well_depth, float(std::fmod(m_time * -0.02 * drift, 1000.0)), float(std::fmod(m_time * -0.05 * drift, 1000.0)));
					submission::Uniform2f(m_u_lake, m_lake ? 1.0f : 0.0f, float(std::fmod(m_time, 6283.1853)));
					submission::Uniform1f(m_u_twinkle, m_twinkle ? 1.0f : 0.0f);
					const bool flat = m_star_flat && m_star_texels > 0 && m_star_floor < 1e29f;
					submission::Uniform2f(m_u_star_floor, flat ? m_star_floor : 0.0f, flat ? m_star_texels : 0.0f);
				}
				submission::Uniform4fv(m_u_material_ambient, 1, glm::value_ptr(b.ambient));
				submission::Uniform4fv(m_u_material_diffuse, 1, glm::value_ptr(b.diffuse));
				if(indexed_deferred)
				{
					submission::DrawElements(GL_TRIANGLES, b.count, GL_UNSIGNED_INT,
						reinterpret_cast<const void*>(std::uintptr_t(b.first) * sizeof(GLuint)));
					game::profiler::add(game::profiler::counter::triangles, std::uint64_t(b.count / 3));
				}
				else
					draw_triangles(b.first, b.count);
				submission::Uniform2f(m_u_swirl, 0, 0);
				glDepthMask(mode == blend::none || cores ? GL_TRUE : GL_FALSE);
				submission::Uniform4f(m_u_well, 0, 0, 0, 0);
				submission::Uniform4f(m_u_falls, 0, 0, 0, 0);
				if(flash_near)
				{
					const glm::mat4 sky_fog_now = glm::inverse(sky_model_now);
					submission::UniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(sky_vp_now));
					submission::UniformMatrix4fv(m_u_model, 1, GL_FALSE, glm::value_ptr(sky_model_now));
					submission::UniformMatrix4fv(m_u_fog_model, 1, GL_FALSE, glm::value_ptr(sky_fog_now));
					submission::Uniform3fv(m_u_sky_eye, 1, glm::value_ptr(sky_eye_now));
					submission::Uniform3fv(m_u_camera, 1, glm::value_ptr(fog_override ? eye : sky_eye_now));
					glEnable(GL_DEPTH_TEST);
				}
				if(lightning) submission::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				submission::Uniform1f(m_u_lightning, 0.0f);
			}
		};
		auto morph_geometry = [&](const part& p) -> const geometric_object* {
			const auto& a = m_meshes[p.mesh].geometry;
			const geometric_object* b = p.morph_mesh && *p.morph_mesh < m_meshes.size() ? &m_meshes[*p.morph_mesh].geometry : nullptr;
			if(b && (a.elements.size() != b->elements.size() || a.vertices.size() != b->vertices.size() ||
				a.normals.size() != b->normals.size() || !a.sprites.empty() || !b->sprites.empty() ||
				!a.skipped_types.empty() || !b->skipped_types.empty())) b = nullptr;
			return b;
		};
		// Temporarily replace this mesh's expanded corners for morphing and saved vertex colours;
		// restore them after drawing so other instances keep their own state.
		auto morph = [&](const part& p, bool restore) {
			const mesh& source = m_meshes[p.mesh];
			const auto& a = source.geometry;
			const auto* b = morph_geometry(p);
			const std::vector<saved_vertex_colour>* colours = nullptr;
			const auto selected = p.colour_set.value_or(a.lod_count == 1 ? 1 : 0);
			if(selected && selected <= a.colour_sets.size())
			{
				const auto& set = a.colour_sets[selected - 1];
				if(!set.empty() && set[0].size() == a.vertices.size()) colours = &set[0];
			}
			if(source.vertices.empty() || (!b && !colours)) return;
			submission::BindBuffer(GL_ARRAY_BUFFER, m_vbo);
			if(restore)
			{
				glBufferSubData(GL_ARRAY_BUFFER, GLintptr(source.first * sizeof(vertex)), GLsizeiptr(source.vertices.size()), source.vertices.data());
				return;
			}
			std::vector<vertex> blended(source.vertices.size() / sizeof(vertex));
			std::memcpy(blended.data(), source.vertices.data(), source.vertices.size());
			const float amount = p.morph_amount / 100.0f;
			for(vertex& v : blended)
			{
				const auto i = v.source_index;
				if(colours)
				{
					const auto& c = (*colours)[i];
					v.color = glm::vec4(c[0], c[1], c[2], c[3]) / 256.0f;
				}
				if(!b) continue;
				const auto& pos = b->vertices[i];
				v.position = glm::mix(v.position, glm::vec3(pos[0], pos[1], pos[2]), amount);
				if(i < b->normals.size())
				{
					const auto& n = b->normals[i];
					auto normal = glm::mix(v.normal, glm::vec3(n[0], n[1], n[2]), amount);
					if(glm::length(normal) > 1e-8f) v.normal = glm::normalize(normal);
				}
			}
			glBufferSubData(GL_ARRAY_BUFFER, GLintptr(source.first * sizeof(vertex)), GLsizeiptr(source.vertices.size()), blended.data());
		};
		// Sky was drawn above. These static-only recipes keep the same materials,
		// fog, lighting and cut-out cores as normal scenery, with their own depth.
		if(!m_backdrop_sectors.empty())
		{
			auto draw_static_pass = [&](const glm::vec3& pass_eye, const glm::mat4& model) {
				submission::stage(submission::pass::sky);
				set_model(model);
				submission::Uniform1i(m_u_lighting, m_lighting ? 1 : 0);
				set_blend(blend::none);
				draw_batches(m_batches, blend::none);
				submission::Uniform1f(m_u_alpha_cut, 0.9f);
				submission::Uniform1f(m_u_light_alpha, 1.0f);
				for(const auto& s : m_see_through)
					if(m_solid_cores.contains(s.b.texture))
						draw_batches(std::span(&s.b, 1), blend::alpha, true);
				std::vector<const see_through_triangle*> order;
				for(const auto& t : m_see_through_triangles)
					if(visible(m_see_through[t.object].b.sector)) order.push_back(&t);
				std::ranges::stable_sort(order, [&](const auto* a, const auto* b) {
					return glm::length(glm::vec3(model * glm::vec4(a->centre, 1)) - pass_eye) > glm::length(glm::vec3(model * glm::vec4(b->centre, 1)) - pass_eye);
				});
				for(const auto* t : order)
				{
					batch b = m_see_through[t->object].b;
					b.first = t->first;
					b.count = 3;
					set_blend(b.mode);
					draw_batches(std::span(&b, 1), b.mode);
				}
			};
			// Jail_10's IPO sky shells and cloud/lightning cards precede the backdrop.
			// Retain sky-shell depth while drawing clouds so the shell still clips them.
			sky_pass = true;
			// The sea and ship have separate shells at different heights. Follow the
			// eye between their layers so that the view stays inside the visible shell.
			glm::vec3 sky_anchor = m_backdrop_anchor;
			if(!m_backdrop_skies.empty())
				sky_anchor.z = std::min(std::max(eye.z, 350.0f), m_backdrop_anchor.z); // Jail_10: the sea shell lies below 383, the ship's above 483
			const glm::vec3 sky_eye = sky_anchor + (eye - sky_anchor) * m_sky_parallax;
			glm::mat4 sky_camera = camera;
			sky_camera[3] = glm::vec4(sky_eye, 1);
			// The horizontal stretch can carry the shell beyond the normal far plane.
			// Extend only this pass's clip range, preserving the supplied near plane/FOV.
			glm::mat4 sky_projection = projection;
			if(!m_backdrop_skies.empty() && m_sky_stretch > 1.0f)
			{
				const float near = projection[3][2] / (projection[2][2] - 1.0f);
				const float far = projection[3][2] / (projection[2][2] + 1.0f) * m_sky_stretch;
				sky_projection[2][2] = -(far + near) / (far - near);
				sky_projection[3][2] = -2.0f * far * near / (far - near);
			}
			const glm::mat4 sky_vp = sky_projection * glm::inverse(sky_camera);
			submission::UniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(sky_vp));
			submission::Uniform3fv(m_u_camera, 1, glm::value_ptr(fog_override ? eye : sky_eye));
			submission::Uniform3fv(m_u_sky_eye, 1, glm::value_ptr(sky_eye));
			const glm::mat4 sky_model = glm::translate(identity, sky_anchor) *
				glm::scale(identity, glm::vec3(m_sky_stretch, m_sky_stretch, 1)) * glm::translate(identity, -sky_anchor);
			const glm::mat4 sky_fog_model = glm::inverse(sky_model);
			sky_vp_now = sky_vp;
			sky_model_now = sky_model;
			sky_eye_now = sky_eye;
			submission::UniformMatrix4fv(m_u_fog_model, 1, GL_FALSE, glm::value_ptr(sky_fog_model));
			draw_static_pass(sky_eye, sky_model);
			submission::UniformMatrix4fv(m_u_fog_model, 1, GL_FALSE, glm::value_ptr(identity));
			submission::Uniform1f(m_u_sky_haze, 0.0f);
			glDepthMask(GL_TRUE);
			glClear(GL_DEPTH_BUFFER_BIT);
			sky_pass = false;
			backdrop_pass = true;
			const glm::vec3 far_eye = m_backdrop_anchor + (eye - m_backdrop_anchor) * m_backdrop_parallax;
			glm::mat4 far_camera = camera;
			far_camera[3] = glm::vec4(far_eye, 1);
			const glm::mat4 far_vp = projection * glm::inverse(far_camera);
			submission::UniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(far_vp));
			// A scripted flash measures depth from the real eye. Parallax must not
			// change its coverage on the distant scenery relative to the sky/world.
			submission::Uniform3fv(m_u_camera, 1, glm::value_ptr(fog_override ? eye : far_eye));
			draw_static_pass(far_eye, identity);
			glDepthMask(GL_TRUE);
			glClear(GL_DEPTH_BUFFER_BIT);
			submission::UniformMatrix4fv(m_u_view_projection, 1, GL_FALSE, glm::value_ptr(view_projection));
			submission::Uniform3fv(m_u_camera, 1, glm::value_ptr(eye));
			backdrop_pass = false;
			use_fog(m_camera_sector);
		}

		// Solid surfaces first, the level's and the actors'.
		submission::stage(submission::pass::opaque);
		blend current = blend::none;
		set_blend(current);
		set_model(identity);
		submission::Uniform1i(m_u_lighting, m_lighting ? 1 : 0);
		draw_batches(m_batches, blend::none);
		submission::stage(submission::pass::actors);
		submission::Uniform1i(m_u_lighting, m_lighting ? 2 : 0);
		for(std::size_t i = 0; i < parts.size(); i++)
		{
			if(parts[i].screen || parts[i].alpha < 1)
				continue; // drawn blended, with the see-through ones
			set_model(parts[i].model);
			use_lights(part_light[i]);
			use_fog(part_sector[i]);
			submission::Uniform1f(m_u_part_alpha, parts[i].alpha);
			morph(parts[i], false);
			draw_batches(m_meshes[parts[i].mesh].batches, blend::none);
			morph(parts[i], true);
		}
		submission::Uniform1f(m_u_sharpen, 0.0f);
		submission::Uniform1f(m_u_part_alpha, 1.0f);
		submission::stage(submission::pass::cores);
		// The nearly opaque middle of cut-out-like soft surfaces (a leaf, not its fringe) is solid
		// too: it hides whatever see-through things lie behind it, however they're sorted (sprites come
		// last). Not for soft gradients like light rays, whose bright body must stay see-through.
		submission::Uniform1f(m_u_alpha_cut, 0.9f);
		submission::Uniform1f(m_u_light_alpha, 1.0f);
		submission::Uniform1i(m_u_lighting, m_lighting ? 1 : 0);
		set_model(identity);
		auto draw_cores = [&](std::span<const batch> batches) {
			for(const batch& b : batches)
			{
				if(m_solid_cores.contains(b.texture))
					draw_batches(std::span(&b, 1), blend::alpha, true);
			}
		};
		for(const see_through& s : m_see_through)
			draw_cores(std::span(&s.b, 1));
		submission::Uniform1i(m_u_lighting, m_lighting ? 2 : 0);
		for(std::size_t i = 0; i < parts.size(); i++)
		{
			if(parts[i].screen || parts[i].alpha < 1)
				continue;
			set_model(parts[i].model);
			use_lights(part_light[i]);
			use_fog(part_sector[i]);
			submission::Uniform1f(m_u_part_alpha, parts[i].alpha);
			morph(parts[i], false);
			draw_cores(m_meshes[parts[i].mesh].batches);
			morph(parts[i], true);
		}
		submission::Uniform1f(m_u_sharpen, 0.0f);
		submission::Uniform1f(m_u_part_alpha, 1.0f);

		use_fog(m_camera_sector);
		use_cull(cull::none);
		submission::stage(submission::pass::shadows);
		// Shadows: blobs laid on the ground below the actors that cast them, over the solid surfaces.
		if(m_shadow_texture && !m_shadows.empty())
		{
			std::vector<vertex> quads;
			for(const shadow& s : m_shadows)
			{
				auto ground = ground_below(glm::vec3(s.actor[3]));
				if(!ground)
					continue;
				const auto& [at, n] = *ground;
				// Projected straight down like the game's: the actor's X and Y, flattened.
				glm::vec2 x(s.actor[0]), y;
				x = glm::length(x) < 1e-4f ? glm::vec2(1, 0) : glm::normalize(x);
				y = glm::vec2(-x.y, x.x);
				const glm::vec2 c(at);
				const float reach = std::max(s.size.x, s.size.y);
				// u 0..2 and v -1..1 across the blob, its centre (u 1, v 0) in the middle, mirrored
				// outwards; outside the bounds nothing is drawn.
				const glm::vec4 bounds{ 0, -1, 2, 1 };
				auto uv = [&](const glm::vec3& p) {
					glm::vec2 d = glm::vec2(p) - c;
					return glm::vec2(1 + glm::dot(d, x) / s.size.x, glm::dot(d, y) / s.size.y);
				};
				// Every solid triangle under the blob, near the ground's height, gets a copy lifted a
				// little along its normal: the blob follows the ground over creases and steps.
				for(std::size_t i = 0; i + 2 < m_floor.size(); i += 3)
				{
					if(!catches_shadow(i / 3))
					continue;
				const glm::vec3 &a = m_floor[i], &b = m_floor[i + 1], &d = m_floor[i + 2];
					if(std::min({ a.x, b.x, d.x }) > c.x + reach || std::max({ a.x, b.x, d.x }) < c.x - reach ||
						std::min({ a.y, b.y, d.y }) > c.y + reach || std::max({ a.y, b.y, d.y }) < c.y - reach)
						continue;
					if(std::min({ a.z, b.z, d.z }) > at.z + 1.5f || std::max({ a.z, b.z, d.z }) < at.z - 1.5f)
						continue;
					glm::vec3 face = glm::cross(b - a, d - a);
					if(glm::length(face) < 1e-8f || std::abs(glm::normalize(face).z) < 0.3f)
						continue; // walls catch no shadow
					// The blob is mapped by X and Y only, so a big slope that merely reaches the
					// ground's height somewhere would show it where it passes above (or below) the
					// actor. Its plane must be near the ground under the blob's centre.
					const float plane_z = a.z - (face.x * (c.x - a.x) + face.y * (c.y - a.y)) / face.z;
					if(std::abs(plane_z - at.z) > 0.75f)
						continue;
					glm::vec3 lift = glm::normalize(face.z < 0 ? -face : face) * 0.02f;
					for(const glm::vec3* p : { &a, &b, &d })
						quads.push_back({ *p + lift, uv(*p), n, bounds });
				}
			}
			if(!quads.empty())
			{
				set_blend(current = blend::alpha);
				submission::Uniform1i(m_u_lighting, 3);
				set_model(identity);
				submission::Uniform2f(m_u_uv_offset, 0, 0);
				submission::Uniform1f(m_u_edge_clamp, 2.0f);
				glEnable(GL_POLYGON_OFFSET_FILL);
				glPolygonOffset(-1, -1);
				submission::BindVertexArray(m_sprite_vao);
				submission::BindBuffer(GL_ARRAY_BUFFER, m_sprite_vbo);
				glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(quads.size() * sizeof(vertex)), quads.data(), GL_STREAM_DRAW);
				submission::BindTexture(GL_TEXTURE_2D, m_shadow_texture);
				draw_triangles(0, GLsizei(quads.size()));
				glDisable(GL_POLYGON_OFFSET_FILL);
				submission::Uniform1f(m_u_edge_clamp, m_edge_clamp ? 1.0f : 0.0f);
				submission::BindVertexArray(m_vao);
			}
		}

		submission::stage(submission::pass::deferred);
		// Then soft and glowing triangles over the solid scene, using the driver's depth buckets.
		constexpr std::size_t level = SIZE_MAX;
		struct see_through_draw
		{
			transparent_sort_key key;
			const batch* b;
			std::size_t part;
			GLint first; // one expanded triangle in the vertex buffer
			blend mode = blend::alpha; // how it's drawn: its own, or alpha for a screen object's cut-outs
		};
		std::vector<see_through_draw> order;
		// GliVd1vf 0x10002151 uses the smallest reciprocal depth of the triangle's corners.
		// 0x100011B0 buckets its float bits and prepends to a linked list: equal depths draw in
		// reverse submission order. The swirl's coincident layers depend on this tie rule.
		auto enqueue = [&](const batch& b, std::size_t part_index, GLint first, blend mode,
			const std::array<glm::vec3, 3>& corners) {
			std::array<float, 3> depths;
			for(int k = 0; k < 3; ++k) depths[k] = glm::dot(corners[k] - eye, forward);
			order.push_back({ transparent_sort_key::from_depths(depths, order.size(), part_index != level && parts[part_index].screen),
				&b, part_index, first, mode });
		};
		for(const see_through_triangle& t : m_see_through_triangles)
			if(visible(m_see_through[t.object].b.sector))
				enqueue(m_see_through[t.object].b, level, t.first, m_see_through[t.object].b.mode, t.corners);
		for(std::size_t i = 0; i < parts.size(); i++)
		{
			const auto& source = m_meshes[parts[i].mesh];
			const auto* target = morph_geometry(parts[i]);
			for(const batch& b : source.batches)
			{
				// A screen object's cut-outs are blended too, so their sharpened edges can be smooth
				// (u_sharpen); in the solid pass they'd be cut at a staircase of texels.
				if(b.mode != blend::none || parts[i].screen || parts[i].alpha < 1)
					for(GLint t = b.first; t + 2 < b.first + b.count; t += 3)
					{
						std::array<glm::vec3, 3> corners;
						for(int k = 0; k < 3; ++k)
						{
							vertex v;
							std::memcpy(&v, source.vertices.data() + (t + k - source.first) * sizeof(vertex), sizeof(vertex));
							if(target)
							{
								const auto& position = target->vertices[v.source_index];
								v.position = glm::mix(v.position, glm::vec3(position[0], position[1], position[2]), parts[i].morph_amount / 100.0f);
							}
							corners[k] = glm::vec3(parts[i].model * glm::vec4(v.position, 1));
						}
						enqueue(b, i, t, b.mode == blend::none ? blend::alpha : b.mode, corners);
					}
			}
		}
		// Screen objects come after everything of the world: the exe draws them in a view of their
		// own (0x419A30 -> 0x45CC80) once the world is done. Sorted by distance with the rest, the
		// pause menu's swirl (an actor around the camera) painted over the menu's Lum.
		std::ranges::stable_sort(order, [&](const see_through_draw& a, const see_through_draw& b) {
			return a.key < b.key;
		});
		// Index the original expanded vertices in EXACT sorted order, including reversed depth
		// ties. GL 3.3 and ES 3.0 both support uint indices. Upload once for all three stages.
		if(!submission::baseline())
		{
			std::vector<GLuint> indices;
			indices.reserve(order.size() * 3);
			for(const auto& d : order)
				for(int corner = 0; corner < 3; ++corner)
					indices.push_back(GLuint(d.first + corner));
			submission::BindVertexArray(m_vao);
			submission::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_deferred_ebo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(GLuint)), indices.data(), GL_STREAM_DRAW);
		}
		// 0: world transparency, 1: opaque screen cut-outs, 2: deferred screen geometry.
		auto draw_order = [&](int stage) {
			submission::stage(submission::pass::deferred);
			std::size_t placed = level - 1; // whose model and lights are set
			for(std::size_t k = 0; k < order.size(); k++)
			{
				const see_through_draw& d = order[k];
				const bool screen = d.part != level && parts[d.part].screen;
				const bool solid_screen = screen && d.b->mode == blend::none && parts[d.part].alpha == 1;
				if(stage != (screen ? (solid_screen ? 1 : 2) : 0)) continue;
				if(d.mode != current)
					set_blend(current = d.mode);
				if(d.part != placed)
				{
					if(placed < parts.size()) morph(parts[placed], true);
					placed = d.part;
					bool is_level = d.part == level;
					set_model(is_level ? identity : parts[d.part].model);
					submission::Uniform1i(m_u_lighting, m_lighting ? (is_level ? 1 : 2) : 0);
					submission::Uniform1f(m_u_part_alpha, is_level ? 1.0f : parts[d.part].alpha);
					if(!is_level)
					{
						use_lights(part_light[d.part]);
						use_fog(part_sector[d.part]);
						morph(parts[d.part], false);
						if(parts[d.part].screen)
						{
							submission::Uniform4f(m_u_fog, 0, 0, 0, 0);
							fogged = no_sector;
						}
					}
					else
						submission::Uniform1f(m_u_sharpen, 0.0f);
				}
				// Adjacent sorted triangles with identical state share a draw, even for
				// reversed/scattered vertex ranges. Never reorder triangles or cross parts.
				batch run = *d.b;
				run.mode = d.mode;
				run.first = submission::baseline() ? d.first : GLint(k * 3);
				run.count = 3;
				while(k + 1 < order.size() && order[k + 1].part == d.part &&
					order[k + 1].mode == d.mode && (submission::baseline()
						? order[k + 1].b == d.b && order[k + 1].first == run.first + run.count
						: order[k + 1].b->same_state(*d.b)))
					run.count += 3, k++;
				indexed_deferred = !submission::baseline();
				draw_batches(std::span(&run, 1), run.mode, false, solid_screen);
				indexed_deferred = false;
			}
			if(placed < parts.size()) morph(parts[placed], true);
			set_model(identity);
			submission::Uniform1f(m_u_sharpen, 0.0f);
			submission::Uniform1f(m_u_part_alpha, 1.0f);
			use_fog(m_camera_sector);
			use_cull(cull::none);
			// Sprites are glows and flames: they shine by themselves.
			submission::Uniform1i(m_u_lighting, m_lighting ? 3 : 0);
			submission::Uniform2f(m_u_uv_offset, 0, 0);
		};
		draw_order(0);

		auto draw_sprites = [&](bool screen) {
			submission::stage(submission::pass::sprites);
			// The level's sprites, and the actors' where their parts are now.
			std::vector<sprite_instance> sprites = screen ? std::vector<sprite_instance>{} : m_sprites;
			for(sprite_instance& s : sprites)
				s.texture = texture_at(s.animation, s.texture);
			for(const part& p : parts)
			{
				if(p.screen != screen) continue;
				glm::vec2 scale(std::cbrt(std::abs(glm::determinant(glm::mat3(p.model)))));
				if(screen)
				{
					// 0x4291E0 uses 0x446200/0x446250 on the combined model/view matrix:
					// screen X and Y row lengths, not a single volume-average scale.
					const auto mv = glm::mat3(view * p.model);
					scale = { glm::length(glm::vec3(mv[0][0], mv[1][0], mv[2][0])),
						glm::length(glm::vec3(mv[0][1], mv[1][1], mv[2][1])) };
				}
				for(sprite_instance s : m_meshes[p.mesh].sprites)
				{
					s.center = glm::vec3(p.model * glm::vec4(s.center, 1.0f));
					s.texture = texture_at(s.animation, s.texture);
					s.half_size *= scale;
					sprites.push_back(s);
				}
			}
			std::ranges::sort(sprites, [](const sprite_instance& a, const sprite_instance& b) {
				return std::tie(a.mode, a.texture) < std::tie(b.mode, b.texture);
			});

			// Sprites: squares facing the camera, built from its right and up directions. Their normal
			// points at the stand-in sun so they come out fully lit.
			if(!sprites.empty())
			{
				glm::vec3 right(view[0][0], view[1][0], view[2][0]);
				glm::vec3 up(view[0][1], view[1][1], view[2][1]);
				glm::vec3 lit = glm::normalize(glm::vec3(0.3f, 0.2f, 0.9f));
				std::vector<vertex> quads;
				quads.reserve(sprites.size() * 6);
				for(const sprite_instance& s : sprites)
				{
					glm::vec3 x = right * s.half_size.x, y = up * s.half_size.y;
					// v counts up in the game; our textures are uploaded top row first.
					const float u = s.uv_extent.x, v = s.uv_extent.y;
					// A plain sprite shows its texture once; a mirrored one spans two copies, unbounded.
					glm::vec4 bounds{ -1e6f, -1e6f, 1e6f, 1e6f };
					if(u <= 1)
						bounds.x = 0, bounds.z = 1;
					if(v <= 1)
						bounds.y = 0, bounds.w = 1;
					vertex bl{ s.center - x - y, { 0, v }, lit, bounds }, br{ s.center + x - y, { u, v }, lit, bounds };
					vertex tl{ s.center - x + y, { 0, 0 }, lit, bounds }, tr{ s.center + x + y, { u, 0 }, lit, bounds };
					quads.insert(quads.end(), { bl, tl, br, br, tl, tr });
				}
				submission::BindVertexArray(m_sprite_vao);
				submission::BindBuffer(GL_ARRAY_BUFFER, m_sprite_vbo);
				glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(quads.size() * sizeof(vertex)), quads.data(), GL_STREAM_DRAW);
				// Additive sprites take no fog: the game's driver clears their fog factor (GliDX6vf.dll
				// 0x10008F4C for texture flag 0x20 without 0x08, used by the sprite routine 0x1000181D).
				// Faded by distance, Rayman's shot was too dim to see shrink when it ran out.
				glm::vec4 fog_was(0.0f);
				glGetUniformfv(m_program, m_u_fog, glm::value_ptr(fog_was));
				for(std::size_t first = 0; first < sprites.size();)
				{
					std::size_t last = first;
					while(last < sprites.size() && sprites[last].texture == sprites[first].texture && sprites[last].mode == sprites[first].mode)
						last++;
					if(sprites[first].mode != current)
						set_blend(current = sprites[first].mode);
					if(sprites[first].mode == blend::additive)
						submission::Uniform4f(m_u_fog, 0, 0, 0, 0);
					else
						submission::Uniform4fv(m_u_fog, 1, glm::value_ptr(fog_was));
					submission::BindTexture(GL_TEXTURE_2D, sprites[first].texture);
					draw_triangles(GLint(first * 6), GLsizei((last - first) * 6));
					first = last;
				}
				submission::Uniform4fv(m_u_fog, 1, glm::value_ptr(fog_was));
			}
		};
		draw_sprites(false);
		submission::stage(submission::pass::particles);
		// Particles keep their far-to-near order. A per-vertex tint lets adjacent
		// quads of the same texture/blend share a draw even when they fade differently.
		if(!m_particles.empty() && !m_effect_materials.empty())
		{
			glm::vec3 right(view[0][0], view[1][0], view[2][0]);
			glm::vec3 up(view[0][1], view[1][1], view[2][1]);
			std::vector<const particle*> farthest_first;
			for(const particle& p : m_particles)
				if(m_effect_materials.contains(p.material))
					farthest_first.push_back(&p);
			std::ranges::sort(farthest_first, [&](const particle* a, const particle* b) {
				return glm::dot(a->center - eye, a->center - eye) > glm::dot(b->center - eye, b->center - eye);
			});
			glm::vec3 lit = glm::normalize(glm::vec3(0.3f, 0.2f, 0.9f));
			std::vector<vertex> quads;
			quads.reserve(farthest_first.size() * 6);
			struct particle_draw { GLuint texture; blend mode; glm::vec4 tint; };
			std::vector<particle_draw> draws;
			auto append_particle = [&](const particle& p, const sprite_instance& m, std::initializer_list<vertex> corners) {
				const float a = std::clamp(p.alpha, 0.0f, 1.0f);
				const glm::vec4 tint = m.mode == blend::additive ? glm::vec4(a) : glm::vec4(1, 1, 1, a);
				draws.push_back({p.age >= 0 ? texture_once(m.animation, m.texture, p.age) : texture_at(m.animation, m.texture), m.mode, tint});
				for(vertex v : corners) { v.color = tint; quads.push_back(v); }
			};
			for(const particle* p : farthest_first)
			{
				const sprite_instance& m = m_effect_materials.at(p->material);
				if(p->segment)
				{
					// 0x460A10 case 0x60000000: a beam. Both ends count, the height difference is
					// dropped (the line stays level), and it is drawn as a rectangle centred on the
					// two points' midpoint: 0.8 wide (0x429CF0 is given 0.4) and as long as the gap,
					// its long axis along the beam and its width square to it. u runs across the
					// beam, v along it, so the 32x64 faisceaumm texture reads as a soft streak.
					glm::vec3 d = p->end - p->center;
					d.z = 0;
					const float length = glm::length(d);
					if(length < 1e-4f)
						continue;
					glm::vec3 mid = (p->center + p->end) * 0.5f;
					mid.z = p->end.z; // 0x460A10 flattens the delta before finding the midpoint.
					const glm::vec3 along = d / length * (length * 0.5f);
					const glm::vec3 across = glm::vec3(-d.y, d.x, 0) / length * 0.4f;
					glm::vec4 bounds{ 0, 0, 1, 1 };
					const vertex a{ mid - along - across, { 0, 0 }, lit, bounds }, b{ mid - along + across, { 1, 0 }, lit, bounds };
					const vertex c{ mid + along - across, { 0, 1 }, lit, bounds }, e{ mid + along + across, { 1, 1 }, lit, bounds };
					append_particle(*p, m, { a, b, c, c, b, e });
					continue;
				}
				const float half_height = p->half_height < 0 ? p->half_size : p->half_height;
				glm::vec3 x = (p->flat ? glm::vec3(1, 0, 0) : right) * p->half_size;
				glm::vec3 y = (p->flat ? glm::vec3(0, 1, 0) : up) * half_height;
				const float u = m.uv_extent.x, v = m.uv_extent.y;
				// A mirrored texture ("_mxy") holds a quarter of the image, so the quad spans two
				// copies and has no single copy to clamp to. Bounding it to 0..1 clipped the second
				// copy to one edge texel, which showed as a flat-edged square around every sparkle
				// of the Hall's selection burst (its material is lums_corps_ad_mxy). Sprites already
				// pick their bounds this way.
				glm::vec4 bounds{ -1e6f, -1e6f, 1e6f, 1e6f };
				if(u <= 1)
					bounds.x = 0, bounds.z = 1;
				if(v <= 1)
					bounds.y = 0, bounds.w = 1;
				vertex bl{ p->center - x - y, { 0, v }, lit, bounds }, br{ p->center + x - y, { u, v }, lit, bounds };
				vertex tl{ p->center - x + y, { 0, 0 }, lit, bounds }, tr{ p->center + x + y, { u, 0 }, lit, bounds };
				append_particle(*p, m, { bl, tl, br, br, tl, tr });
			}
			submission::BindVertexArray(m_sprite_vao);
			submission::BindBuffer(GL_ARRAY_BUFFER, m_sprite_vbo);
			glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(quads.size() * sizeof(vertex)), quads.data(), GL_STREAM_DRAW);

			submission::Uniform1i(m_u_lighting, submission::baseline() ? 4 : 5);
			submission::Uniform1f(m_u_light_alpha, 1.0f);
			for(std::size_t i = 0; i < draws.size();)
			{
				const auto& d = draws[i];
				if(d.mode != current) set_blend(current = d.mode);
				std::size_t end = i + 1;
				if(submission::baseline())
					submission::Uniform4fv(m_u_material_diffuse, 1, glm::value_ptr(d.tint));
				else
					while(end < draws.size() && draws[end].texture == d.texture && draws[end].mode == d.mode) ++end;
				submission::BindTexture(GL_TEXTURE_2D, d.texture);
				draw_triangles(GLint(i * 6), GLsizei((end - i) * 6));
				i = end;
			}
		}

		if(std::ranges::any_of(parts, [](const part& p) { return p.screen; }))
		{
			// 0x419A30 / 0x45CC80: a separate view. Keyed faces write depth before deferred glows.
			glDepthMask(GL_TRUE);
			glClear(GL_DEPTH_BUFFER_BIT);
			submission::BindVertexArray(m_vao);
			draw_order(1);
			draw_order(2);
			draw_sprites(true);
		}

		// The collision overlay, last and slightly pulled forward so it shows on the surfaces it
		// shares with the visible meshes.
		if(m_show_collision && !m_collision.empty())
		{
			glm::vec3 sun = glm::normalize(glm::vec3(0.3f, 0.2f, 0.9f));
			std::vector<vertex> faces;
			faces.reserve(m_collision.size() * 3);
			for(const collision_face& f : m_collision)
			{
				float shade = 0.55f + 0.45f * std::abs(glm::dot(f.normal, sun));
				glm::vec4 colour(glm::vec3(f.colour) * shade, f.colour.a);
				for(const glm::vec3& corner : f.corners)
					faces.push_back(vertex{ corner, { 0, 0 }, f.normal, { -1e6f, -1e6f, 1e6f, 1e6f }, colour });
			}
			submission::BindVertexArray(m_sprite_vao);
			submission::BindBuffer(GL_ARRAY_BUFFER, m_sprite_vbo);
			glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(faces.size() * sizeof(vertex)), faces.data(), GL_STREAM_DRAW);
			set_model(glm::mat4(1));
			submission::Uniform1i(m_u_lighting, 5);
			submission::Uniform1f(m_u_light_alpha, 1.0f);
			submission::Uniform4f(m_u_fog, 0, 0, 0, 0);
			set_blend(current = blend::alpha);
			submission::BindTexture(GL_TEXTURE_2D, m_white);
			glDepthMask(GL_FALSE);
			glEnable(GL_POLYGON_OFFSET_FILL);
			glPolygonOffset(-1, -2);
			draw_triangles(0, GLsizei(faces.size()));
			glDisable(GL_POLYGON_OFFSET_FILL);
			glDepthMask(GL_TRUE);
		}

		set_blend(blend::none);
		submission::BindVertexArray(0);
		glDisable(GL_DEPTH_TEST);
	}
}
