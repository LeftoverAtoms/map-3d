shader_type spatial;

render_mode unshaded, cull_back, depth_draw_opaque;

uniform sampler2D base_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D lightmap_texture : filter_linear, repeat_disable;

uniform bool use_lightmap = true;

void fragment()
{
	vec3 base = texture(base_texture, UV).rgb;

	if (use_lightmap)
	{
		vec3 lightmap = texture(lightmap_texture, UV2).rgb;

		// Approximate Q3 overbright for diagnosis.
		lightmap *= 2.0;

		ALBEDO = base * lightmap;
	}
	else
	{
		ALBEDO = base;
	}
}